#include "pch.h"
#include "Modrinth.h"
#include "Http.h"
#include <fstream>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Modrinth
{
    namespace
    {
        constexpr wchar_t kApi[] = L"https://api.modrinth.com/v2";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";
        constexpr wchar_t kFabricApi[] = L"P7dR8mSH";

        hstring OptStr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::String)
                    return o.GetNamedString(key);
            }
            catch (...)
            {
            }
            return hstring{};
        }

        ModFile ToFile(JsonObject const& o)
        {
            ModFile f{};
            f.url = OptStr(o, L"url");
            f.filename = OptStr(o, L"filename");
            try
            {
                if (o.HasKey(L"size"))
                    f.size = static_cast<long long>(o.GetNamedNumber(L"size"));
                if (o.HasKey(L"primary"))
                    f.primary = o.GetNamedBoolean(L"primary");
                if (o.HasKey(L"hashes"))
                {
                    auto h = o.GetNamedObject(L"hashes");
                    f.sha1 = OptStr(h, L"sha1");
                    f.sha512 = OptStr(h, L"sha512");
                }
            }
            catch (...)
            {
            }
            // Filenames arrive with '+' percent-encoded; keep the URL as-is,
            // decode only for the local filename.
            try
            {
                f.filename = Uri::UnescapeComponent(f.filename);
            }
            catch (...)
            {
            }
            return f;
        }

        ModVersion ToVersion(JsonObject const& o)
        {
            ModVersion v{};
            v.id = OptStr(o, L"id");
            v.versionNumber = OptStr(o, L"version_number");
            try
            {
                if (o.HasKey(L"files"))
                {
                    for (auto const& fv : o.GetNamedArray(L"files"))
                    {
                        if (fv.ValueType() == JsonValueType::Object)
                            v.files.push_back(ToFile(fv.GetObject()));
                    }
                }
            }
            catch (...)
            {
            }
            return v;
        }

        std::vector<ModVersion> ListedVersions(JsonArray const& arr)
        {
            std::vector<ModVersion> out;
            try
            {
                if (!arr)
                    return out;
                for (auto const& vv : arr)
                {
                    if (vv.ValueType() != JsonValueType::Object)
                        continue;
                    auto o = vv.GetObject();
                    try
                    {
                        if (o.HasKey(L"status") && o.GetNamedString(L"status") != L"listed")
                            continue;
                    }
                    catch (...)
                    {
                    }
                    auto v = ToVersion(o);
                    if (!v.id.empty() && !v.files.empty())
                        out.push_back(std::move(v));
                }
            }
            catch (...)
            {
            }
            return out;
        }

        IAsyncOperation<JsonArray> VersionListJsonAsync(
            hstring projectIdOrSlug, hstring mcVersion, hstring loader)
        {
            JsonArray out{ nullptr };
            try
            {
                std::wstring url = std::wstring{ kApi } + L"/project/" + std::wstring{ projectIdOrSlug } + L"/version?";
                bool first = true;
                if (!loader.empty() && loader != L"all")
                {
                    url += L"loaders=" + std::wstring{ Http::Escape(hstring{ L"[\"" + std::wstring{ loader } + L"\"]" }) };
                    first = false;
                }
                if (!mcVersion.empty())
                {
                    if (!first)
                        url += L"&";
                    url += L"game_versions=" + std::wstring{ Http::Escape(hstring{ L"[\"" + std::wstring{ mcVersion } + L"\"]" }) };
                }
                url += L"&limit=10";
                auto text = co_await Http::GetStringAsync(hstring{ url }, kUA);
                out = JsonArray::Parse(text);
            }
            catch (...)
            {
            }
            co_return out;
        }

        ModFile PickPrimary(std::vector<ModVersion> const& versions)
        {
            ModFile picked{};
            if (versions.empty())
                return picked;
            auto const& files = versions.front().files;
            for (auto const& f : files)
            {
                if (f.primary && !f.url.empty())
                    return f;
            }
            for (auto const& f : files)
            {
                if (!f.url.empty())
                    return f;
            }
            return picked;
        }
    } // namespace

    fire_and_forget SearchAsync(hstring query, hstring mcVersion, hstring loader, SearchFn done)
    {
        std::vector<ModHit> out;
        try
        {
            std::wstring facets = L"[[\"project_type:mod\"]";
            if (!mcVersion.empty())
                facets += L",[\"versions:" + std::wstring{ mcVersion } + L"\"]";
            if (!loader.empty() && loader != L"all")
                facets += L",[\"categories:" + std::wstring{ loader } + L"\"]";
            facets += L"]";
            hstring url = hstring{ kApi } + L"/search?query=" + Http::Escape(query) +
                L"&facets=" + Http::Escape(hstring{ facets }) + L"&limit=20&index=relevance";
            auto text = co_await Http::GetStringAsync(url, kUA);
            auto root = JsonObject::Parse(text);
            if (root.HasKey(L"hits"))
            {
                for (auto const& hv : root.GetNamedArray(L"hits"))
                {
                    if (hv.ValueType() != JsonValueType::Object)
                        continue;
                    auto o = hv.GetObject();
                    ModHit hit{};
                    hit.projectId = OptStr(o, L"project_id");
                    hit.slug = OptStr(o, L"slug");
                    hit.title = OptStr(o, L"title");
                    hit.description = OptStr(o, L"description");
                    hit.iconUrl = OptStr(o, L"icon_url");
                    try
                    {
                        if (o.HasKey(L"downloads"))
                            hit.downloads = static_cast<long long>(o.GetNamedNumber(L"downloads"));
                    }
                    catch (...)
                    {
                    }
                    if (!hit.projectId.empty())
                        out.push_back(std::move(hit));
                }
            }
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    fire_and_forget GetVersionsAsync(
        hstring projectIdOrSlug, hstring mcVersion, hstring loader, VersionsFn done)
    {
        std::vector<ModVersion> out;
        try
        {
            out = ListedVersions(co_await VersionListJsonAsync(projectIdOrSlug, mcVersion, loader));
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    fire_and_forget PickFileAsync(hstring projectIdOrSlug, hstring mcVersion, hstring loader, PickFn done)
    {
        ModFile picked{};
        try
        {
            picked = PickPrimary(ListedVersions(co_await VersionListJsonAsync(projectIdOrSlug, mcVersion, loader)));
        }
        catch (...)
        {
        }
        done(std::move(picked));
    }

    IAsyncOperation<hstring> DownloadFileAsync(ModFile const& file, std::wstring const& destDir)
    {
        hstring status = L"Download failed.";
        try
        {
            if (file.url.empty() || file.filename.empty())
                co_return L"Empty file entry.";
            std::error_code ec;
            std::filesystem::create_directories(destDir, ec);
            auto dest = std::filesystem::path{ destDir } / std::filesystem::path{ std::wstring{ file.filename } };
            if (std::filesystem::exists(dest, ec))
            {
                auto have = static_cast<long long>(std::filesystem::file_size(dest, ec));
                if (!ec && file.size > 0 && have == file.size)
                    co_return hstring{ L"Already present: " } + file.filename;
            }
            auto bytes = Http::BufferToVector(co_await Http::GetBufferAsync(file.url, kUA));
            if (file.size > 0 && static_cast<long long>(bytes.size()) != file.size)
                co_return L"Size mismatch, retry.";
            if (!Http::WriteFile(dest, bytes))
                co_return L"Could not write file.";
            if (!file.sha1.empty())
            {
                std::wstring hex;
                if (Http::Sha1OfFile(dest, hex) &&
                    _wcsicmp(hex.c_str(), std::wstring{ file.sha1 }.c_str()) != 0)
                {
                    std::filesystem::remove(dest, ec);
                    co_return L"Checksum mismatch, removed.";
                }
            }
            status = hstring{ L"Installed " } + file.filename;
        }
        catch (...)
        {
            status = L"Download failed (network?).";
        }
        co_return status;
    }

    IAsyncOperation<hstring> EnsureFabricApiAsync(std::wstring const& gameDir, hstring mcVersion)
    {
        hstring status = L"Fabric API check failed.";
        try
        {
            auto mods = std::filesystem::path{ gameDir } / L"mods";
            std::error_code ec;
            if (std::filesystem::exists(mods, ec))
            {
                for (auto const& e : std::filesystem::directory_iterator(mods, ec))
                {
                    std::wstring name = e.path().filename().wstring();
                    for (auto& c : name)
                        c = static_cast<wchar_t>(towlower(c));
                    if (name.find(L"fabric-api") != std::wstring::npos)
                        co_return L"Fabric API already present.";
                }
            }
            auto versions = ListedVersions(co_await VersionListJsonAsync(kFabricApi, mcVersion, L"fabric"));
            auto file = PickPrimary(versions);
            if (file.url.empty())
                co_return hstring{ L"No Fabric API build for " } + mcVersion + L".";
            status = co_await DownloadFileAsync(file, mods.wstring());
        }
        catch (...)
        {
        }
        co_return status;
    }
}
