#pragma once

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

    private:
        void SetStatus(hstring const& line);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::TextBox m_username{};
        Microsoft::UI::Xaml::Controls::TextBox m_gameDir{};
        Microsoft::UI::Xaml::Controls::TextBox m_java{};
        Microsoft::UI::Xaml::Controls::ComboBox m_mem{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
    };
}
