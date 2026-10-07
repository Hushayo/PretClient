#include "pch.h"
#include "ModsPage.h"
#include "Theme.h"
#include "../Settings.h"
#include "../Paths.h"
#include "../Minecraft/Http.h"
#include <chrono>
#include <cwctype>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

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
        head.Text(L"Mods - Modrinth");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_root.Children().Append(head);

        StackPanel row{};
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8);

        m_query.PlaceholderText(L"Search mods... (e.g. sodium)");
        m_query.Width(280);
        row.Children().Append(m_query);

        m_loader.Items().Append(box_value(L"all"));
        m_loader.Items().Append(box_value(L"fabric"));
        m_loader.Items().Append(box_value(L"forge"));
        m_loader.Items().Append(box_value(L"quilt"));
        m_loader.Items().Append(box_value(L"neoforge"));
        m_loader.SelectedIndex(0);
        row.Children().Append(m_loader);

        // MC version is automatic from the target instance (read-only): the
        // list only ever shows mods matching that version.
        m_mc.PlaceholderText(L"Auto (instance)");
        m_mc.IsReadOnly(true);
        m_mc.IsEnabled(false);
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

        m_scroll.MaxHeight(560);
        m_scroll.Content(m_results);
        m_scroll.ViewChanged([this](IInspectable const&, ScrollViewerViewChangedEventArgs const&) {
            try
            {
                if (m_loading || m_total <= 0 || m_offset >= m_total)
                    return;
                if (m_scroll.VerticalOffset() > m_scroll.ScrollableHeight() - 600.0)
                    FetchPage();
            }
            catch (...)
            {
            }
        });
        m_results.Spacing(8);
        m_root.Children().Append(m_scroll);

        // Live search: typing filters without needing the Search button.
        // (Unnamed event-arg types: TextBoxTextChangedEventArgs is not
        // visible in this SDK's headers, so the handler takes auto params.)
        m_query.TextChanged([this](auto&&, auto&&) {
            if (m_syncing)
                return;
            ScheduleSearch();
        });
        m_loader.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
            if (m_syncing)
                return;
            OnSearch();
        });
        m_target.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
            if (m_syncing)
                return;
            SyncFiltersFromTarget();
            OnSearch();
        });

        try
        {
            m_debounce = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_debounce.Interval(std::chrono::milliseconds{ 400 });
            m_debounce.Tick([this](auto&&, auto&&) {
                try
                {
                    m_debounce.Stop();
                }
                catch (...)
                {
                }
                OnSearch();
            });
        }
        catch (...)
        {
        }

        RefreshInstances();
    }

    void ModsPage::ScheduleSearch()
    {
        try
        {
            if (m_debounce)
            {
                m_debounce.Stop();
                m_debounce.Start();
                return;
            }
        }
        catch (...)
        {
        }
        OnSearch();
    }

    void ModsPage::SyncFiltersFromTarget()
    {
        m_syncing = true;
        try
        {
            if (m_targets.empty() || m_target.SelectedIndex() < 0 ||
                static_cast<size_t>(m_target.SelectedIndex()) >= m_targets.size())
            {
                m_mc.Text(L"");
                m_loader.IsEnabled(true);
                m_syncing = false;
                return;
            }
            auto const& inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            m_mc.Text(inst.mcVersion);
            if (inst.loader == L"vanilla")
            {
                // No loader on the instance: let the user browse any loader.
                // Reset a previously locked selection (e.g. fabric) back to
                // "all" so the result line can't promise a loader the
                // target doesn't have.
                if (!m_loader.IsEnabled())
                    m_loader.SelectedIndex(0);
                m_loader.IsEnabled(true);
                if (m_loader.SelectedIndex() < 0)
                    m_loader.SelectedIndex(0);
            }
            else
            {
                // Lock the loader filter to the instance loader so only
                // matching mods are listed.
                bool found = false;
                for (uint32_t i = 0; i < m_loader.Items().Size(); ++i)
                {
                    if (unbox_value_or<hstring>(m_loader.Items().GetAt(i), L"") == inst.loader)
                    {
                        m_loader.SelectedIndex(i);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    m_loader.SelectedIndex(0);
                m_loader.IsEnabled(false);
            }
        }
        catch (...)
        {
        }
        m_syncing = false;
    }

    void ModsPage::RefreshInstances()
    {
        hstring keepId{};
        try
        {
            if (!m_targets.empty() && m_target.SelectedIndex() >= 0 &&
                static_cast<size_t>(m_target.SelectedIndex()) < m_targets.size())
                keepId = m_targets[static_cast<size_t>(m_target.SelectedIndex())].id;
        }
        catch (...)
        {
        }
        m_targets = LoadInstances();
        m_syncing = true;
        try
        {
            m_target.Items().Clear();
            for (auto const& i : m_targets)
                m_target.Items().Append(box_value(i.name + L" (" + i.mcVersion + L" " + i.loader + L")"));
            int pick = 0;
            if (!keepId.empty())
            {
                for (size_t i = 0; i < m_targets.size(); ++i)
                {
                    if (m_targets[i].id == keepId)
                    {
                        pick = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (!m_targets.empty())
                m_target.SelectedIndex(pick);
        }
        catch (...)
        {
        }
        m_syncing = false;
        SyncFiltersFromTarget();
        // Auto-load: opening the Mods tab shows matching mods immediately.
        OnSearch();
    }

    void ModsPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }

    fire_and_forget ModsPage::OnSearch()
    {
        ++m_searchGen;
        m_lastQuery = m_query.Text();
        // Filters always come from the target instance so mismatched
        // versions never list. Vanilla instances have no loader, so fall
        // back to the (user-pickable) loader box; modded instances lock it.
        if (!m_targets.empty() && m_target.SelectedIndex() >= 0 &&
            static_cast<size_t>(m_target.SelectedIndex()) < m_targets.size())
        {
            auto const& inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            m_lastMc = inst.mcVersion;
            if (inst.loader == L"vanilla")
                m_lastLoader = unbox_value_or<hstring>(m_loader.SelectedItem(), L"all");
            else
                m_lastLoader = inst.loader;
        }
        else
        {
            m_lastMc = m_mc.Text();
            m_lastLoader = unbox_value_or<hstring>(m_loader.SelectedItem(), L"all");
        }
        m_offset = 0;
        m_total = 0;
        m_results.Children().Clear();
        m_loading = false; // abandon any in-flight page; its callback is stale
        FetchPage();
        co_return;
    }

    void ModsPage::FetchPage()
    {
        if (m_loading)
            return;
        m_loading = true;
        int gen = m_searchGen;
        SetStatus(L"Searching Modrinth...");
        Modrinth::SearchAsync(
            m_lastQuery, m_lastMc, m_lastLoader, m_offset,
            [this, gen](Modrinth::SearchResult result) {
                if (gen != m_searchGen)
                    return; // superseded by a newer search; keep new state
                m_loading = false;
                if (result.hits.empty() && m_offset == 0)
                {
                    SetStatus(L"No results (check the query, or offline?).");
                    return;
                }
                m_total = result.total;
                m_offset += static_cast<int>(result.hits.size());
                wchar_t buf[256]{};
                std::wstring filter{ m_lastMc.empty() ? L"" : L" for " + std::wstring{ m_lastMc } };
                if (!m_lastLoader.empty() && m_lastLoader != L"all")
                    filter += L" " + std::wstring{ m_lastLoader };
                if (m_total > 0)
                    swprintf_s(buf, L"%d of %lld results%s (scroll for more).", m_offset, m_total, filter.c_str());
                else
                    swprintf_s(buf, L"%d result(s)%s.", m_offset, filter.c_str());
                SetStatus(buf);
                for (auto const& hit : result.hits)
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

                    StackPanel head{};
                    head.Orientation(Orientation::Horizontal);
                    head.Spacing(10);

                    Image icon{};
                    icon.Width(48);
                    icon.Height(48);
                    icon.Stretch(Stretch::Uniform);
                    if (!hit.iconUrl.empty())
                        LoadIcon(hit.iconUrl, icon);
                    head.Children().Append(icon);

                    StackPanel info{};
                    info.Spacing(2);
                    head.Children().Append(info);
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
                    wchar_t mbuf[128]{};
                    swprintf_s(mbuf, L"downloads %lld | %s", hit.downloads, std::wstring{ hit.slug }.c_str());
                    meta.Text(mbuf);
                    meta.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    meta.Opacity(0.6);
                    info.Children().Append(meta);

                    StackPanel actions{};
                    actions.Spacing(6);
                    actions.VerticalAlignment(VerticalAlignment::Center);
                    Button install{};
                    install.Content(box_value(L"Install"));
                    install.Click([this, hit](IInspectable const&, RoutedEventArgs const&) { OnInstall(hit); });
                    actions.Children().Append(install);
                    Button builds{};
                    builds.Content(box_value(L"Builds"));
                    builds.Click([this, hit](IInspectable const&, RoutedEventArgs const&) { BuildsDialog(hit); });
                    actions.Children().Append(builds);

                    Grid::SetColumn(info, 0);
                    Grid::SetColumn(actions, 1);
                    grid.Children().Append(head);
                    grid.Children().Append(actions);
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
        if (inst.loader == L"vanilla")
        {
            SetStatus(L"Target is vanilla (no mod loader) — mods won't load. Create a Fabric instance instead.");
            co_return;
        }
        hstring loader = inst.loader == L"vanilla" ? m_lastLoader : inst.loader;
        if (loader == L"all")
            loader = L"";
        hstring mc = inst.mcVersion;
        SetStatus(hstring{ L"Resolving " } + hit.title + L"...");
        Modrinth::PickFileAsync(
            hit.projectId.empty() ? hit.slug : hit.projectId, mc, loader,
            [this, hit, inst](Modrinth::ModFile file) {
                if (file.url.empty())
                {
                    SetStatus(L"No matching file (version/loader?). Try Builds for a specific one.");
                    return;
                }
                auto settings = LoadSettings();
                auto mods = InstanceModsDir(settings, inst.id);
                SetStatus(hstring{ L"Downloading " } + file.filename + L"...");
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

    fire_and_forget ModsPage::BuildsDialog(Modrinth::ModHit hit)
    {
        // Same instance-locked filter as the list: only builds matching the
        // target instance version/loader are offered.
        hstring mc = m_lastMc;
        hstring loader = m_lastLoader;
        if (!m_targets.empty() && m_target.SelectedIndex() >= 0 &&
            static_cast<size_t>(m_target.SelectedIndex()) < m_targets.size())
        {
            auto const& inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            mc = inst.mcVersion;
            if (inst.loader != L"vanilla")
                loader = inst.loader;
        }
        if (loader == L"all")
            loader = L"";

        StackPanel panel{};
        panel.Spacing(8);
        TextBlock loading{};
        loading.Text(L"Loading builds...");
        loading.Opacity(0.7);
        panel.Children().Append(loading);
        StackPanel list{};
        list.Spacing(6);
        panel.Children().Append(list);

        ContentDialog dialog{};
        dialog.Title(box_value(hit.title + L" - builds"));
        dialog.Content(panel);
        dialog.CloseButtonText(L"Close");
        dialog.XamlRoot(m_root.XamlRoot());

        std::wstring modsDir;
        bool targetVanilla = false;
        if (!m_targets.empty() && m_target.SelectedIndex() >= 0 &&
            static_cast<size_t>(m_target.SelectedIndex()) < m_targets.size())
        {
            auto inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            targetVanilla = (inst.loader == L"vanilla");
            auto settings = LoadSettings();
            modsDir = InstanceModsDir(settings, inst.id).wstring();
        }

        Modrinth::GetVersionsAsync(
            hit.projectId.empty() ? hit.slug : hit.projectId, mc, loader,
            [this, list, loading, modsDir, dialog, targetVanilla](std::vector<Modrinth::ModVersion> versions) mutable {
                loading.Visibility(Visibility::Collapsed);
                if (versions.empty())
                {
                    TextBlock t{};
                    t.Text(L"No builds match this instance version.");
                    t.Opacity(0.7);
                    list.Children().Append(t);
                    return;
                }
                int shown = 0;
                for (auto const& v : versions)
                {
                    if (++shown > 30)
                        break;
                    Border card{};
                    card.Background(Theme::CardBrush());
                    card.BorderBrush(Theme::CardStroke());
                    card.BorderThickness(ThicknessHelper::FromUniformLength(1));
                    card.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
                    card.Padding(ThicknessHelper::FromUniformLength(10));

                    Grid grid{};
                    grid.ColumnSpacing(10);
                    grid.ColumnDefinitions().Append(ColumnDefinition{});
                    grid.ColumnDefinitions().Append(ColumnDefinition{});
                    grid.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
                    grid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

                    StackPanel info{};
                    TextBlock name{};
                    name.Text(v.versionNumber);
                    info.Children().Append(name);
                    TextBlock meta{};
                    std::wstring games;
                    // versions arrays can be huge; show a few.
                    // (game list arrives in ModVersion? show id instead.)
                    meta.Text(hstring{ L"id " } + v.id);
                    meta.Opacity(0.6);
                    meta.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    info.Children().Append(meta);

                    Button install{};
                    install.Content(box_value(L"Install this"));
                    install.VerticalAlignment(VerticalAlignment::Center);
                    install.Click([this, v, modsDir, dialog, targetVanilla](IInspectable const&, RoutedEventArgs const&) mutable {
                        if (targetVanilla)
                        {
                            SetStatus(L"Target is vanilla (no mod loader) — mods won't load. Create a Fabric instance instead.");
                            return;
                        }
                        Modrinth::ModFile file{};
                        for (auto const& f : v.files)
                        {
                            if (f.primary && !f.url.empty())
                            {
                                file = f;
                                break;
                            }
                        }
                        if (file.url.empty())
                        {
                            for (auto const& f : v.files)
                            {
                                if (!f.url.empty())
                                {
                                    file = f;
                                    break;
                                }
                            }
                        }
                        if (file.url.empty() || modsDir.empty())
                        {
                            SetStatus(L"No file in that build (or no target instance).");
                            return;
                        }
                        SetStatus(hstring{ L"Downloading " } + file.filename + L"...");
                        InstallOneFile(file, modsDir);
                        try
                        {
                            dialog.Hide();
                        }
                        catch (...)
                        {
                        }
                    });

                    Grid::SetColumn(info, 0);
                    Grid::SetColumn(install, 1);
                    grid.Children().Append(info);
                    grid.Children().Append(install);
                    card.Child(grid);
                    list.Children().Append(card);
                }
            });

        co_await dialog.ShowAsync();
    }

    fire_and_forget ModsPage::LoadIcon(hstring url, Image img)
    {
        try
        {
            std::wstring u{ url };
            auto q = u.find(L'?');
            if (q != std::wstring::npos)
                u = u.substr(0, q);
            auto slash = u.find_last_of(L'/');
            std::wstring name = (slash == std::wstring::npos) ? u : u.substr(slash + 1);
            for (auto& c : name)
            {
                if (!std::iswalnum(c) && c != L'.' && c != L'-' && c != L'_')
                    c = L'_';
            }
            if (name.empty() || name == L".")
                name = L"icon.png";
            if (name.find(L'.') == std::wstring::npos)
                name += L".png";
            wchar_t suffix[24]{};
            swprintf_s(suffix, L"_%08x",
                static_cast<unsigned>(std::hash<std::wstring>{}(std::wstring{ url })));
            name += suffix;
            auto file = Paths::DataDir() / L"icons" / name;
            std::error_code ec;
            if (!std::filesystem::exists(file, ec))
            {
                auto err = co_await Http::DownloadToFileAsync(url, file, L"PretClient icon fetch", {});
                if (!err.empty())
                    co_return;
            }
            std::wstring uri{ L"file:///" };
            std::wstring p{ file.wstring() };
            for (auto& c : p)
            {
                if (c == L'\\')
                    c = L'/';
            }
            uri += p;
            img.Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{ Windows::Foundation::Uri{ uri } });
        }
        catch (...)
        {
        }
    }
}
