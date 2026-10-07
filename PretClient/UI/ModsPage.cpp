#include "pch.h"
#include "ModsPage.h"
#include "Theme.h"
#include "../Settings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;

namespace winrt::PretClient
{
    ModsPage::ModsPage()
    {
        m_root.Spacing(12);
        m_root.Padding(ThicknessHelper::FromUniformLength(24));

        TextBlock head{};
        head.Text(L"Mods — Modrinth");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_root.Children().Append(head);

        StackPanel row{};
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8);

        m_query.PlaceholderText(L"Search mods… (e.g. sodium)");
        m_query.Width(280);
        row.Children().Append(m_query);

        m_loader.Items().Append(box_value(L"all"));
        m_loader.Items().Append(box_value(L"fabric"));
        m_loader.Items().Append(box_value(L"forge"));
        m_loader.Items().Append(box_value(L"quilt"));
        m_loader.Items().Append(box_value(L"neoforge"));
        m_loader.SelectedIndex(1);
        row.Children().Append(m_loader);

        m_mc.PlaceholderText(L"MC (e.g. 1.20.1)");
        m_mc.Width(140);
        row.Children().Append(m_mc);

        Button go{};
        go.Content(box_value(L"Search"));
        go.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        go.Click([this](IInspectable const&, RoutedEventArgs const&) { OnSearch(); });
        row.Children().Append(go);
        m_root.Children().Append(row);

        StackPanel targetRow{};
        targetRow.Orientation(Orientation::Horizontal);
        targetRow.Spacing(8);
        TextBlock tl{};
        tl.Text(L"Install to:");
        tl.VerticalAlignment(VerticalAlignment::Center);
        targetRow.Children().Append(tl);
        targetRow.Children().Append(m_target);
        m_root.Children().Append(targetRow);

        m_status.Opacity(0.7);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_root.Children().Append(m_status);

        ScrollViewer scroll{};
        scroll.MaxHeight(520);
        scroll.Content(m_results);
        m_results.Spacing(8);
        m_root.Children().Append(scroll);

        RefreshInstances();
    }

    void ModsPage::RefreshInstances()
    {
        m_targets = LoadInstances();
        m_target.Items().Clear();
        for (auto const& i : m_targets)
            m_target.Items().Append(box_value(i.name + L" (" + i.mcVersion + L" " + i.loader + L")"));
        if (!m_targets.empty())
            m_target.SelectedIndex(0);
    }

    void ModsPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }

    fire_and_forget ModsPage::OnSearch()
    {
        SetStatus(L"Searching Modrinth…");
        m_results.Children().Clear();
        hstring loader = unbox_value_or<hstring>(m_loader.SelectedItem(), L"fabric");
        hstring query = m_query.Text();
        hstring mc = m_mc.Text();
        Modrinth::SearchAsync(
            query, mc, loader, [this](std::vector<Modrinth::ModHit> hits) {
                if (hits.empty())
                {
                    SetStatus(L"No results (check the query, or offline?).");
                    return;
                }
                SetStatus(to_hstring(hits.size()) + L" result(s).");
                for (auto const& hit : hits)
                {
                    Border card{};
                    card.Background(Theme::CardBrush());
                    card.BorderBrush(Theme::CardStroke());
                    card.BorderThickness(ThicknessHelper::FromUniformLength(1));
                    card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                    card.Padding(ThicknessHelper::FromUniformLength(12));

                    Grid grid{};
                    grid.ColumnSpacing(12);
                    grid.ColumnDefinitions().Append(ColumnDefinition{});
                    grid.ColumnDefinitions().Append(ColumnDefinition{});
                    grid.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
                    grid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

                    StackPanel info{};
                    info.Spacing(2);
                    TextBlock title{};
                    title.Text(hit.title);
                    title.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
                    info.Children().Append(title);
                    TextBlock desc{};
                    desc.Text(hit.description);
                    desc.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    desc.Opacity(0.7);
                    desc.TextWrapping(TextWrapping::Wrap);
                    info.Children().Append(desc);
                    TextBlock meta{};
                    wchar_t buf[96]{};
                    swprintf_s(buf, L"\u2B07 %lld \u00B7 %s", hit.downloads, std::wstring{ hit.slug }.c_str());
                    meta.Text(buf);
                    meta.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    meta.Opacity(0.6);
                    info.Children().Append(meta);

                    Button install{};
                    install.Content(box_value(L"Install"));
                    install.VerticalAlignment(VerticalAlignment::Center);
                    install.Click([this, hit](IInspectable const&, RoutedEventArgs const&) { OnInstall(hit); });

                    Grid::SetColumn(info, 0);
                    Grid::SetColumn(install, 1);
                    grid.Children().Append(info);
                    grid.Children().Append(install);
                    card.Child(grid);
                    m_results.Children().Append(card);
                }
            });
    }

    fire_and_forget ModsPage::OnInstall(Modrinth::ModHit hit)
    {
        if (m_targets.empty() || m_target.SelectedIndex() < 0)
        {
            SetStatus(L"Pick a target instance first.");
            co_return;
        }
        auto inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
        hstring loader = inst.loader == L"vanilla"
            ? unbox_value_or<hstring>(m_loader.SelectedItem(), L"fabric")
            : inst.loader;
        if (loader == L"all")
            loader = L"";
        SetStatus(hstring{ L"Resolving " } + hit.title + L"…");
        Modrinth::PickFileAsync(
            hit.projectId.empty() ? hit.slug : hit.projectId, inst.mcVersion, loader,
            [this](Modrinth::ModFile file) {
                if (file.url.empty())
                {
                    SetStatus(L"No matching file (version/loader?).");
                    return;
                }
                auto settings = LoadSettings();
                auto mods = std::filesystem::path{ std::wstring{ EffectiveGameDir(settings) } } / L"mods";
                SetStatus(hstring{ L"Downloading " } + file.filename + L"…");
                InstallOneFile(file, mods.wstring());
            });
    }

    fire_and_forget ModsPage::InstallOneFile(Modrinth::ModFile file, std::wstring modsDir)
    {
        hstring status;
        try
        {
            status = co_await Modrinth::DownloadFileAsync(file, modsDir);
        }
        catch (...)
        {
            status = L"Install failed.";
        }
        SetStatus(status);
    }
}
