#pragma once

#include <functional>
#include <vector>

// meta.fabricmc.net: loader versions + launcher-ready profile JSON.
// fabric-api itself resolves through the Modrinth module.
namespace winrt::PretClient::Fabric
{
    using LoaderListFn = std::function<void(std::vector<hstring>)>;

    winrt::fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done);
    Windows::Foundation::IAsyncOperation<hstring> GetLatestLoader(hstring mcVersion);
    Windows::Foundation::IAsyncOperation<Windows::Data::Json::JsonObject> GetProfile(
        hstring mcVersion, hstring loader);

    // group:artifact:version[:classifier] + maven base -> relative jar path.
    hstring MavenJarPath(hstring name);
}
