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
        hstring curseforgeKey{}; // per-user CurseForge API key (Mods page tab)
        int minMemMb = 512;
        int maxMemMb = 2048;
        bool fpsBoost = true; // tuned G1GC JVM flags + discrete-GPU env hints
        bool highPriority = true; // launch game above-normal priority
        hstring extraJvmArgs{}; // user JVM args, appended after the boost flags
    };

    Settings LoadSettings();
    void SaveSettings(Settings const& s);
    hstring EffectiveGameDir(Settings const& s);
    // <gameDir>/instances/<id> -- per-instance root (legacy scratch space).
    // New layout: <gameDir>/instances/<id>/game is the isolated game folder
    // (saves, configs, logs, mods, resourcepacks). The shared <gameDir> root
    // stays as the download cache (libraries, assets, versions).
    std::filesystem::path InstanceDir(Settings const& s, hstring const& instanceId);
    // <gameDir>/instances/<id>/game -- isolated work dir for one instance.
    // This is game_directory + logs location, so old and new versions never
    // share options.txt / logs / saves.
    std::filesystem::path InstanceGameDir(Settings const& s, hstring const& instanceId);
    // <gameDir>/instances/<id>/game/mods -- mods owned by one instance.
    // The game reads them in place; no staging into a shared folder.
    std::filesystem::path InstanceModsDir(Settings const& s, hstring const& instanceId);
    // <gameDir>/instances/<id>/game/resourcepacks -- packs owned by one
    // instance; read in place (works for vanilla + all loaders).
    std::filesystem::path InstanceResourcePacksDir(Settings const& s, hstring const& instanceId);
    // One-time migration: move legacy <instance>/mods + <instance>/resourcepacks
    // into <instance>/game/ and create the game dir. Safe to call every launch.
    void EnsureInstanceGameDir(Settings const& s, hstring const& instanceId);
}
