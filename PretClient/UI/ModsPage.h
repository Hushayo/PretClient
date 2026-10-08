#pragma once

#include "../Minecraft/Instance.h"
#include "../Minecraft/Modrinth.h"
#include "../Minecraft/CurseForge.h"

// Mods page: content tabs (Mods | Resource Packs) plus source tabs
// (Modrinth | CurseForge) up top, auto-loads on tab open, filters locked to
// the selected instance's MC version/loader, live search as you type,
// infinite scroll, per-row install into the selected instance, and a
// per-entry builds dialog for installing one specific version.
// Required mod dependencies auto-install with the mod (see ModDeps);
// optional ones are left alone. Resource packs install into the instance's
// resourcepacks folder (works on vanilla too) with no dependency step.
// Modrinth needs no key; CurseForge needs a per-user API key from Settings.
namespace winrt::PretClient
{
    struct ModsPage
    {
        enum class Source
        {
            Modrinth = 0,
            CurseForge = 1,
        };

        enum class Kind
        {
            Mods = 0,
            Packs = 1,
        };

        ModsPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
        {
            return m_root;
        }
        void RefreshInstances();

    private:
        void SetSource(Source s);
        void PaintSourceTabs();
        void SetKind(Kind k);
        void PaintKindTabs();
        // Head text + search placeholder follow the active kind + source.
        void UpdateHead();
        void FetchPage();
        void AddResultCard(hstring title, hstring desc, hstring meta, hstring iconUrl,
            std::function<void()> onInstall, std::function<void()> onBuilds);
        void SyncFiltersFromTarget();
        void ScheduleSearch();
        winrt::fire_and_forget OnSearch();
        winrt::fire_and_forget OnInstall(Modrinth::ModHit hit);
        // Version-level install: downloads the primary file, then pulls in
        // required dependencies for (mcVersion, loader) via ModDeps.
        winrt::fire_and_forget InstallVersion(Modrinth::ModVersion version,
            hstring mcVersion, hstring loader, std::wstring modsDir);
        // Pack install: downloads the primary file straight into the
        // instance's resourcepacks folder (no loader, no dependencies).
        winrt::fire_and_forget InstallPackVersion(Modrinth::ModVersion version,
            std::wstring packsDir);
        winrt::fire_and_forget BuildsDialog(Modrinth::ModHit hit);
        winrt::fire_and_forget OnInstallCF(CurseForge::ModHit hit);
        winrt::fire_and_forget InstallVersionCF(CurseForge::ModVersion version, int modId,
            hstring mcVersion, hstring loader, hstring apiKey, std::wstring modsDir);
        winrt::fire_and_forget InstallPackVersionCF(CurseForge::ModVersion version,
            std::wstring packsDir);
        winrt::fire_and_forget BuildsDialogCF(CurseForge::ModHit hit);
        winrt::fire_and_forget LoadIcon(hstring url, Microsoft::UI::Xaml::Controls::Image img);
        void SetStatus(hstring const& line);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::ScrollViewer m_scroll{};
        Microsoft::UI::Xaml::Controls::TextBlock m_head{};
        Microsoft::UI::Xaml::Controls::Button m_tabModrinth{};
        Microsoft::UI::Xaml::Controls::Button m_tabCurse{};
        Microsoft::UI::Xaml::Controls::Button m_tabMods{};
        Microsoft::UI::Xaml::Controls::Button m_tabPacks{};
        Microsoft::UI::Xaml::Controls::TextBox m_query{};
        Microsoft::UI::Xaml::Controls::ComboBox m_loader{};
        Microsoft::UI::Xaml::Controls::TextBox m_mc{};
        Microsoft::UI::Xaml::Controls::ComboBox m_target{};
        Microsoft::UI::Xaml::Controls::StackPanel m_results{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        std::vector<Instance> m_targets{};

        Source m_source = Source::Modrinth;
        Kind m_kind = Kind::Mods;
        hstring m_lastQuery{};
        hstring m_lastMc{};
        hstring m_lastLoader{};
        int m_offset = 0;
        long long m_total = 0;
        bool m_loading = false;
        int m_searchGen = 0;
        bool m_syncing = false;
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_debounce{ nullptr };
    };
}
