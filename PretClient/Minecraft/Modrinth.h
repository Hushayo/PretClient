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

    struct Dependency
    {
        hstring projectId{}; // empty when the author pinned only a version
        hstring versionId{}; // author's pinned build (may be empty)
        hstring type{}; // required | optional | incompatible | embedded
    };

    struct ModVersion
    {
        hstring id{};
        hstring projectId{};
        hstring versionNumber{};
        std::vector<ModFile> files{};
        std::vector<Dependency> dependencies{};
        std::vector<hstring> gameVersions{};
        std::vector<hstring> loaders{};
    };

    struct SearchResult
    {
        std::vector<ModHit> hits{};
        long long total = 0;
    };

    using SearchFn = std::function<void(SearchResult)>;
    using VersionsFn = std::function<void(std::vector<ModVersion>)>;
    using PickFn = std::function<void(ModFile)>;
    using VersionFn = std::function<void(ModVersion)>;

    winrt::fire_and_forget SearchAsync(
        hstring query, hstring mcVersion, hstring loader, int offset, SearchFn done,
        hstring projectType = L"mod");
    winrt::fire_and_forget GetVersionsAsync(
        hstring projectIdOrSlug, hstring mcVersion, hstring loader, VersionsFn done);
    winrt::fire_and_forget PickFileAsync(hstring projectIdOrSlug, hstring mcVersion, hstring loader, PickFn done);
    // Version-level resolve (keeps the dependency list, which PickFileAsync
    // drops) for the auto-dependency installer in ModDeps.
    winrt::fire_and_forget PickVersionAsync(
        hstring projectIdOrSlug, hstring mcVersion, hstring loader, VersionFn done);

    // Awaitable building blocks for ModDeps (raw WinRT JSON, so they can be
    // co_awaited; parsing stays in plain C++ below).
    Windows::Foundation::IAsyncOperation<Windows::Data::Json::JsonArray> ListVersionsJsonAsync(
        hstring projectIdOrSlug, hstring mcVersion, hstring loader);
    Windows::Foundation::IAsyncOperation<Windows::Data::Json::JsonObject> GetVersionJsonAsync(hstring versionId);
    // Best-effort project title for "missing dependency" messages.
    Windows::Foundation::IAsyncOperation<hstring> GetProjectTitleAsync(hstring projectIdOrSlug);
    std::vector<ModVersion> ParseVersions(Windows::Data::Json::JsonArray const& arr);
    ModVersion ParseVersion(Windows::Data::Json::JsonObject const& o);

    // Download + size check into destDir/filename. Returns a status message.
    Windows::Foundation::IAsyncOperation<hstring> DownloadFileAsync(ModFile const& file, std::wstring const& destDir);

    // fabric-api (P7dR8mSH): newest listed file for (mc, fabric), skipped when
    // a fabric-api*.jar already sits in modsDir. Returns status text.
    Windows::Foundation::IAsyncOperation<hstring> EnsureFabricApiAsync(
        std::wstring const& modsDir, hstring mcVersion);
}
