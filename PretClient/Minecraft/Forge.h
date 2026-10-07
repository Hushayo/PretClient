#pragma once

#include <functional>
#include <vector>

// files.minecraftforge.net promotions (recommended/latest per MC) plus the
// maven installer layout. Forge installs by running its own installer jar
// headless (java -jar <installer> --installClient <gameDir>), which writes
// versions/<mc>-forge-<ver>/<mc>-forge-<ver>.json -- the downloader then
// treats that JSON like a fabric/quilt profile (libraries + mainClass).
namespace winrt::PretClient::Forge
{
    using LoaderListFn = std::function<void(std::vector<hstring>)>;

    winrt::fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done);
    Windows::Foundation::IAsyncOperation<hstring> GetLatestForge(hstring mcVersion);

    // "47.2.0" + "1.20.1" -> "1.20.1-47.2.0" (passes a full artifact through).
    hstring ArtifactId(hstring mcVersion, hstring forgeVersion);
    hstring InstallerUrl(hstring mcVersion, hstring forgeVersion);
    // "1.20.1-forge-47.2.0": where the installer drops its version JSON.
    hstring ExpectedVersionId(hstring mcVersion, hstring forgeVersion);
}
