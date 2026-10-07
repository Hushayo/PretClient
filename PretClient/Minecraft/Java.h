#pragma once

#include <vector>

// Locate a java.exe able to run a given MC version (by javaVersion.majorVersion).
namespace winrt::PretClient::Java
{
    struct Install
    {
        hstring path{};
        int major = 0;
        bool is64Bit = false;
    };

    std::vector<Install> FindAll(); // probes Mojang runtimes, JAVA_HOME, PATH, registry, Program Files
    Install Verify(hstring const& exePath); // runs "<exe> -version", major 0 = unusable
    struct PickResult
    {
        hstring path{}; // best fit, or "" when none fits
        int checked = 0; // installs probed
        int bestMajor = 0; // newest major seen (even when too old)
    };
    PickResult PickDetailed(int requiredMajor);
    hstring Pick(int requiredMajor); // best candidate path, or "" when none fits
}
