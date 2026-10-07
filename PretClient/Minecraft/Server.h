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
    // First launchable jar in a server folder (server.jar, spigot-*.jar,
    // minecraft_server*.jar, ...). Skips installers/BuildTools. Empty if none.
    std::filesystem::path FindServerJar(std::filesystem::path const& dir);

    // In-app server consoles, keyed by server id. Output is pumped on a
    // background thread into the sink; the UI marshals it to the page.
    using ConsoleLineFn = std::function<void(hstring id, std::string line, bool exited, int exitCode)>;
    void SetConsoleSink(ConsoleLineFn fn);
    bool ConsoleRunning(hstring const& id);
    void* ConsoleHandle(hstring const& id); // process HANDLE for stats, null when stopped
    bool StartConsole(hstring const& id, std::filesystem::path const& dir,
        std::filesystem::path const& jar, hstring const& javaExe, int maxMemMb, hstring& error);
    void SendConsole(hstring const& id, std::wstring const& line);
    void StopConsole(hstring const& id); // graceful "stop", kill fallback

    // Runs "java -jar <jar> <args>" hidden in workDir, waiting on a
    // background thread. Returns the process exit code (-1 on failure).
    Windows::Foundation::IAsyncOperation<int> RunJavaJarAsync(
        hstring javaExe, hstring jarPath, hstring args, hstring workDir);
    // Minimum Java major for a server on this MC version.
    int RequiredJava(hstring const& mcVersion);
    // Creates backups/<label>-<timestamp>.tar.gz of the server folder.
    // Restore extracts a backup over the folder (server must be stopped).
    // Both run the wait off-thread; "" = success, else error text.
    Windows::Foundation::IAsyncOperation<hstring> BackupServerAsync(
        std::filesystem::path const& dir, hstring const& label);
    Windows::Foundation::IAsyncOperation<hstring> RestoreBackupAsync(
        std::filesystem::path const& dir, std::filesystem::path const& backup);
}
