#pragma once

#include <functional>

// Settings page: tabbed (General | Advanced) inside one scroll view.
// General holds everyday items (profile, folders, performance); Advanced
// holds Java overrides and the CurseForge key so the main view stays clean.
// Save + update row sit below both tabs and always stay reachable.
namespace winrt::PretClient
{
    struct SettingsPage
    {
        SettingsPage();
        Microsoft::UI::Xaml::Controls::Grid Root() const
        {
            return m_root;
        }
        void Refresh();
        void SetStatus(hstring const& line);
        void OnCheckUpdates(std::function<void()> cb)
        {
            m_onCheckUpdates = std::move(cb);
        }

    private:
        void SetTab(int idx);
        void PaintTabs();
        winrt::fire_and_forget DownloadJava();

        Microsoft::UI::Xaml::Controls::Grid m_root{};
        Microsoft::UI::Xaml::Controls::ScrollViewer m_scroll{};
        Microsoft::UI::Xaml::Controls::StackPanel m_inner{};
        Microsoft::UI::Xaml::Controls::StackPanel m_general{};
        Microsoft::UI::Xaml::Controls::StackPanel m_advanced{};
        Microsoft::UI::Xaml::Controls::Button m_tabGeneral{};
        Microsoft::UI::Xaml::Controls::Button m_tabAdvanced{};
        int m_tab = 0;
        Microsoft::UI::Xaml::Controls::TextBox m_username{};
        Microsoft::UI::Xaml::Controls::TextBox m_gameDir{};
        Microsoft::UI::Xaml::Controls::TextBox m_java{};
        Microsoft::UI::Xaml::Controls::TextBox m_cfKey{};
        Microsoft::UI::Xaml::Controls::ComboBox m_mem{};
        Microsoft::UI::Xaml::Controls::CheckBox m_fpsBoost{};
        Microsoft::UI::Xaml::Controls::CheckBox m_highPriority{};
        Microsoft::UI::Xaml::Controls::TextBox m_extraJvm{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        std::function<void()> m_onCheckUpdates{};
    };
}
