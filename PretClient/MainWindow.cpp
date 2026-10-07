#include "pch.h"
#include "MainWindow.h"
#include "UI/Theme.h"
#include "Minecraft/Http.h"
#include "Update/Updater.h"
#include <filesystem>
#include <shellapi.h>

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

        Grid root{};
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
        root.RowDefinitions().GetAt(1).Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));

        m_banner.Orientation(Orientation::Horizontal);
        m_banner.Spacing(12);
        m_banner.Padding(ThicknessHelper::FromLengths(24, 12, 24, 12));
        m_banner.Visibility(Visibility::Collapsed);
        m_banner.Background(SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(38, 0x44, 0xBD, 0x32) });
        m_updateText.VerticalAlignment(VerticalAlignment::Center);
        m_updateText.TextWrapping(TextWrapping::Wrap);
        Button updateButton{};
        updateButton.Content(box_value(L"Download and install"));
        updateButton.Click([this, updateButton](IInspectable const&, RoutedEventArgs const&) { InstallUpdate(updateButton); });
        m_banner.Children().Append(m_updateText);
        m_banner.Children().Append(updateButton);
        Grid::SetRow(m_banner, 0);
        root.Children().Append(m_banner);

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
        m_navSettings.Content(box_value(L"Settings"));
        m_navSettings.Icon(SymbolIcon(Symbol::Setting));
        m_navSettings.Tag(box_value(L"settings"));
        nav.MenuItems().Append(m_navInstances);
        nav.MenuItems().Append(m_navMods);
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
            }
            else if (tag == L"settings")
            {
                m_settings.Refresh();
                m_host.Children().Append(m_settings.Root());
            }
            else
            {
                m_instances.Refresh();
                m_host.Children().Append(m_instances.Root());
            }
        });
        Grid::SetRow(nav, 1);
        root.Children().Append(nav);

        Content(root);
        nav.SelectedItem(m_navInstances);

        CheckForUpdates();
    }

    fire_and_forget MainWindow::CheckForUpdates()
    {
        JsonObject release = co_await Update::GetLatestReleaseAsync();
        if (!release)
            co_return;
        hstring latest = release.GetNamedString(L"tag_name", L"");
        hstring current = Update::CurrentVersionTag();
        if (!latest.empty() && Update::IsNewerTag(current, latest))
        {
            m_updateUrl = Update::DownloadUrlFor(release);
            m_updateText.Text(L"Update available: " + current + L" -> " + latest);
            m_banner.Visibility(Visibility::Visible);
        }
    }

    fire_and_forget MainWindow::InstallUpdate(Button button)
    {
        if (m_updateUrl.empty())
            co_return;
        if (!Update::IsInstallerUrl(m_updateUrl))
        {
            Update::OpenUrl(m_updateUrl); // release-page fallback: no direct asset
            co_return;
        }
        button.IsEnabled(false);
        m_updateText.Text(L"Downloading update...");
        auto dest = std::filesystem::temp_directory_path() / L"PretClient-Setup.exe";
        hstring err = co_await Http::DownloadToFileAsync(m_updateUrl, dest, L"PretClient/1.0",
            [this](unsigned long long done, unsigned long long total, double) {
                if (total == 0)
                    return;
                wchar_t buf[128]{};
                swprintf_s(buf, L"Downloading update... %llu%%", done * 100 / total);
                m_updateText.Text(buf);
            });
        if (!err.empty())
        {
            m_updateText.Text(hstring{ L"Update download failed: " } + err);
            button.IsEnabled(true);
            co_return;
        }
        // Per-user install (no UAC): run it silent, let it replace us, exit
        // now so no files are locked. The installer's postinstall entry
        // relaunches the app when done.
        m_updateText.Text(L"Installing update... the app will close and reopen.");
        auto rc = ShellExecuteW(nullptr, L"open", dest.c_str(),
            L"/SILENT /CLOSEAPPLICATIONS", nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(rc) <= 32)
        {
            m_updateText.Text(L"Could not start installer.");
            button.IsEnabled(true);
            co_return;
        }
        Application::Current().Exit();
    }
}
