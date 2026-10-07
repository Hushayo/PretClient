#pragma once

#include <functional>
#include <vector>

// api.curseforge.com read API: mod search + file listing with loader + MC
// filters, direct CDN download. Search/list calls need a per-user API key
// (x-api-key header) kept in Settings -- CurseForge hands keys out per app
// at its API console, so the key lives with the user, never in this repo.
// File bytes come from edge.forgecdn.net and need no key.
namespace winrt::PretClient::CurseForge
{
    struct ModHit
    {
        int modId = 0;
        hstring slug{};
        hstring title{};
        hstring description{};
        hstring iconUrl{};
        long long downloads = 0;
    };

    struct ModFile
    {
        hstring url{};
        hstring filename{};
        long long size = 0;
        hstring sha1{};
        hstring displayName{};
    };

    struct ModVersion
    {
        hstring id{}; // file id as text
        hstring versionNumber{}; // displayName
        std::vector<ModFile> files{};
    };

    struct SearchResult
    {
        std::vector<ModHit> hits{};
        long long total = 0;
    };

    using SearchFn = std::function<void(SearchResult)>;
    using VersionsFn = std::function<void(std::vector<ModVersion>)>;
    using PickFn = std::function<void(ModFile)>;

    // Public community key (the same one Prism Launcher documents) so the
    // CurseForge tab works out of the box. A key stored in Settings
    // overrides it via EffectiveApiKey.
    inline hstring DefaultApiKey()
    {
        return hstring{ L"$2a$10$bL4bIL5pUWqfcO7KQtnMReakwtfHbNKh6v1uTpKlzhwoueEJQnPnm" };
    }

    inline hstring EffectiveApiKey(hstring const& stored)
    {
        return stored.empty() ? DefaultApiKey() : stored;
    }

    winrt::fire_and_forget SearchAsync(
        hstring query, hstring mcVersion, hstring loader, int offset, hstring apiKey, SearchFn done);
    winrt::fire_and_forget GetVersionsAsync(
        int modId, hstring mcVersion, hstring loader, hstring apiKey, VersionsFn done);
    winrt::fire_and_forget PickFileAsync(
        int modId, hstring mcVersion, hstring loader, hstring apiKey, PickFn done);

    // Download + size check into destDir/filename. Returns a status message.
    // The CDN URL needs no API key, so this reuses the shared Http client.
    Windows::Foundation::IAsyncOperation<hstring> DownloadFileAsync(ModFile const& file, std::wstring const& destDir);
}
