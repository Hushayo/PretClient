#include "pch.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "../Minecraft/CurseForge.h"
#include "../Minecraft/Java.h"
#include "../Paths.h"
#include "../Settings.h"
#include "../Update/Updater.h"
#include <coroutine>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;

namespace winrt::PretClient
{
    namespace
    {
        // One settings group: title + padded body inside a theme-aware card.
        // Cards match Instances/Mods/LocalServer styling and keep long forms
        // scannable instead of one flat wall of text boxes.
        Border MakeCard(hstring const& title, StackPanel const& body)
        {
            TextBlock head{};
            head.Text(title);
            head.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            head.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

            StackPanel wrap{};
            wrap.Spacing(8);
            wrap.Children().Append(head);
            wrap.Children().Append(body);

            Border card{};
            card.Background(Theme::CardBrush());
            card.BorderBrush(Theme::CardStroke());
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
            card.Padding(ThicknessHelper::FromUniformLength(16));
            card.HorizontalAlignment(HorizontalAlignment::Stretch);
            card.Child(wrap);
            return card;
        }
    } // namespace

    SettingsPage::SettingsPage()
    {
        // Inner column: capped width for readability on wide windows, shrinks
        // on narrow ones so fields never clip horizontally.
        m_inner.Spacing(12);
        m_inner.Padding(ThicknessHelper::FromUniformLength(24));
        m_inner.MaxWidth(640);
        m_inner.HorizontalAlignment(HorizontalAlignment::Left);
        m_inner.VerticalAlignment(VerticalAlignment::Top);

        TextBlock head{};
        head.Text(L"Settings");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_inner.Children().Append(head);

        TextBlock sub{};
        sub.Text(L"Profile, folders, performance and updates. Java + API key live under Advanced.");
        sub.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        sub.Opacity(0.6);
        sub.TextWrapping(TextWrapping::Wrap);
        m_inner.Children().Append(sub);

        // Tab switcher: everyday settings stay front and center, overrides
        // move to Advanced instead of cluttering the main view.
        StackPanel tabs{};
        tabs.Orientation(Orientation::Horizontal);
        tabs.Spacing(8);
        m_tabGeneral.Content(box_value(L"General"));
        m_tabGeneral.Click([this](IInspectable const&, RoutedEventArgs const&) { SetTab(0); });
        m_tabAdvanced.Content(box_value(L"Advanced"));
        m_tabAdvanced.Click([this](IInspectable const&, RoutedEventArgs const&) { SetTab(1); });
        tabs.Children().Append(m_tabGeneral);
        tabs.Children().Append(m_tabAdvanced);
        m_inner.Children().Append(tabs);

        m_general.Spacing(12);
        m_advanced.Spacing(12);

        // General: Profile
        {
            StackPanel body{};
            body.Spacing(8);
            m_username.Header(box_value(L"Username"));
            m_username.MaxLength(16);
            m_username.HorizontalAlignment(HorizontalAlignment::Stretch);
            body.Children().Append(m_username);
            m_general.Children().Append(MakeCard(L"Profile", body));
        }

        // General: Game folder
        {
            StackPanel body{};
            body.Spacing(8);
            m_gameDir.Header(box_value(L"Game folder (roaming .minecraft)"));
            m_gameDir.HorizontalAlignment(HorizontalAlignment::Stretch);
            body.Children().Append(m_gameDir);

            Button def{};
            def.Content(box_value(L"Use default roaming folder"));
            def.HorizontalAlignment(HorizontalAlignment::Left);
            def.Click([this](IInspectable const&, RoutedEventArgs const&) {
                m_gameDir.Text(hstring{ Paths::DefaultGameDir().wstring() });
            });
            body.Children().Append(def);
            m_general.Children().Append(MakeCard(L"Game folder", body));
        }

        // General: Performance (everyday knobs only; JVM flags live in Advanced)
        {
            StackPanel body{};
            body.Spacing(8);
            m_mem.Header(box_value(L"Max memory"));
            m_mem.MinWidth(160);
            m_mem.HorizontalAlignment(HorizontalAlignment::Left);
            m_mem.Items().Append(box_value(L"1024 MB"));
            m_mem.Items().Append(box_value(L"2048 MB"));
            m_mem.Items().Append(box_value(L"4096 MB"));
            m_mem.Items().Append(box_value(L"8192 MB"));
            body.Children().Append(m_mem);

            m_fpsBoost.Content(box_value(L"FPS boost (tuned GC flags)"));
            body.Children().Append(m_fpsBoost);

            m_highPriority.Content(box_value(L"Run game above-normal priority"));
            body.Children().Append(m_highPriority);
            m_general.Children().Append(MakeCard(L"Performance", body));
        }
        m_inner.Children().Append(m_general);

        // Advanced: Java & JVM
        {
            StackPanel body{};
            body.Spacing(8);
            m_java.Header(box_value(L"Java path (empty = auto-detect)"));
            m_java.HorizontalAlignment(HorizontalAlignment::Stretch);
            body.Children().Append(m_java);

            m_extraJvm.Header(box_value(L"Extra JVM args (optional)"));
            m_extraJvm.PlaceholderText(L"e.g. -XX:G1HeapRegionSize=16M");
            m_extraJvm.TextWrapping(TextWrapping::Wrap);
            m_extraJvm.AcceptsReturn(false);
            m_extraJvm.HorizontalAlignment(HorizontalAlignment::Stretch);
            body.Children().Append(m_extraJvm);

            // One-click fix when auto-detect finds nothing: game launches
            // (and the Forge/NeoForge installer + local servers) already do
            // this automatically, this is just the manual button for it.
            Button dlJava{};
            dlJava.Content(box_value(L"Auto-install missing Java (8 / 17 / 21)"));
            dlJava.HorizontalAlignment(HorizontalAlignment::Left);
            dlJava.Click([this](IInspectable const&, RoutedEventArgs const&) { DownloadJava(); });
            body.Children().Append(dlJava);

            TextBlock javaHint{};
            javaHint.Text(L"Downloads Temurin (Adoptium) into PretClient's own data folder - no admin rights, no PATH changes. Missing versions are fetched automatically at launch too.");
            javaHint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            javaHint.Opacity(0.6);
            javaHint.TextWrapping(TextWrapping::Wrap);
            body.Children().Append(javaHint);
            m_advanced.Children().Append(MakeCard(L"Java & JVM", body));
        }

        // Advanced: CurseForge API key (pre-filled, rarely needs touching)
        {
            StackPanel body{};
            body.Spacing(8);
            m_cfKey.Header(box_value(L"CurseForge API key"));
            m_cfKey.PlaceholderText(L"Replace with your own key if you have one");
            m_cfKey.HorizontalAlignment(HorizontalAlignment::Stretch);
            body.Children().Append(m_cfKey);

            TextBlock hint{};
            hint.Text(L"Pre-filled with the public community key. Only change this if CurseForge gave you your own key.");
            hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            hint.Opacity(0.6);
            hint.TextWrapping(TextWrapping::Wrap);
            body.Children().Append(hint);
            m_advanced.Children().Append(MakeCard(L"Mod API", body));
        }
        m_inner.Children().Append(m_advanced);

        Button save{};
        save.Content(box_value(L"Save"));
        save.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        save.HorizontalAlignment(HorizontalAlignment::Left);
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
        // Shared Save: reads both tabs so edits under Advanced persist too.
        m_inner.Children().Append(save);

        m_status.Opacity(0.7);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_inner.Children().Append(m_status);

        // Updates card: Grid row (version star + button auto) so the button
        // never clips on narrow windows — the version label wraps instead.
        {
            StackPanel body{};
            body.Spacing(8);

            Grid updateGrid{};
            updateGrid.ColumnDefinitions().Append(ColumnDefinition{});
            updateGrid.ColumnDefinitions().Append(ColumnDefinition{});
            updateGrid.ColumnDefinitions().GetAt(0).Width(
                GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            updateGrid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

            TextBlock ver{};
            ver.Opacity(0.5);
            ver.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            ver.Text(hstring{ L"PretClient " } + Update::CurrentVersionTag());
            ver.VerticalAlignment(VerticalAlignment::Center);
            ver.TextWrapping(TextWrapping::Wrap);
            Grid::SetColumn(ver, 0);
            updateGrid.Children().Append(ver);

            Button check{};
            check.Content(box_value(L"Check for updates"));
            check.VerticalAlignment(VerticalAlignment::Center);
            check.Click([this](IInspectable const&, RoutedEventArgs const&) {
                SetStatus(L"Checking for updates...");
                if (m_onCheckUpdates)
                    m_onCheckUpdates();
            });
            Grid::SetColumn(check, 1);
            updateGrid.Children().Append(check);
            body.Children().Append(updateGrid);

            TextBlock hint{};
            hint.Text(L"Checks GitHub Releases. Startup + a 6h background poll check silently.");
            hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            hint.Opacity(0.6);
            hint.TextWrapping(TextWrapping::Wrap);
            body.Children().Append(hint);
            m_inner.Children().Append(MakeCard(L"Updates", body));
        }

        // Scrollable so the Save + Updates sections stay reachable in short
        // windows. The Grid root is what makes this work: a StackPanel root
        // measures children with infinite height, so the ScrollViewer would
        // just grow off-screen and never scroll.
        m_scroll.Content(m_inner);
        m_scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_scroll.HorizontalScrollMode(ScrollMode::Disabled);
        m_scroll.ZoomMode(ZoomMode::Disabled);
        m_scroll.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_scroll.VerticalAlignment(VerticalAlignment::Stretch);
        m_root.Children().Append(m_scroll);

        SetTab(0);
        Refresh();
    }

    void SettingsPage::SetTab(int idx)
    {
        m_tab = idx;
        m_general.Visibility(m_tab == 0 ? Visibility::Visible : Visibility::Collapsed);
        m_advanced.Visibility(m_tab == 1 ? Visibility::Visible : Visibility::Collapsed);
        PaintTabs();
    }

    void SettingsPage::PaintTabs()
    {
        try
        {
            auto accent =
                Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>();
            if (m_tab == 0)
            {
                m_tabGeneral.Style(accent);
                m_tabAdvanced.Style(Style{ nullptr });
            }
            else
            {
                m_tabAdvanced.Style(accent);
                m_tabGeneral.Style(Style{ nullptr });
            }
        }
        catch (...)
        {
        }
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

    winrt::fire_and_forget SettingsPage::DownloadJava()
    {
        Microsoft::UI::Dispatching::DispatcherQueue dq{ nullptr };
        try
        {
            dq = m_root.DispatcherQueue();
        }
        catch (...)
        {
        }
        auto say = [this, dq](hstring const& line) {
            try
            {
                if (dq)
                    dq.TryEnqueue([this, line] {
                        try
                        {
                            SetStatus(line);
                        }
                        catch (...)
                        {
                        }
                    });
                else
                    SetStatus(line);
            }
            catch (...)
            {
            }
        };
        say(L"Checking Java...");
        // Probing (java -version spawns) and the download block: off the UI.
        co_await winrt::resume_background();
        int feats[] = { 8, 17, 21 };
        for (int f : feats)
        {
            bool have = false;
            try
            {
                if (!Java::ManagedJava(f).empty())
                    have = true;
                else if (!Java::PickDetailed(f).path.empty())
                    have = true;
            }
            catch (...)
            {
            }
            if (have)
                continue;
            hstring got;
            try
            {
                // Callbacks fire on this (background) thread: say marshals.
                got = co_await Java::EnsureAsync(f, say, nullptr);
            }
            catch (...)
            {
            }
            if (got.empty())
                say(hstring{ L"Java " } + to_hstring(f) + L" auto-download failed - check your connection.");
            else
                say(hstring{ L"Java " } + to_hstring(f) + L" ready.");
        }
        say(L"Java check done.");
    }
}
