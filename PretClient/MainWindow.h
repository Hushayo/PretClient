#pragma once

namespace winrt::PretClient
{
    struct MainWindow : Microsoft::UI::Xaml::WindowT<MainWindow>
    {
        MainWindow();

    private:
        void OnPlayClicked();
        void OnUpdateClicked();
        winrt::fire_and_forget CheckForUpdates();
        winrt::fire_and_forget RunOfflineLaunchAsync(winrt::hstring username, winrt::hstring version);
        void AppendLog(winrt::hstring const& line);

        Microsoft::UI::Xaml::Controls::TextBox m_username{};
        Microsoft::UI::Xaml::Controls::ComboBox m_versions{};
        Microsoft::UI::Xaml::Controls::Button m_play{};
        Microsoft::UI::Xaml::Controls::ProgressBar m_progress{};
        Microsoft::UI::Xaml::Controls::TextBox m_log{};
        Microsoft::UI::Xaml::Controls::StackPanel m_updateBanner{};
        Microsoft::UI::Xaml::Controls::TextBlock m_updateText{};
        winrt::hstring m_updateUrl{};
    };
}
