#pragma once

#include <functional>
#include <vector>

// api.modrinth.com read API: mod search, version listing with loader +
// MC filters, primary-file download. Used by the Mods page and by the
// fabric-api auto-install.
namespace winrt::PretClient::Modrinth
{
    struct ModHit
    {
        hstring projectId{};
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
        hstring sha512{};
        bool primary = false;
    };

    struct ModVersion
    {
        hstring id{};
        hstring versionNumber{};
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

    winrt::fire_and_forget SearchAsync(
        hstring query, hstring mcVersion, hstring loader, int offset, SearchFn done);
    winrt::fire_and_forget GetVersionsAsync(
        hstring projectIdOrSlug, hstring mcVersion, hstring loader, VersionsFn done);
    winrt::fire_and_forget PickFileAsync(hstring projectIdOrSlug, hstring mcVersion, hstring loader, PickFn done);

    // Download + size check into destDir/filename. Returns a status message.
    Windows::Foundation::IAsyncOperation<hstring> DownloadFileAsync(ModFile const& file, std::wstring const& destDir);

    // fabric-api (P7dR8mSH): newest listed file for (mc, fabric), skipped when
    // a fabric-api*.jar already sits in <gameDir>/mods. Returns status text.
    Windows::Foundation::IAsyncOperation<hstring> EnsureFabricApiAsync(
        std::wstring const& gameDir, hstring mcVersion);
}
