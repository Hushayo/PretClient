#pragma once

#include "UI/InstancesPage.h"
#include "UI/ModsPage.h"
#include "UI/SettingsPage.h"

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

        Microsoft::UI::Xaml::Controls::Grid m_host{};
        Microsoft::UI::Xaml::Controls::StackPanel m_banner{};
        Microsoft::UI::Xaml::Controls::TextBlock m_updateText{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navInstances{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navMods{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_navSettings{};
        InstancesPage m_instances{};
        ModsPage m_mods{};
        SettingsPage m_settings{};
        winrt::hstring m_updateUrl{};
    };
}
