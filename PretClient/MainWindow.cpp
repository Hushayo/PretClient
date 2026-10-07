#include "pch.h"
#include "MainWindow.h"
#include "Settings.h"
#include "UI/Theme.h"
#include "Minecraft/Http.h"
#include "Update/Toast.h"
#include "Update/Updater.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <shellapi.h>
#include <winrt/Microsoft.UI.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient
{
    MainWindow::MainWindow()
    {
        Title(L"PretClient");
        SystemBackdrop(MicaBackdrop{});
        try
        {
            AppWindow().Resize(Windows::Graphics::SizeInt32{ 1160, 760 });
        }
        catch (...)
        {
        }
        try
        {
            auto appWin = AppWindow();
            // Black title bar + our own exe icon (icon.ico ships next to it).
            wchar_t exe[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0)
            {
                auto icon = std::filesystem::path{ exe }.parent_path() / L"icon.ico";
                std::error_code ec;
                if (std::filesystem::exists(icon, ec))
                    appWin.SetIcon(hstring{ icon.wstring() });
            }
            auto black = Microsoft::UI::ColorHelper::FromArgb(255, 0, 0, 0);
            auto white = Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255);
            auto hover = Microsoft::UI::ColorHelper::FromArgb(255, 48, 48, 48);
            auto bar = appWin.TitleBar();
            bar.BackgroundColor(black);
            bar.ForegroundColor(white);
            bar.InactiveBackgroundColor(black);
            bar.InactiveForegroundColor(white);
            bar.ButtonBackgroundColor(black);
            bar.ButtonForegroundColor(white);
            bar.ButtonHoverBackgroundColor(hover);
            bar.ButtonHoverForegroundColor(white);
            bar.ButtonPressedBackgroundColor(hover);
            bar.ButtonPressedForegroundColor(white);
            bar.ButtonInactiveBackgroundColor(black);
            bar.ButtonInactiveForegroundColor(white);
        }
        catch (...)
        {
        }

        Grid root{};
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
        root.RowDefinitions().GetAt(1).Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));

        // Top-right round profile avatar (global, visible on every page).
        m_topBar.Padding(ThicknessHelper::FromLengths(0, 8, 16, 0));
        m_profileButton.Width(36);
        m_profileButton.Height(36);
        m_profileButton.Padding(ThicknessHelper::FromUniformLength(0));
        m_profileButton.CornerRadius(CornerRadiusHelper::FromUniformRadius(18));
        m_profileButton.Background(SolidColorBrush{ Theme::AccentBase() });
        m_profileButton.BorderBrush(Theme::CardStroke());
        m_profileButton.HorizontalAlignment(HorizontalAlignment::Right);
        m_profileButton.VerticalAlignment(VerticalAlignment::Center);
        m_profileButton.HorizontalContentAlignment(HorizontalAlignment::Center);
        m_profileButton.VerticalContentAlignment(VerticalAlignment::Center);
        m_profileAvatar.HorizontalAlignment(HorizontalAlignment::Center);
        m_profileAvatar.VerticalAlignment(VerticalAlignment::Center);
        m_profileAvatar.FontSize(16);
        m_profileAvatar.FontWeight(Windows::UI::Text::FontWeights::Bold());
        m_profileAvatar.Foreground(SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255) });
        m_profileButton.Content(m_profileAvatar);
        m_profileButton.Click([this](IInspectable const&, RoutedEventArgs const&) { m_instances.ProfileDialog(); });
        m_topBar.Children().Append(m_profileButton);
        Grid::SetRow(m_topBar, 0);
        root.Children().Append(m_topBar);

        NavigationView nav{};
        nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        nav.IsSettingsVisible(false);
        nav.PaneTitle(L"PretClient");

        m_navInstances.Content(box_value(L"Instances"));
        m_navInstances.Icon(SymbolIcon(Symbol::Library));
        m_navInstances.Tag(box_value(L"instances"));
        m_navMods.Content(box_value(L"Mods"));
        m_navMods.Icon(SymbolIcon(Symbol::Download));
        m_navMods.Tag(box_value(L"mods"));
        m_navLocalServer.Content(box_value(L"Local Server"));
        m_navLocalServer.Icon(SymbolIcon(Symbol::Globe));
        m_navLocalServer.Tag(box_value(L"localserver"));
        m_navSettings.Content(box_value(L"Settings"));
        m_navSettings.Icon(SymbolIcon(Symbol::Setting));
        m_navSettings.Tag(box_value(L"settings"));
        nav.MenuItems().Append(m_navInstances);
        nav.MenuItems().Append(m_navMods);
        nav.MenuItems().Append(m_navLocalServer);
        nav.MenuItems().Append(m_navSettings);

        nav.PaneTitle(L"PretClient");

        nav.Content(m_host);
        nav.SelectionChanged([this](NavigationView const&, NavigationViewSelectionChangedEventArgs const& args) {
            hstring tag;
            try
            {
                tag = unbox_value_or<hstring>(args.SelectedItem().as<NavigationViewItem>().Tag(), L"");
            }
            catch (...)
            {
            }
            m_host.Children().Clear();
            if (tag == L"mods")
            {
                m_mods.RefreshInstances();
                m_host.Children().Append(m_mods.Root());
                FadeContent(m_mods.Root());
            }
            else if (tag == L"localserver")
            {
                m_localServer.Refresh();
                m_host.Children().Append(m_localServer.Root());
                FadeContent(m_localServer.Root());
            }
            else if (tag == L"settings")
            {
                m_settings.Refresh();
                m_host.Children().Append(m_settings.Root());
                FadeContent(m_settings.Root());
            }
            else
            {
                m_instances.Refresh();
                m_host.Children().Append(m_instances.Root());
                FadeContent(m_instances.Root());
            }
        });
        Grid::SetRow(nav, 1);
        root.Children().Append(nav);

        // Keep the avatar initial/tooltip in sync (profile switches call
        // InstancesPage::Refresh, which nudges us via this callback).
        m_instances.SetOnProfileChanged([this]() { RefreshProfileAvatar(); });
        RefreshProfileAvatar();

        Content(root);
        nav.SelectedItem(m_navInstances);

        m_settings.OnCheckUpdates([this] { CheckForUpdates(); });
        // Toast button clicks land here in-process (no protocol/COM setup):
        // Install runs the self-update flow, Release-notes opens the page.
        Update::Toast::EnsureRegistered([this](hstring const& action) {
            std::wstring a{ action };
            if (a == L"action=install")
            {
                InstallUpdateLatest();
                return;
            }
            std::wstring prefix = L"action=notes;";
            if (a.rfind(prefix, 0) == 0)
                Update::OpenUrl(hstring{ a.substr(prefix.size()) });
        });
        CheckForUpdates();
        StartUpdatePolling();
    }

    void MainWindow::RefreshProfileAvatar()
    {
        try
        {
            auto settings = LoadSettings();
            std::wstring name{ settings.username };
            wchar_t initial = L'?';
            if (!name.empty())
                initial = static_cast<wchar_t>(towupper(name[0]));
            m_profileAvatar.Text(hstring{ std::wstring(1, initial) });
            ToolTipService::SetToolTip(m_profileButton,
                box_value(hstring{ L"Profile: " } + settings.username + L" — click to switch"));
        }
        catch (...)
        {
        }
    }

    void MainWindow::FadeContent(UIElement const& el)
    {
        try
        {
            // The timer lives in a member: a timer nobody references can die
            // mid-fade and leave the page stuck at opacity 0 (invisible).
            // Any failure restores opacity instead of leaving a blank page.
            el.Opacity(0.0);
            m_fadeTimer = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_fadeTimer.Interval(std::chrono::milliseconds{ 16 });
            auto step = std::make_shared<int>(0);
            m_fadeTimer.Tick([this, el, step](auto&&, auto&&) {
                try
                {
                    *step += 1;
                    el.Opacity((std::min)(1.0, *step / 10.0));
                    if (*step >= 10)
                        m_fadeTimer.Stop();
                }
                catch (...)
                {
                    try
                    {
                        el.Opacity(1.0);
                    }
                    catch (...)
                    {
                    }
                }
            });
            m_fadeTimer.Start();
        }
        catch (...)
        {
            try
            {
                el.Opacity(1.0);
            }
            catch (...)
            {
            }
        }
    }

    namespace
    {
        hstring MbText(unsigned long long b)
        {
            wchar_t buf[32]{};
            swprintf_s(buf, L"%.1f MB", b / 1048576.0);
            return hstring{ buf };
        }
    } // namespace

    void MainWindow::StartUpdatePolling()
    {
        try
        {
            // Held in a member like m_fadeTimer: an unreferenced timer can die
            // and silently stop polling.
            m_updateTimer =
                Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_updateTimer.Interval(std::chrono::hours{ 6 });
            m_updateTimer.Tick([this](auto&&, auto&&) {
                CheckForUpdates(/*silent=*/true);
            });
            m_updateTimer.Start();
        }
        catch (...)
        {
        }
    }

    fire_and_forget MainWindow::CheckForUpdates(bool silent)
    {
        JsonObject release = co_await Update::GetLatestReleaseAsync();
        if (!release)
        {
            // Background polls stay quiet offline so they never overwrite a
            // meaningful Settings status line; manual/startup checks report it.
            if (!silent)
                m_settings.SetStatus(L"Could not check for updates (offline?).");
            co_return;
        }
        hstring latest = release.GetNamedString(L"tag_name", L"");
        hstring current = Update::CurrentVersionTag();
        if (!latest.empty() && Update::IsNewerTag(current, latest))
        {
            // One toast per version for background polls: a new release
            // published mid-session still notifies, but the 6h poll doesn't
            // re-toast the same tag forever. Manual checks always re-notify.
            if (silent && latest == m_lastNotifiedTag)
                co_return;
            m_lastNotifiedTag = latest;
            m_settings.SetStatus(hstring{ L"Update " } + latest + L" available — see notification.");
            Update::Toast::ShowAvailable(current, latest);
        }
        else if (!silent)
        {
            m_settings.SetStatus(L"PretClient " + current + L" is up to date.");
        }
    }

    // Toast-driven one-click self-update (toast Install button lands here via
    // the EnsureRegistered action handler). Re-resolves the release so the toast never
    // carries stale URLs; progress + errors go to the toast, Settings status
    // mirrors the headline states.
    fire_and_forget MainWindow::InstallUpdateLatest()
    {
        if (m_updating)
            co_return;
        m_updating = true;
        struct ResetGuard
        {
            MainWindow* self;
            bool disarm = false;
            ~ResetGuard()
            {
                if (!disarm && self)
                    self->m_updating = false;
            }
        };
        ResetGuard guard{ this };

        auto failUpdate = [this](hstring const& msg) {
            m_settings.SetStatus(msg);
            Update::Toast::ShowError(msg);
        };

        m_settings.SetStatus(L"Checking for updates...");
        JsonObject release{ nullptr };
        try
        {
            release = co_await Update::GetLatestReleaseAsync();
        }
        catch (...)
        {
        }
        hstring latest;
        hstring current = Update::CurrentVersionTag();
        try
        {
            if (release)
                latest = release.GetNamedString(L"tag_name", L"");
        }
        catch (...)
        {
        }
        if (!release || latest.empty() || !Update::IsNewerTag(current, latest))
        {
            m_settings.SetStatus(L"Already up to date.");
            Update::Toast::ShowDone(L"Already up to date.");
            co_return;
        }
        auto asset = Update::FindSetupAsset(release);
        if (asset.url.empty() || !Update::IsInstallerUrl(asset.url))
        {
            // No direct setup asset: hand the user the release page instead.
            Update::OpenUrl(Update::DownloadUrlFor(release));
            guard.disarm = true;
            m_updating = false;
            m_settings.SetStatus(L"Opened release page.");
            co_return;
        }

        Update::Toast::ShowDownloading(latest);
        m_settings.SetStatus(hstring{ L"Downloading update " } + latest + L"...");
        auto dest = std::filesystem::temp_directory_path() / L"PretClient-Setup.exe";
        hstring err = co_await Http::DownloadToFileAsync(asset.url, dest, L"PretClient/1.0",
            [this, latest](unsigned long long done, unsigned long long total, double) {
                wchar_t buf[192]{};
                if (total > 0)
                {
                    double pct = 100.0 * static_cast<double>(done) / static_cast<double>(total);
                    swprintf_s(buf, L"Downloading update... %.0f%% (%s / %s)", pct,
                        MbText(done).c_str(), MbText(total).c_str());
                    m_settings.SetStatus(buf);
                }
                else
                {
                    swprintf_s(buf, L"Downloading update... %s", MbText(done).c_str());
                    m_settings.SetStatus(buf);
                }
                Update::Toast::ShowProgress(buf, done, total);
            });
        if (!err.empty())
        {
            failUpdate(hstring{ L"Update download failed: " } + err);
            co_return;
        }
        // Size check against the published asset size.
        if (asset.size > 0)
        {
            std::error_code ec;
            auto have = std::filesystem::file_size(dest, ec);
            if (ec || have != asset.size)
            {
                std::filesystem::remove(dest, ec);
                failUpdate(L"Update failed: size mismatch, please retry.");
                co_return;
            }
        }
        // Hash check against the release's published sha256 digest.
        if (!asset.sha256.empty())
        {
            m_settings.SetStatus(L"Checking hash...");
            Update::Toast::ShowProgress(L"Checking hash...", 0, 0);
            std::wstring hex;
            if (!Http::Sha256OfFile(dest, hex) ||
                _wcsicmp(hex.c_str(), std::wstring{ asset.sha256 }.c_str()) != 0)
            {
                std::error_code ec;
                std::filesystem::remove(dest, ec);
                failUpdate(L"Update failed: hash mismatch, please retry.");
                co_return;
            }
        }
        // Per-user install (no UAC): run it silent, let it replace us, exit
        // now so no files are locked. The installer's postinstall entry
        // relaunches the app when done.
        m_settings.SetStatus(L"Installing update... the app will close and reopen.");
        Update::Toast::ShowDone(L"Installing... the app will close and reopen.");
        auto rc = ShellExecuteW(nullptr, L"open", dest.c_str(),
            L"/SILENT /CLOSEAPPLICATIONS", nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(rc) <= 32)
        {
            failUpdate(L"Could not start installer.");
            co_return;
        }
        guard.disarm = true; // committed: the process exits below
        Application::Current().Exit();
    }
}
