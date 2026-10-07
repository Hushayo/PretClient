#pragma once

#include <functional>
#include <vector>

// piston-meta.mojang.com version manifest: every release, snapshot,
// old_beta and old_alpha, plus per-version package fetch.
// Plain C++ results travel over callbacks (only WinRT types may be
// coroutine results).
namespace winrt::PretClient::Versions
{
    struct McVersion
    {
        hstring id{};
        hstring type{}; // release | snapshot | old_beta | old_alpha
        hstring url{};
        hstring sha1{};
    };

    struct Manifest
    {
        std::vector<McVersion> entries; // manifest order, newest first
        hstring latestRelease{};
        hstring latestSnapshot{};
    };

    using ManifestFn = std::function<void(Manifest)>;
    using VersionJsonFn = std::function<void(Windows::Data::Json::JsonObject)>;

    winrt::fire_and_forget FetchManifestAsync(ManifestFn done);
    Windows::Foundation::IAsyncOperation<Windows::Data::Json::JsonObject> FetchVersionJsonAsync(
        McVersion const& entry);
}
