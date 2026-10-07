#pragma once

namespace winrt::PretClient
{
    struct Settings
    {
        hstring username{ L"Steve" };
        hstring gameDir{}; // empty = roaming .minecraft
        hstring javaPath{};
        int minMemMb = 512;
        int maxMemMb = 2048;
    };

    Settings LoadSettings();
    void SaveSettings(Settings const& s);
    hstring EffectiveGameDir(Settings const& s);
}
