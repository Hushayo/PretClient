#pragma once

#include <filesystem>
#include <functional>
#include <vector>

// Locate a java.exe able to run a given MC version (by javaVersion.majorVersion).
// When nothing suitable is installed, EnsureAsync downloads a Temurin JRE/JDK
// into %APPDATA%\PretClient\java so launches Just Work.
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
    // Same, but never returns a newer major than maxMajor (maxMajor <= 0 =
    // no cap). Legacy launchwrapper versions die on Java 9+, so they pin
    // required == max == 8. bestMajor is the newest major within the cap.
    PickResult PickCapped(int requiredMajor, int maxMajor);
    hstring Pick(int requiredMajor); // best candidate path, or "" when none fits

    // Adoptium feature train that satisfies a MC java requirement
    // (8 -> 8, 16/17 -> 17, everything newer -> 21).
    int FeatureFor(int requiredMajor);
    std::filesystem::path ManagedRoot(); // %APPDATA%\PretClient\java
    std::filesystem::path ManagedHome(int feature); // ...\temurin-<feature>
    // Already-downloaded managed java.exe, or "" when absent/unusable.
    hstring ManagedJava(int requiredMajor);
    using LogFn = std::function<void(hstring const&)>;
    using ProgFn = std::function<void(unsigned long long done, unsigned long long total, double bytesPerSec)>;
    // Best usable java.exe: explicit/system installs first, otherwise
    // downloads + extracts Temurin and returns the managed java.exe.
    // maxMajor (>0) pins the ceiling: legacy launchwrapper versions pass 8
    // here so a Java 21 box still ends up on Temurin 8 instead of crashing.
    // Returns "" when offline or the download fails (the caller logs the
    // outcome). log/prog fire on the calling thread only.
    Windows::Foundation::IAsyncOperation<hstring> EnsureAsync(
        int requiredMajor, LogFn log, ProgFn prog, int maxMajor = 0);
}
