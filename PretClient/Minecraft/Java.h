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
    hstring Pick(int requiredMajor); // best candidate path, or "" when none fits
}
