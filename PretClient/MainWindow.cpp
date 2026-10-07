#include "pch.h"
#include "MainWindow.h"
#include "Update/Updater.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace std::chrono_literals;

namespace winrt::PretClient
{
    MainWindow::MainWindow()
    {
        Title(L"PretClient \u2014 Minecraft Launcher");
        SystemBackdrop(MicaBackdrop{});

        ScrollViewer root{};
        root.Padding(ThicknessHelper::FromUniformLength(24));

        StackPanel layout{};
        layout.Spacing(12);
        layout.MaxWidth(560);

        TextBlock title{};
        title.Text(L"PretClient");
        title.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());

        TextBlock subtitle{};
        subtitle.Text(L"Fancy Minecraft launcher \u2014 offline first");
        subtitle.Opacity(0.7);

        TextBlock version{};
        version.Text(Update::CurrentVersionTag() + L" (offline first)");
        version.Opacity(0.6);

        m_updateBanner.Orientation(Orientation::Horizontal);
        m_updateBanner.Spacing(12);
        m_updateBanner.Padding(ThicknessHelper::FromUniformLength(12));
        m_updateBanner.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        m_updateBanner.Visibility(Visibility::Collapsed);
        m_updateText.VerticalAlignment(VerticalAlignment::Center);
        m_updateText.TextWrapping(TextWrapping::Wrap);
        Button updateButton{};
        updateButton.Content(box_value(L"Download update"));
        updateButton.Click([this](IInspectable const&, RoutedEventArgs const&) { OnUpdateClicked(); });
        m_updateBanner.Children().Append(m_updateText);
        m_updateBanner.Children().Append(updateButton);

        m_username.Header(box_value(L"Username (offline)"));
        m_username.PlaceholderText(L"Steve");
        m_username.MaxLength(16);

        m_versions.Header(box_value(L"Version"));
        m_versions.Items().Append(box_value(L"1.21.4 (vanilla)"));
        m_versions.Items().Append(box_value(L"1.20.4 (vanilla)"));
        m_versions.Items().Append(box_value(L"1.8.9 (vanilla)"));
        m_versions.SelectedIndex(0);

        m_play.Content(box_value(L"Play (offline)"));
        m_play.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        m_play.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_play.Height(40);
        m_play.Click([this](IInspectable const&, RoutedEventArgs const&) { OnPlayClicked(); });

        m_progress.IsIndeterminate(true);
        m_progress.Visibility(Visibility::Collapsed);

        m_log.Header(box_value(L"Log"));
        m_log.IsReadOnly(true);
        m_log.AcceptsReturn(true);
        m_log.TextWrapping(TextWrapping::Wrap);
        m_log.MinHeight(160);
        m_log.MaxHeight(320);
        m_log.FontFamily(FontFamily(L"Consolas"));

        layout.Children().Append(title);
        layout.Children().Append(subtitle);
        layout.Children().Append(version);
        layout.Children().Append(m_updateBanner);
        layout.Children().Append(m_username);
        layout.Children().Append(m_versions);
        layout.Children().Append(m_play);
        layout.Children().Append(m_progress);
        layout.Children().Append(m_log);

        root.Content(layout);
        Content(root);

        AppendLog(L"PretClient ready. Enter a username and press Play.");
        CheckForUpdates();
    }

    void MainWindow::OnPlayClicked()
    {
        hstring username = m_username.Text();
        if (username.empty())
        {
            username = L"Steve";
            m_username.Text(username);
        }
        hstring version = L"1.21.4";
        if (auto item = m_versions.SelectedItem())
            version = unbox_value<hstring>(item);
        RunOfflineLaunchAsync(username, version);
    }

    void MainWindow::OnUpdateClicked()
    {
        Update::OpenUrl(m_updateUrl);
    }

    fire_and_forget MainWindow::CheckForUpdates()
    {
        AppendLog(L"Checking for launcher updates...");
        JsonObject release = co_await Update::GetLatestReleaseAsync();
        if (!release)
        {
            AppendLog(L"No releases found or offline \u2014 running dev build.");
            co_return;
        }
        hstring latest = release.GetNamedString(L"tag_name", L"");
        hstring current = Update::CurrentVersionTag();
        if (!latest.empty() && Update::IsNewerTag(current, latest))
        {
            m_updateUrl = Update::DownloadUrlFor(release);
            m_updateText.Text(L"Update available: " + current + L" \u2192 " + latest);
            m_updateBanner.Visibility(Visibility::Visible);
            AppendLog(L"Update available: " + latest);
        }
        else
        {
            AppendLog(L"Launcher is up to date (" + current + L").");
        }
    }

    fire_and_forget MainWindow::RunOfflineLaunchAsync(hstring username, hstring version)
    {
        m_play.IsEnabled(false);
        m_progress.Visibility(Visibility::Visible);
        AppendLog(L"Launching " + version + L" as " + username + L" (offline)...");
        AppendLog(L"Resolving version manifest...");
        co_await resume_after(600ms);
        AppendLog(L"Game download + Java launch land in the next iteration.");
        co_await resume_after(400ms);
        AppendLog(L"Stub complete \u2014 UI shell v1 works.");
        m_progress.Visibility(Visibility::Collapsed);
        m_play.IsEnabled(true);
    }

    void MainWindow::AppendLog(hstring const& line)
    {
        m_log.Text(m_log.Text() + line + L"\r\n");
    }
}
