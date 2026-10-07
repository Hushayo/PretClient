#include "pch.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "../Minecraft/Auth.h"
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

        TextBlock accHead{};
        accHead.Text(L"Account");
        accHead.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
        m_root.Children().Append(accHead);

        m_account.Opacity(0.7);
        m_account.TextWrapping(TextWrapping::Wrap);
        m_root.Children().Append(m_account);

        StackPanel accRow{};
        accRow.Orientation(Orientation::Horizontal);
        accRow.Spacing(8);
        m_signIn.Content(box_value(L"Sign in with Microsoft"));
        m_signIn.Click([this](IInspectable const&, RoutedEventArgs const&) { SignIn(); });
        accRow.Children().Append(m_signIn);
        m_signOut.Content(box_value(L"Sign out"));
        m_signOut.Click([this](IInspectable const&, RoutedEventArgs const&) {
            Auth::SignOut();
            Refresh();
            SetStatus(L"Signed out. Offline mode.");
        });
        accRow.Children().Append(m_signOut);
        m_root.Children().Append(accRow);

        m_clientId.Header(box_value(L"Azure client ID (Microsoft sign-in)"));
        m_root.Children().Append(m_clientId);

        TextBlock accHint{};
        accHint.Opacity(0.6);
        accHint.TextWrapping(TextWrapping::Wrap);
        accHint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        accHint.Text(L"Register an app at portal.azure.com (consumers accounts, no secret) and paste "
                     L"its client ID. New apps must also be approved for Minecraft Services "
                     L"(aka.ms/mce-reviewappid) or sign-in stops at a 403. Leave empty to play offline.");
        m_root.Children().Append(accHint);

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

        m_cfKey.Header(box_value(L"CurseForge API key (public community key is pre-filled)"));
        m_cfKey.PlaceholderText(L"Replace with your own key if you have one");
        m_root.Children().Append(m_cfKey);

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
            // Start from the stored settings so Save never wipes fields this
            // page doesn't edit (profiles list, etc.).
            Settings s = LoadSettings();
            s.username = m_username.Text().empty() ? hstring{ L"Steve" } : hstring{ m_username.Text() };
            s.gameDir = m_gameDir.Text();
            s.javaPath = m_java.Text();
            s.msClientId = m_clientId.Text();
            s.curseforgeKey = m_cfKey.Text();
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
        m_root.Children().Append(updateRow);

        Refresh();
    }

    void SettingsPage::Refresh()
    {
        auto s = LoadSettings();
        m_username.Text(s.username);
        m_clientId.Text(s.msClientId);
        hstring acc = Auth::AccountName();
        m_account.Text(acc.empty() ? hstring{ L"Mode: offline (" } + s.username + L"). Online servers need a Microsoft sign-in."
                                   : hstring{ L"Mode: Microsoft (" } + acc + L"). Launches use this account.");
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
    }

    void SettingsPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }

    void SettingsPage::SignIn()
    {
        // Persist the client ID first so refreshes keep working.
        try
        {
            Settings s = LoadSettings();
            s.msClientId = m_clientId.Text();
            SaveSettings(s);
        }
        catch (...)
        {
        }
        m_signIn.IsEnabled(false);
        m_signOut.IsEnabled(false);
        SetStatus(L"Starting Microsoft sign-in...");
        Auth::SignInAsync(m_clientId.Text(),
            [this](hstring line) { SetStatus(line); },
            [this](bool ok, hstring message) {
                m_signIn.IsEnabled(true);
                m_signOut.IsEnabled(true);
                Refresh();
                SetStatus(message);
                (void)ok;
            });
    }
}
