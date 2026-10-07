#pragma once

#include "UI/InstancesPage.h"
#include "UI/ModsPage.h"
#include "UI/SettingsPage.h"
#include "Update/Updater.h"

// Sidebar shell: NavigationView (Instances / Mods / Settings) + update
// banner + version footer. Pages are built in code and swapped on selection.
namespace winrt::PretClient
{
    struct MainWindow : Microsoft::UI::Xaml::WindowT<MainWindow>
    {
        MainWindow();

    private:
        winrt::fire_and_forget CheckForUpdates();
        // One-click self-update: download the setup in-app with progress,
        // run it silent (it closes + replaces us), installer relaunches us.
        winrt::fire_and_forget InstallUpdate(Microsoft::UI::Xaml::Controls::Button button);
        // 160ms fade-in for tab content swaps. The timer is held in a member:
        // a timer nobody references can die mid-fade and leave the page stuck
        // at opacity 0 (invisible). Any failure restores opacity instead.
        void FadeContent(Microsoft::UI::Xaml::UIElement const& el);

        Microsoft::UI::Xaml::Controls::Grid m_host{};
        Microsoft::UI::Xaml::Controls::StackPanel m_banner{};
        Microsoft::UI::Xaml::Controls::TextBlock m_updateText{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navInstances{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navMods{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navSettings{};
        Microsoft::UI::Xaml::Controls::ProgressBar m_updateProg{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_fadeTimer{ nullptr };
        InstancesPage m_instances{};
        ModsPage m_mods{};
        SettingsPage m_settings{};
        Update::ReleaseAsset m_updateAsset{};
        winrt::hstring m_updateUrl{};
    };
}
