#pragma once

#include <functional>

// Resolves + downloads everything one instance needs into the shared
// roaming .minecraft folder: client jar, libraries (rules-filtered),
// natives extraction, asset index + objects, logging config, and for
// modded instances the loader pieces:
//   fabric/quilt: loader profile libraries (maven) + fabric-api (fabric only)
//   forge/neoforge: installer run (--installClient) + installed profile libraries.
//
// Offline: the vanilla version JSON (versions/<id>/<id>.json) and
// fabric/quilt profiles (versions/<mc>/pretclient-<loader>-<ver>.json) are
// cached on every online run. A later launch with no internet reuses them
// plus the already-downloaded jars/assets and skips every download.
namespace winrt::PretClient::Downloader
{
    using LogFn = std::function<void(hstring const&)>;

    struct PreparedGame
    {
        Windows::Data::Json::JsonObject versionJson{ nullptr }; // vanilla package
        Windows::Data::Json::JsonObject loaderProfile{ nullptr }; // null when vanilla
        hstring vanillaId{};
        hstring clientJar{};
        hstring nativesDir{};
        hstring assetsDir{};
        hstring assetIndexId{};
        hstring loggingPath{};
        hstring gameDir{};
        std::vector<hstring> extraClasspath{}; // loader jars (maven + loader-only artifacts)
        int javaMajor = 8;
    };

    // Per-file progress: label, bytes done/total (total 0 when unknown), B/s.
    using FileProgFn = std::function<void(hstring file, unsigned long long done, unsigned long long total, double bps)>;
    using DoneFn = std::function<void(bool ok, PreparedGame game, hstring error)>;

    // NOTE: gameDir/modsDir are BY VALUE on purpose. PrepareAsync is
    // fire_and_forget: the caller returns while downloads are still in
    // flight, so const-ref params would dangle (this caused "bad
    // allocation" prepare failures). Do not change back to references.
    winrt::fire_and_forget PrepareAsync(
        hstring mcVersion, hstring loader, hstring loaderVersion,
        std::wstring gameDir, std::wstring modsDir, hstring javaPathHint,
        LogFn log, FileProgFn prog, DoneFn done);
}
