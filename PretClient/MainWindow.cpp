#include "pch.h"
#include "MainWindow.h"
#include "UI/Theme.h"
#include "Update/Updater.h"

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
        Title(L"PretClient — Minecraft Launcher");
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
        updateButton.Content(box_value(L"Download update"));
        updateButton.Click([this](IInspectable const&, RoutedEventArgs const&) { Update::OpenUrl(m_updateUrl); });
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

        nav.PaneTitle(hstring{ L"PretClient " } + Update::CurrentVersionTag());

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
            m_updateText.Text(L"Update available: " + current + L" → " + latest);
            m_banner.Visibility(Visibility::Visible);
        }
    }
}
