#pragma once

#include "../Minecraft/Instance.h"
#include "../Minecraft/Modrinth.h"

// Mods page: auto-loads Modrinth on tab open, filters locked to the
// selected instance's MC version/loader, live search as you type, infinite
// scroll, per-row install into the selected instance, and a per-mod builds
// dialog for installing one specific version.
namespace winrt::PretClient
{
    struct ModsPage
    {
        ModsPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
        {
            return m_root;
        }
        void RefreshInstances();

    private:
        void FetchPage();
        void SyncFiltersFromTarget();
        void ScheduleSearch();
        winrt::fire_and_forget OnSearch();
        winrt::fire_and_forget OnInstall(Modrinth::ModHit hit);
        winrt::fire_and_forget InstallOneFile(Modrinth::ModFile file, std::wstring modsDir);
        winrt::fire_and_forget BuildsDialog(Modrinth::ModHit hit);
        winrt::fire_and_forget LoadIcon(hstring url, Microsoft::UI::Xaml::Controls::Image img);
        void SetStatus(hstring const& line);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::ScrollViewer m_scroll{};
        Microsoft::UI::Xaml::Controls::TextBox m_query{};
        Microsoft::UI::Xaml::Controls::ComboBox m_loader{};
        Microsoft::UI::Xaml::Controls::TextBox m_mc{};
        Microsoft::UI::Xaml::Controls::ComboBox m_target{};
        Microsoft::UI::Xaml::Controls::StackPanel m_results{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        std::vector<Instance> m_targets{};

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
