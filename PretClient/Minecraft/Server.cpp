#include "pch.h"
#include "Server.h"
#include "Fabric.h"
#include "Forge.h"
#include "Http.h"
#include "NeoForge.h"
#include "Quilt.h"
#include "../Paths.h"
#include <coroutine>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Server
{
    namespace
    {
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";
        constexpr size_t kCap = 150;

        hstring OptStr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o && o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::String)
                    return o.GetNamedString(key);
            }
            catch (...)
            {
            }
            return hstring{};
        }

        JsonObject OptObj(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o && o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Object)
                    return o.GetNamedObject(key);
            }
            catch (...)
            {
            }
            return JsonObject{ nullptr };
        }

        JsonArray OptArr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o && o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Array)
                    return o.GetNamedArray(key);
            }
            catch (...)
            {
            }
            return JsonArray{ nullptr };
        }

        unsigned long long OptSize(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o && o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Number)
                    return static_cast<unsigned long long>(o.GetNamedNumber(key));
            }
            catch (...)
            {
            }
            return 0;
        }

        IAsyncOperation<JsonObject> GetObjAsync(hstring const& url)
        {
            JsonObject out{ nullptr };
            try
            {
                out = JsonObject::Parse(co_await Http::GetStringAsync(url, kUA));
            }
            catch (...)
            {
            }
            co_return out;
        }

        // Some endpoints return a bare array (Fill builds), others wrap it.
        IAsyncOperation<JsonArray> GetArrAsync(hstring const& url, wchar_t const* wrapKey = nullptr)
        {
            JsonArray out{ nullptr };
            try
            {
                auto text = co_await Http::GetStringAsync(url, kUA);
                try
                {
                    out = JsonArray::Parse(text);
                }
                catch (...)
                {
                    if (wrapKey)
                        out = OptArr(JsonObject::Parse(text), wrapKey);
                }
            }
            catch (...)
            {
            }
            co_return out;
        }

        std::vector<hstring> StringsOf(JsonArray const& arr)
        {
            std::vector<hstring> out;
            try
            {
                if (!arr)
                    return out;
                for (auto const& v : arr)
                {
                    if (v.ValueType() != JsonValueType::String)
                        continue;
                    hstring s = v.GetString();
                    if (!s.empty())
                        out.push_back(s);
                    if (out.size() >= kCap)
                        break;
                }
            }
            catch (...)
            {
            }
            return out;
        }

        // "21" | "21.1" | 21.1 (number) -> "21.1"-style build tag.
        hstring BuildStr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (!o || !o.HasKey(key))
                    return hstring{};
                auto v = o.GetNamedValue(key);
                if (v.ValueType() == JsonValueType::String)
                    return v.GetString();
                if (v.ValueType() == JsonValueType::Number)
                    return to_hstring(static_cast<long long>(v.GetNumber()));
            }
            catch (...)
            {
            }
            return hstring{};
        }

        bool StartsWith(std::wstring const& s, std::wstring const& prefix)
        {
            return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
        }

        // Fill v3 builds array -> best artifact. Prefers the "server:default"
        // download, else the first download entry carrying a url.
        bool FillArtifact(JsonArray const& builds, Artifact& art)
        {
            try
            {
                if (!builds || builds.Size() == 0)
                    return false;
                JsonObject best{ nullptr };
                for (auto const& v : builds)
                {
                    if (v.ValueType() != JsonValueType::Object)
                        continue;
                    auto o = v.GetObject();
                    if (!best)
                        best = o;
                    if (OptStr(o, L"channel") == L"STABLE")
                    {
                        best = o;
                        break;
                    }
                }
                if (!best)
                    return false;
                auto downloads = OptObj(best, L"downloads");
                if (!downloads)
                    return false;
                JsonObject dl = OptObj(downloads, L"server:default");
                if (!dl)
                {
                    for (auto const& kv : downloads) // velocity/proxy shape
                    {
                        try
                        {
                            if (kv.Value().ValueType() != JsonValueType::Object)
                                continue;
                            auto cand = kv.Value().GetObject();
                            if (!OptStr(cand, L"url").empty())
                            {
                                dl = cand;
                                break;
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
                hstring url = OptStr(dl, L"url");
                if (url.empty())
                    return false;
                art.url = url;
                art.sha256 = OptStr(OptObj(dl, L"checksums"), L"sha256");
                art.size = OptSize(dl, L"size");
                art.kind = InstallKind::Jar;
                art.fileName = L"server.jar";
                return true;
            }
            catch (...)
            {
            }
            return false;
        }

        IAsyncOperation<hstring> LatestInstaller(hstring base)
        {
            hstring latest{};
            try
            {
                auto arr = co_await GetArrAsync(hstring{ base } + L"/versions/installer");
                if (arr && arr.Size() > 0)
                {
                    try
                    {
                        latest = OptStr(arr.GetObjectAt(0), L"version");
                    }
                    catch (...)
                    {
                    }
                    if (latest.empty()) // bare-string shape fallback
                    {
                        try
                        {
                            if (arr.GetAt(0).ValueType() == JsonValueType::String)
                                latest = arr.GetStringAt(0);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
            catch (...)
            {
            }
            co_return latest;
        }

        // NeoForge "21.1.148" -> MC "1.21.1", "26.3.0.9-beta" -> "26.3".
        std::wstring NeoToMc(std::wstring const& neo)
        {
            try
            {
                auto core = neo;
                if (auto dash = core.find(L'-'); dash != std::wstring::npos)
                    core = core.substr(0, dash);
                if (StartsWith(core, L"26."))
                {
                    auto dot = core.find(L'.', 3);
                    return core.substr(0, dot == std::wstring::npos ? core.size() : dot);
                }
                auto dot1 = core.find(L'.');
                auto dot2 = dot1 == std::wstring::npos ? std::wstring::npos : core.find(L'.', dot1 + 1);
                std::wstring minor = dot1 == std::wstring::npos ? core : core.substr(0, dot1);
                std::wstring patch = (dot1 == std::wstring::npos || dot2 == std::wstring::npos)
                    ? L"0"
                    : core.substr(dot1 + 1, dot2 - dot1 - 1);
                return L"1." + minor + L"." + patch;
            }
            catch (...)
            {
            }
            return L"";
        }

        // Generic last resort: first http(s) string under a *download* key.
        hstring ScanDownload(IJsonValue const& v, bool underDownload)
        {
            try
            {
                auto type = v.ValueType();
                if (type == JsonValueType::String && underDownload)
                {
                    std::wstring s{ v.GetString() };
                    if (StartsWith(s, L"https://") || StartsWith(s, L"http://"))
                        return v.GetString();
                    return hstring{};
                }
                if (type == JsonValueType::Object)
                {
                    auto o = v.GetObject();
                    for (auto const& kv : o)
                    {
                        std::wstring key{ kv.Key() };
                        std::wstring low = key;
                        for (auto& c : low)
                            c = static_cast<wchar_t>(std::towlower(c));
                        bool dl = underDownload || low.find(L"download") != std::wstring::npos;
                        hstring hit = ScanDownload(kv.Value(), dl);
                        if (!hit.empty())
                            return hit;
                    }
                }
                else if (type == JsonValueType::Array)
                {
                    auto arr = v.GetArray();
                    for (uint32_t i = 0; i < arr.Size(); ++i)
                    {
                        hstring hit = ScanDownload(arr.GetAt(i), underDownload);
                        if (!hit.empty())
                            return hit;
                    }
                }
            }
            catch (...)
            {
            }
            return hstring{};
        }

        std::filesystem::path StoreFile()
        {
            return Paths::DataDir() / L"servers.json";
        }
    } // namespace

    std::vector<SoftwareInfo> AllSoftware()
    {
        return {
            { L"vanilla", L"Vanilla (official)", L"Official Mojang server. No plugins or mods. Download via piston-meta." },
            { L"paper", L"Paper (plugins)", L"Fast Spigot fork with plugins. Most popular. API: fill.papermc.io." },
            { L"folia", L"Folia (regionised)", L"Paper with regionised multithreading. API: fill.papermc.io." },
            { L"purpur", L"Purpur (plugins)", L"Paper fork with gameplay tweaks and plugins. API: api.purpurmc.org." },
            { L"leaves", L"Leaves (Paper fork)", L"Paper fork, extra performance. MC 1.19-1.21. API: api.leavesmc.org." },
            { L"fabric", L"Fabric (mods)", L"Mod loader server. API: meta.fabricmc.net." },
            { L"quilt", L"Quilt (mods)", L"Mod loader server, Fabric fork. API: meta.quiltmc.org." },
            { L"neoforge", L"NeoForge (mods)", L"Modded, MC 1.20.2+. Runs the installer. Maven: maven.neoforged.net." },
            { L"forge", L"Forge (mods)", L"Classic modded. Runs the installer. Promotions: files.minecraftforge.net." },
            { L"sponge", L"SpongeVanilla (plugins)", L"Sponge API plugins. API: dl-api.spongepowered.org." },
            { L"velocity", L"Velocity (proxy)", L"Modern proxy, not a game server. API: fill.papermc.io." },
            { L"spigot", L"Spigot (manual)", L"No direct jar (DMCA). Compiled locally via BuildTools (needs Java + git, takes minutes)." },
        };
    }

    std::vector<hstring> RecentMcVersions()
    {
        return {
            L"26.3", L"26.2", L"26.1", L"1.21.11", L"1.21.8", L"1.21.4",
            L"1.21.1", L"1.20.6", L"1.20.4", L"1.20.1", L"1.19.4",
            L"1.18.2", L"1.16.5", L"1.12.2", L"1.8.9",
        };
    }

    fire_and_forget FetchVersionsAsync(hstring softwareId, VersionListFn done)
    {
        std::vector<hstring> out;
        try
        {
            std::wstring id{ softwareId };
            if (id == L"spigot")
            {
                out = RecentMcVersions(); // BuildTools takes --rev per MC
            }
            else if (id == L"vanilla")
            {
                auto text = co_await Http::GetStringAsync(
                    L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json", kUA);
                auto root = JsonObject::Parse(text);
                auto arr = OptArr(root, L"versions");
                if (arr)
                {
                    for (uint32_t i = 0; i < arr.Size() && out.size() < kCap; ++i)
                    {
                        try
                        {
                            auto o = arr.GetObjectAt(i);
                            if (OptStr(o, L"type") == L"release")
                            {
                                hstring vid = OptStr(o, L"id");
                                if (!vid.empty())
                                    out.push_back(vid);
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
            else if (id == L"paper" || id == L"folia" || id == L"velocity")
            {
                // Fill v3: {versions:{group:[...]}} (bare-array fallback).
                auto root = co_await GetObjAsync(
                    hstring{ L"https://fill.papermc.io/v3/projects/" } + softwareId);
                auto versions = OptObj(root, L"versions");
                JsonArray group{ nullptr };
                if (versions && versions.HasKey(L"group"))
                    group = OptArr(versions, L"group");
                else
                    group = OptArr(root, L"versions");
                out = StringsOf(group);
            }
            else if (id == L"purpur")
            {
                auto root = co_await GetObjAsync(L"https://api.purpurmc.org/v2/purpur");
                out = StringsOf(OptArr(root, L"versions"));
            }
            else if (id == L"leaves")
            {
                auto root = co_await GetObjAsync(L"https://api.leavesmc.org/v2/projects/leaves");
                auto arr = OptArr(root, L"versions");
                if (!arr) // object-with-group shape fallback
                    arr = OptArr(OptObj(root, L"versions"), L"group");
                out = StringsOf(arr);
            }
            else if (id == L"fabric")
            {
                auto arr = co_await GetArrAsync(L"https://meta.fabricmc.net/v2/versions/game");
                if (arr)
                {
                    for (uint32_t i = 0; i < arr.Size() && out.size() < kCap; ++i)
                    {
                        try
                        {
                            hstring v = OptStr(arr.GetObjectAt(i), L"version");
                            if (!v.empty())
                                out.push_back(v);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
            else if (id == L"quilt")
            {
                auto arr = co_await GetArrAsync(L"https://meta.quiltmc.org/v3/versions/game");
                if (arr)
                {
                    for (uint32_t i = 0; i < arr.Size() && out.size() < kCap; ++i)
                    {
                        try
                        {
                            hstring v = OptStr(arr.GetObjectAt(i), L"version");
                            if (!v.empty())
                                out.push_back(v);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
            else if (id == L"neoforge")
            {
                auto root = co_await GetObjAsync(
                    L"https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge");
                std::set<std::wstring> have;
                auto arr = OptArr(root, L"versions");
                if (arr)
                {
                    for (uint32_t i = 0; i < arr.Size(); ++i)
                    {
                        try
                        {
                            if (arr.GetAt(i).ValueType() != JsonValueType::String)
                                continue;
                            std::wstring mc = NeoToMc(std::wstring{ arr.GetStringAt(i) });
                            if (!mc.empty())
                                have.insert(mc);
                        }
                        catch (...)
                        {
                        }
                    }
                }
                for (auto const& v : RecentMcVersions())
                {
                    if (have.find(std::wstring{ v }) != have.end())
                        out.push_back(v);
                }
            }
            else if (id == L"forge")
            {
                auto root = co_await GetObjAsync(
                    L"https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");
                std::set<std::wstring> have;
                auto promos = OptObj(root, L"promos");
                if (promos)
                {
                    for (auto const& kv : promos)
                    {
                        try
                        {
                            std::wstring key{ kv.Key() };
                            auto dash = key.rfind(L'-');
                            if (dash != std::wstring::npos && dash > 0)
                                have.insert(key.substr(0, dash));
                        }
                        catch (...)
                        {
                        }
                    }
                }
                for (auto const& v : RecentMcVersions())
                {
                    if (have.find(std::wstring{ v }) != have.end())
                        out.push_back(v);
                }
            }
            else if (id == L"sponge")
            {
                auto root = co_await GetObjAsync(
                    L"https://dl-api.spongepowered.org/v2/groups/org.spongepowered/artifacts/spongevanilla");
                auto tags = OptObj(root, L"tags");
                out = StringsOf(OptArr(tags, L"minecraft"));
            }
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    fire_and_forget ResolveArtifactAsync(hstring softwareId, hstring mcVersion, ArtifactFn done)
    {
        Artifact art{};
        hstring err = L"Could not resolve a download for this version.";
        try
        {
            std::wstring id{ softwareId };
            if (id == L"vanilla")
            {
                auto text = co_await Http::GetStringAsync(
                    L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json", kUA);
                auto root = JsonObject::Parse(text);
                hstring pkgUrl{};
                auto arr = OptArr(root, L"versions");
                if (arr)
                {
                    for (uint32_t i = 0; i < arr.Size(); ++i)
                    {
                        try
                        {
                            auto o = arr.GetObjectAt(i);
                            if (OptStr(o, L"id") == mcVersion)
                            {
                                pkgUrl = OptStr(o, L"url");
                                break;
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
                if (!pkgUrl.empty())
                {
                    auto pkg = co_await GetObjAsync(pkgUrl);
                    auto server = OptObj(OptObj(pkg, L"downloads"), L"server");
                    hstring url = OptStr(server, L"url");
                    if (!url.empty())
                    {
                        art.url = url;
                        art.sha1 = OptStr(server, L"sha1");
                        art.size = OptSize(server, L"size");
                        art.kind = InstallKind::Jar;
                        art.fileName = L"server.jar";
                        err.clear();
                    }
                    else
                    {
                        err = L"This version has no server jar (very old alpha/beta).";
                    }
                }
            }
            else if (id == L"paper" || id == L"folia" || id == L"velocity")
            {
                auto builds = co_await GetArrAsync(
                    hstring{ L"https://fill.papermc.io/v3/projects/" } + softwareId +
                        L"/versions/" + mcVersion + L"/builds",
                    L"builds");
                if (FillArtifact(builds, art))
                    err.clear();
            }
            else if (id == L"purpur")
            {
                auto ver = co_await GetObjAsync(
                    hstring{ L"https://api.purpurmc.org/v2/purpur/" } + mcVersion);
                hstring build = OptStr(OptObj(ver, L"builds"), L"latest");
                if (build.empty())
                    build = BuildStr(OptObj(ver, L"builds"), L"latest");
                if (!build.empty())
                {
                    art.url = hstring{ L"https://api.purpurmc.org/v2/purpur/" } + mcVersion +
                        L"/" + build + L"/download";
                    art.kind = InstallKind::Jar;
                    art.fileName = L"server.jar";
                    err.clear();
                }
            }
            else if (id == L"leaves")
            {
                auto root = co_await GetObjAsync(
                    hstring{ L"https://api.leavesmc.org/v2/projects/leaves/versions/" } +
                    mcVersion + L"/builds");
                auto builds = OptArr(root, L"builds");
                if (builds && builds.Size() > 0)
                {
                    auto o = builds.GetObjectAt(builds.Size() - 1); // newest last
                    hstring build = BuildStr(o, L"build");
                    auto app = OptObj(OptObj(o, L"downloads"), L"application");
                    hstring name = OptStr(app, L"name");
                    if (!build.empty() && !name.empty())
                    {
                        art.url = hstring{ L"https://api.leavesmc.org/v2/projects/leaves/versions/" } +
                            mcVersion + L"/builds/" + build + L"/downloads/" + name;
                        art.sha256 = OptStr(app, L"sha256");
                        art.kind = InstallKind::Jar;
                        art.fileName = L"server.jar";
                        err.clear();
                    }
                }
            }
            else if (id == L"fabric" || id == L"quilt")
            {
                bool quilt = (id == L"quilt");
                hstring base = quilt ? L"https://meta.quiltmc.org/v3" : L"https://meta.fabricmc.net/v2";
                hstring loader = quilt ? co_await Quilt::GetLatestLoader(mcVersion)
                                       : co_await Fabric::GetLatestLoader(mcVersion);
                hstring installer = co_await LatestInstaller(base);
                if (!loader.empty() && !installer.empty())
                {
                    art.url = base + L"/versions/loader/" + mcVersion + L"/" + loader + L"/" +
                        installer + L"/server/jar";
                    art.kind = InstallKind::Jar;
                    art.fileName = L"server.jar";
                    err.clear();
                }
                else
                {
                    err = L"No loader/installer for this MC version.";
                }
            }
            else if (id == L"neoforge")
            {
                hstring neo = co_await NeoForge::GetLatestNeoForge(mcVersion);
                hstring url = neo.empty() ? hstring{} : NeoForge::InstallerUrl(neo);
                if (!url.empty())
                {
                    art.url = url;
                    art.kind = InstallKind::Installer;
                    art.fileName = L"neoforge-installer.jar";
                    err.clear();
                }
                else
                {
                    err = L"No NeoForge build for this MC version (needs 1.20.2+).";
                }
            }
            else if (id == L"forge")
            {
                hstring fv = co_await Forge::GetLatestForge(mcVersion);
                hstring url = fv.empty() ? hstring{} : Forge::InstallerUrl(mcVersion, fv);
                if (!url.empty())
                {
                    art.url = url;
                    art.kind = InstallKind::Installer;
                    art.fileName = L"forge-installer.jar";
                    err.clear();
                }
                else
                {
                    err = L"No Forge build for this MC version.";
                }
            }
            else if (id == L"sponge")
            {
                auto text = co_await Http::GetStringAsync(
                    hstring{ L"https://dl-api.spongepowered.org/v2/groups/org.spongepowered/"
                             L"artifacts/spongevanilla/versions?minecraft=" } +
                        mcVersion,
                    kUA);
                hstring url;
                try
                {
                    try
                    {
                        url = ScanDownload(JsonObject::Parse(text), false);
                    }
                    catch (...)
                    {
                        url = ScanDownload(JsonArray::Parse(text), false);
                    }
                }
                catch (...)
                {
                }
                if (!url.empty())
                {
                    art.url = url;
                    art.kind = InstallKind::Jar;
                    art.fileName = L"server.jar";
                    err.clear();
                }
                else
                {
                    err = L"Sponge API shape changed; could not find a download.";
                }
            }
            else if (id == L"spigot")
            {
                art.url = L"https://hub.spigotmc.org/jenkins/job/BuildTools/"
                          L"lastSuccessfulBuild/artifact/target/BuildTools.jar";
                art.kind = InstallKind::BuildTools;
                art.fileName = L"BuildTools.jar";
                err.clear();
            }
            else
            {
                err = L"Unknown server software.";
            }
        }
        catch (...)
        {
        }
        done(std::move(art), err);
    }

    std::filesystem::path ServersDir()
    {
        auto dir = Paths::DataDir() / L"servers";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    hstring NewServerId()
    {
        GUID g{};
        CoCreateGuid(&g);
        wchar_t buf[64]{};
        swprintf_s(
            buf, L"srv-%08lX-%04hX-%04hX", g.Data1 ^ GetTickCount(), g.Data2, g.Data3);
        return hstring{ buf };
    }

    std::vector<ServerEntry> LoadServers()
    {
        std::vector<ServerEntry> out;
        try
        {
            std::error_code ec;
            if (!std::filesystem::exists(StoreFile(), ec))
                return out;
            std::ifstream f(StoreFile(), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (text.empty())
                return out;
            auto arr = JsonArray::Parse(to_hstring(text));
            for (uint32_t i = 0; i < arr.Size(); ++i)
            {
                try
                {
                    auto o = arr.GetObjectAt(i);
                    ServerEntry e{};
                    e.id = OptStr(o, L"id");
                    e.name = OptStr(o, L"name");
                    e.software = OptStr(o, L"software");
                    e.mcVersion = OptStr(o, L"mcVersion");
                    if (!e.id.empty())
                        out.push_back(std::move(e));
                }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
        }
        return out;
    }

    void SaveServers(std::vector<ServerEntry> const& servers)
    {
        try
        {
            JsonArray arr{};
            for (auto const& s : servers)
            {
                JsonObject o{};
                o.SetNamedValue(L"id", JsonValue::CreateStringValue(s.id));
                o.SetNamedValue(L"name", JsonValue::CreateStringValue(s.name));
                o.SetNamedValue(L"software", JsonValue::CreateStringValue(s.software));
                o.SetNamedValue(L"mcVersion", JsonValue::CreateStringValue(s.mcVersion));
                arr.Append(o);
            }
            std::ofstream f(StoreFile(), std::ios::binary | std::ios::trunc);
            f << to_string(arr.Stringify());
        }
        catch (...)
        {
        }
    }

    IAsyncOperation<int> RunJavaJarAsync(
        hstring javaExe, hstring jarPath, hstring args, hstring workDir)
    {
        int code = -1;
        try
        {
            co_await winrt::resume_background();
            std::wstring cmd = L"\"" + std::wstring{ javaExe } + L"\" -jar \"" +
                std::wstring{ jarPath } + L"\" " + std::wstring{ args };
            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            std::wstring mutableCmd = cmd;
            std::wstring dir{ workDir };
            if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                    nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi))
            {
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD exit = 1;
                GetExitCodeProcess(pi.hProcess, &exit);
                code = static_cast<int>(exit);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
        catch (...)
        {
        }
        co_return code;
    }

    namespace Console
    {
        struct Proc
        {
            HANDLE hProcess = nullptr;
            HANDLE hThread = nullptr;
            HANDLE hInW = nullptr; // parent -> child stdin
            HANDLE hOutR = nullptr; // child stdout/stderr -> parent
            bool closed = false;
        };

        std::mutex g_mutex{};
        std::map<std::wstring, Proc> g_procs{};
        ConsoleLineFn g_sink{};

        void Sink(hstring id, std::string line, bool exited, int exitCode)
        {
            ConsoleLineFn fn;
            try
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                fn = g_sink;
            }
            catch (...)
            {
            }
            if (fn)
            {
                try
                {
                    fn(id, line, exited, exitCode);
                }
                catch (...)
                {
                }
            }
        }

        void CloseProc(std::wstring const& id)
        {
            try
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                auto it = g_procs.find(id);
                if (it == g_procs.end() || it->second.closed)
                    return;
                it->second.closed = true;
                HANDLE hs[] = { it->second.hProcess, it->second.hThread,
                    it->second.hInW, it->second.hOutR };
                for (HANDLE h : hs)
                {
                    if (h)
                        CloseHandle(h);
                }
                g_procs.erase(it);
            }
            catch (...)
            {
            }
        }

        // Reads the merged stdout/stderr pipe until the child dies, then
        // reports the exit and releases the handles. Never touches the UI.
        void Pump(std::wstring id, HANDLE hOut, HANDLE hProcess)
        {
            char buf[4096];
            std::string carry;
            for (;;)
            {
                DWORD got = 0;
                if (!ReadFile(hOut, buf, sizeof(buf), &got, nullptr) || got == 0)
                    break;
                carry.append(buf, got);
                size_t pos = 0;
                while ((pos = carry.find('\n')) != std::string::npos)
                {
                    std::string line = carry.substr(0, pos);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    Sink(hstring{ id }, line, false, 0);
                    carry.erase(0, pos + 1);
                }
            }
            if (!carry.empty())
                Sink(hstring{ id }, carry, false, 0);
            DWORD code = 1;
            GetExitCodeProcess(hProcess, &code);
            Sink(hstring{ id }, std::string{}, true, static_cast<int>(code));
            CloseProc(id);
        }
    } // namespace Console

    void SetConsoleSink(ConsoleLineFn fn)
    {
        try
        {
            std::lock_guard<std::mutex> lk(Console::g_mutex);
            Console::g_sink = std::move(fn);
        }
        catch (...)
        {
        }
    }

    bool ConsoleRunning(hstring const& id)
    {
        try
        {
            std::lock_guard<std::mutex> lk(Console::g_mutex);
            auto it = Console::g_procs.find(std::wstring{ id });
            return it != Console::g_procs.end() && !it->second.closed;
        }
        catch (...)
        {
        }
        return false;
    }

    void* ConsoleHandle(hstring const& id)
    {
        try
        {
            std::lock_guard<std::mutex> lk(Console::g_mutex);
            auto it = Console::g_procs.find(std::wstring{ id });
            if (it != Console::g_procs.end() && !it->second.closed)
                return static_cast<void*>(it->second.hProcess);
        }
        catch (...)
        {
        }
        return nullptr;
    }

    std::filesystem::path FindServerJar(std::filesystem::path const& dir)
    {
        try
        {
            std::error_code ec;
            auto direct = dir / L"server.jar";
            if (std::filesystem::exists(direct, ec))
                return direct;
            std::vector<std::filesystem::path> cands;
            for (auto const& e : std::filesystem::directory_iterator(dir, ec))
            {
                if (!e.is_regular_file(ec))
                    continue;
                auto p = e.path();
                if (p.extension() != L".jar")
                    continue;
                std::wstring n = p.filename().wstring();
                std::wstring low = n;
                for (auto& c : low)
                    c = static_cast<wchar_t>(towlower(c));
                if (low.find(L"installer") != std::wstring::npos || low == L"buildtools.jar")
                    continue;
                if (low.rfind(L"spigot-", 0) == 0 || low.rfind(L"minecraft_server", 0) == 0)
                    return p; // preferred names win immediately
                cands.push_back(p);
            }
            if (!cands.empty())
                return cands.front();
        }
        catch (...)
        {
        }
        return std::filesystem::path{};
    }

    bool StartConsole(hstring const& id, std::filesystem::path const& dir,
        std::filesystem::path const& jar, hstring const& javaExe, int maxMemMb, hstring& error)
    {
        error.clear();
        try
        {
            {
                std::lock_guard<std::mutex> lk(Console::g_mutex);
                if (Console::g_procs.find(std::wstring{ id }) != Console::g_procs.end())
                {
                    error = L"Already running.";
                    return false;
                }
            }
            if (maxMemMb < 512)
                maxMemMb = 2048;
            std::wstring cmd = L"\"" + std::wstring{ javaExe } + L"\" -Xmx" +
                std::to_wstring(maxMemMb) + L"M -jar \"" + jar.wstring() + L"\" nogui";

            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE outR = nullptr;
            HANDLE outW = nullptr;
            HANDLE inR = nullptr;
            HANDLE inW = nullptr;
            if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&inR, &inW, &sa, 0))
            {
                HANDLE hs[] = { outR, outW, inR, inW };
                for (HANDLE h : hs)
                {
                    if (h)
                        CloseHandle(h);
                }
                error = L"Could not create pipes.";
                return false;
            }
            SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = outW;
            si.hStdError = outW; // merged into the one pump
            si.hStdInput = inR;
            PROCESS_INFORMATION pi{};
            std::wstring mutableCmd = cmd;
            std::wstring work = dir.wstring();
            BOOL launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, work.c_str(), &si, &pi);
            CloseHandle(outW); // parent's copies: the child has its own now
            CloseHandle(inR);
            if (!launched)
            {
                CloseHandle(outR);
                CloseHandle(inW);
                wchar_t buf[128]{};
                swprintf_s(buf, L"Could not start Java (error %lu).", GetLastError());
                error = buf;
                return false;
            }
            {
                std::lock_guard<std::mutex> lk(Console::g_mutex);
                Console::Proc p{};
                p.hProcess = pi.hProcess;
                p.hThread = pi.hThread;
                p.hInW = inW;
                p.hOutR = outR;
                Console::g_procs[std::wstring{ id }] = p;
            }
            std::wstring key{ id };
            HANDLE childProc = pi.hProcess;
            std::thread([key, outR, childProc] {
                Console::Pump(key, outR, childProc);
            }).detach();
            return true;
        }
        catch (...)
        {
            error = L"Could not start the server.";
        }
        return false;
    }

    void SendConsole(hstring const& id, std::wstring const& line)
    {
        try
        {
            std::lock_guard<std::mutex> lk(Console::g_mutex);
            auto it = Console::g_procs.find(std::wstring{ id });
            if (it == Console::g_procs.end() || it->second.closed || !it->second.hInW)
                return;
            std::string narrow(line.begin(), line.end());
            narrow += "\n";
            DWORD wrote = 0;
            WriteFile(it->second.hInW, narrow.data(), static_cast<DWORD>(narrow.size()), &wrote, nullptr);
        }
        catch (...)
        {
        }
    }

    void StopConsole(hstring const& id)
    {
        HANDLE h = nullptr;
        try
        {
            std::lock_guard<std::mutex> lk(Console::g_mutex);
            auto it = Console::g_procs.find(std::wstring{ id });
            if (it == Console::g_procs.end() || it->second.closed)
                return;
            const char stop[] = "stop\n";
            DWORD wrote = 0;
            if (it->second.hInW)
                WriteFile(it->second.hInW, stop, 5, &wrote, nullptr);
            h = it->second.hProcess;
        }
        catch (...)
        {
            return;
        }
        // Wait for the graceful shutdown off-thread; the reader reports the
        // exit and releases the handles. Kill only a stuck process.
        std::wstring key{ id };
        std::thread([key, h] {
            try
            {
                if (WaitForSingleObject(h, 10000) == WAIT_TIMEOUT)
                {
                    std::lock_guard<std::mutex> lk(Console::g_mutex);
                    auto it = Console::g_procs.find(key);
                    if (it != Console::g_procs.end() && !it->second.closed)
                        TerminateProcess(it->second.hProcess, 1);
                }
            }
            catch (...)
            {
            }
        }).detach();
    }

    Windows::Foundation::IAsyncOperation<hstring> BackupServerAsync(
        std::filesystem::path const& dir, hstring const& label)
    {
        hstring err = L"Backup failed.";
        try
        {
            co_await winrt::resume_background();
            std::wstring safe{ label };
            for (auto& c : safe)
            {
                if (!(iswalnum(c) || c == L'-' || c == L'_'))
                    c = L'_';
            }
            if (safe.empty())
                safe = L"server";
            SYSTEMTIME st{};
            GetLocalTime(&st);
            wchar_t stamp[32]{};
            swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            auto destDir = dir / L"backups";
            std::error_code ec;
            std::filesystem::create_directories(destDir, ec);
            auto dest = destDir / (safe + L"-" + stamp + L".tar.gz");
            std::wstring cmd = L"tar -czf \"" + dest.wstring() + L"\" -C \"" +
                dir.wstring() + L"\" --exclude=backups .";
            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            std::wstring mutableCmd = cmd;
            if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                    nullptr, nullptr, &si, &pi))
                co_return err;
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code == 0)
                co_return hstring{};
        }
        catch (...)
        {
        }
        co_return err;
    }

    Windows::Foundation::IAsyncOperation<hstring> RestoreBackupAsync(
        std::filesystem::path const& dir, std::filesystem::path const& backup)
    {
        hstring err = L"Restore failed.";
        try
        {
            co_await winrt::resume_background();
            std::wstring cmd = L"tar -xzf \"" + backup.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            std::wstring mutableCmd = cmd;
            if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                    nullptr, nullptr, &si, &pi))
                co_return err;
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code == 0)
                co_return hstring{};
        }
        catch (...)
        {
        }
        co_return err;
    }

    int RequiredJava(hstring const& mcVersion)
    {
        try
        {
            std::wstring mc{ mcVersion };
            if (StartsWith(mc, L"26."))
                return 21;
            if (!StartsWith(mc, L"1."))
                return 21;
            auto dot = mc.find(L'.', 2);
            int minor = std::stoi(mc.substr(2, dot == std::wstring::npos ? std::wstring::npos : dot - 2));
            if (minor >= 21)
                return 21;
            if (minor == 20)
            {
                int patch = 0;
                if (dot != std::wstring::npos)
                {
                    try
                    {
                        patch = std::stoi(mc.substr(dot + 1));
                    }
                    catch (...)
                    {
                    }
                }
                return patch >= 5 ? 21 : 17;
            }
            if (minor >= 17)
                return 17;
            return 8;
        }
        catch (...)
        {
        }
        return 17;
    }
}
