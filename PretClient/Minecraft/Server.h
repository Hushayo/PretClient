#pragma once

#include <filesystem>
#include <functional>
#include <vector>

// Dedicated server jars: version lists + one-click artifact resolution for
// every software with a public download API. Manual flows (Spigot BuildTools,
// Forge/NeoForge installers) are represented explicitly so the UI can run
// the right post-download step. Plain C++ results travel over callbacks
// (only WinRT types may be coroutine results).
namespace winrt::PretClient::Server
{
    struct SoftwareInfo
    {
        hstring id{};
        hstring label{};
        hstring desc{};
    };

    std::vector<SoftwareInfo> AllSoftware();
    // Newest-first MC list used as fallback and as ordering reference.
    std::vector<hstring> RecentMcVersions();

    enum class InstallKind
    {
        Jar, // drop-in server.jar
        Installer, // run: java -jar <file> --installServer (forge/neoforge)
        BuildTools, // run: java -jar BuildTools.jar --rev <mc> (spigot)
    };

    struct Artifact
    {
        hstring url{};
        hstring fileName{ L"server.jar" };
        hstring sha256{};
        hstring sha1{};
        unsigned long long size = 0;
        InstallKind kind = InstallKind::Jar;
    };

    using VersionListFn = std::function<void(std::vector<hstring>)>;
    using ArtifactFn = std::function<void(Artifact, hstring error)>;

    winrt::fire_and_forget FetchVersionsAsync(hstring softwareId, VersionListFn done);
    winrt::fire_and_forget ResolveArtifactAsync(
        hstring softwareId, hstring mcVersion, ArtifactFn done);

    // <DataDir>/servers -- one subfolder per created server.
    std::filesystem::path ServersDir();
    hstring NewServerId();

    // One created server (servers.json). The folder holds the jar.
    struct ServerEntry
    {
        hstring id{};
        hstring name{};
        hstring software{};
        hstring mcVersion{};
    };
    std::vector<ServerEntry> LoadServers();
    void SaveServers(std::vector<ServerEntry> const& servers);

    // Runs "java -jar <jar> <args>" hidden in workDir, waiting on a
    // background thread. Returns the process exit code (-1 on failure).
    Windows::Foundation::IAsyncOperation<int> RunJavaJarAsync(
        hstring javaExe, hstring jarPath, hstring args, hstring workDir);
    // Minimum Java major for a server on this MC version.
    int RequiredJava(hstring const& mcVersion);
}
