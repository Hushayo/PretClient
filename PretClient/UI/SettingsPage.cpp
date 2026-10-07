#include "pch.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "../Paths.h"
#include "../Settings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;

namespace winrt::PretClient
{
    SettingsPage::SettingsPage()
    {
        m_root.Spacing(12);
        m_root.Padding(ThicknessHelper::FromUniformLength(24));
        m_root.MaxWidth(560);
        m_root.HorizontalAlignment(HorizontalAlignment::Left);

        TextBlock head{};
        head.Text(L"Settings");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_root.Children().Append(head);

        m_username.Header(box_value(L"Username (offline, non-premium)"));
        m_username.MaxLength(16);
        m_root.Children().Append(m_username);

        m_gameDir.Header(box_value(L"Game folder (roaming .minecraft)"));
        m_root.Children().Append(m_gameDir);

        Button def{};
        def.Content(box_value(L"Use default roaming folder"));
        def.Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_gameDir.Text(hstring{ Paths::DefaultGameDir().wstring() });
        });
        m_root.Children().Append(def);

        m_java.Header(box_value(L"Java path (empty = auto-detect)"));
        m_root.Children().Append(m_java);

        TextBlock memHead{};
        memHead.Text(L"Max memory");
        m_root.Children().Append(memHead);
        m_mem.Items().Append(box_value(L"1024 MB"));
        m_mem.Items().Append(box_value(L"2048 MB"));
        m_mem.Items().Append(box_value(L"4096 MB"));
        m_mem.Items().Append(box_value(L"8192 MB"));
        m_root.Children().Append(m_mem);

        Button save{};
        save.Content(box_value(L"Save"));
        save.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        save.Click([this](IInspectable const&, RoutedEventArgs const&) {
            Settings s{};
            s.username = m_username.Text().empty() ? hstring{ L"Steve" } : hstring{ m_username.Text() };
            s.gameDir = m_gameDir.Text();
            s.javaPath = m_java.Text();
            int mems[] = { 1024, 2048, 4096, 8192 };
            int idx = m_mem.SelectedIndex();
            s.maxMemMb = (idx >= 0 && idx < 4) ? mems[idx] : 2048;
            s.minMemMb = 512;
            SaveSettings(s);
            SetStatus(L"Saved.");
        });
        m_root.Children().Append(save);

        m_status.Opacity(0.7);
        m_root.Children().Append(m_status);

        Refresh();
    }

    void SettingsPage::Refresh()
    {
        auto s = LoadSettings();
        m_username.Text(s.username);
        m_gameDir.Text(EffectiveGameDir(s));
        m_java.Text(s.javaPath);
        int idx = 1;
        if (s.maxMemMb >= 8192)
            idx = 3;
        else if (s.maxMemMb >= 4096)
            idx = 2;
        else if (s.maxMemMb <= 1024)
            idx = 0;
        m_mem.SelectedIndex(idx);
    }

    void SettingsPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }
}
