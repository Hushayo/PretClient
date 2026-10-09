#include "pch.h"
#include "InstancesPage.h"
#include "Theme.h"
#include "../Minecraft/Downloader.h"
#include "../Minecraft/Fabric.h"
#include "../Minecraft/Forge.h"
#include "../Minecraft/Http.h"
#include "../Minecraft/Modrinth.h"
#include "../Minecraft/NeoForge.h"
#include "../Minecraft/Quilt.h"
#include <algorithm>
#include <chrono>
#include <commdlg.h>
#include <fstream>
#include <shellapi.h>
#include <thread>
#include "../Minecraft/Java.h"
#include "../Minecraft/Launcher.h"
#include "../Minecraft/Versions.h"
#include "../Settings.h"
#include "../Paths.h"
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
        // Profile avatar lives in the MainWindow top-right header now. Nudge
        // it whenever the page refreshes (profile switch included).
        if (m_onProfileChanged)
        {
            try
            {
                m_onProfileChanged();
            }
            catch (...)
            {
            }
        }

        m_cards.Children().Clear();
        m_cardList.clear();
        auto instances = LoadInstances();
        for (auto const& inst : instances)
        {
            bool running = Launcher::IsRunning(inst.id);
            bool isModded = (inst.loader != L"vanilla");
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
            if (inst.loader == L"fabric")
                icon.Background(Theme::IconFabricBackground().as<Microsoft::UI::Xaml::Media::Brush>());
            else if (inst.loader == L"quilt")
                icon.Background(Theme::IconQuiltBackground().as<Microsoft::UI::Xaml::Media::Brush>());
            else if (inst.loader == L"forge")
                icon.Background(Theme::IconForgeBackground().as<Microsoft::UI::Xaml::Media::Brush>());
            else if (inst.loader == L"neoforge")
                icon.Background(Theme::IconNeoForgeBackground().as<Microsoft::UI::Xaml::Media::Brush>());
            else
                icon.Background(Theme::IconVanillaBackground().as<Microsoft::UI::Xaml::Media::Brush>());
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
            // Loader logo in the tile (per-loader mark shipped in Assets/;
            // initial letter stays as fallback when no art exists yet).
            bool logoOk = false;
            try
            {
                wchar_t exe[MAX_PATH]{};
                if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0)
                {
                    std::wstring artFile = isModded
                        ? std::wstring{ inst.loader } + L".png"
                        : std::wstring{ L"vanilla.png" };
                    auto art = std::filesystem::path{ exe }.parent_path() / L"Assets" / artFile;
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
                        // Vanilla ships our own grass-block cube art: give it
                        // more presence than the small per-loader marks.
                        double logoSize = isModded ? 30.0 : 40.0;
                        logo.Width(logoSize);
                        logo.Height(logoSize);
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
            if (isModded)
            {
                badge.Background(SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(38, 0x44, 0xBD, 0x32) });
                badgeText.Foreground(Theme::GoodBrush());
                std::wstring upper{ inst.loader };
                for (auto& c : upper)
                    c = static_cast<wchar_t>(towupper(c));
                hstring label{ upper };
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
            // Starts collapsed; it lives behind the per-card Log tab button and
            // is only shown while that tab is open (an empty-but-visible box
            // reads as broken).
            gamelog.Visibility(Visibility::Collapsed);
            gamelog.Header(box_value(L"Client log"));
            body.Children().Append(gamelog);

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
                RestartInstance(id);
            });
            buttons.Children().Append(play);
            buttons.Children().Append(stop);
            buttons.Children().Append(restart);

            Button logBtn{};
            bool logOpen = m_logOpen.find(std::wstring{ id }) != m_logOpen.end();
            logBtn.Content(box_value(logOpen ? L"Hide Log" : L"Log"));
            logBtn.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                try
                {
                    auto key = std::wstring{ id };
                    bool open = false;
                    if (auto it = m_logOpen.find(key); it == m_logOpen.end())
                    {
                        m_logOpen.insert(key);
                        open = true;
                    }
                    else
                    {
                        m_logOpen.erase(it);
                    }
                    auto* card = FindCard(id);
                    if (!card)
                        return;
                    if (card->logBtn)
                        card->logBtn.Content(box_value(open ? L"Hide Log" : L"Log"));
                    if (!card->gamelog)
                        return;
                    if (!open)
                    {
                        card->gamelog.Visibility(Visibility::Collapsed);
                        return;
                    }
                    // Opening: prefill from this instance's log files when the
                    // live tail hasn't painted anything yet (e.g. game stopped).
                    if (card->gamelog.Text().empty())
                    {
                        try
                        {
                            auto s = LoadSettings();
                            hstring tail = TailText(LogFileFor(s, id));
                            if (tail.empty())
                                tail = TailText(LogGameFor(s, id));
                            if (!tail.empty())
                                card->gamelog.Text(tail);
                            else
                                SetStatus(L"No client log yet - Play first.");
                        }
                        catch (...)
                        {
                        }
                    }
                    card->gamelog.Visibility(Visibility::Visible);
                }
                catch (...)
                {
                }
            });
            buttons.Children().Append(logBtn);

            if (isModded)
            {
                Button modsBtn{};
                modsBtn.Content(box_value(L"Mods"));
                modsBtn.Click([this, id](IInspectable const&, RoutedEventArgs const&) { ModsDialog(id); });
                buttons.Children().Append(modsBtn);
            }

            Button packsBtn{};
            packsBtn.Content(box_value(L"Resource Packs"));
            packsBtn.Click([this, id](IInspectable const&, RoutedEventArgs const&) { ResourcePacksDialog(id); });
            buttons.Children().Append(packsBtn);

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
                m_logOpen.erase(std::wstring{ id });
                auto all = LoadInstances();
                all.erase(std::remove_if(all.begin(), all.end(),
                    [&](Instance const& i) { return i.id == id; }), all.end());
                SaveInstances(all);
                // Remove the isolated game dir so deletes free saves/logs/mods.
                try
                {
                    auto s = LoadSettings();
                    std::error_code ec;
                    std::filesystem::remove_all(InstanceDir(s, id), ec);
                }
                catch (...)
                {
                }
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
            c.logBtn = logBtn;
            // Repaint the Log tab state onto the fresh card (starts collapsed
            // by default; prefill last text so a stopped game's log reopens).
            if (logOpen)
            {
                gamelog.Visibility(Visibility::Visible);
                try
                {
                    auto s = LoadSettings();
                    hstring tail = TailText(LogFileFor(s, id));
                    if (tail.empty())
                        tail = TailText(LogGameFor(s, id));
                    if (!tail.empty())
                        gamelog.Text(tail);
                }
                catch (...)
                {
                }
            }
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
        // an async update check may want to touch later. Name/version/icon
        // come from the jar's fabric.mod.json (or quilt.mod.json for
        // quilt-native mods) -- never the filename.
        struct ModMeta
        {
            hstring modId{};
            hstring name{};
            hstring version{};
            hstring icon{}; // path inside the jar, "" when none
            hstring metaLoader{}; // "fabric" | "quilt" | "" (unknown)
        };

        struct ModRow
        {
            std::filesystem::path file{};
            bool enabled = true;
            Microsoft::UI::Xaml::Controls::TextBlock name{};
            Microsoft::UI::Xaml::Controls::TextBlock version{};
            Microsoft::UI::Xaml::Controls::TextBlock status{};
            Microsoft::UI::Xaml::Controls::StackPanel actions{};
            Microsoft::UI::Xaml::Controls::Image icon{};
            ModMeta meta{};
        };

        using ModMetaCache = std::map<std::wstring, ModMeta>;

        ModMeta ReadModMeta(std::filesystem::path const& jar)
        {
            ModMeta m{};
            try
            {
                std::string text;
                if (Http::ZipEntryToString(jar, L"fabric.mod.json", text) && !text.empty())
                {
                    auto o = JsonObject::Parse(to_hstring(text));
                    try
                    {
                        if (o.HasKey(L"id"))
                            m.modId = o.GetNamedString(L"id");
                        if (o.HasKey(L"name"))
                            m.name = o.GetNamedString(L"name");
                        if (o.HasKey(L"version"))
                            m.version = o.GetNamedString(L"version");
                        if (o.HasKey(L"icon"))
                            m.icon = o.GetNamedString(L"icon");
                    }
                    catch (...)
                    {
                    }
                    if (!m.modId.empty())
                    {
                        m.metaLoader = L"fabric";
                        return m;
                    }
                }
                // Quilt-native mods carry quilt.mod.json instead.
                text.clear();
                if (Http::ZipEntryToString(jar, L"quilt.mod.json", text) && !text.empty())
                {
                    auto o = JsonObject::Parse(to_hstring(text));
                    try
                    {
                        if (o.HasKey(L"quilt_loader"))
                        {
                            auto q = o.GetNamedObject(L"quilt_loader");
                            if (q.HasKey(L"id"))
                                m.modId = q.GetNamedString(L"id");
                            if (q.HasKey(L"version"))
                                m.version = q.GetNamedString(L"version");
                            if (q.HasKey(L"metadata"))
                            {
                                auto md = q.GetNamedObject(L"metadata");
                                if (md.HasKey(L"name"))
                                    m.name = md.GetNamedString(L"name");
                                if (md.HasKey(L"icon"))
                                    m.icon = md.GetNamedString(L"icon");
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                    if (!m.modId.empty())
                        m.metaLoader = L"quilt";
                }
            }
            catch (...)
            {
            }
            return m;
        }

        std::filesystem::path ModIconCachePath(std::filesystem::path const& jar)
        {
            std::error_code ec;
            unsigned long long size = 0;
            size = static_cast<unsigned long long>(std::filesystem::file_size(jar, ec));
            wchar_t buf[32]{};
            swprintf_s(buf, L"_%llu", size);
            std::wstring stem = jar.stem().wstring();
            for (auto& c : stem)
            {
                if (!(iswalnum(c) || c == L'-' || c == L'_'))
                    c = L'_';
            }
            if (stem.empty())
                stem = L"mod";
            return Paths::DataDir() / L"modicons" / (stem + buf + L".png");
        }

        void PaintModIcon(Microsoft::UI::Xaml::Controls::Image const& img,
            std::filesystem::path const& png)
        {
            try
            {
                std::wstring uri{ L"file:///" };
                std::wstring fp{ png.wstring() };
                for (auto& c : fp)
                {
                    if (c == L'\\')
                        c = L'/';
                }
                uri += fp;
                img.Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{
                    Windows::Foundation::Uri{ uri } });
            }
            catch (...)
            {
            }
        }

        // Extract one jar's icon into the disk cache, then paint it. Runs
        // the tar extraction off the UI thread; safe to call for detached
        // (rebuilt-away) rows -- it just paints a dead element.
        fire_and_forget EnsureModIcon(std::filesystem::path jar, hstring iconRel,
            Microsoft::UI::Xaml::Controls::Image img,
            Microsoft::UI::Dispatching::DispatcherQueue queue)
        {
            try
            {
                auto cached = ModIconCachePath(jar);
                std::error_code ec;
                if (!std::filesystem::exists(cached, ec))
                {
                    co_await winrt::resume_background();
                    std::vector<std::uint8_t> bytes;
                    bool ok = false;
                    try
                    {
                        ok = Http::ZipEntryToBytes(jar, std::wstring{ iconRel }, bytes);
                    }
                    catch (...)
                    {
                    }
                    if (!ok || bytes.empty())
                        co_return;
                    try
                    {
                        std::filesystem::create_directories(cached.parent_path(), ec);
                        std::ofstream f(cached, std::ios::binary | std::ios::trunc);
                        if (!f.good())
                            co_return;
                        f.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
                        f.close();
                        if (!f)
                            co_return;
                    }
                    catch (...)
                    {
                        co_return;
                    }
                    co_await ForegroundAwait{ queue };
                }
                PaintModIcon(img, cached);
            }
            catch (...)
            {
            }
        }

        // Read one jar's mod metadata off the UI thread, cache it, then
        // fill the row's name/version and kick its icon load.
        fire_and_forget EnsureModMeta(std::filesystem::path jar,
            std::shared_ptr<ModMetaCache> cache,
            std::shared_ptr<std::vector<ModRow>> rows,
            Microsoft::UI::Dispatching::DispatcherQueue queue)
        {
            ModMeta m{};
            try
            {
                co_await winrt::resume_background();
                m = ReadModMeta(jar);
            }
            catch (...)
            {
            }
            try
            {
                co_await ForegroundAwait{ queue };
                (*cache)[jar.wstring()] = m; // negative entries too: don't re-read
                for (auto& r : *rows)
                {
                    if (r.file != jar)
                        continue;
                    r.meta = m;
                    if (!m.name.empty())
                    {
                        r.name.Text(m.name);
                        r.version.Text(m.version);
                        hstring tip = hstring{ m.name };
                        if (!m.version.empty())
                            tip = tip + L" " + m.version;
                        tip = tip + L"\n" + hstring{ jar.filename().wstring() };
                        if (!m.modId.empty())
                            tip = tip + L"\nModrinth id: " + m.modId;
                        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                            r.name, box_value(tip));
                    }
                    if (!m.icon.empty())
                        EnsureModIcon(jar, m.icon, r.icon, queue);
                    break;
                }
            }
            catch (...)
            {
            }
        }

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

        // Ask Modrinth for the newest build of this mod on (mc, loader) and,
        // when it is newer than the jar's own embedded version, add an
        // Update button to the row. The loader comes from whichever metadata
        // the jar actually carries (fabric.mod.json / quilt.mod.json).
        fire_and_forget CheckModUpdate(ModRow row, hstring mc, std::function<void()> refresh)
        {
            try
            {
                hstring modId = row.meta.modId;
                hstring curVer = row.meta.version;
                if (modId.empty() || curVer.empty())
                {
                    if (row.status)
                        row.status.Text(L"cannot check this mod");
                    co_return;
                }
                if (row.status)
                    row.status.Text(hstring{ L"checking " } + modId + L"...");

                VersionsWaiter wait{};
                hstring queryLoader = row.meta.metaLoader.empty() ? hstring{ L"fabric" } : row.meta.metaLoader;
                Modrinth::GetVersionsAsync(modId, mc, queryLoader,
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

    std::filesystem::path InstancesPage::LogFileFor(Settings const& s, hstring const& id)
    {
        return InstanceGameDir(s, id) / L"logs-pretclient" / L"latest.txt";
    }

    std::filesystem::path InstancesPage::LogGameFor(Settings const& s, hstring const& id)
    {
        return InstanceGameDir(s, id) / L"logs" / L"latest.log";
    }

    namespace
    {
        std::wstring LowerW(std::wstring s)
        {
            for (auto& c : s)
                c = static_cast<wchar_t>(towlower(c));
            return s;
        }

        bool EndsWith(std::wstring const& s, std::wstring const& suffix)
        {
            if (suffix.size() > s.size())
                return false;
            return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    } // namespace

    // NOTE: legacy StageMods/StageResourcePacks removed. Mods/packs live in
    // <instance>/game/mods|resourcepacks and are read in place; no
    // shared-folder staging, so instances can run side by side.

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
        ScrollViewer scroll{};
        scroll.MaxHeight(430);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.Content(list);
        panel.Children().Append(scroll);

        TextBlock status{};
        status.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        status.Opacity(0.7);
        status.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(status);

        auto rows = std::make_shared<std::vector<ModRow>>();
        auto metaCache = std::make_shared<ModMetaCache>();
        auto queue = m_dispatcher;
        auto rebuild = std::make_shared<std::function<void()>>();
        *rebuild = [rows, list, dirText, modsDir, status, metaCache, queue,
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

                    ModMeta meta{};
                    if (auto cit = metaCache->find(p.wstring()); cit != metaCache->end())
                        meta = cit->second;

                    Grid row{};
                    row.ColumnSpacing(8);
                    auto colIcon = ColumnDefinition{};
                    colIcon.Width(GridLengthHelper::Auto());
                    auto colName = ColumnDefinition{};
                    colName.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
                    auto colStatus = ColumnDefinition{};
                    colStatus.Width(GridLengthHelper::FromValueAndType(130, GridUnitType::Pixel));
                    auto colActions = ColumnDefinition{};
                    colActions.Width(GridLengthHelper::Auto());
                    row.ColumnDefinitions().Append(colIcon);
                    row.ColumnDefinitions().Append(colName);
                    row.ColumnDefinitions().Append(colStatus);
                    row.ColumnDefinitions().Append(colActions);

                    // Icon cell: initial letter underneath, jar icon painted
                    // on top once extracted (a failed load just shows the
                    // letter, so rows never look broken).
                    Grid cell{};
                    cell.Width(36);
                    cell.Height(36);
                    cell.VerticalAlignment(VerticalAlignment::Center);
                    hstring baseName = !meta.name.empty() ? meta.name :
                        hstring{ p.stem().wstring() };
                    wchar_t initialCh = L'?';
                    if (!baseName.empty())
                    {
                        try
                        {
                            initialCh = static_cast<wchar_t>(
                                towupper(std::wstring{ baseName }[0]));
                        }
                        catch (...)
                        {
                        }
                    }
                    TextBlock letter{};
                    letter.Text(hstring{ std::wstring(1, initialCh) });
                    letter.HorizontalAlignment(HorizontalAlignment::Center);
                    letter.VerticalAlignment(VerticalAlignment::Center);
                    letter.FontSize(16);
                    letter.FontWeight(Windows::UI::Text::FontWeights::Bold());
                    letter.Opacity(0.6);
                    cell.Children().Append(letter);
                    Image icon{};
                    icon.Width(32);
                    icon.Height(32);
                    icon.HorizontalAlignment(HorizontalAlignment::Center);
                    icon.VerticalAlignment(VerticalAlignment::Center);
                    icon.Stretch(Stretch::Uniform);
                    cell.Children().Append(icon);
                    Grid::SetColumn(cell, 0);
                    row.Children().Append(cell);

                    StackPanel nameStack{};
                    nameStack.Orientation(Orientation::Horizontal);
                    nameStack.Spacing(6);
                    nameStack.VerticalAlignment(VerticalAlignment::Center);
                    TextBlock t{};
                    t.Text(!meta.name.empty() ? meta.name : hstring{ p.filename().wstring() });
                    t.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                    t.VerticalAlignment(VerticalAlignment::Center);
                    t.TextTrimming(TextTrimming::CharacterEllipsis);
                    nameStack.Children().Append(t);
                    TextBlock v{};
                    v.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    v.Opacity(0.6);
                    v.VerticalAlignment(VerticalAlignment::Center);
                    v.Text(meta.version);
                    nameStack.Children().Append(v);
                    hstring tip = hstring{ p.filename().wstring() };
                    if (!meta.modId.empty())
                        tip = hstring{ L"Modrinth id: " } + meta.modId + L"\n" + tip;
                    ToolTipService::SetToolTip(nameStack, box_value(tip));
                    Grid::SetColumn(nameStack, 1);
                    row.Children().Append(nameStack);

                    TextBlock st{};
                    st.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    st.Opacity(0.7);
                    st.Width(130);
                    st.VerticalAlignment(VerticalAlignment::Center);
                    st.TextTrimming(TextTrimming::CharacterEllipsis);
                    st.Text(enabled ? L"" : L"disabled");
                    Grid::SetColumn(st, 2);
                    row.Children().Append(st);

                    StackPanel actions{};
                    actions.Orientation(Orientation::Horizontal);
                    actions.Spacing(6);
                    actions.VerticalAlignment(VerticalAlignment::Center);
                    Grid::SetColumn(actions, 3);
                    row.Children().Append(actions);

                    ModRow state{};
                    state.file = p;
                    state.enabled = enabled;
                    state.name = t;
                    state.version = v;
                    state.status = st;
                    state.actions = actions;
                    state.icon = icon;
                    state.meta = meta;

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
                    if (meta.modId.empty() && meta.name.empty())
                        EnsureModMeta(p, metaCache, rows, queue);
                    else if (!meta.icon.empty())
                        EnsureModIcon(p, meta.icon, icon, queue);
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
        dialog.MinWidth(700);
        dialog.XamlRoot(m_root.XamlRoot());
        co_await dialog.ShowAsync();
    }

    fire_and_forget InstancesPage::ResourcePacksDialog(hstring id)
    {
        auto settings = LoadSettings();
        auto packsDir = InstanceResourcePacksDir(settings, id);
        std::error_code ec;
        std::filesystem::create_directories(packsDir, ec);

        hstring instName;
        for (auto const& i : LoadInstances())
        {
            if (i.id == id)
            {
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

        TextBlock hint{};
        hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        hint.Opacity(0.6);
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Text(L"Packs are stored per instance and staged into the shared resourcepacks "
            L"folder on Play. Turn them on in-game via Options > Resource Packs.");
        panel.Children().Append(hint);

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
        ScrollViewer scroll{};
        scroll.MaxHeight(380);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.Content(list);
        panel.Children().Append(scroll);

        TextBlock status{};
        status.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        status.Opacity(0.7);
        status.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(status);

        auto rebuild = std::make_shared<std::function<void()>>();
        *rebuild = [list, dirText, packsDir, status,
            weak = std::weak_ptr<std::function<void()>>(rebuild)]() {
            try
            {
                list.Children().Clear();
                std::error_code e;
                std::vector<std::filesystem::path> entries;
                if (std::filesystem::exists(packsDir, e))
                {
                    for (auto const& en : std::filesystem::directory_iterator(packsDir, e))
                    {
                        auto lname = LowerW(en.path().filename().wstring());
                        if (en.is_regular_file(e))
                        {
                            if (lname.size() >= 4 &&
                                (EndsWith(lname, L".zip") || EndsWith(lname, L".zip.disabled")))
                                entries.push_back(en.path());
                        }
                        else if (en.is_directory(e))
                            entries.push_back(en.path());
                    }
                }
                std::sort(entries.begin(), entries.end());

                int count = 0;
                for (auto const& p : entries)
                {
                    std::error_code e2;
                    bool isDir = std::filesystem::is_directory(p, e2);
                    auto lname = LowerW(p.filename().wstring());
                    bool enabled = !EndsWith(lname, L".disabled");

                    Grid row{};
                    row.ColumnSpacing(8);
                    auto colName = ColumnDefinition{};
                    colName.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
                    auto colInfo = ColumnDefinition{};
                    colInfo.Width(GridLengthHelper::FromValueAndType(130, GridUnitType::Pixel));
                    auto colActions = ColumnDefinition{};
                    colActions.Width(GridLengthHelper::Auto());
                    row.ColumnDefinitions().Append(colName);
                    row.ColumnDefinitions().Append(colInfo);
                    row.ColumnDefinitions().Append(colActions);

                    TextBlock t{};
                    t.Text(hstring{ p.filename().wstring() });
                    t.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                    t.VerticalAlignment(VerticalAlignment::Center);
                    t.TextTrimming(TextTrimming::CharacterEllipsis);
                    ToolTipService::SetToolTip(t, box_value(hstring{ p.wstring() }));
                    Grid::SetColumn(t, 0);
                    row.Children().Append(t);

                    TextBlock info{};
                    info.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                    info.Opacity(0.7);
                    info.VerticalAlignment(VerticalAlignment::Center);
                    info.TextTrimming(TextTrimming::CharacterEllipsis);
                    if (isDir)
                        info.Text(enabled ? L"folder" : L"folder (off)");
                    else
                    {
                        auto bytes = std::filesystem::file_size(p, e2);
                        hstring size = e2 ? hstring{ L"?" } : FormatBytes(static_cast<unsigned long long>(bytes));
                        info.Text(enabled ? size : hstring{ size + L" (off)" });
                    }
                    Grid::SetColumn(info, 1);
                    row.Children().Append(info);

                    StackPanel actions{};
                    actions.Orientation(Orientation::Horizontal);
                    actions.Spacing(6);
                    actions.VerticalAlignment(VerticalAlignment::Center);
                    Grid::SetColumn(actions, 2);
                    row.Children().Append(actions);

                    Button toggle{};
                    toggle.Content(box_value(enabled ? L"Disable" : L"Enable"));
                    toggle.Click([weak, p, enabled, status](IInspectable const&, RoutedEventArgs const&) {
                        std::error_code e3;
                        if (enabled)
                        {
                            std::filesystem::rename(p,
                                std::filesystem::path{ p.wstring() + L".disabled" }, e3);
                        }
                        else
                        {
                            std::wstring w{ p.wstring() };
                            if (w.size() > 9 && EndsWith(LowerW(w), L".disabled"))
                                w = w.substr(0, w.size() - 9);
                            std::filesystem::rename(p, std::filesystem::path{ w }, e3);
                        }
                        if (e3 && status)
                            status.Text(L"Could not toggle that pack.");
                        if (auto r = weak.lock())
                            if (*r)
                                (*r)();
                    });
                    actions.Children().Append(toggle);

                    Button rm{};
                    rm.Content(box_value(L"Remove"));
                    rm.Click([weak, p, status](IInspectable const&, RoutedEventArgs const&) {
                        std::error_code e3;
                        bool isDir2 = std::filesystem::is_directory(p, e3);
                        if (isDir2)
                            std::filesystem::remove_all(p, e3);
                        else
                            std::filesystem::remove(p, e3);
                        if (status)
                        {
                            if (e3)
                                status.Text(L"Could not remove that pack.");
                            else
                                status.Text(hstring{ L"Removed " } + hstring{ p.filename().wstring() });
                        }
                        if (auto r = weak.lock())
                            if (*r)
                                (*r)();
                    });
                    actions.Children().Append(rm);

                    list.Children().Append(row);
                    ++count;
                }

                wchar_t dbuf[512]{};
                swprintf_s(dbuf, L"Folder: %s  (%d pack(s))",
                    std::wstring{ packsDir.wstring() }.c_str(), count);
                dirText.Text(dbuf);
                if (count == 0)
                {
                    TextBlock empty{};
                    empty.Text(L"No resource packs yet. Click Add .zip... or drop files into the folder.");
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

        Button addBtn{};
        addBtn.Content(box_value(L"Add .zip..."));
        addBtn.Click([packsDir, status, rebuild](IInspectable const&, RoutedEventArgs const&) {
            wchar_t fileBuf[32768]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = ARRAYSIZE(fileBuf);
            ofn.lpstrFilter = L"Resource packs (*.zip)\0*.zip\0All files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            ofn.lpstrTitle = L"Add resource pack (.zip)";
            if (!GetOpenFileNameW(&ofn))
                return; // cancelled
            try
            {
                std::filesystem::path src{ fileBuf };
                std::error_code ec2;
                std::filesystem::create_directories(packsDir, ec2);
                std::filesystem::copy_file(src, packsDir / src.filename(),
                    std::filesystem::copy_options::overwrite_existing, ec2);
                if (ec2)
                {
                    if (status)
                        status.Text(L"Could not add that file.");
                    return;
                }
                if (status)
                    status.Text(hstring{ L"Added " } + hstring{ src.filename().wstring() });
                if (rebuild && *rebuild)
                    (*rebuild)();
            }
            catch (...)
            {
                if (status)
                    status.Text(L"Could not add that file.");
            }
        });
        tools.Children().Append(addBtn);

        Button openBtn{};
        openBtn.Content(box_value(L"Open folder"));
        openBtn.Click([packsDir](IInspectable const&, RoutedEventArgs const&) {
            std::error_code e;
            std::filesystem::create_directories(packsDir, e);
            ShellExecuteW(nullptr, L"open", packsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
        tools.Children().Append(openBtn);

        ContentDialog dialog{};
        dialog.Title(box_value(hstring{ L"Resource Packs - " } + instName));
        dialog.Content(panel);
        dialog.CloseButtonText(L"Close");
        dialog.MinWidth(620);
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
            // Log paths are per-instance now, so snapshot those too.
            std::vector<hstring> ids;
            std::vector<bool> preparingSnap;
            std::vector<std::filesystem::path> logFiles;
            std::vector<std::filesystem::path> logGames;
            try
            {
                auto s = LoadSettings();
                for (auto const& card : m_cardList)
                {
                    ids.push_back(card.id);
                    preparingSnap.push_back(
                        m_preparing.find(std::wstring{ card.id }) != m_preparing.end());
                    logFiles.push_back(LogFileFor(s, card.id));
                    logGames.push_back(LogGameFor(s, card.id));
                }
            }
            catch (...)
            {
                for (auto const& card : m_cardList)
                {
                    ids.push_back(card.id);
                    preparingSnap.push_back(
                        m_preparing.find(std::wstring{ card.id }) != m_preparing.end());
                }
            }

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
            for (size_t i = 0; i < ids.size(); ++i)
            {
                auto const& id = ids[i];
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
                std::filesystem::path lf = i < logFiles.size() ? logFiles[i] : std::filesystem::path{};
                std::filesystem::path lg = i < logGames.size() ? logGames[i] : std::filesystem::path{};
                if (!lf.empty())
                    r.tail = TailText(lf);
                if (r.tail.empty() && !lg.empty())
                    r.tail = TailText(lg); // log4j file log: written even when the stdout redirect yields nothing
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
                    // The log lives on the Log tab: keep the last text when idle
                    // (crash logs stay readable) and follow the tab toggle.
                    if (card->gamelog)
                    {
                        bool open = m_logOpen.find(std::wstring{ r.id }) != m_logOpen.end();
                        card->gamelog.Visibility(open ? Visibility::Visible : Visibility::Collapsed);
                    }
                    continue;
                }
                paintState(true, false);
                wchar_t buf[192]{};
                swprintf_s(buf, L"pid %lu | CPU %s | RAM %s | GPU %s",
                    r.pid,
                    Pct(r.cpu).c_str(), FormatBytes(r.ram).c_str(), Pct(sys.gpuPercent).c_str());
                card->stats.Text(buf);
                if (!r.tail.empty() && card->gamelog)
                {
                    card->gamelog.Text(r.tail);
                    // Paint the text but only show it on the Log tab.
                    bool open = m_logOpen.find(std::wstring{ r.id }) != m_logOpen.end();
                    card->gamelog.Visibility(open ? Visibility::Visible : Visibility::Collapsed);
                    // Auto-scroll to the newest lines: park the caret at the
                    // end so the TextBox's internal ScrollViewer brings the
                    // tail into view (otherwise it sits at the top).
                    if (open)
                    {
                        try
                        {
                            card->gamelog.Select(static_cast<int32_t>(r.tail.size()), 0);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget InstancesPage::RestartInstance(hstring id)
    {
        try
        {
            Launcher::Stop(id);
            SetStatus(L"Stopping...");
            Refresh();
            co_await winrt::resume_background();
            for (int i = 0; i < 40 && Launcher::IsRunning(id); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            co_await ForegroundAwait{ m_dispatcher };
            if (Launcher::IsRunning(id))
            {
                SetStatus(L"Still stopping - try again in a few seconds.");
                Refresh();
                co_return;
            }
            PlayInstance(id);
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
        // Split layout: shared cache + per-instance game dir. Migration of
        // legacy <instance>/mods|resourcepacks runs here so Mods/Packs dialogs
        // and the launch below all see the new paths.
        EnsureInstanceGameDir(settings, id);
        std::wstring cacheDir{ EffectiveGameDir(settings) };
        auto instanceGameDir = InstanceGameDir(settings, id);
        hstring username = settings.username.empty() ? hstring{ L"Steve" } : settings.username;
        hstring javaPath = settings.javaPath;
        int minMem = settings.minMemMb;
        int maxMem = settings.maxMemMb;
        bool fpsBoost = settings.fpsBoost;
        hstring extraJvmArgs = settings.extraJvmArgs;
        bool highPriority = settings.highPriority;

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

        if (inst.loader != L"vanilla" && inst.loaderVersion.empty())
        {
            hstring kind = inst.loader; // fabric | quilt | forge | neoforge
            SetStatus(hstring{ L"Resolving " } + kind + hstring{ L" loader..." });
            hstring loaderVer;
            try
            {
                if (kind == L"fabric")
                    loaderVer = co_await Fabric::GetLatestLoader(inst.mcVersion);
                else if (kind == L"quilt")
                    loaderVer = co_await Quilt::GetLatestLoader(inst.mcVersion);
                else if (kind == L"forge")
                    loaderVer = co_await Forge::GetLatestForge(inst.mcVersion);
                else if (kind == L"neoforge")
                    loaderVer = co_await NeoForge::GetLatestNeoForge(inst.mcVersion);
            }
            catch (...)
            {
            }
            if (!loaderVer.empty())
            {
                inst.loaderVersion = loaderVer;
            }
            // When offline the lookup above stays empty on purpose: the
            // downloader reuses the cached loader profile instead of failing
            // here, so a previous online run can still launch with no net.
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
            inst.mcVersion, inst.loader, inst.loaderVersion, cacheDir,
            instanceGameDir.wstring(),
            javaPath,
            logCb, progCb,
            [this, id, username, javaPath, minMem, maxMem, fail, instanceGameDir,
                fpsBoost, extraJvmArgs, highPriority](
                bool ok, Downloader::PreparedGame game, hstring error) {
                if (!ok)
                {
                    fail(hstring{ L"Prepare failed: " } + error);
                    return;
                }
                FinishLaunch(id, username, javaPath, minMem, maxMem,
                    std::move(game), instanceGameDir,
                    fpsBoost, extraJvmArgs, highPriority);
            });
    }

    fire_and_forget InstancesPage::FinishLaunch(hstring id, hstring username, hstring javaPath,
        int minMem, int maxMem, Downloader::PreparedGame game,
        std::filesystem::path instanceGameDir,
        bool fpsBoost, hstring extraJvmArgs, bool highPriority)
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
        // No staging: mods/packs/saves live in the per-instance game dir.
        // Make sure the work dir exists (the game is launched with it as cwd
        // and writes logs-pretclient/latest.txt there).
        try
        {
            std::error_code ec;
            std::filesystem::create_directories(instanceGameDir, ec);
            // The downloader already set gameDir, but re-assert in case an
            // offline/cached path was assembled from the shared cache.
            if (!instanceGameDir.empty())
                game.gameDir = hstring{ instanceGameDir.wstring() };
        }
        catch (...)
        {
        }
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
        if (javaExe.empty())
        {
            // Nothing usable installed: fetch a managed Temurin copy into
            // %APPDATA%\PretClient\java instead of making the user hunt for
            // a download. Progress paints onto the card via the dispatcher
            // (this coroutine is on a background thread here).
            auto dq = m_dispatcher;
            int need = game.javaMajor;
            if (dq)
            {
                dq.TryEnqueue([this, id, need]() {
                    try
                    {
                        SetStatus(hstring{ L"Downloading Java " } + to_hstring(need) +
                            L" (one-time, ~1 min)...");
                        m_downloads[std::wstring{ id }] = DownloadState{ L"Java", 0, 0, 0.0 };
                        if (auto* card = FindCard(id))
                        {
                            card->prog.Visibility(Visibility::Visible);
                            card->prog.IsIndeterminate(true);
                            card->progText.Visibility(Visibility::Visible);
                            card->progText.Text(L"Downloading Java...");
                        }
                    }
                    catch (...)
                    {
                    }
                });
            }
            auto uiLog = [this, dq](hstring const& line) {
                if (!dq)
                    return;
                dq.TryEnqueue([this, line]() {
                    try
                    {
                        SetStatus(line);
                    }
                    catch (...)
                    {
                    }
                });
            };
            auto uiProg = [this, dq, id](unsigned long long done, unsigned long long total, double bps) {
                if (!dq)
                    return;
                dq.TryEnqueue([this, id, done, total, bps]() {
                    try
                    {
                        m_downloads[std::wstring{ id }] = DownloadState{ L"Java", done, total, bps };
                        if (auto* card = FindCard(id))
                        {
                            card->prog.Visibility(Visibility::Visible);
                            card->progText.Visibility(Visibility::Visible);
                            PaintProgress(*card, L"Java", done, total, bps);
                        }
                    }
                    catch (...)
                    {
                    }
                });
            };
            try
            {
                javaExe = co_await Java::EnsureAsync(game.javaMajor, uiLog, uiProg);
            }
            catch (...)
            {
                javaExe = L"";
            }
        }

        co_await ForegroundAwait{ m_dispatcher };
        m_preparing.erase(std::wstring{ id });
        m_downloads.erase(std::wstring{ id });
        if (javaExe.empty())
        {
            wchar_t buf[400]{};
            if (pickedBest > 0)
                swprintf_s(buf, L"No Java %d+ found and auto-download failed (checked %d install(s), newest is Java %d). Check your connection or install a 64-bit Java %d+ manually and set java.exe in Settings.",
                    game.javaMajor, pickedChecked, pickedBest, game.javaMajor);
            else
                swprintf_s(buf, L"No Java %d+ found and auto-download failed (checked %d install(s)). Check your connection or install a 64-bit Java %d+ manually and set java.exe in Settings.",
                    game.javaMajor, pickedChecked, game.javaMajor);
            failed(buf);
            co_return;
        }
        // Offline session: classic non-premium login.
        auto cmd = Launcher::BuildCommand(game, username, Launcher::OfflineUuid(username),
            L"0", L"legacy", L"", minMem, maxMem, javaExe, fpsBoost, extraJvmArgs);
        hstring err;
        if (Launcher::Start(cmd, id, err, highPriority, fpsBoost))
            SetStatus(hstring{ L"Running (pid " } + to_hstring(static_cast<std::uint32_t>(Launcher::Pid(id))) +
                L"). Game log below and in instances/<id>/game/logs-pretclient/latest.txt");
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
        loaderBox.Items().Append(box_value(L"quilt"));
        loaderBox.Items().Append(box_value(L"forge"));
        loaderBox.Items().Append(box_value(L"neoforge"));
        loaderBox.SelectedIndex(0);

        ComboBox loaderVerBox{};
        loaderVerBox.Header(box_value(L"Loader build"));
        loaderVerBox.Items().Append(box_value(L"latest (auto)"));
        loaderVerBox.SelectedIndex(0);
        loaderVerBox.IsEnabled(false); // vanilla default: no build to pick

        // Loader builds are fetched per (loader, mcVersion) via the
        // Fabric/Quilt/Forge/NeoForge modules (GetLoaderVersions). A
        // generation counter drops stale callbacks when the user flips
        // loader/version quickly.
        auto buildGen = std::make_shared<int>(0);
        auto refreshLoaderBuilds = std::make_shared<std::function<void()>>();
        *refreshLoaderBuilds = [this, loaderBox, versionBox, loaderVerBox, buildGen]() {
            hstring loader = unbox_value_or<hstring>(loaderBox.SelectedItem(), L"vanilla");
            hstring mc = unbox_value_or<hstring>(versionBox.SelectedItem(), L"");
            if (loader == L"vanilla" || mc.empty())
            {
                loaderVerBox.Items().Clear();
                loaderVerBox.Items().Append(box_value(L"latest (auto)"));
                loaderVerBox.SelectedIndex(0);
                loaderVerBox.IsEnabled(false);
                return;
            }
            int gen = ++(*buildGen);
            loaderVerBox.Items().Clear();
            loaderVerBox.Items().Append(box_value(L"loading..."));
            loaderVerBox.SelectedIndex(0);
            loaderVerBox.IsEnabled(false);
            auto fill = [this, loaderVerBox, buildGen, gen](std::vector<hstring> builds) {
                try
                {
                    if (*buildGen != gen)
                        return; // stale request
                    loaderVerBox.Items().Clear();
                    loaderVerBox.Items().Append(box_value(L"latest (auto)"));
                    for (auto const& b : builds)
                        loaderVerBox.Items().Append(box_value(b));
                    if (builds.empty())
                        SetStatus(L"No loader builds found (latest will be used).");
                    loaderVerBox.SelectedIndex(0);
                    loaderVerBox.IsEnabled(true);
                }
                catch (...)
                {
                }
            };
            try
            {
                if (loader == L"fabric")
                    Fabric::GetLoaderVersions(mc, std::move(fill));
                else if (loader == L"quilt")
                    Quilt::GetLoaderVersions(mc, std::move(fill));
                else if (loader == L"forge")
                    Forge::GetLoaderVersions(mc, std::move(fill));
                else if (loader == L"neoforge")
                    NeoForge::GetLoaderVersions(mc, std::move(fill));
                else
                    fill({});
            }
            catch (...)
            {
                fill({});
            }
        };
        loaderBox.SelectionChanged(
            [refreshLoaderBuilds](IInspectable const&, SelectionChangedEventArgs const&) {
                if (*refreshLoaderBuilds)
                    (*refreshLoaderBuilds)();
            });
        // MC version changes also invalidate the build list; wired after the
        // manifest fills versionBox (see below) to avoid double-fetch here.
        auto hookVersionChanges = [versionBox, refreshLoaderBuilds]() {
            versionBox.SelectionChanged(
                [refreshLoaderBuilds](IInspectable const&, SelectionChangedEventArgs const&) {
                    if (*refreshLoaderBuilds)
                        (*refreshLoaderBuilds)();
                });
        };

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
            [this, typeBox, versionBox, loaderBox, loaderVerBox, nameBox, dialog,
                refreshLoaderBuilds, hookVersionChanges](
                Versions::Manifest m) mutable {
                auto fillVersions = [versionBox, typeBox, m, refreshLoaderBuilds]() mutable {
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
                hookVersionChanges();
                if (*refreshLoaderBuilds)
                    (*refreshLoaderBuilds)();
                SetStatus(L"");
                ShowCreateDialog(dialog, nameBox, versionBox, loaderBox, loaderVerBox);
            });
    }

    fire_and_forget InstancesPage::ShowCreateDialog(
        ContentDialog dialog, TextBox nameBox, ComboBox versionBox,
        ComboBox loaderBox, ComboBox loaderVerBox)
    {
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
            co_return;
        Instance in{};
        in.id = NewInstanceId();
        in.name = nameBox.Text().empty() ? hstring{ L"Instance" } : hstring{ nameBox.Text() };
        in.mcVersion = unbox_value_or<hstring>(versionBox.SelectedItem(), L"1.21.4");
        in.loader = unbox_value_or<hstring>(loaderBox.SelectedItem(), L"vanilla");
        hstring picked = unbox_value_or<hstring>(loaderVerBox.SelectedItem(), L"latest (auto)");
        if (in.loader == L"vanilla" || picked == L"latest (auto)" ||
            picked == L"loading..." || picked.empty())
            in.loaderVersion = L""; // empty = resolve latest at launch
        else
            in.loaderVersion = picked;
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
