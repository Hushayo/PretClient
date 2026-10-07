#pragma once

#include <functional>

// Settings page: username, roaming .minecraft folder, java override, RAM.
namespace winrt::PretClient
{
    struct SettingsPage
    {
        SettingsPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
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
        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::ScrollViewer m_scroll{};
        Microsoft::UI::Xaml::Controls::StackPanel m_inner{};
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
