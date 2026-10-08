#include "pch.h"
#include "CurseForge.h"
#include "Http.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::CurseForge
{
    namespace
    {
        constexpr wchar_t kApi[] = L"https://api.curseforge.com/v1";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";
        constexpr int kGameMinecraft = 432;
        constexpr int kClassMods = 6;

        // CurseForge modLoaderType enum: 0 = any (omit), 1 = Forge,
        // 4 = Fabric, 5 = Quilt, 6 = NeoForge.
        int LoaderType(hstring const& loader)
        {
            std::wstring l{ loader };
            if (l == L"forge")
                return 1;
            if (l == L"fabric")
                return 4;
            if (l == L"quilt")
                return 5;
            if (l == L"neoforge")
                return 6;
            return 0;
        }

        // Authed fetch: the shared Http client carries only a User-Agent, so
        // CurseForge calls go through a per-call client that also sends the
        // user's x-api-key. Search/list traffic is tiny (no bulk downloads).
        IAsyncOperation<hstring> GetAuthedStringAsync(hstring url, hstring apiKey)
        {
            Windows::Web::Http::HttpClient client;
            try
            {
                client.DefaultRequestHeaders().Append(L"User-Agent", kUA);
            }
            catch (...)
            {
            }
            try
            {
                client.DefaultRequestHeaders().Append(L"x-api-key", apiKey);
            }
            catch (...)
            {
            }
            try
            {
                client.DefaultRequestHeaders().Append(L"Accept", L"application/json");
            }
            catch (...)
            {
            }
            auto resp = co_await client.GetAsync(Uri{ url });
            resp.EnsureSuccessStatusCode();
            co_return co_await resp.Content().ReadAsStringAsync();
        }

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

        int OptInt(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Number)
                    return static_cast<int>(o.GetNamedNumber(key));
            }
            catch (...)
            {
            }
            return 0;
        }

        long long OptLong(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Number)
                    return static_cast<long long>(o.GetNamedNumber(key));
            }
            catch (...)
            {
            }
            return 0;
        }

        ModFile ToFile(JsonObject const& o)
        {
            ModFile f{};
            f.displayName = OptStr(o, L"displayName");
            f.filename = OptStr(o, L"fileName");
            f.url = OptStr(o, L"downloadUrl");
            f.size = OptLong(o, L"fileLength");
            try
            {
                if (o.HasKey(L"hashes"))
                {
                    for (auto const& hv : o.GetNamedArray(L"hashes"))
                    {
                        if (hv.ValueType() != JsonValueType::Object)
                            continue;
                        auto h = hv.GetObject();
                        // algo 1 = sha1.
                        if (OptInt(h, L"algo") == 1)
                            f.sha1 = OptStr(h, L"value");
                    }
                }
                if (o.HasKey(L"dependencies"))
                {
                    for (auto const& dv : o.GetNamedArray(L"dependencies"))
                    {
                        if (dv.ValueType() != JsonValueType::Object)
                            continue;
                        auto d = dv.GetObject();
                        Dependency dep{};
                        dep.modId = OptInt(d, L"modId");
                        dep.relationType = OptInt(d, L"relationType");
                        if (dep.modId != 0)
                            f.dependencies.push_back(dep);
                    }
                }
            }
            catch (...)
            {
            }
            try
            {
                f.filename = Uri::UnescapeComponent(f.filename);
            }
            catch (...)
            {
            }
            return f;
        }

        bool FileUsable(JsonObject const& o)
        {
            try
            {
                if (o.HasKey(L"isAvailable") &&
                    o.GetNamedValue(L"isAvailable").ValueType() == JsonValueType::Boolean &&
                    !o.GetNamedBoolean(L"isAvailable"))
                    return false;
            }
            catch (...)
            {
            }
            return !OptStr(o, L"downloadUrl").empty() && !OptStr(o, L"fileName").empty();
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
                    if (!FileUsable(o))
                        continue;
                    ModVersion v{};
                    v.id = to_hstring(OptInt(o, L"id"));
                    v.versionNumber = OptStr(o, L"displayName");
                    if (v.versionNumber.empty())
                        v.versionNumber = OptStr(o, L"fileName");
                    v.files.push_back(ToFile(o));
                    if (!v.id.empty() && !v.files.front().url.empty())
                        out.push_back(std::move(v));
                }
            }
            catch (...)
            {
            }
            return out;
        }

        IAsyncOperation<JsonArray> FileListJsonAsync(
            int modId, hstring mcVersion, hstring loader, hstring apiKey)
        {
            JsonArray out{ nullptr };
            try
            {
                std::wstring url = std::wstring{ kApi } + L"/mods/" + std::to_wstring(modId) + L"/files?";
                bool first = true;
                auto add = [&](std::wstring const& kv) {
                    if (!first)
                        url += L"&";
                    url += kv;
                    first = false;
                };
                if (!std::wstring{ mcVersion }.empty())
                    add(L"gameVersion=" + std::wstring{ Http::Escape(mcVersion) });
                if (int lt = LoaderType(loader); lt != 0)
                    add(L"modLoaderType=" + std::to_wstring(lt));
                add(L"pageSize=30");
                auto text = co_await GetAuthedStringAsync(hstring{ url }, apiKey);
                auto root = JsonObject::Parse(text);
                if (root.HasKey(L"data"))
                    out = root.GetNamedArray(L"data");
            }
            catch (...)
            {
            }
            co_return out;
        }
    } // namespace

    fire_and_forget SearchAsync(
        hstring query, hstring mcVersion, hstring loader, int offset, hstring apiKey, SearchFn done)
    {
        SearchResult out{};
        try
        {
            std::wstring url = std::wstring{ kApi } + L"/mods/search?gameId=" +
                std::to_wstring(kGameMinecraft) + L"&classId=" + std::to_wstring(kClassMods);
            if (!std::wstring{ query }.empty())
                url += L"&searchFilter=" + std::wstring{ Http::Escape(query) };
            if (!std::wstring{ mcVersion }.empty())
                url += L"&gameVersion=" + std::wstring{ Http::Escape(mcVersion) };
            if (int lt = LoaderType(loader); lt != 0)
                url += L"&modLoaderType=" + std::to_wstring(lt);
            url += L"&pageSize=20&index=" + std::to_wstring(offset) +
                L"&sortField=2&sortOrder=desc"; // 2 = popularity
            auto text = co_await GetAuthedStringAsync(hstring{ url }, apiKey);
            auto root = JsonObject::Parse(text);
            if (root.HasKey(L"data"))
            {
                for (auto const& hv : root.GetNamedArray(L"data"))
                {
                    if (hv.ValueType() != JsonValueType::Object)
                        continue;
                    auto o = hv.GetObject();
                    ModHit hit{};
                    hit.modId = OptInt(o, L"id");
                    hit.slug = OptStr(o, L"slug");
                    hit.title = OptStr(o, L"name");
                    hit.description = OptStr(o, L"summary");
                    hit.downloads = OptLong(o, L"downloadCount");
                    try
                    {
                        if (o.HasKey(L"logo"))
                        {
                            auto logo = o.GetNamedObject(L"logo");
                            hit.iconUrl = OptStr(logo, L"url");
                        }
                    }
                    catch (...)
                    {
                    }
                    if (hit.modId != 0)
                        out.hits.push_back(std::move(hit));
                }
            }
            try
            {
                if (root.HasKey(L"pagination"))
                    out.total = OptLong(root.GetNamedObject(L"pagination"), L"totalCount");
            }
            catch (...)
            {
            }
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    fire_and_forget GetVersionsAsync(
        int modId, hstring mcVersion, hstring loader, hstring apiKey, VersionsFn done)
    {
        std::vector<ModVersion> out;
        try
        {
            out = ListedVersions(co_await FileListJsonAsync(modId, mcVersion, loader, apiKey));
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    fire_and_forget PickFileAsync(
        int modId, hstring mcVersion, hstring loader, hstring apiKey, PickFn done)
    {
        ModFile picked{};
        try
        {
            auto versions = ListedVersions(co_await FileListJsonAsync(modId, mcVersion, loader, apiKey));
            if (!versions.empty() && !versions.front().files.empty())
                picked = versions.front().files.front();
        }
        catch (...)
        {
        }
        done(std::move(picked));
    }

    fire_and_forget PickVersionAsync(
        int modId, hstring mcVersion, hstring loader, hstring apiKey, VersionFn done)
    {
        ModVersion out{};
        try
        {
            auto versions = ListedVersions(co_await FileListJsonAsync(modId, mcVersion, loader, apiKey));
            if (!versions.empty())
                out = std::move(versions.front());
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    IAsyncOperation<JsonArray> ListFilesJsonAsync(
        int modId, hstring mcVersion, hstring loader, hstring apiKey)
    {
        JsonArray out{ nullptr };
        try
        {
            out = co_await FileListJsonAsync(modId, mcVersion, loader, apiKey);
        }
        catch (...)
        {
        }
        co_return out;
    }

    IAsyncOperation<hstring> GetModNameAsync(int modId, hstring apiKey)
    {
        hstring name{};
        try
        {
            std::wstring url = std::wstring{ kApi } + L"/mods/" + std::to_wstring(modId);
            auto text = co_await GetAuthedStringAsync(hstring{ url }, apiKey);
            auto root = JsonObject::Parse(text);
            if (root.HasKey(L"data"))
                name = OptStr(root.GetNamedObject(L"data"), L"name");
        }
        catch (...)
        {
        }
        co_return name;
    }

    std::vector<ModVersion> ParseVersions(JsonArray const& arr)
    {
        return ListedVersions(arr);
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
            // CDN bytes need no API key.
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
}
