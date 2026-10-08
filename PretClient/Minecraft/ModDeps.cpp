#include "pch.h"
#include "ModDeps.h"
#include <set>
#include <utility>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::ModDeps
{
    namespace
    {
        // Hard stop on pathological chains; real mod trees are 1-3 deep.
        constexpr int kMaxDepth = 8;
        // Belt-and-braces on top of the visited set (uniq queue entries).
        constexpr int kMaxSteps = 64;

        bool AlreadyOnDisk(std::wstring const& modsDir, hstring const& filename)
        {
            if (filename.empty())
                return false;
            try
            {
                std::error_code ec;
                return std::filesystem::exists(
                    std::filesystem::path{ modsDir } / std::filesystem::path{ std::wstring{ filename } }, ec);
            }
            catch (...)
            {
                return false;
            }
        }

        bool WasInstalled(hstring const& status)
        {
            std::wstring s{ status };
            return s.rfind(L"Installed", 0) == 0 || s.rfind(L"Already present", 0) == 0;
        }

        // A pinned/exact Modrinth build is only usable when it actually
        // targets this MC version/loader and has a downloadable file. Empty
        // lists mean the API didn't say (be lenient, not strict).
        bool VersionFits(Modrinth::ModVersion const& v, hstring const& mc, hstring const& loader)
        {
            if (!mc.empty() && !v.gameVersions.empty())
            {
                bool ok = false;
                for (auto const& g : v.gameVersions)
                {
                    if (g == mc)
                    {
                        ok = true;
                        break;
                    }
                }
                if (!ok)
                    return false;
            }
            if (!loader.empty() && !v.loaders.empty())
            {
                bool ok = false;
                for (auto const& l : v.loaders)
                {
                    if (l == loader)
                    {
                        ok = true;
                        break;
                    }
                }
                if (!ok)
                    return false;
            }
            for (auto const& f : v.files)
            {
                if (!f.url.empty())
                    return true;
            }
            return false;
        }

        Modrinth::ModFile PrimaryOf(Modrinth::ModVersion const& v)
        {
            for (auto const& f : v.files)
            {
                if (f.primary && !f.url.empty())
                    return f;
            }
            for (auto const& f : v.files)
            {
                if (!f.url.empty())
                    return f;
            }
            return Modrinth::ModFile{};
        }
    } // namespace

    fire_and_forget EnsureModrinthAsync(Modrinth::ModVersion version,
        hstring mcVersion, hstring loader, std::wstring modsDir, SummaryFn done)
    {
        Summary sum{};
        try
        {
            // Seed with the installed mod itself so a dependency pointing
            // back at its parent (or at the same build) is a no-op.
            std::set<std::wstring> seen;
            if (!version.projectId.empty())
                seen.insert(L"p:" + std::wstring{ version.projectId });
            if (!version.id.empty())
                seen.insert(L"v:" + std::wstring{ version.id });

            // Explicit worklist (not recursion): each step may await HTTP.
            std::vector<std::pair<Modrinth::Dependency, int>> stack;
            for (auto const& d : version.dependencies)
            {
                if (d.type == L"required")
                    stack.emplace_back(d, 0);
            }

            int steps = 0;
            while (!stack.empty() && steps++ < kMaxSteps)
            {
                auto [dep, depth] = std::move(stack.back());
                stack.pop_back();
                if (depth > kMaxDepth)
                    continue;
                std::wstring key = !dep.projectId.empty() ? L"p:" + std::wstring{ dep.projectId }
                                                          : L"v:" + std::wstring{ dep.versionId };
                if (key == L"p:" || key == L"v:" || !seen.insert(key).second)
                    continue;

                // Prefer the author's pinned build when it fits this
                // version/loader; otherwise take the newest matching build.
                Modrinth::ModVersion rv{};
                bool have = false;
                if (!dep.versionId.empty())
                {
                    try
                    {
                        auto cand = Modrinth::ParseVersion(
                            co_await Modrinth::GetVersionJsonAsync(dep.versionId));
                        if (!cand.id.empty() && VersionFits(cand, mcVersion, loader))
                        {
                            rv = std::move(cand);
                            have = true;
                        }
                    }
                    catch (...)
                    {
                    }
                }
                if (!have && !dep.projectId.empty())
                {
                    try
                    {
                        auto list = Modrinth::ParseVersions(co_await Modrinth::ListVersionsJsonAsync(
                            dep.projectId, mcVersion, loader));
                        if (!list.empty())
                        {
                            rv = std::move(list.front());
                            have = true;
                        }
                    }
                    catch (...)
                    {
                    }
                }
                if (!have)
                {
                    hstring label = dep.projectId;
                    if (!dep.projectId.empty())
                    {
                        try
                        {
                            auto title = co_await Modrinth::GetProjectTitleAsync(dep.projectId);
                            if (!title.empty())
                                label = title;
                        }
                        catch (...)
                        {
                        }
                    }
                    else if (!dep.versionId.empty())
                    {
                        label = hstring{ L"pinned build " } + dep.versionId;
                    }
                    if (!label.empty())
                        sum.missing.push_back(label);
                    continue;
                }

                auto file = PrimaryOf(rv);
                if (file.url.empty())
                {
                    sum.missing.push_back(
                        rv.versionNumber.empty() ? rv.id : rv.versionNumber);
                    continue;
                }
                if (AlreadyOnDisk(modsDir, file.filename))
                {
                    sum.alreadyThere.push_back(file.filename);
                }
                else
                {
                    hstring st = L"Download failed.";
                    try
                    {
                        st = co_await Modrinth::DownloadFileAsync(file, modsDir);
                    }
                    catch (...)
                    {
                    }
                    if (WasInstalled(st))
                    {
                        if (std::wstring{ st }.rfind(L"Installed", 0) == 0)
                            sum.installed.push_back(file.filename);
                        else
                            sum.alreadyThere.push_back(file.filename);
                    }
                    else
                    {
                        // Don't chase transitive deps of a failed download.
                        sum.missing.push_back(file.filename);
                        continue;
                    }
                }
                for (auto const& d : rv.dependencies)
                {
                    if (d.type == L"required")
                        stack.emplace_back(d, depth + 1);
                }
            }
        }
        catch (...)
        {
        }
        done(std::move(sum));
    }

    fire_and_forget EnsureCurseForgeAsync(CurseForge::ModFile file, int modId,
        hstring mcVersion, hstring loader, hstring apiKey, std::wstring modsDir, SummaryFn done)
    {
        Summary sum{};
        try
        {
            std::set<int> seen;
            if (modId != 0)
                seen.insert(modId);

            std::vector<std::pair<CurseForge::Dependency, int>> stack;
            for (auto const& d : file.dependencies)
            {
                // relationType 3 = required; everything else (optional,
                // embedded, tool, incompatible, include) is left alone.
                if (d.relationType == 3)
                    stack.emplace_back(d, 0);
            }

            int steps = 0;
            while (!stack.empty() && steps++ < kMaxSteps)
            {
                auto [dep, depth] = std::move(stack.back());
                stack.pop_back();
                if (depth > kMaxDepth || dep.modId == 0 || !seen.insert(dep.modId).second)
                    continue;

                CurseForge::ModVersion rv{};
                try
                {
                    auto list = CurseForge::ParseVersions(co_await CurseForge::ListFilesJsonAsync(
                        dep.modId, mcVersion, loader, apiKey));
                    if (!list.empty())
                        rv = std::move(list.front());
                }
                catch (...)
                {
                }
                hstring name{};
                try
                {
                    name = co_await CurseForge::GetModNameAsync(dep.modId, apiKey);
                }
                catch (...)
                {
                }
                if (name.empty())
                    name = L"mod #" + to_hstring(dep.modId);
                if (rv.files.empty() || rv.files.front().url.empty())
                {
                    sum.missing.push_back(name);
                    continue;
                }
                auto const& df = rv.files.front();
                if (AlreadyOnDisk(modsDir, df.filename))
                {
                    sum.alreadyThere.push_back(df.filename);
                }
                else
                {
                    hstring st = L"Download failed.";
                    try
                    {
                        st = co_await CurseForge::DownloadFileAsync(df, modsDir);
                    }
                    catch (...)
                    {
                    }
                    if (WasInstalled(st))
                    {
                        if (std::wstring{ st }.rfind(L"Installed", 0) == 0)
                            sum.installed.push_back(df.filename);
                        else
                            sum.alreadyThere.push_back(df.filename);
                    }
                    else
                    {
                        sum.missing.push_back(name);
                        continue;
                    }
                }
                for (auto const& d : df.dependencies)
                {
                    if (d.relationType == 3)
                        stack.emplace_back(d, depth + 1);
                }
            }
        }
        catch (...)
        {
        }
        done(std::move(sum));
    }

    hstring FormatStatus(hstring const& mainStatus, Summary const& s)
    {
        try
        {
            if (s.installed.empty() && s.missing.empty())
            {
                if (!s.alreadyThere.empty())
                    return mainStatus + hstring{ L" (dependencies already present)" };
                return mainStatus;
            }
            std::wstring out{ mainStatus };
            if (!s.installed.empty())
            {
                out += L" + " + std::to_wstring(s.installed.size()) +
                    (s.installed.size() == 1 ? L" dependency: " : L" dependencies: ");
                for (size_t i = 0; i < s.installed.size(); ++i)
                {
                    if (i > 0)
                        out += L", ";
                    out += std::wstring{ s.installed[i] };
                }
            }
            if (!s.missing.empty())
            {
                out += s.installed.empty() ? L" (couldn't resolve: " : L" (still missing: ";
                for (size_t i = 0; i < s.missing.size(); ++i)
                {
                    if (i > 0)
                        out += L", ";
                    out += std::wstring{ s.missing[i] };
                }
                out += L")";
            }
            return hstring{ out };
        }
        catch (...)
        {
            return mainStatus;
        }
    }
}
