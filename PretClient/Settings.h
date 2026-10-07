#pragma once

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
}
