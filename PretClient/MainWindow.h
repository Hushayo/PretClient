#pragma once

#include "UI/InstancesPage.h"
#include "UI/LocalServerPage.h"
#include "UI/ModsPage.h"
#include "UI/SettingsPage.h"
#include "Update/Updater.h"

// Sidebar shell: NavigationView (Instances / Mods / Local Server / Settings) + version
// footer. Pages are built in code and swapped on selection. Self-update tells
// the user two ways: a Windows toast (AppNotifications, in-process buttons)
// on startup, on manual check, and on a 6h background poll while open, plus a
// persistent in-window InfoBar with an Install button. InstallUpdateLatest()
// runs the one-click flow.
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
        winrt::fire_and_forget CheckForUpdates(bool silent = false);
        // Background poll: re-checks GitHub Releases every few hours while
        // the app is open so a release published mid-session still toasts.
        void StartUpdatePolling();
        // 160ms fade-in for tab content swaps. The timer is held in a member:
        // a timer nobody references can die mid-fade and leave the page stuck
        // at opacity 0 (invisible). Any failure restores opacity instead.
        void FadeContent(Microsoft::UI::Xaml::UIElement const& el);
        // Floating round profile avatar (top-right over content): initial
        // letter, tooltip shows full name, click opens the Profiles dialog.
        void RefreshProfileAvatar();
        // Persistent in-window update notice (survives a dismissed toast).
        void ShowUpdateBanner(hstring const& latest);
        void HideUpdateBanner();

        Microsoft::UI::Xaml::Controls::Grid m_host{};
        Microsoft::UI::Xaml::Controls::Button m_profileButton{};
        Microsoft::UI::Xaml::Controls::TextBlock m_profileAvatar{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navInstances{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navMods{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navLocalServer{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navSettings{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_fadeTimer{ nullptr };
        // In-window update banner (row 0) + its Install button.
        Microsoft::UI::Xaml::Controls::InfoBar m_updateBar{};
        Microsoft::UI::Xaml::Controls::Button m_updateInstallButton{};
        InstancesPage m_instances{};
        ModsPage m_mods{};
        LocalServerPage m_localServer{};
        SettingsPage m_settings{};
        // Update in flight (toast-driven). Blocks a second InstallUpdateLatest.
        bool m_updating = false;
        // Periodic update poll (kept in a member so it survives). Fires while
        // the window is open; each tick re-resolves the latest release.
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_updateTimer{ nullptr };
        // Last version we already toasted for — stops the poll re-notifying
        // every interval for the same release.
        hstring m_lastNotifiedTag{};
    };
}
