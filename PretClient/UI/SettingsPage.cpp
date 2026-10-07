#include "pch.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "../Minecraft/CurseForge.h"
#include "../Paths.h"
#include "../Settings.h"
#include "../Update/Updater.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;

namespace winrt::PretClient
{
    SettingsPage::SettingsPage()
    {
        m_inner.Spacing(12);
        m_inner.Padding(ThicknessHelper::FromUniformLength(24));
        m_inner.MaxWidth(560);
        m_inner.HorizontalAlignment(HorizontalAlignment::Left);

        TextBlock head{};
        head.Text(L"Settings");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_inner.Children().Append(head);

        m_username.Header(box_value(L"Username"));
        m_username.MaxLength(16);
        m_inner.Children().Append(m_username);

        m_gameDir.Header(box_value(L"Game folder (roaming .minecraft)"));
        m_inner.Children().Append(m_gameDir);

        Button def{};
        def.Content(box_value(L"Use default roaming folder"));
        def.Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_gameDir.Text(hstring{ Paths::DefaultGameDir().wstring() });
        });
        m_inner.Children().Append(def);

        m_java.Header(box_value(L"Java path (empty = auto-detect)"));
        m_inner.Children().Append(m_java);

        m_cfKey.Header(box_value(L"CurseForge API key (public community key is pre-filled)"));
        m_cfKey.PlaceholderText(L"Replace with your own key if you have one");
        m_inner.Children().Append(m_cfKey);

        TextBlock memHead{};
        memHead.Text(L"Max memory");
        m_inner.Children().Append(memHead);
        m_mem.Items().Append(box_value(L"1024 MB"));
        m_mem.Items().Append(box_value(L"2048 MB"));
        m_mem.Items().Append(box_value(L"4096 MB"));
        m_mem.Items().Append(box_value(L"8192 MB"));
        m_inner.Children().Append(m_mem);

        TextBlock perfHead{};
        perfHead.Text(L"Performance");
        m_inner.Children().Append(perfHead);

        m_fpsBoost.Content(box_value(L"FPS boost (tuned GC flags)"));
        m_inner.Children().Append(m_fpsBoost);

        m_highPriority.Content(box_value(L"Run game above-normal priority"));
        m_inner.Children().Append(m_highPriority);

        m_extraJvm.Header(box_value(L"Extra JVM args (optional)"));
        m_extraJvm.PlaceholderText(L"e.g. -XX:G1HeapRegionSize=16M");
        m_extraJvm.TextWrapping(TextWrapping::Wrap);
        m_extraJvm.AcceptsReturn(false);
        m_inner.Children().Append(m_extraJvm);

        Button save{};
        save.Content(box_value(L"Save"));
        save.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        save.Click([this](IInspectable const&, RoutedEventArgs const&) {
            // Start from the stored settings so Save never wipes fields this
            // page doesn't edit (profiles list, etc.).
            Settings s = LoadSettings();
            s.username = m_username.Text().empty() ? hstring{ L"Steve" } : hstring{ m_username.Text() };
            s.gameDir = m_gameDir.Text();
            s.javaPath = m_java.Text();
            s.curseforgeKey = m_cfKey.Text();
            int mems[] = { 1024, 2048, 4096, 8192 };
            int idx = m_mem.SelectedIndex();
            s.maxMemMb = (idx >= 0 && idx < 4) ? mems[idx] : 2048;
            s.minMemMb = 512;
            s.fpsBoost = unbox_value_or<bool>(m_fpsBoost.IsChecked(), true);
            s.highPriority = unbox_value_or<bool>(m_highPriority.IsChecked(), true);
            s.extraJvmArgs = m_extraJvm.Text();
            SaveSettings(s);
            SetStatus(L"Saved.");
        });
        m_inner.Children().Append(save);

        m_status.Opacity(0.7);
        m_inner.Children().Append(m_status);

        TextBlock ver{};
        ver.Opacity(0.5);
        ver.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        ver.Text(hstring{ L"PretClient " } + Update::CurrentVersionTag());
        ver.VerticalAlignment(VerticalAlignment::Center);

        StackPanel updateRow{};
        updateRow.Orientation(Orientation::Horizontal);
        updateRow.Spacing(12);
        updateRow.Children().Append(ver);
        Button check{};
        check.Content(box_value(L"Check for updates"));
        check.Click([this](IInspectable const&, RoutedEventArgs const&) {
            SetStatus(L"Checking for updates...");
            if (m_onCheckUpdates)
                m_onCheckUpdates();
        });
        updateRow.Children().Append(check);
        m_inner.Children().Append(updateRow);

        // Scrollable so the update row stays reachable in short windows.
        m_scroll.Content(m_inner);
        m_scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_scroll.HorizontalScrollMode(ScrollMode::Disabled);
        m_root.Children().Append(m_scroll);

        Refresh();
    }

    void SettingsPage::Refresh()
    {
        auto s = LoadSettings();
        m_username.Text(s.username);
        m_gameDir.Text(EffectiveGameDir(s));
        m_java.Text(s.javaPath);
        m_cfKey.Text(s.curseforgeKey.empty() ? CurseForge::DefaultApiKey() : s.curseforgeKey);
        int idx = 1;
        if (s.maxMemMb >= 8192)
            idx = 3;
        else if (s.maxMemMb >= 4096)
            idx = 2;
        else if (s.maxMemMb <= 1024)
            idx = 0;
        m_mem.SelectedIndex(idx);
        // SetValue (not IsChecked(box_value(...))): the IsChecked setter
        // takes IReference<bool> and the boxed conversion does not compile
        // here, while SetValue takes plain IInspectable.
        auto checkedProp =
            Microsoft::UI::Xaml::Controls::Primitives::ToggleButton::IsCheckedProperty();
        m_fpsBoost.SetValue(checkedProp, box_value(s.fpsBoost));
        m_highPriority.SetValue(checkedProp, box_value(s.highPriority));
        m_extraJvm.Text(s.extraJvmArgs);
    }

    void SettingsPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }
}
