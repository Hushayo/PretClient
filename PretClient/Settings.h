#pragma once

#include <filesystem>
#include <vector>

namespace winrt::PretClient
{
    struct Settings
    {
        hstring username{ L"Steve" };
        std::vector<hstring> profiles{};
        hstring gameDir{}; // empty = roaming .minecraft
        hstring javaPath{};
        int minMemMb = 512;
        int maxMemMb = 2048;
    };

    Settings LoadSettings();
    void SaveSettings(Settings const& s);
    hstring EffectiveGameDir(Settings const& s);
    // <gameDir>/instances/<id> -- per-instance scratch space.
    std::filesystem::path InstanceDir(Settings const& s, hstring const& instanceId);
    // <gameDir>/instances/<id>/mods -- mods owned by one instance; staged
    // into <gameDir>/mods right before launch so Fabric sees them.
    std::filesystem::path InstanceModsDir(Settings const& s, hstring const& instanceId);
}
