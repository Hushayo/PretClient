#include "pch.h"
#include "InstancesPage.h"
#include "Theme.h"
#include "../Minecraft/Downloader.h"
#include "../Minecraft/Fabric.h"
#include "../Minecraft/Http.h"
#include "../Minecraft/Modrinth.h"
#include <algorithm>
#include <fstream>
#include <shellapi.h>
#include "../Minecraft/Java.h"
#include "../Minecraft/Launcher.h"
#include "../Minecraft/Versions.h"
#include "../Settings.h"
#include <coroutine>
#include <winrt/Windows.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace std::chrono_literals;

namespace winrt::PretClient
{
    namespace
    {
        hstring FormatBytes(unsigned long long b)
        {
            wchar_t buf[64]{};
            if (b >= 1024ull * 1024 * 1024)
                swprintf_s(buf, L"%.1f GB", b / 1073741824.0);
            else if (b >= 1024 * 1024)
                swprintf_s(buf, L"%.0f MB", b / 1048576.0);
            else
                swprintf_s(buf, L"%llu KB", b / 1024);
            return hstring{ buf };
        }

        hstring FormatSpeed(double bps)
        {
            wchar_t buf[64]{};
            if (bps >= 1024 * 1024)
                swprintf_s(buf, L"%.1f MB/s", bps / 1048576.0);
            else if (bps >= 1024)
                swprintf_s(buf, L"%.0f KB/s", bps / 1024.0);
            else
                swprintf_s(buf, L"%.0f B/s", bps);
            return hstring{ buf };
        }

        hstring Pct(double v)
        {
            if (v < 0)
                return L"...";
            wchar_t buf[32]{};
            swprintf_s(buf, L"%.1f%%", v);
            return hstring{ buf };
        }

        // Minimal foreground awaitable (C++/WinRT's resume_foreground is not
        // available in this SDK): hops the coroutine onto the UI thread via
        // the dispatcher queue the project already uses for its timer.
        struct ForegroundAwait
        {
            Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };
            bool await_ready() const noexcept
            {
                return false;
            }
            void await_suspend(std::coroutine_handle<> h) const
            {
                queue.TryEnqueue(
                    Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal,
                    [h]() mutable { h.resume(); });
            }
            void await_resume() const noexcept
            {
            }
        };
    } // namespace

    // Single place that renders a DownloadState onto a card, so live
    // updates and Refresh-restores can never disagree (and the bar can
    // never jump backwards from two writers).
    void InstancesPage::PaintProgress(InstancesPage::Card& card, hstring const& label,
        unsigned long long done, unsigned long long total, double bps)
    {
        card.prog.IsIndeterminate(total == 0);
        if (total > 0)
            card.prog.Value(100.0 * static_cast<double>(done) / static_cast<double>(total));
        wchar_t buf[256]{};
        if (total > 0)
            swprintf_s(buf, L"%s %s / %s (%s)", std::wstring{ label }.c_str(),
                FormatBytes(done).c_str(), FormatBytes(total).c_str(), FormatSpeed(bps).c_str());
        else
            swprintf_s(buf, L"%s %s (%s)", std::wstring{ label }.c_str(),
                FormatBytes(done).c_str(), FormatSpeed(bps).c_str());
        card.progText.Text(buf);
    }

    InstancesPage::InstancesPage()
    {
        m_root.Spacing(12);
        m_root.Padding(ThicknessHelper::FromUniformLength(24));

        TextBlock head{};
        head.Text(L"Instances");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_root.Children().Append(head);

        StackPanel topRow{};
        topRow.Orientation(Orientation::Horizontal);
        topRow.Spacing(8);

        Button add{};
        add.Content(box_value(L"+ New instance"));
        add.Click([this](IInspectable const&, RoutedEventArgs const&) { AddDialog(); });
        topRow.Children().Append(add);

        m_profile.Click([this](IInspectable const&, RoutedEventArgs const&) { ProfileDialog(); });
        topRow.Children().Append(m_profile);
        m_root.Children().Append(topRow);

        m_status.Opacity(0.7);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_root.Children().Append(m_status);

        m_root.Children().Append(m_cards);
        m_cards.Spacing(12);

        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_timer = m_dispatcher.CreateTimer();
        m_timer.Interval(std::chrono::seconds{ 2 });
        m_timer.Tick([this](auto&&, auto&&) { UpdateStatsAsync(); });
        m_timer.Start();

        Refresh();
    }

    InstancesPage::Card* InstancesPage::FindCard(hstring const& id)
    {
        for (auto& c : m_cardList)
        {
            if (c.id == id)
                return &c;
        }
        return nullptr;
    }

    void InstancesPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }

    void InstancesPage::Refresh()
    {
        // Safety net: a failed content fade must never leave the page
        // transparent (opacity is set to 0 only right before a fade starts).
        try
        {
            m_root.Opacity(1.0);
        }
        catch (...)
        {
        }
        auto settings = LoadSettings();
        m_profile.Content(box_value(hstring{ L"Profile: " } + settings.username));

        m_cards.Children().Clear();
        m_cardList.clear();
        auto instances = LoadInstances();
        for (auto const& inst : instances)
        {
            bool running = Launcher::IsRunning(inst.id);
            bool isFabric = (inst.loader == L"fabric");
            bool preparing = m_preparing.find(std::wstring{ inst.id }) != m_preparing.end();

            // Status-driven accent so cards read at a glance.
            Microsoft::UI::Xaml::Media::SolidColorBrush railBrush = Theme::RailIdleBrush();
            Microsoft::UI::Xaml::Media::SolidColorBrush pillBg = Theme::PillIdleBackground();
            Microsoft::UI::Xaml::Media::Brush pillFg = Theme::DimBrush().as<Microsoft::UI::Xaml::Media::Brush>();
            Microsoft::UI::Xaml::Media::Brush frameStroke = Theme::CardStroke();
            hstring stateText = L"Idle";
            if (running)
            {
                railBrush = Theme::RailRunningBrush();
                pillBg = Theme::PillRunningBackground();
                pillFg = Theme::PillRunningForeground().as<Microsoft::UI::Xaml::Media::Brush>();
                frameStroke = Microsoft::UI::Xaml::Media::SolidColorBrush{
                    Windows::UI::ColorHelper::FromArgb(110, 0x44, 0xBD, 0x32)
                }.as<Microsoft::UI::Xaml::Media::Brush>();
                stateText = L"Running";
            }
            else if (preparing)
            {
                railBrush = Theme::RailPreparingBrush();
                pillBg = Theme::PillPreparingBackground();
                pillFg = Theme::PillPreparingForeground().as<Microsoft::UI::Xaml::Media::Brush>();
                frameStroke = Microsoft::UI::Xaml::Media::SolidColorBrush{
                    Windows::UI::ColorHelper::FromArgb(110, 0xE0, 0xA6, 0x3C)
                }.as<Microsoft::UI::Xaml::Media::Brush>();
                stateText = L"Preparing";
            }

            Border card{};
            card.Background(Theme::CardBrush());
            card.BorderBrush(frameStroke);
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
            card.Padding(ThicknessHelper::FromUniformLength(0));

            // Left status rail + padded content.
            Grid shell{};
            shell.ColumnDefinitions().Append(ColumnDefinition{});
            shell.ColumnDefinitions().Append(ColumnDefinition{});
            shell.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::Auto());
            shell.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));

            Border rail{};
            rail.Width(5);
            rail.Background(railBrush);
            rail.VerticalAlignment(VerticalAlignment::Stretch);
            rail.HorizontalAlignment(HorizontalAlignment::Left);
            rail.CornerRadius(Microsoft::UI::Xaml::CornerRadius{ 12, 0, 0, 12 });
            Grid::SetColumn(rail, 0);
            shell.Children().Append(rail);

            StackPanel body{};
            body.Spacing(10);
            body.Padding(ThicknessHelper::FromUniformLength(16));
            Grid::SetColumn(body, 1);
            shell.Children().Append(body);

            Grid head{};
            head.ColumnSpacing(16);
            head.ColumnDefinitions().Append(ColumnDefinition{});
            head.ColumnDefinitions().Append(ColumnDefinition{});
            head.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            head.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

            // Identity: game-icon tile + name stack.
            StackPanel identity{};
            identity.Orientation(Orientation::Horizontal);
            identity.Spacing(12);
            identity.VerticalAlignment(VerticalAlignment::Center);

            Border icon{};
            icon.Width(46);
            icon.Height(46);
            icon.CornerRadius(CornerRadiusHelper::FromUniformRadius(11));
            icon.BorderBrush(Theme::CardStroke());
            icon.BorderThickness(ThicknessHelper::FromUniformLength(1));
            icon.VerticalAlignment(VerticalAlignment::Center);
            icon.Background(isFabric ? Theme::IconFabricBackground().as<Microsoft::UI::Xaml::Media::Brush>()
                                     : Theme::IconVanillaBackground().as<Microsoft::UI::Xaml::Media::Brush>());
            wchar_t initialCh = L'?';
            try
            {
                std::wstring nm{ inst.name };
                if (!nm.empty())
                    initialCh = static_cast<wchar_t>(towupper(nm[0]));
            }
            catch (...)
            {
            }
            TextBlock initial{};
            initial.Text(hstring{ std::wstring(1, initialCh) });
            initial.HorizontalAlignment(HorizontalAlignment::Center);
            initial.VerticalAlignment(VerticalAlignment::Center);
            initial.FontSize(20);
            initial.FontWeight(Windows::UI::Text::FontWeights::Bold());
            initial.Foreground(Theme::IconForeground());
            // Loader logo in the tile (official Fabric mark / vanilla grass
            // block shipped in Assets/); initial letter stays as fallback.
            bool logoOk = false;
            try
            {
                wchar_t exe[MAX_PATH]{};
                if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0)
                {
                    auto art = std::filesystem::path{ exe }.parent_path() / L"Assets" /
                        (isFabric ? L"fabric.png" : L"vanilla.png");
                    std::error_code ec;
                    if (std::filesystem::exists(art, ec))
                    {
                        std::wstring uri{ L"file:///" };
                        std::wstring fp{ art.wstring() };
                        for (auto& c : fp)
                        {
                            if (c == L'\\')
                                c = L'/';
                        }
                        uri += fp;
                        Image logo{};
                        logo.Width(30);
                        logo.Height(30);
                        logo.HorizontalAlignment(HorizontalAlignment::Center);
                        logo.VerticalAlignment(VerticalAlignment::Center);
                        logo.Stretch(Stretch::Uniform);
                        logo.Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{
                            Windows::Foundation::Uri{ uri } });
                        icon.Child(logo);
                        logoOk = true;
                    }
                }
            }
            catch (...)
            {
            }
            if (!logoOk)
                icon.Child(initial);
            identity.Children().Append(icon);

            StackPanel nameCol{};
            nameCol.Orientation(Orientation::Vertical);
            nameCol.Spacing(5);
            nameCol.VerticalAlignment(VerticalAlignment::Center);

            StackPanel nameRow{};
            nameRow.Orientation(Orientation::Horizontal);
            nameRow.Spacing(8);
            nameRow.VerticalAlignment(VerticalAlignment::Center);

            TextBlock name{};
            name.Text(inst.name);
            name.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            name.VerticalAlignment(VerticalAlignment::Center);
            nameRow.Children().Append(name);

            Border badge{};
            badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            badge.Padding(ThicknessHelper::FromLengths(8, 3, 8, 3));
            badge.VerticalAlignment(VerticalAlignment::Center);
            TextBlock badgeText{};
            badgeText.FontSize(11);
            badgeText.FontWeight(Windows::UI::Text::FontWeights::Bold());
            if (isFabric)
            {
                badge.Background(SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(38, 0x44, 0xBD, 0x32) });
                badgeText.Foreground(Theme::GoodBrush());
                hstring label = L"FABRIC";
                if (!inst.loaderVersion.empty())
                    label = label + L" " + inst.loaderVersion;
                badgeText.Text(label);
            }
            else
            {
                badge.Background(SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(30, 0x9A, 0x9A, 0x9A) });
                badgeText.Foreground(Theme::DimBrush());
                badgeText.Text(L"VANILLA");
            }
            badge.Child(badgeText);
            nameRow.Children().Append(badge);

            TextBlock ver{};
            ver.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            ver.Text(hstring{ L"MC " } + inst.mcVersion);
            ver.VerticalAlignment(VerticalAlignment::Center);
            ver.Opacity(0.75);
            nameRow.Children().Append(ver);
            nameCol.Children().Append(nameRow);

            // Status pill + live details.
            StackPanel metaRow{};
            metaRow.Orientation(Orientation::Horizontal);
            metaRow.Spacing(8);
            metaRow.VerticalAlignment(VerticalAlignment::Center);

            Border pill{};
            pill.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
            pill.Padding(ThicknessHelper::FromLengths(10, 4, 10, 4));
            pill.Background(pillBg);
            pill.VerticalAlignment(VerticalAlignment::Center);
            StackPanel pillInner{};
            pillInner.Orientation(Orientation::Horizontal);
            pillInner.Spacing(6);
            pillInner.VerticalAlignment(VerticalAlignment::Center);
            Microsoft::UI::Xaml::Shapes::Ellipse dot{};
            dot.Width(8);
            dot.Height(8);
            dot.Fill(railBrush);
            dot.VerticalAlignment(VerticalAlignment::Center);
            pillInner.Children().Append(dot);
            TextBlock stateLabel{};
            stateLabel.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            stateLabel.Text(stateText);
            stateLabel.Foreground(pillFg);
            stateLabel.VerticalAlignment(VerticalAlignment::Center);
            pillInner.Children().Append(stateLabel);
            pill.Child(pillInner);
            metaRow.Children().Append(pill);

            TextBlock stats{};
            stats.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            stats.Opacity(0.65);
            stats.VerticalAlignment(VerticalAlignment::Center);
            stats.TextTrimming(TextTrimming::CharacterEllipsis);
            if (running)
                stats.Text(L"Starting game...");
            else if (preparing)
                stats.Text(L"Downloading game files...");
            else
                stats.Text(L"Ready to play");
            metaRow.Children().Append(stats);
            nameCol.Children().Append(metaRow);

            identity.Children().Append(nameCol);
            Grid::SetColumn(identity, 0);
            head.Children().Append(identity);

            body.Children().Append(head);

            ProgressBar prog{};
            prog.Minimum(0);
            prog.Maximum(100);
            prog.Height(6);
            prog.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
            prog.Visibility(Visibility::Collapsed);
            body.Children().Append(prog);

            TextBlock progText{};
            progText.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            progText.Opacity(0.7);
            progText.Visibility(Visibility::Collapsed);
            progText.TextWrapping(TextWrapping::Wrap);
            body.Children().Append(progText);

            TextBox gamelog{};
            gamelog.IsReadOnly(true);
            gamelog.AcceptsReturn(true);
            gamelog.TextWrapping(TextWrapping::Wrap);
            gamelog.MinHeight(120);
            gamelog.MaxHeight(220);
            gamelog.FontFamily(FontFamily(L"Consolas"));
            gamelog.FontSize(12);
            gamelog.Background(Theme::LogBackgroundBrush());
            gamelog.BorderBrush(Theme::CardStroke());
            gamelog.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
            gamelog.Padding(ThicknessHelper::FromUniformLength(8));
            gamelog.Visibility(running ? Visibility::Visible : Visibility::Collapsed);
            gamelog.Header(box_value(L"Client log"));
            body.Children().Append(gamelog);

            if (isFabric)
            {
                Border infoBar{};
                infoBar.Background(Theme::PillRunningBackground());
                infoBar.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                infoBar.Padding(ThicknessHelper::FromLengths(10, 8, 10, 8));
                TextBlock note{};
                note.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                note.Foreground(Theme::GoodBrush());
                note.Opacity(0.9);
                note.TextWrapping(TextWrapping::Wrap);
                note.Text(L"Fabric loader + Fabric API install automatically on Play.");
                infoBar.Child(note);
                body.Children().Append(infoBar);
            }

            StackPanel buttons{};
            buttons.Orientation(Orientation::Horizontal);
            buttons.Spacing(8);
            buttons.VerticalAlignment(VerticalAlignment::Center);

            hstring id = inst.id;
            Button play{};
            play.Content(box_value(L"Play"));
            play.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
            play.MinWidth(88);
            play.IsEnabled(!running && !preparing);
            play.Click([this, id](IInspectable const&, RoutedEventArgs const&) { PlayInstance(id); });

            Button stop{};
            stop.Content(box_value(L"Stop"));
            stop.IsEnabled(running);
            stop.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                Launcher::Stop(id);
                SetStatus(L"Stopped.");
                Refresh();
            });

            Button restart{};
            restart.Content(box_value(L"Restart"));
            restart.IsEnabled(running && !preparing);
            restart.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                Launcher::Stop(id);
                PlayInstance(id);
            });
            buttons.Children().Append(play);
            buttons.Children().Append(stop);
            buttons.Children().Append(restart);

            if (isFabric)
            {
                Button modsBtn{};
                modsBtn.Content(box_value(L"Mods"));
                modsBtn.Click([this, id](IInspectable const&, RoutedEventArgs const&) { ModsDialog(id); });
                buttons.Children().Append(modsBtn);
            }

            Button del{};
            del.Content(box_value(L"Delete"));
            del.IsEnabled(!running && !preparing);
            del.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                if (m_preparing.find(std::wstring{ id }) != m_preparing.end() ||
                    Launcher::IsRunning(id))
                {
                    SetStatus(L"Stop it first.");
                    return;
                }
                m_downloads.erase(std::wstring{ id });
                auto all = LoadInstances();
                all.erase(std::remove_if(all.begin(), all.end(),
                    [&](Instance const& i) { return i.id == id; }), all.end());
                SaveInstances(all);
                SetStatus(L"Instance deleted.");
                Refresh();
            });
            buttons.Children().Append(del);
            Grid::SetColumn(buttons, 1);
            head.Children().Append(buttons);

            card.Child(shell);
            m_cards.Children().Append(card);

            Card c{};
            c.id = id;
            c.frame = card;
            c.rail = rail;
            c.dot = dot;
            c.stateLabel = stateLabel;
            c.stats = stats;
            c.play = play;
            c.stop = stop;
            c.restart = restart;
            c.prog = prog;
            c.progText = progText;
            c.gamelog = gamelog;
            m_cardList.push_back(std::move(c));
            // Repaint a download that is still in flight (tab switch or any
            // Refresh rebuilds the card collapsed by default).
            if (auto dit = m_downloads.find(std::wstring{ id }); dit != m_downloads.end())
            {
                auto& back = m_cardList.back();
                back.prog.Visibility(Visibility::Visible);
                back.progText.Visibility(Visibility::Visible);
                PaintProgress(back, dit->second.label, dit->second.done,
                    dit->second.total, dit->second.bps);
            }
        }
        UpdateStatsAsync();
    }

    namespace
    {
        // One row of the mods dialog: what file it is plus the live widgets
        // an async update check may want to touch later.
        struct ModRow
        {
            std::filesystem::path file{};
            bool enabled = true;
            Microsoft::UI::Xaml::Controls::TextBlock status{};
            Microsoft::UI::Xaml::Controls::StackPanel actions{};
        };

        struct VersionsWaiter
        {
            std::vector<Modrinth::ModVersion> versions{};
            bool done = false;
            std::coroutine_handle<> handle{};

            bool await_ready() const
            {
                return done;
            }
            void await_suspend(std::coroutine_handle<> h)
            {
                handle = h;
            }
            std::vector<Modrinth::ModVersion> await_resume()
            {
                return std::move(versions);
            }
        };

        std::vector<int> VersionParts(std::wstring s)
        {
            if (!s.empty() && (s[0] == L'v' || s[0] == L'V'))
                s = s.substr(1);
            if (auto dash = s.find(L'-'); dash != std::wstring::npos)
                s = s.substr(0, dash);
            std::vector<int> parts;
            size_t start = 0;
            while (start <= s.size())
            {
                auto dot = s.find(L'.', start);
                auto token = s.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start);
                try
                {
                    parts.push_back(token.empty() ? 0 : std::stoi(token));
                }
                catch (...)
                {
                    parts.push_back(0);
                }
                if (dot == std::wstring::npos)
                    break;
                start = dot + 1;
            }
            return parts;
        }

        bool VersionNewer(std::wstring const& candidate, std::wstring const& current)
        {
            if (candidate.empty() || current.empty())
                return false;
            auto a = VersionParts(current);
            auto b = VersionParts(candidate);
            size_t n = (std::max)(a.size(), b.size());
            a.resize(n, 0);
            b.resize(n, 0);
            for (size_t i = 0; i < n; ++i)
            {
                if (b[i] != a[i])
                    return b[i] > a[i];
            }
            return false;
        }

        // Download the picked build over the old jar, drop the old file when
        // the names differ, then rebuild the dialog list.
        fire_and_forget ApplyModUpdate(ModRow row, Modrinth::ModFile file,
            std::function<void()> refresh)
        {
            hstring msg;
            try
            {
                msg = co_await Modrinth::DownloadFileAsync(file, row.file.parent_path().wstring());
            }
            catch (...)
            {
                msg = L"Download failed.";
            }
            std::wstring text{ msg };
            bool ok = text.rfind(L"Installed", 0) == 0 || text.rfind(L"Already present", 0) == 0;
            if (ok && row.file.filename() != std::filesystem::path{ std::wstring{ file.filename } }.filename())
            {
                std::error_code ec;
                std::filesystem::remove(row.file, ec);
                if (refresh)
                    refresh();
            }
            if (row.status)
                row.status.Text(msg);
        }

        // Ask Modrinth for the newest build of this mod on (mc, fabric) and,
        // when it is newer than the jar's own fabric.mod.json version, add an
        // Update button to the row.
        fire_and_forget CheckModUpdate(ModRow row, hstring mc, std::function<void()> refresh)
        {
            try
            {
                if (row.status)
                    row.status.Text(L"reading...");
                std::string meta;
                if (!Http::ZipEntryToString(row.file, L"fabric.mod.json", meta) || meta.empty())
                {
                    if (row.status)
                        row.status.Text(L"no fabric.mod.json");
                    co_return;
                }
                auto o = JsonObject::Parse(to_hstring(meta));
                hstring modId;
                hstring curVer;
                try
                {
                    if (o.HasKey(L"id"))
                        modId = o.GetNamedString(L"id");
                    if (o.HasKey(L"version"))
                        curVer = o.GetNamedString(L"version");
                }
                catch (...)
                {
                }
                if (modId.empty() || curVer.empty())
                {
                    if (row.status)
                        row.status.Text(L"cannot read mod id/version");
                    co_return;
                }
                if (row.status)
                    row.status.Text(hstring{ L"checking " } + modId + L"...");

                VersionsWaiter wait{};
                Modrinth::GetVersionsAsync(modId, mc, L"fabric",
                    [&wait](std::vector<Modrinth::ModVersion> versions) {
                        wait.versions = std::move(versions);
                        wait.done = true;
                        if (wait.handle)
                        {
                            auto h = wait.handle;
                            wait.handle = {};
                            h.resume();
                        }
                    });
                auto versions = co_await wait;
                if (versions.empty())
                {
                    if (row.status)
                        row.status.Text(hstring{ L"no builds for " } + mc);
                    co_return;
                }
                auto const& latest = versions.front();
                if (!VersionNewer(std::wstring{ latest.versionNumber }, std::wstring{ curVer }))
                {
                    if (row.status)
                        row.status.Text(hstring{ L"up to date (" } + curVer + L")");
                    co_return;
                }

                Modrinth::ModFile file{};
                for (auto const& f : latest.files)
                {
                    if (f.primary && !f.url.empty())
                    {
                        file = f;
                        break;
                    }
                }
                if (file.url.empty())
                {
                    for (auto const& f : latest.files)
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
                    if (row.status)
                        row.status.Text(L"latest build has no file");
                    co_return;
                }
                if (row.status)
                    row.status.Text(hstring{ L"update: " } + curVer + L" -> " + latest.versionNumber);
                if (row.actions)
                {
                    Button upd{};
                    upd.Content(box_value(L"Update"));
                    upd.Click([row, file, refresh](IInspectable const&, RoutedEventArgs const&) {
                        ApplyModUpdate(row, file, refresh);
                    });
                    row.actions.Children().Append(upd);
                }
            }
            catch (...)
            {
                if (row.status)
                    row.status.Text(L"update check failed");
            }
        }
    } // namespace

    void InstancesPage::StageMods(std::filesystem::path const& instanceMods,
        std::filesystem::path const& gameMods)
    {
        // Fabric only reads <gameDir>/mods, but game files are shared by all
        // instances, so the instance's own folder is staged in right before
        // launch. *.jar.disabled files are deliberately left behind.
        try
        {
            std::error_code ec;
            std::filesystem::create_directories(instanceMods, ec);
            std::filesystem::create_directories(gameMods, ec);

            // First time this instance gets its own folder: adopt the jars
            // from the old shared layout so nothing silently disappears.
            bool emptyInstance = true;
            for (auto const& e : std::filesystem::directory_iterator(instanceMods, ec))
            {
                if (e.is_regular_file(ec))
                {
                    emptyInstance = false;
                    break;
                }
            }
            if (emptyInstance)
            {
                for (auto const& e : std::filesystem::directory_iterator(gameMods, ec))
                {
                    if (!e.is_regular_file(ec))
                        continue;
                    auto ext = e.path().extension().wstring();
                    for (auto& c : ext)
                        c = static_cast<wchar_t>(towlower(c));
                    if (ext != L".jar")
                        continue;
                    std::filesystem::copy_file(e.path(), instanceMods / e.path().filename(),
                        std::filesystem::copy_options::overwrite_existing, ec);
                }
            }

            // Collect first: erasing entries while a directory_iterator is
            // mid-walk can truncate the walk on Windows.
            std::vector<std::filesystem::path> sharedJars;
            for (auto const& e : std::filesystem::directory_iterator(gameMods, ec))
            {
                if (!e.is_regular_file(ec))
                    continue;
                auto ext = e.path().extension().wstring();
                for (auto& c : ext)
                    c = static_cast<wchar_t>(towlower(c));
                if (ext == L".jar")
                    sharedJars.push_back(e.path());
            }
            for (auto const& p : sharedJars)
                std::filesystem::remove(p, ec);
            for (auto const& e : std::filesystem::directory_iterator(instanceMods, ec))
            {
                if (!e.is_regular_file(ec))
                    continue;
                auto ext = e.path().extension().wstring();
                for (auto& c : ext)
                    c = static_cast<wchar_t>(towlower(c));
                if (ext != L".jar")
                    continue;
                std::filesystem::copy_file(e.path(), gameMods / e.path().filename(),
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget InstancesPage::ModsDialog(hstring id)
    {
        auto settings = LoadSettings();
        auto modsDir = InstanceModsDir(settings, id);
        std::error_code ec;
        std::filesystem::create_directories(modsDir, ec);

        hstring mc;
        hstring instName;
        for (auto const& i : LoadInstances())
        {
            if (i.id == id)
            {
                mc = i.mcVersion;
                instName = i.name;
                break;
            }
        }
        if (instName.empty())
        {
            SetStatus(L"Instance is gone.");
            co_return;
        }

        StackPanel panel{};
        panel.Spacing(8);

        TextBlock dirText{};
        dirText.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        dirText.Opacity(0.5);
        dirText.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(dirText);

        StackPanel tools{};
        tools.Orientation(Orientation::Horizontal);
        tools.Spacing(8);
        panel.Children().Append(tools);

        StackPanel list{};
        list.Spacing(6);
        panel.Children().Append(list);

        TextBlock status{};
        status.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        status.Opacity(0.7);
        status.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(status);

        auto rows = std::make_shared<std::vector<ModRow>>();
        auto rebuild = std::make_shared<std::function<void()>>();
        *rebuild = [rows, list, dirText, modsDir, status,
            weak = std::weak_ptr<std::function<void()>>(rebuild)]() {
            try
            {
                list.Children().Clear();
                rows->clear();
                std::error_code e;
                std::vector<std::filesystem::path> files;
                if (std::filesystem::exists(modsDir, e))
                {
                    for (auto const& en : std::filesystem::directory_iterator(modsDir, e))
                    {
                        if (!en.is_regular_file(e))
                            continue;
                        auto ext = en.path().extension().wstring();
                        for (auto& c : ext)
                            c = static_cast<wchar_t>(towlower(c));
                        if (ext != L".jar" && ext != L".disabled")
                            continue;
                        files.push_back(en.path());
                    }
                }
                std::sort(files.begin(), files.end());

                int count = 0;
                for (auto const& p : files)
                {
                    auto ext = p.extension().wstring();
                    for (auto& c : ext)
                        c = static_cast<wchar_t>(towlower(c));
                    bool enabled = (ext == L".jar");

                    StackPanel row{};
                    row.Orientation(Orientation::Horizontal);
                    row.Spacing(8);

                    TextBlock t{};
                    t.Text(hstring{ p.filename().wstring() });
                    t.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    t.VerticalAlignment(VerticalAlignment::Center);
                    t.MaxWidth(340);
                    t.TextTrimming(TextTrimming::CharacterEllipsis);
                    row.Children().Append(t);

                    TextBlock st{};
                    st.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    st.Opacity(0.7);
                    st.Width(190);
                    st.VerticalAlignment(VerticalAlignment::Center);
                    st.Text(enabled ? L"" : L"disabled");
                    row.Children().Append(st);

                    StackPanel actions{};
                    actions.Orientation(Orientation::Horizontal);
                    actions.Spacing(6);
                    actions.VerticalAlignment(VerticalAlignment::Center);
                    row.Children().Append(actions);

                    ModRow state{};
                    state.file = p;
                    state.enabled = enabled;
                    state.status = st;
                    state.actions = actions;

                    Button toggle{};
                    toggle.Content(box_value(enabled ? L"Disable" : L"Enable"));
                    toggle.Click([weak, p, enabled](IInspectable const&, RoutedEventArgs const&) {
                        std::error_code e2;
                        std::filesystem::path target;
                        if (enabled)
                            target = std::filesystem::path{ p.wstring() + L".disabled" };
                        else
                        {
                            std::wstring w{ p.wstring() };
                            if (w.size() > 9 && w.substr(w.size() - 9) == L".disabled")
                                w = w.substr(0, w.size() - 9);
                            target = std::filesystem::path{ w };
                        }
                        std::filesystem::rename(p, target, e2);
                        if (auto r = weak.lock())
                            if (*r)
                                (*r)();
                    });
                    actions.Children().Append(toggle);

                    Button rm{};
                    rm.Content(box_value(L"Remove"));
                    rm.Click([weak, p, status](IInspectable const&, RoutedEventArgs const&) {
                        std::error_code e2;
                        std::filesystem::remove(p, e2);
                        if (status)
                            status.Text(hstring{ L"Removed " } + hstring{ p.filename().wstring() });
                        if (auto r = weak.lock())
                            if (*r)
                                (*r)();
                    });
                    actions.Children().Append(rm);

                    list.Children().Append(row);
                    rows->push_back(state);
                    if (++count >= 60)
                        break;
                }

                wchar_t dbuf[512]{};
                swprintf_s(dbuf, L"Folder: %s  (%d mod(s))",
                    std::wstring{ modsDir.wstring() }.c_str(), count);
                dirText.Text(dbuf);
                if (count == 0)
                {
                    TextBlock empty{};
                    empty.Text(L"No mods in this instance yet. Install some from the Mods page.");
                    empty.Opacity(0.6);
                    empty.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    empty.TextWrapping(TextWrapping::Wrap);
                    list.Children().Append(empty);
                }
            }
            catch (...)
            {
            }
        };
        (*rebuild)();

        Button openBtn{};
        openBtn.Content(box_value(L"Open folder"));
        openBtn.Click([modsDir](IInspectable const&, RoutedEventArgs const&) {
            std::error_code e;
            std::filesystem::create_directories(modsDir, e);
            ShellExecuteW(nullptr, L"open", modsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
        tools.Children().Append(openBtn);

        Button check{};
        check.Content(box_value(L"Check for updates"));
        check.Click([rows, mc, rebuild, status](IInspectable const&, RoutedEventArgs const&) {
            if (rows->empty())
            {
                if (status)
                    status.Text(L"No mods to check.");
                return;
            }
            if (status)
                status.Text(hstring{ L"Checking " } + to_hstring(rows->size()) +
                    L" mod(s) against Modrinth for " + mc + L"...");
            std::function<void()> refresh = *rebuild;
            for (auto const& r : *rows)
                CheckModUpdate(r, mc, refresh);
        });
        tools.Children().Append(check);

        ContentDialog dialog{};
        dialog.Title(box_value(hstring{ L"Mods - " } + instName));
        dialog.Content(panel);
        dialog.CloseButtonText(L"Close");
        dialog.XamlRoot(m_root.XamlRoot());
        co_await dialog.ShowAsync();
    }

    hstring InstancesPage::TailText(std::filesystem::path const& file)
    {
        try
        {
            std::ifstream f(file, std::ios::binary | std::ios::ate);
            if (!f.good())
                return L"";
            auto size = f.tellg();
            const long long cap = 16384;
            long long start = size > cap ? (long long)size - cap : 0;
            f.seekg(start);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (start > 0)
            {
                auto nl = text.find('\n');
                if (nl != std::string::npos)
                    text = text.substr(nl + 1);
            }
            if (text.size() > 16000)
                text = text.substr(text.size() - 16000);
            return to_hstring(text);
        }
        catch (...)
        {
            return L"(log unavailable)";
        }
    }

    fire_and_forget InstancesPage::UpdateStatsAsync()
    {
        bool expected = false;
        if (!m_statsBusy.compare_exchange_strong(expected, true))
            co_return; // previous sample still running; skip, never stack
        struct BusyGuard
        {
            std::atomic<bool>& flag;
            ~BusyGuard()
            {
                flag.store(false);
            }
        };
        BusyGuard guard{ m_statsBusy };
        try
        {
            // Snapshot on the UI thread: Refresh() mutates this list here.
            std::vector<hstring> ids;
            std::vector<bool> preparingSnap;
            for (auto const& card : m_cardList)
            {
                ids.push_back(card.id);
                preparingSnap.push_back(
                    m_preparing.find(std::wstring{ card.id }) != m_preparing.end());
            }
            auto logFile = std::filesystem::path{ std::wstring{ EffectiveGameDir(LoadSettings()) } } /
                L"logs-pretclient" / L"latest.txt";

            co_await winrt::resume_background();
            // Everything below may block (PDH, process queries, disk reads)
            // and now runs off the UI thread.
            auto sys = m_sampler.PollSystem();
            struct Row
            {
                hstring id{};
                bool running = false;
                void* handle = nullptr;
                unsigned long pid = 0;
                double cpu = -1.0;
                unsigned long long ram = 0;
                hstring tail{};
            };
            std::vector<Row> rows;
            for (auto const& id : ids)
            {
                Row r{};
                r.id = id;
                r.running = Launcher::IsRunning(id);
                if (!r.running)
                {
                    rows.push_back(std::move(r));
                    continue;
                }
                r.handle = Launcher::RawHandle(id);
                r.pid = Launcher::Pid(id);
                r.cpu = m_sampler.PollProcessCpu(r.handle);
                r.ram = m_sampler.ProcessPrivateBytes(r.handle);
                r.tail = TailText(logFile);
                rows.push_back(std::move(r));
            }

            co_await ForegroundAwait{ m_dispatcher };
            for (size_t i = 0; i < rows.size(); ++i)
            {
                auto const& r = rows[i];
                bool preparing = i < preparingSnap.size() ? preparingSnap[i] : false;
                // A download still in flight also counts as preparing, even if
                // the set was cleared between snapshot and paint.
                if (!preparing && m_downloads.find(std::wstring{ r.id }) != m_downloads.end() && !r.running)
                    preparing = true;
                auto* card = FindCard(r.id);
                if (!card)
                    continue;
                auto paintState = [&](bool isRunning, bool isPreparing) {
                    if (!card->rail || !card->dot || !card->stateLabel || !card->frame)
                        return;
                    if (isRunning)
                    {
                        card->rail.Background(Theme::RailRunningBrush());
                        card->dot.Fill(Theme::RailRunningBrush());
                        card->stateLabel.Text(L"Running");
                        card->stateLabel.Foreground(Theme::PillRunningForeground());
                        card->frame.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush{
                            Windows::UI::ColorHelper::FromArgb(110, 0x44, 0xBD, 0x32) });
                    }
                    else if (isPreparing)
                    {
                        card->rail.Background(Theme::RailPreparingBrush());
                        card->dot.Fill(Theme::RailPreparingBrush());
                        card->stateLabel.Text(L"Preparing");
                        card->stateLabel.Foreground(Theme::PillPreparingForeground());
                        card->frame.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush{
                            Windows::UI::ColorHelper::FromArgb(110, 0xE0, 0xA6, 0x3C) });
                    }
                    else
                    {
                        card->rail.Background(Theme::RailIdleBrush());
                        card->dot.Fill(Theme::RailIdleBrush());
                        card->stateLabel.Text(L"Idle");
                        card->stateLabel.Foreground(Theme::DimBrush());
                        card->frame.BorderBrush(Theme::CardStroke());
                    }
                };
                if (!r.running)
                {
                    paintState(false, preparing);
                    if (preparing)
                        card->stats.Text(L"Downloading game files...");
                    else
                        card->stats.Text(L"Ready to play");
                    // Keep a preparing bar visible; hide the log when idle.
                    if (!preparing)
                        card->gamelog.Visibility(Visibility::Collapsed);
                    continue;
                }
                paintState(true, false);
                wchar_t buf[192]{};
                swprintf_s(buf, L"pid %lu | CPU %s | RAM %s | GPU %s",
                    r.pid,
                    Pct(r.cpu).c_str(), FormatBytes(r.ram).c_str(), Pct(sys.gpuPercent).c_str());
                card->stats.Text(buf);
                if (!r.tail.empty())
                {
                    card->gamelog.Text(r.tail);
                    card->gamelog.Visibility(Visibility::Visible);
                }
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget InstancesPage::PlayInstance(hstring id)
    {
        SetStatus(L"Preparing...");
        Instance inst{};
        bool found = false;
        for (auto const& i : LoadInstances())
        {
            if (i.id == id)
            {
                inst = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            SetStatus(L"Instance is gone.");
            co_return;
        }
        // One prepare per instance: a second Play would start a duplicate
        // download fighting over the same progress bar (and the same files).
        if (Launcher::IsRunning(id))
        {
            SetStatus(L"Already running.");
            co_return;
        }
        if (m_preparing.find(std::wstring{ id }) != m_preparing.end())
        {
            SetStatus(L"Already preparing (download in progress).");
            co_return;
        }
        m_preparing.insert(std::wstring{ id });
        auto settings = LoadSettings();
        std::wstring gameDir{ EffectiveGameDir(settings) };
        auto instanceMods = InstanceModsDir(settings, id);
        auto gameMods = std::filesystem::path{ gameDir } / L"mods";
        bool isFabric = (inst.loader == L"fabric");
        hstring username = settings.username.empty() ? hstring{ L"Steve" } : settings.username;
        hstring javaPath = settings.javaPath;
        int minMem = settings.minMemMb;
        int maxMem = settings.maxMemMb;

        if (auto* card = FindCard(id))
        {
            card->prog.Visibility(Visibility::Visible);
            card->prog.IsIndeterminate(true);
            card->progText.Visibility(Visibility::Visible);
            card->progText.Text(L"Starting...");
            if (card->rail && card->dot && card->stateLabel && card->frame)
            {
                card->rail.Background(Theme::RailPreparingBrush());
                card->dot.Fill(Theme::RailPreparingBrush());
                card->stateLabel.Text(L"Preparing");
                card->stateLabel.Foreground(Theme::PillPreparingForeground());
                card->frame.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush{
                    Windows::UI::ColorHelper::FromArgb(110, 0xE0, 0xA6, 0x3C) });
            }
            card->stats.Text(L"Downloading game files...");
            card->play.IsEnabled(false);
        }

        auto fail = [this, id](hstring const& msg) {
            m_preparing.erase(std::wstring{ id });
            m_downloads.erase(std::wstring{ id });
            SetStatus(msg);
            if (auto* card = FindCard(id))
            {
                card->prog.Visibility(Visibility::Collapsed);
                card->progText.Visibility(Visibility::Collapsed);
            }
            Refresh();
        };

        if (inst.loader == L"fabric" && inst.loaderVersion.empty())
        {
            SetStatus(L"Resolving fabric loader...");
            hstring loaderVer;
            try
            {
                loaderVer = co_await Fabric::GetLatestLoader(inst.mcVersion);
            }
            catch (...)
            {
            }
            if (loaderVer.empty())
            {
                fail(hstring{ L"No fabric loader for " } + inst.mcVersion);
                co_return;
            }
            inst.loaderVersion = loaderVer;
        }

        SetStatus(hstring{ L"Preparing " } + inst.mcVersion + L"... (first run downloads game files)");
        auto logCb = [this](hstring const& line) { SetStatus(line); };
        auto progCb = [this, id](hstring file, unsigned long long done, unsigned long long total, double bps) {
            m_downloads[std::wstring{ id }] = DownloadState{ file, done, total, bps };
            if (auto* card = FindCard(id))
            {
                card->prog.Visibility(Visibility::Visible);
                card->progText.Visibility(Visibility::Visible);
                PaintProgress(*card, file, done, total, bps);
            }
        };
        Downloader::PrepareAsync(
            inst.mcVersion, inst.loader, inst.loaderVersion, gameDir,
            isFabric ? std::wstring{ instanceMods.wstring() } : std::wstring{},
            logCb, progCb,
            [this, id, username, javaPath, minMem, maxMem, fail, isFabric, instanceMods, gameMods](
                bool ok, Downloader::PreparedGame game, hstring error) {
                if (!ok)
                {
                    fail(hstring{ L"Prepare failed: " } + error);
                    return;
                }
                FinishLaunch(id, username, javaPath, minMem, maxMem,
                    std::move(game), isFabric, instanceMods, gameMods);
            });
    }

    fire_and_forget InstancesPage::FinishLaunch(hstring id, hstring username, hstring javaPath,
        int minMem, int maxMem, Downloader::PreparedGame game, bool isFabric,
        std::filesystem::path instanceMods, std::filesystem::path gameMods)
    {
        auto failed = [this, id](hstring const& msg) {
            SetStatus(msg);
            if (auto* card = FindCard(id))
            {
                card->prog.Visibility(Visibility::Collapsed);
                card->progText.Visibility(Visibility::Collapsed);
            }
            Refresh();
        };

        SetStatus(hstring{ L"Locating Java " } + to_hstring(game.javaMajor) + L"+...");
        co_await winrt::resume_background();
        // Everything down to the foreground hop may block (disk copies,
        // java -version probes with long waits) and now runs off the UI.
        if (isFabric)
            StageMods(instanceMods, gameMods);
        hstring javaExe = javaPath;
        if (!javaExe.empty())
        {
            auto v = Java::Verify(javaExe);
            if (v.major < game.javaMajor)
                javaExe = L"";
        }
        int pickedChecked = 0;
        int pickedBest = 0;
        if (javaExe.empty())
        {
            auto picked = Java::PickDetailed(game.javaMajor);
            javaExe = picked.path;
            pickedChecked = picked.checked;
            pickedBest = picked.bestMajor;
        }

        co_await ForegroundAwait{ m_dispatcher };
        m_preparing.erase(std::wstring{ id });
        m_downloads.erase(std::wstring{ id });
        if (javaExe.empty())
        {
            wchar_t buf[320]{};
            if (pickedBest > 0)
                swprintf_s(buf, L"No Java %d+ found (checked %d install(s), newest is Java %d). Install a 64-bit Java %d+ or set the java.exe path in Settings.",
                    game.javaMajor, pickedChecked, pickedBest, game.javaMajor);
            else
                swprintf_s(buf, L"No Java %d+ found (checked %d install(s)). Install a 64-bit Java %d+ or set the java.exe path in Settings.",
                    game.javaMajor, pickedChecked, game.javaMajor);
            failed(buf);
            co_return;
        }
        auto uuid = Launcher::OfflineUuid(username);
        auto cmd = Launcher::BuildCommand(game, username, uuid, minMem, maxMem, javaExe);
        hstring err;
        if (Launcher::Start(cmd, id, err))
            SetStatus(hstring{ L"Running (pid " } + to_hstring(static_cast<std::uint32_t>(Launcher::Pid(id))) +
                L"). Game log below and in logs-pretclient/latest.txt");
        else
            SetStatus(hstring{ L"Launch failed: " } + err);
        Refresh();
    }

    fire_and_forget InstancesPage::AddDialog()
    {
        SetStatus(L"Loading version list...");

        TextBox nameBox{};
        nameBox.Header(box_value(L"Name"));
        nameBox.Text(L"New Instance");

        ComboBox typeBox{};
        typeBox.Header(box_value(L"Type"));
        typeBox.Items().Append(box_value(L"release"));
        typeBox.Items().Append(box_value(L"snapshot"));
        typeBox.Items().Append(box_value(L"old_beta"));
        typeBox.Items().Append(box_value(L"old_alpha"));
        typeBox.SelectedIndex(0);

        ComboBox versionBox{};
        versionBox.Header(box_value(L"Version"));
        versionBox.Items().Append(box_value(L"1.21.4"));
        versionBox.SelectedIndex(0);

        ComboBox loaderBox{};
        loaderBox.Header(box_value(L"Loader"));
        loaderBox.Items().Append(box_value(L"vanilla"));
        loaderBox.Items().Append(box_value(L"fabric"));
        loaderBox.SelectedIndex(0);

        TextBox loaderVerBox{};
        loaderVerBox.Header(box_value(L"Fabric loader version (empty = latest)"));

        StackPanel panel{};
        panel.Spacing(8);
        panel.Children().Append(nameBox);
        panel.Children().Append(typeBox);
        panel.Children().Append(versionBox);
        panel.Children().Append(loaderBox);
        panel.Children().Append(loaderVerBox);

        ContentDialog dialog{};
        dialog.Title(box_value(L"New instance"));
        dialog.Content(panel);
        dialog.PrimaryButtonText(L"Create");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(m_root.XamlRoot());

        Versions::FetchManifestAsync(
            [this, typeBox, versionBox, loaderBox, loaderVerBox, nameBox, dialog](
                Versions::Manifest m) mutable {
                auto fillVersions = [versionBox, typeBox, m]() mutable {
                    versionBox.Items().Clear();
                    hstring type = unbox_value_or<hstring>(typeBox.SelectedItem(), L"release");
                    int added = 0;
                    for (auto const& e : m.entries)
                    {
                        if (e.type == type)
                        {
                            versionBox.Items().Append(box_value(e.id));
                            if (++added >= 400)
                                break;
                        }
                    }
                    if (added == 0)
                    {
                        versionBox.Items().Append(box_value(L"1.21.4"));
                        versionBox.Items().Append(box_value(L"1.20.4"));
                        versionBox.Items().Append(box_value(L"1.8.9"));
                    }
                    versionBox.SelectedIndex(0);
                };
                typeBox.SelectionChanged(
                    [fillVersions](IInspectable const&, SelectionChangedEventArgs const&) mutable {
                        fillVersions();
                    });
                fillVersions();
                SetStatus(L"");
                ShowCreateDialog(dialog, nameBox, versionBox, loaderBox, loaderVerBox);
            });
    }

    fire_and_forget InstancesPage::ShowCreateDialog(
        ContentDialog dialog, TextBox nameBox, ComboBox versionBox,
        ComboBox loaderBox, TextBox loaderVerBox)
    {
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
            co_return;
        Instance in{};
        in.id = NewInstanceId();
        in.name = nameBox.Text().empty() ? hstring{ L"Instance" } : hstring{ nameBox.Text() };
        in.mcVersion = unbox_value_or<hstring>(versionBox.SelectedItem(), L"1.21.4");
        in.loader = unbox_value_or<hstring>(loaderBox.SelectedItem(), L"vanilla");
        in.loaderVersion = loaderVerBox.Text();
        auto all = LoadInstances();
        all.push_back(std::move(in));
        SaveInstances(all);
        SetStatus(L"Instance created.");
        Refresh();
    }

    fire_and_forget InstancesPage::ProfileDialog()
    {
        auto settings = LoadSettings();
        StackPanel panel{};
        panel.Spacing(8);

        TextBlock cur{};
        cur.Text(hstring{ L"Active profile: " } + settings.username);
        cur.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
        panel.Children().Append(cur);

        StackPanel list{};
        list.Spacing(4);
        panel.Children().Append(list);

        auto rebuild = [&]() {
            list.Children().Clear();
            auto s = LoadSettings();
            for (auto const& name : s.profiles)
            {
                StackPanel row{};
                row.Orientation(Orientation::Horizontal);
                row.Spacing(8);
                TextBlock t{};
                t.Text(name + (name == s.username ? hstring{ L" (active)" } : hstring{}));
                t.VerticalAlignment(VerticalAlignment::Center);
                t.Width(220);
                row.Children().Append(t);
                if (name != s.username)
                {
                    Button use{};
                    use.Content(box_value(L"Switch"));
                    use.Click([this, name](IInspectable const&, RoutedEventArgs const&) {
                        auto s2 = LoadSettings();
                        s2.username = name;
                        SaveSettings(s2);
                        SetStatus(hstring{ L"Profile: " } + name);
                        Refresh();
                    });
                    row.Children().Append(use);
                    Button del{};
                    del.Content(box_value(L"Remove"));
                    del.Click([this, name](IInspectable const&, RoutedEventArgs const&) {
                        auto s2 = LoadSettings();
                        s2.profiles.erase(std::remove(s2.profiles.begin(), s2.profiles.end(), name),
                            s2.profiles.end());
                        if (s2.profiles.empty())
                            s2.profiles.push_back(L"Steve");
                        if (s2.username == name)
                            s2.username = s2.profiles.front();
                        SaveSettings(s2);
                        SetStatus(L"Profile removed.");
                        Refresh();
                    });
                    row.Children().Append(del);
                }
                list.Children().Append(row);
            }
        };
        rebuild();

        StackPanel addRow{};
        addRow.Orientation(Orientation::Horizontal);
        addRow.Spacing(8);
        TextBox addBox{};
        addBox.PlaceholderText(L"New profile name");
        addBox.MaxLength(16);
        addBox.Width(220);
        addRow.Children().Append(addBox);
        Button addBtn{};
        addBtn.Content(box_value(L"Add"));
        addBtn.Click([addBox, rebuild, this](IInspectable const&, RoutedEventArgs const&) mutable {
            hstring name = addBox.Text();
            if (name.empty())
                return;
            auto s = LoadSettings();
            bool has = false;
            for (auto const& p : s.profiles)
            {
                if (_wcsicmp(p.c_str(), name.c_str()) == 0)
                {
                    has = true;
                    break;
                }
            }
            if (!has)
                s.profiles.push_back(name);
            s.username = name;
            SaveSettings(s);
            addBox.Text(L"");
            SetStatus(hstring{ L"Profile: " } + name);
            rebuild();
            Refresh();
        });
        addRow.Children().Append(addBtn);
        panel.Children().Append(addRow);

        ContentDialog dialog{};
        dialog.Title(box_value(L"Profiles"));
        dialog.Content(panel);
        dialog.CloseButtonText(L"Close");
        dialog.XamlRoot(m_root.XamlRoot());
        co_await dialog.ShowAsync();
    }
}
