#include "pch.h"
#include "ModsPage.h"
#include "Theme.h"
#include "../Settings.h"
#include "../Paths.h"
#include "../Minecraft/Http.h"
#include "../Minecraft/ModDeps.h"
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
    namespace
    {
        // "downloads 28610903 | fabric-api" is developer output, not UI.
        hstring DownloadCount(long long n)
        {
            wchar_t buf[64]{};
            if (n >= 1000000)
                swprintf_s(buf, L"%.1fM downloads", static_cast<double>(n) / 1000000.0);
            else if (n >= 1000)
                swprintf_s(buf, L"%.1fK downloads", static_cast<double>(n) / 1000.0);
            else
                swprintf_s(buf, L"%lld downloads", n);
            return hstring{ buf };
        }

        std::wstring GroupDigits(long long n)
        {
            wchar_t raw[32]{};
            swprintf_s(raw, L"%lld", n);
            std::wstring digits{ raw };
            std::wstring out;
            for (size_t i = 0; i < digits.size(); ++i)
            {
                if (i > 0 && (digits.size() - i) % 3 == 0)
                    out.push_back(L',');
                out.push_back(digits[i]);
            }
            return out;
        }
    } // namespace

    ModsPage::ModsPage()
    {
        // Fixed header + star-sized result list: the old fixed MaxHeight
        // pushed the bottom of the results below the window on short screens.
        m_root.Padding(ThicknessHelper::FromUniformLength(24));
        m_root.RowDefinitions().Append(RowDefinition{});
        m_root.RowDefinitions().Append(RowDefinition{});
        m_root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
        m_root.RowDefinitions().GetAt(1).Height(
            GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));

        StackPanel top{};
        top.Spacing(10);

        TextBlock head{};
        head.Text(L"Mods");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        top.Children().Append(head);
        m_head = head;

        TextBlock sub{};
        sub.Text(L"Browse Modrinth or CurseForge and install straight into an instance.");
        sub.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        sub.Opacity(0.6);
        sub.TextWrapping(TextWrapping::Wrap);
        top.Children().Append(sub);

        // Content and source tabs share one row: four stacked buttons spent
        // a whole row of vertical space and read as two unrelated toolbars.
        Grid tabRow{};
        tabRow.ColumnDefinitions().Append(ColumnDefinition{});
        tabRow.ColumnDefinitions().Append(ColumnDefinition{});
        tabRow.ColumnDefinitions().GetAt(0).Width(
            GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        tabRow.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

        StackPanel kindTabs{};
        kindTabs.Orientation(Orientation::Horizontal);
        kindTabs.Spacing(8);
        m_tabMods.Content(box_value(L"Mods"));
        m_tabPacks.Content(box_value(L"Resource Packs"));
        m_tabMods.MinWidth(96);
        m_tabPacks.MinWidth(96);
        m_tabMods.Click([this](IInspectable const&, RoutedEventArgs const&) { SetKind(Kind::Mods); });
        m_tabPacks.Click([this](IInspectable const&, RoutedEventArgs const&) { SetKind(Kind::Packs); });
        kindTabs.Children().Append(m_tabMods);
        kindTabs.Children().Append(m_tabPacks);
        Grid::SetColumn(kindTabs, 0);
        tabRow.Children().Append(kindTabs);

        StackPanel tabs{};
        tabs.Orientation(Orientation::Horizontal);
        tabs.Spacing(8);
        tabs.HorizontalAlignment(HorizontalAlignment::Right);
        m_tabModrinth.Content(box_value(L"Modrinth"));
        m_tabCurse.Content(box_value(L"CurseForge"));
        m_tabModrinth.MinWidth(96);
        m_tabCurse.MinWidth(96);
        m_tabModrinth.Click([this](IInspectable const&, RoutedEventArgs const&) { SetSource(Source::Modrinth); });
        m_tabCurse.Click([this](IInspectable const&, RoutedEventArgs const&) { SetSource(Source::CurseForge); });
        tabs.Children().Append(m_tabModrinth);
        tabs.Children().Append(m_tabCurse);
        Grid::SetColumn(tabs, 1);
        tabRow.Children().Append(tabs);
        top.Children().Append(tabRow);
        PaintKindTabs();
        PaintSourceTabs();

        Grid filters{};
        filters.ColumnSpacing(8);
        for (int i = 0; i < 4; ++i)
            filters.ColumnDefinitions().Append(ColumnDefinition{});
        filters.ColumnDefinitions().GetAt(0).Width(
            GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        for (int i = 1; i < 4; ++i)
            filters.ColumnDefinitions().GetAt(i).Width(GridLengthHelper::Auto());

        m_query.PlaceholderText(L"Search mods... (e.g. sodium)");
        m_query.MinWidth(220);
        Grid::SetColumn(m_query, 0);
        filters.Children().Append(m_query);

        m_loader.Items().Append(box_value(L"all"));
        m_loader.Items().Append(box_value(L"fabric"));
        m_loader.Items().Append(box_value(L"forge"));
        m_loader.Items().Append(box_value(L"quilt"));
        m_loader.Items().Append(box_value(L"neoforge"));
        m_loader.SelectedIndex(0);
        m_loader.MinWidth(112);
        Grid::SetColumn(m_loader, 1);
        filters.Children().Append(m_loader);

        // MC version is automatic from the target instance (read-only): the
        // list only ever shows mods matching that version.
        m_mc.PlaceholderText(L"Auto (instance)");
        m_mc.IsReadOnly(true);
        m_mc.IsEnabled(false);
        m_mc.MinWidth(112);
        Grid::SetColumn(m_mc, 2);
        filters.Children().Append(m_mc);

        Button go{};
        go.Content(box_value(L"Search"));
        go.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        go.MinWidth(88);
        go.Click([this](IInspectable const&, RoutedEventArgs const&) { OnSearch(); });
        Grid::SetColumn(go, 3);
        filters.Children().Append(go);
        top.Children().Append(filters);

        StackPanel targetRow{};
        targetRow.Orientation(Orientation::Horizontal);
        targetRow.Spacing(8);
        TextBlock tl{};
        tl.Text(L"Install to:");
        tl.VerticalAlignment(VerticalAlignment::Center);
        targetRow.Children().Append(tl);
        m_target.MinWidth(280);
        targetRow.Children().Append(m_target);
        top.Children().Append(targetRow);

        m_status.Opacity(0.7);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_status.Visibility(Visibility::Collapsed);
        top.Children().Append(m_status);

        Grid::SetRow(top, 0);
        m_root.Children().Append(top);

        m_scroll.Content(m_results);
        m_scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_scroll.HorizontalScrollMode(ScrollMode::Disabled);
        m_scroll.ZoomMode(ZoomMode::Disabled);
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
        m_results.Padding(ThicknessHelper::FromLengths(0, 0, 0, 24));
        Grid::SetRow(m_scroll, 1);
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
        // Resource packs have no loader: the box stays disabled and the
        // search ignores it (MC version still filters).
        bool packs = (m_kind == Kind::Packs);
        try
        {
            if (m_targets.empty() || m_target.SelectedIndex() < 0 ||
                static_cast<size_t>(m_target.SelectedIndex()) >= m_targets.size())
            {
                m_mc.Text(L"");
                m_loader.IsEnabled(!packs);
                m_syncing = false;
                return;
            }
            auto const& inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            m_mc.Text(inst.mcVersion);
            if (packs)
            {
                m_loader.IsEnabled(false);
            }
            else if (inst.loader == L"vanilla")
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
        m_status.Visibility(line.empty() ? Visibility::Collapsed : Visibility::Visible);
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
            if (m_kind == Kind::Packs)
                m_lastLoader = L"";
            else if (inst.loader == L"vanilla")
                m_lastLoader = unbox_value_or<hstring>(m_loader.SelectedItem(), L"all");
            else
                m_lastLoader = inst.loader;
        }
        else
        {
            m_lastMc = m_mc.Text();
            m_lastLoader = (m_kind == Kind::Packs)
                ? hstring{ L"" }
                : unbox_value_or<hstring>(m_loader.SelectedItem(), L"all");
        }
        m_offset = 0;
        m_total = 0;
        m_results.Children().Clear();
        m_loading = false; // abandon any in-flight page; its callback is stale
        FetchPage();
        co_return;
    }

    void ModsPage::SetSource(Source s)
    {
        if (m_source == s)
            return;
        m_source = s;
        PaintSourceTabs();
        OnSearch();
    }

    void ModsPage::PaintSourceTabs()
    {
        try
        {
            auto accent = Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>();
            bool cf = (m_source == Source::CurseForge);
            m_tabModrinth.Style(cf ? Style{ nullptr } : accent);
            m_tabCurse.Style(cf ? accent : Style{ nullptr });
        }
        catch (...)
        {
        }
        UpdateHead();
    }

    void ModsPage::SetKind(Kind k)
    {
        if (m_kind == k)
            return;
        m_kind = k;
        PaintKindTabs();
        SyncFiltersFromTarget();
        OnSearch();
    }

    void ModsPage::PaintKindTabs()
    {
        try
        {
            auto accent = Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>();
            bool packs = (m_kind == Kind::Packs);
            m_tabMods.Style(packs ? Style{ nullptr } : accent);
            m_tabPacks.Style(packs ? accent : Style{ nullptr });
        }
        catch (...)
        {
        }
        UpdateHead();
    }

    void ModsPage::UpdateHead()
    {
        try
        {
            bool packs = (m_kind == Kind::Packs);
            // The source tab right below already says Modrinth/CurseForge;
            // repeating it in the title was pure noise.
            if (m_head)
                m_head.Text(packs ? L"Resource Packs" : L"Mods");
            if (m_query)
                m_query.PlaceholderText(packs
                    ? L"Search resource packs..."
                    : L"Search mods... (e.g. sodium)");
        }
        catch (...)
        {
        }
    }

    void ModsPage::AddResultCard(hstring title, hstring desc, hstring meta, hstring iconUrl,
        std::function<void()> onInstall, std::function<void()> onBuilds)
    {
        Border card{};
        card.Background(Theme::CardBrush());
        card.BorderBrush(Theme::CardStroke());
        card.BorderThickness(ThicknessHelper::FromUniformLength(1));
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
        card.Padding(ThicknessHelper::FromUniformLength(14));

        Grid grid{};
        grid.ColumnSpacing(12);
        grid.ColumnDefinitions().Append(ColumnDefinition{});
        grid.ColumnDefinitions().Append(ColumnDefinition{});
        grid.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        grid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

        // A horizontal StackPanel measures its children with infinite width,
        // so the wrapped description never wrapped and got clipped at the
        // actions column. A two-column grid constrains the text properly.
        Grid head{};
        head.ColumnSpacing(10);
        head.ColumnDefinitions().Append(ColumnDefinition{});
        head.ColumnDefinitions().Append(ColumnDefinition{});
        head.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::Auto());
        head.ColumnDefinitions().GetAt(1).Width(
            GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));

        Image icon{};
        icon.Width(44);
        icon.Height(44);
        icon.Stretch(Stretch::Uniform);
        if (!iconUrl.empty())
            LoadIcon(iconUrl, icon);
        Grid::SetColumn(icon, 0);
        head.Children().Append(icon);

        StackPanel info{};
        info.Spacing(2);
        Grid::SetColumn(info, 1);
        head.Children().Append(info);
        TextBlock titleBlock{};
        titleBlock.Text(title);
        titleBlock.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        titleBlock.FontSize(16);
        info.Children().Append(titleBlock);
        TextBlock descBlock{};
        descBlock.Text(desc);
        descBlock.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        descBlock.Opacity(0.7);
        descBlock.TextWrapping(TextWrapping::Wrap);
        descBlock.MaxLines(2);
        descBlock.TextTrimming(TextTrimming::CharacterEllipsis);
        info.Children().Append(descBlock);
        TextBlock metaBlock{};
        metaBlock.Text(meta);
        metaBlock.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        metaBlock.Opacity(0.6);
        info.Children().Append(metaBlock);

        StackPanel actions{};
        actions.Spacing(6);
        actions.VerticalAlignment(VerticalAlignment::Center);
        Button install{};
        install.Content(box_value(L"Install"));
        install.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        install.MinWidth(84);
        install.Click([onInstall](IInspectable const&, RoutedEventArgs const&) {
            if (onInstall)
                onInstall();
        });
        actions.Children().Append(install);
        Button builds{};
        builds.Content(box_value(L"Builds"));
        builds.MinWidth(84);
        builds.Click([onBuilds](IInspectable const&, RoutedEventArgs const&) {
            if (onBuilds)
                onBuilds();
        });
        actions.Children().Append(builds);

        Grid::SetColumn(info, 0);
        Grid::SetColumn(actions, 1);
        grid.Children().Append(head);
        grid.Children().Append(actions);
        card.Child(grid);
        m_results.Children().Append(card);
    }

    void ModsPage::FetchPage()
    {
        if (m_loading)
            return;
        m_loading = true;
        int gen = m_searchGen;
        bool packs = (m_kind == Kind::Packs);
        if (m_source == Source::CurseForge)
        {
            hstring key = CurseForge::EffectiveApiKey(LoadSettings().curseforgeKey);
            if (key.empty())
            {
                m_loading = false;
                SetStatus(L"CurseForge needs an API key: paste yours in Settings (free at the CurseForge API console).");
                return;
            }
            SetStatus(packs ? L"Searching CurseForge resource packs..." : L"Searching CurseForge...");
            CurseForge::SearchAsync(
                m_lastQuery, m_lastMc, m_lastLoader, m_offset, key,
                [this, gen](CurseForge::SearchResult result) {
                    if (gen != m_searchGen)
                        return; // superseded by a newer search; keep new state
                    m_loading = false;
                    if (result.hits.empty() && m_offset == 0)
                    {
                        SetStatus(L"No matching results. Try a broader search or check your connection.");
                        return;
                    }
                    m_total = result.total;
                    m_offset += static_cast<int>(result.hits.size());
                    wchar_t buf[256]{};
                    std::wstring filter{ m_lastMc.empty() ? L"" : L" for " + std::wstring{ m_lastMc } };
                    if (!m_lastLoader.empty() && m_lastLoader != L"all")
                        filter += L" " + std::wstring{ m_lastLoader };
                    if (m_total > 0)
                        swprintf_s(buf, L"Showing %d of %s%s", m_offset,
                            GroupDigits(m_total).c_str(), filter.c_str());
                    else
                        swprintf_s(buf, L"%d result(s)%s", m_offset, filter.c_str());
                    SetStatus(buf);
                    for (auto const& hit : result.hits)
                    {
                        AddResultCard(hit.title, hit.description, DownloadCount(hit.downloads),
                            hit.iconUrl,
                            [this, hit]() { OnInstallCF(hit); },
                            [this, hit]() { BuildsDialogCF(hit); });
                    }
                },
                packs ? CurseForge::kClassResourcePacks : CurseForge::kClassMods);
            return;
        }
        SetStatus(packs ? L"Searching Modrinth resource packs..." : L"Searching Modrinth...");
        Modrinth::SearchAsync(
            m_lastQuery, m_lastMc, m_lastLoader, m_offset,
            [this, gen](Modrinth::SearchResult result) {
                if (gen != m_searchGen)
                    return; // superseded by a newer search; keep new state
                m_loading = false;
                if (result.hits.empty() && m_offset == 0)
                {
                    SetStatus(L"No matching results. Try a broader search or check your connection.");
                    return;
                }
                m_total = result.total;
                m_offset += static_cast<int>(result.hits.size());
                wchar_t buf[256]{};
                std::wstring filter{ m_lastMc.empty() ? L"" : L" for " + std::wstring{ m_lastMc } };
                if (!m_lastLoader.empty() && m_lastLoader != L"all")
                    filter += L" " + std::wstring{ m_lastLoader };
                if (m_total > 0)
                    swprintf_s(buf, L"Showing %d of %s%s", m_offset,
                        GroupDigits(m_total).c_str(), filter.c_str());
                else
                    swprintf_s(buf, L"%d result(s)%s", m_offset, filter.c_str());
                SetStatus(buf);
                for (auto const& hit : result.hits)
                {
                    AddResultCard(hit.title, hit.description, DownloadCount(hit.downloads),
                        hit.iconUrl,
                        [this, hit]() { OnInstall(hit); },
                        [this, hit]() { BuildsDialog(hit); });
                }
            },
            packs ? hstring{ L"resourcepack" } : hstring{ L"mod" });
    }

    fire_and_forget ModsPage::OnInstall(Modrinth::ModHit hit)
    {
        if (m_targets.empty() || m_target.SelectedIndex() < 0)
        {
            SetStatus(L"Pick a target instance first.");
            co_return;
        }
        auto inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
        hstring mc = inst.mcVersion;
        if (m_kind == Kind::Packs)
        {
            // Packs work on vanilla too: straight into the instance's
            // resourcepacks folder, no loader filter, no dependencies.
            auto settings = LoadSettings();
            auto packs = InstanceResourcePacksDir(settings, inst.id);
            SetStatus(hstring{ L"Resolving " } + hit.title + L"...");
            Modrinth::PickVersionAsync(
                hit.projectId.empty() ? hit.slug : hit.projectId, mc, L"",
                [this, hit, packs](Modrinth::ModVersion version) {
                    if (version.files.empty())
                    {
                        SetStatus(L"No matching file for this version. Try Builds for a specific one.");
                        return;
                    }
                    SetStatus(hstring{ L"Installing " } + hit.title + L"...");
                    InstallPackVersion(version, packs.wstring());
                });
            co_return;
        }
        if (inst.loader == L"vanilla")
        {
            SetStatus(L"Target is vanilla (no mod loader) — mods won't load. Create a modded instance instead.");
            co_return;
        }
        hstring loader = inst.loader == L"vanilla" ? m_lastLoader : inst.loader;
        if (loader == L"all")
            loader = L"";
        SetStatus(hstring{ L"Resolving " } + hit.title + L"...");
        Modrinth::PickVersionAsync(
            hit.projectId.empty() ? hit.slug : hit.projectId, mc, loader,
            [this, hit, inst, mc, loader](Modrinth::ModVersion version) {
                if (version.files.empty())
                {
                    SetStatus(L"No matching file (version/loader?). Try Builds for a specific one.");
                    return;
                }
                auto settings = LoadSettings();
                auto mods = InstanceModsDir(settings, inst.id);
                SetStatus(hstring{ L"Installing " } + hit.title + L"...");
                InstallVersion(version, mc, loader, mods.wstring());
            });
    }

    fire_and_forget ModsPage::InstallVersion(Modrinth::ModVersion version,
        hstring mcVersion, hstring loader, std::wstring modsDir)
    {
        hstring main;
        try
        {
            Modrinth::ModFile file{};
            for (auto const& f : version.files)
            {
                if (f.primary && !f.url.empty())
                {
                    file = f;
                    break;
                }
            }
            if (file.url.empty())
            {
                for (auto const& f : version.files)
                {
                    if (!f.url.empty())
                    {
                        file = f;
                        break;
                    }
                }
            }
            if (file.url.empty())
            {
                SetStatus(L"No file in that build.");
                co_return;
            }
            main = co_await Modrinth::DownloadFileAsync(file, modsDir);
            std::wstring done{ main };
            if (done.rfind(L"Installed", 0) == 0 || done.rfind(L"Already present", 0) == 0)
            {
                // Main file is down; pull required deps in the background
                // and fold them into one status line when finished.
                ModDeps::EnsureModrinthAsync(version, mcVersion, loader, modsDir,
                    [this, main](ModDeps::Summary s) { SetStatus(ModDeps::FormatStatus(main, s)); });
                co_return;
            }
        }
        catch (...)
        {
            main = L"Install failed.";
        }
        SetStatus(main);
    }

    fire_and_forget ModsPage::InstallPackVersion(Modrinth::ModVersion version,
        std::wstring packsDir)
    {
        hstring status;
        try
        {
            Modrinth::ModFile file{};
            for (auto const& f : version.files)
            {
                if (f.primary && !f.url.empty())
                {
                    file = f;
                    break;
                }
            }
            if (file.url.empty())
            {
                for (auto const& f : version.files)
                {
                    if (!f.url.empty())
                    {
                        file = f;
                        break;
                    }
                }
            }
            if (file.url.empty())
            {
                SetStatus(L"No file in that build.");
                co_return;
            }
            status = co_await Modrinth::DownloadFileAsync(file, packsDir);
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
        // target instance version/loader are offered (packs: version only).
        bool packs = (m_kind == Kind::Packs);
        hstring mc = m_lastMc;
        hstring loader = packs ? hstring{ L"" } : m_lastLoader;
        if (!m_targets.empty() && m_target.SelectedIndex() >= 0 &&
            static_cast<size_t>(m_target.SelectedIndex()) < m_targets.size())
        {
            auto const& inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            mc = inst.mcVersion;
            if (!packs && inst.loader != L"vanilla")
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
            modsDir = packs
                ? InstanceResourcePacksDir(settings, inst.id).wstring()
                : InstanceModsDir(settings, inst.id).wstring();
        }

        Modrinth::GetVersionsAsync(
            hit.projectId.empty() ? hit.slug : hit.projectId, mc, loader,
            [this, list, loading, modsDir, dialog, targetVanilla, packs, mc, loader](std::vector<Modrinth::ModVersion> versions) mutable {
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
                    install.Click([this, v, modsDir, dialog, targetVanilla, packs, mc, loader](IInspectable const&, RoutedEventArgs const&) mutable {
                        if (packs)
                        {
                            if (v.files.empty() || modsDir.empty())
                            {
                                SetStatus(L"No file in that build (or no target instance).");
                                return;
                            }
                            SetStatus(L"Installing pack...");
                            InstallPackVersion(v, modsDir);
                            try
                            {
                                dialog.Hide();
                            }
                            catch (...)
                            {
                            }
                            return;
                        }
                        if (targetVanilla)
                        {
                            SetStatus(L"Target is vanilla (no mod loader) — mods won't load. Create a modded instance instead.");
                            return;
                        }
                        if (v.files.empty() || modsDir.empty())
                        {
                            SetStatus(L"No file in that build (or no target instance).");
                            return;
                        }
                        SetStatus(L"Installing build...");
                        InstallVersion(v, mc, loader, modsDir);
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

    fire_and_forget ModsPage::OnInstallCF(CurseForge::ModHit hit)
    {
        if (m_targets.empty() || m_target.SelectedIndex() < 0)
        {
            SetStatus(L"Pick a target instance first.");
            co_return;
        }
        auto inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
        hstring mc = inst.mcVersion;
        hstring key = CurseForge::EffectiveApiKey(LoadSettings().curseforgeKey);
        if (key.empty())
        {
            SetStatus(L"CurseForge needs an API key: paste yours in Settings.");
            co_return;
        }
        if (m_kind == Kind::Packs)
        {
            auto settings = LoadSettings();
            auto packs = InstanceResourcePacksDir(settings, inst.id);
            SetStatus(hstring{ L"Resolving " } + hit.title + L"... (CurseForge)");
            CurseForge::PickVersionAsync(
                hit.modId, mc, L"", key,
                [this, hit, packs](CurseForge::ModVersion version) {
                    if (version.files.empty() || version.files.front().url.empty())
                    {
                        SetStatus(L"No matching file for this version. Try Builds for a specific one.");
                        return;
                    }
                    SetStatus(hstring{ L"Installing " } + hit.title + L"... (CurseForge)");
                    InstallPackVersionCF(version, packs.wstring());
                });
            co_return;
        }
        if (inst.loader == L"vanilla")
        {
            SetStatus(L"Target is vanilla (no mod loader) — mods won't load. Create a modded instance instead.");
            co_return;
        }
        hstring loader = inst.loader == L"vanilla" ? m_lastLoader : inst.loader;
        if (loader == L"all")
            loader = L"";
        SetStatus(hstring{ L"Resolving " } + hit.title + L"... (CurseForge)");
        CurseForge::PickVersionAsync(
            hit.modId, mc, loader, key,
            [this, hit, inst, mc, loader, key](CurseForge::ModVersion version) {
                if (version.files.empty() || version.files.front().url.empty())
                {
                    SetStatus(L"No matching file (version/loader?). Try Builds for a specific one.");
                    return;
                }
                auto settings = LoadSettings();
                auto mods = InstanceModsDir(settings, inst.id);
                SetStatus(hstring{ L"Installing " } + hit.title + L"... (CurseForge)");
                InstallVersionCF(version, hit.modId, mc, loader, key, mods.wstring());
            });
    }

    fire_and_forget ModsPage::InstallVersionCF(CurseForge::ModVersion version, int modId,
        hstring mcVersion, hstring loader, hstring apiKey, std::wstring modsDir)
    {
        hstring main;
        try
        {
            if (version.files.empty() || version.files.front().url.empty())
            {
                SetStatus(L"No file in that build.");
                co_return;
            }
            auto file = version.files.front();
            main = co_await CurseForge::DownloadFileAsync(file, modsDir);
            std::wstring done{ main };
            if (done.rfind(L"Installed", 0) == 0 || done.rfind(L"Already present", 0) == 0)
            {
                ModDeps::EnsureCurseForgeAsync(file, modId, mcVersion, loader, apiKey, modsDir,
                    [this, main](ModDeps::Summary s) { SetStatus(ModDeps::FormatStatus(main, s)); });
                co_return;
            }
        }
        catch (...)
        {
            main = L"Install failed.";
        }
        SetStatus(main);
    }

    fire_and_forget ModsPage::InstallPackVersionCF(CurseForge::ModVersion version,
        std::wstring packsDir)
    {
        hstring status;
        try
        {
            if (version.files.empty() || version.files.front().url.empty())
            {
                SetStatus(L"No file in that build.");
                co_return;
            }
            status = co_await CurseForge::DownloadFileAsync(version.files.front(), packsDir);
        }
        catch (...)
        {
            status = L"Install failed.";
        }
        SetStatus(status);
    }

    fire_and_forget ModsPage::BuildsDialogCF(CurseForge::ModHit hit)
    {
        // Same instance-locked filter as the list: only builds matching the
        // target instance version/loader are offered (packs: version only).
        bool packs = (m_kind == Kind::Packs);
        hstring mc = m_lastMc;
        hstring loader = packs ? hstring{ L"" } : m_lastLoader;
        if (!m_targets.empty() && m_target.SelectedIndex() >= 0 &&
            static_cast<size_t>(m_target.SelectedIndex()) < m_targets.size())
        {
            auto const& inst = m_targets[static_cast<size_t>(m_target.SelectedIndex())];
            mc = inst.mcVersion;
            if (!packs && inst.loader != L"vanilla")
                loader = inst.loader;
        }
        if (loader == L"all")
            loader = L"";

        StackPanel panel{};
        panel.Spacing(8);
        TextBlock loading{};
        loading.Text(L"Loading builds... (CurseForge)");
        loading.Opacity(0.7);
        panel.Children().Append(loading);
        StackPanel list{};
        list.Spacing(6);
        panel.Children().Append(list);

        ContentDialog dialog{};
        dialog.Title(box_value(hit.title + L" - builds (CurseForge)"));
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
            modsDir = packs
                ? InstanceResourcePacksDir(settings, inst.id).wstring()
                : InstanceModsDir(settings, inst.id).wstring();
        }

        hstring key = CurseForge::EffectiveApiKey(LoadSettings().curseforgeKey);
        if (key.empty())
        {
            loading.Text(L"CurseForge needs an API key: paste yours in Settings.");
        }
        else
        {
            CurseForge::GetVersionsAsync(
                hit.modId, mc, loader, key,
                [this, list, loading, modsDir, dialog, targetVanilla, packs, hit, mc, loader, key](std::vector<CurseForge::ModVersion> versions) mutable {
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
                        hstring fileName = v.files.empty() ? hstring{} : v.files.front().filename;
                        meta.Text(hstring{ L"file " } + v.id + (fileName.empty() ? hstring{} : hstring{ L" | " } + fileName));
                        meta.Opacity(0.6);
                        meta.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                        info.Children().Append(meta);

                        Button install{};
                        install.Content(box_value(L"Install this"));
                        install.VerticalAlignment(VerticalAlignment::Center);
                        install.Click([this, v, modsDir, dialog, targetVanilla, packs, hit, mc, loader, key](IInspectable const&, RoutedEventArgs const&) mutable {
                            if (packs)
                            {
                                if (v.files.empty() || modsDir.empty())
                                {
                                    SetStatus(L"No file in that build (or no target instance).");
                                    return;
                                }
                                SetStatus(L"Installing pack... (CurseForge)");
                                InstallPackVersionCF(v, modsDir);
                                try
                                {
                                    dialog.Hide();
                                }
                                catch (...)
                                {
                                }
                                return;
                            }
                            if (targetVanilla)
                            {
                                SetStatus(L"Target is vanilla (no mod loader) — mods won't load. Create a modded instance instead.");
                                return;
                            }
                            if (v.files.empty() || modsDir.empty())
                            {
                                SetStatus(L"No file in that build (or no target instance).");
                                return;
                            }
                            SetStatus(L"Installing build... (CurseForge)");
                            InstallVersionCF(v, hit.modId, mc, loader, key, modsDir);
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
        }

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
