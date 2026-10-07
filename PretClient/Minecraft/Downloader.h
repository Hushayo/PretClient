#pragma once

#include <functional>

// Resolves + downloads everything one instance needs into the shared
// roaming .minecraft folder: client jar, libraries (rules-filtered),
// natives extraction, asset index + objects, logging config, and for
// fabric instances the loader profile libraries + fabric-api.
namespace winrt::PretClient::Downloader
{
    using LogFn = std::function<void(hstring const&)>;

    struct PreparedGame
    {
        Windows::Data::Json::JsonObject versionJson{ nullptr };
        Windows::Data::Json::JsonObject fabricProfile{ nullptr }; // null unless fabric
        hstring vanillaId{};
        hstring clientJar{};
        hstring nativesDir{};
        hstring assetsDir{};
        hstring assetIndexId{};
        hstring loggingPath{};
        hstring gameDir{};
        std::vector<hstring> extraClasspath{}; // fabric loader/intermediary jars
        std::vector<hstring> fabricJvmExtras{};
        hstring fabricMainClass{};
        int javaMajor = 8;
    };

    using DoneFn = std::function<void(bool ok, PreparedGame game, hstring error)>;

    winrt::fire_and_forget PrepareAsync(
        hstring mcVersion, hstring loader, hstring loaderVersion,
        std::wstring const& gameDir, LogFn log, DoneFn done);
}
