#pragma once

#include "UI/InstancesPage.h"
#include "UI/LocalServerPage.h"
#include "UI/ModsPage.h"
#include "UI/SettingsPage.h"
#include "Update/Updater.h"

// Sidebar shell: NavigationView (Instances / Mods / Local Server / Settings) + version
// footer. Pages are built in code and swapped on selection. Self-update has
// no in-window UI: CheckForUpdates raises a Windows toast (AppNotifications,
// in-process buttons), and InstallUpdateLatest() runs the one-click flow.
namespace winrt::PretClient
{
    struct MainWindow : Microsoft::UI::Xaml::WindowT<MainWindow>
    {
        MainWindow();

        // One-click self-update: re-resolves the latest release, downloads
        // the setup with toast progress, verifies it, runs it silent (it
        // closes + replaces us), installer relaunches us. Safe to call twice.
        winrt::fire_and_forget InstallUpdateLatest();

    private:
        winrt::fire_and_forget CheckForUpdates();
        // 160ms fade-in for tab content swaps. The timer is held in a member:
        // a timer nobody references can die mid-fade and leave the page stuck
        // at opacity 0 (invisible). Any failure restores opacity instead.
        void FadeContent(Microsoft::UI::Xaml::UIElement const& el);
        // Round profile avatar in the top-right header: initial letter,
        // tooltip shows full name, click opens the Profiles dialog.
        void RefreshProfileAvatar();

        Microsoft::UI::Xaml::Controls::Grid m_host{};
        Microsoft::UI::Xaml::Controls::Grid m_topBar{};
        Microsoft::UI::Xaml::Controls::Button m_profileButton{};
        Microsoft::UI::Xaml::Controls::TextBlock m_profileAvatar{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navInstances{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navMods{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navLocalServer{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navSettings{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_fadeTimer{ nullptr };
        InstancesPage m_instances{};
        ModsPage m_mods{};
        LocalServerPage m_localServer{};
        SettingsPage m_settings{};
        // Update in flight (toast-driven). Blocks a second InstallUpdateLatest.
        bool m_updating = false;
    };
}
