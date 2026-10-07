#pragma once

#include <functional>
#include <vector>

// meta.quiltmc.org v3: per-MC loader versions + launcher-ready profile JSON.
// The profile shape matches Fabric's (inheritsFrom, mainClass, maven
// libraries), so the downloader treats both the same way.
namespace winrt::PretClient::Quilt
{
    using LoaderListFn = std::function<void(std::vector<hstring>)>;

    winrt::fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done);
    Windows::Foundation::IAsyncOperation<hstring> GetLatestLoader(hstring mcVersion);
    Windows::Foundation::IAsyncOperation<Windows::Data::Json::JsonObject> GetProfile(
        hstring mcVersion, hstring loader);
}
