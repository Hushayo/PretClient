#pragma once

#include <functional>
#include <vector>

// maven.neoforged.net release list, filtered to the MC at hand. NeoForge
// versions track MC ("20.4.x" = MC 1.20.4, "21.1.x" = MC 1.21.1), so only
// stable builds with the matching prefix are offered. Installs the same way
// as Forge: its installer jar runs headless (--installClient) and writes
// versions/<mc>-neoforge-<ver>/<mc>-neoforge-<ver>.json.
namespace winrt::PretClient::NeoForge
{
    using LoaderListFn = std::function<void(std::vector<hstring>)>;

    winrt::fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done);
    Windows::Foundation::IAsyncOperation<hstring> GetLatestNeoForge(hstring mcVersion);

    hstring InstallerUrl(hstring neoVersion);
    // "1.21.1-neoforge-21.1.148": where the installer drops its version JSON.
    hstring ExpectedVersionId(hstring mcVersion, hstring neoVersion);
}
