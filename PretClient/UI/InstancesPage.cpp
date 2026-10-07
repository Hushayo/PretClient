#include "pch.h"
#include "InstancesPage.h"
#include "Theme.h"
#include "../Minecraft/Downloader.h"
#include "../Minecraft/Fabric.h"
#include <algorithm>
#include <fstream>
#include <shellapi.h>
#include "../Minecraft/Java.h"
#include "../Minecraft/Launcher.h"
#include "../Minecraft/Modrinth.h"
#include "../Minecraft/Versions.h"
#include "../Settings.h"
#include <fstream>

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
    } // namespace

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

        m_timer = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_timer.Interval(std::chrono::seconds{ 2 });
        m_timer.Tick([this](auto&&, auto&&) { UpdateStats(); });
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
        auto settings = LoadSettings();
        m_profile.Content(box_value(hstring{ L"Profile: " } + settings.username));

        m_cards.Children().Clear();
        m_cardList.clear();
        auto instances = LoadInstances();
        for (auto const& inst : instances)
        {
            bool running = Launcher::IsRunning(inst.id);
            bool isFabric = (inst.loader == L"fabric");

            Border card{};
            card.Background(Theme::CardBrush());
            card.BorderBrush(Theme::CardStroke());
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
            card.Padding(ThicknessHelper::FromUniformLength(16));

            Grid grid{};
            grid.ColumnSpacing(16);
            grid.ColumnDefinitions().Append(ColumnDefinition{});
            grid.ColumnDefinitions().Append(ColumnDefinition{});
            grid.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            grid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

            StackPanel left{};
            left.Spacing(6);

            TextBlock name{};
            name.Text(inst.name);
            name.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            left.Children().Append(name);

            StackPanel meta{};
            meta.Orientation(Orientation::Horizontal);
            meta.Spacing(8);

            Border badge{};
            badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
            badge.Padding(ThicknessHelper::FromLengths(8, 4, 8, 4));
            TextBlock badgeText{};
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
            meta.Children().Append(badge);

            TextBlock ver{};
            ver.Text(inst.mcVersion);
            ver.VerticalAlignment(VerticalAlignment::Center);
            ver.Opacity(0.8);
            meta.Children().Append(ver);
            left.Children().Append(meta);

            TextBlock stats{};
            stats.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            stats.Opacity(0.7);
            stats.Text(running ? L"* starting..." : L"Idle");
            left.Children().Append(stats);

            ProgressBar prog{};
            prog.Minimum(0);
            prog.Maximum(100);
            prog.Visibility(Visibility::Collapsed);
            left.Children().Append(prog);

            TextBlock progText{};
            progText.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            progText.Opacity(0.7);
            progText.Visibility(Visibility::Collapsed);
            progText.TextWrapping(TextWrapping::Wrap);
            left.Children().Append(progText);

            TextBox gamelog{};
            gamelog.IsReadOnly(true);
            gamelog.AcceptsReturn(true);
            gamelog.TextWrapping(TextWrapping::Wrap);
            gamelog.MinHeight(120);
            gamelog.MaxHeight(220);
            gamelog.FontFamily(FontFamily(L"Consolas"));
            gamelog.Visibility(running ? Visibility::Visible : Visibility::Collapsed);
            gamelog.Header(box_value(L"Client log"));
            left.Children().Append(gamelog);

            if (isFabric)
            {
                TextBlock modsHead{};
                modsHead.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                modsHead.Opacity(0.7);
                modsHead.Text(L"Mods in this instance:");
                left.Children().Append(modsHead);
            }
            StackPanel modsBox{};
            modsBox.Spacing(4);
            if (isFabric)
                left.Children().Append(modsBox);

            if (isFabric)
            {
                TextBlock note{};
                note.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                note.Opacity(0.6);
                note.Text(L"Fabric loader + Fabric API install automatically on Play.");
                left.Children().Append(note);
            }

            StackPanel buttons{};
            buttons.Orientation(Orientation::Horizontal);
            buttons.Spacing(8);
            buttons.VerticalAlignment(VerticalAlignment::Center);

            hstring id = inst.id;
            Button play{};
            play.Content(box_value(L"Play"));
            play.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
            play.IsEnabled(!running);
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
            restart.IsEnabled(running);
            restart.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                Launcher::Stop(id);
                PlayInstance(id);
            });
            buttons.Children().Append(play);
            buttons.Children().Append(stop);
            buttons.Children().Append(restart);

            if (isFabric)
            {
                Button api{};
                api.Content(box_value(L"Fabric API"));
                api.Click([this, id](IInspectable const&, RoutedEventArgs const&) { InstallFabricApi(id); });
                buttons.Children().Append(api);
            }

            Button del{};
            del.Content(box_value(L"Delete"));
            del.IsEnabled(!running);
            del.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                auto all = LoadInstances();
                all.erase(std::remove_if(all.begin(), all.end(),
                    [&](Instance const& i) { return i.id == id; }), all.end());
                SaveInstances(all);
                SetStatus(L"Instance deleted.");
                Refresh();
            });
            buttons.Children().Append(del);

            Grid::SetColumn(left, 0);
            Grid::SetColumn(buttons, 1);
            grid.Children().Append(left);
            grid.Children().Append(buttons);
            card.Child(grid);
            m_cards.Children().Append(card);

            Card c{};
            c.id = id;
            c.stats = stats;
            c.play = play;
            c.stop = stop;
            c.restart = restart;
            c.prog = prog;
            c.progText = progText;
            c.gamelog = gamelog;
            c.modsBox = modsBox;
            m_cardList.push_back(std::move(c));
            if (isFabric)
                RefreshMods(m_cardList.back(), inst);
        }
        UpdateStats();
    }

    void InstancesPage::RefreshMods(Card& card, Instance const& inst)
    {
        try
        {
            card.modsBox.Children().Clear();
            auto settings = LoadSettings();
            auto mods = std::filesystem::path{ std::wstring{ EffectiveGameDir(settings) } } / L"mods";
            std::error_code ec;
            if (!std::filesystem::exists(mods, ec))
            {
                TextBlock empty{};
                empty.Text(L"(no mods installed)");
                empty.Opacity(0.6);
                empty.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                card.modsBox.Children().Append(empty);
                return;
            }
            int count = 0;
            for (auto const& e : std::filesystem::directory_iterator(mods, ec))
            {
                if (!e.is_regular_file(ec))
                    continue;
                auto ext = e.path().extension().wstring();
                for (auto& c : ext)
                    c = static_cast<wchar_t>(towlower(c));
                if (ext != L".jar" && ext != L".disabled")
                    continue;
                hstring fname{ e.path().filename().wstring() };
                StackPanel row{};
                row.Orientation(Orientation::Horizontal);
                row.Spacing(8);
                TextBlock t{};
                t.Text(fname);
                t.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                t.VerticalAlignment(VerticalAlignment::Center);
                t.MaxWidth(420);
                t.TextTrimming(TextTrimming::CharacterEllipsis);
                row.Children().Append(t);
                Button rm{};
                rm.Content(box_value(L"Remove"));
                rm.Click([this, id = card.id, fname](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        auto s = LoadSettings();
                        auto p = std::filesystem::path{ std::wstring{ EffectiveGameDir(s) } } / L"mods" /
                            std::filesystem::path{ std::wstring{ fname } };
                        std::error_code ec2;
                        std::filesystem::remove(p, ec2);
                        SetStatus(hstring{ L"Removed " } + fname);
                    }
                    catch (...)
                    {
                    }
                    Refresh();
                });
                row.Children().Append(rm);
                card.modsBox.Children().Append(row);
                if (++count >= 50)
                    break;
            }
            if (count == 0)
            {
                TextBlock empty{};
                empty.Text(L"(no mods installed)");
                empty.Opacity(0.6);
                empty.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                card.modsBox.Children().Append(empty);
            }
            Button open{};
            open.Content(box_value(L"Open mods folder"));
            open.Click([](IInspectable const&, RoutedEventArgs const&) {
                try
                {
                    auto s = LoadSettings();
                    auto p = std::filesystem::path{ std::wstring{ EffectiveGameDir(s) } } / L"mods";
                    std::error_code ec3;
                    std::filesystem::create_directories(p, ec3);
                    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                catch (...)
                {
                }
            });
            card.modsBox.Children().Append(open);
        }
        catch (...)
        {
        }
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

    void InstancesPage::UpdateStats()
    {
        try
        {
            auto sys = m_sampler.PollSystem();
            for (auto& card : m_cardList)
            {
                if (!Launcher::IsRunning(card.id))
                {
                    card.stats.Text(L"Idle");
                    card.gamelog.Visibility(Visibility::Collapsed);
                    continue;
                }
                void* h = Launcher::RawHandle(card.id);
                double cpu = m_sampler.PollProcessCpu(h);
                auto ram = m_sampler.ProcessPrivateBytes(h);
                wchar_t buf[192]{};
                swprintf_s(buf, L"pid %lu | CPU %s | RAM %s | GPU %s",
                    Launcher::Pid(card.id),
                    Pct(cpu).c_str(), FormatBytes(ram).c_str(), Pct(sys.gpuPercent).c_str());
                card.stats.Text(buf);
                try
                {
                    auto s = LoadSettings();
                    auto log = std::filesystem::path{ std::wstring{ EffectiveGameDir(s) } } /
                        L"logs-pretclient" / L"latest.txt";
                    auto tail = TailText(log);
                    if (!tail.empty())
                    {
                        card.gamelog.Text(tail);
                        card.gamelog.Visibility(Visibility::Visible);
                    }
                }
                catch (...)
                {
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
        auto settings = LoadSettings();
        std::wstring gameDir{ EffectiveGameDir(settings) };
        hstring username = settings.username.empty() ? hstring{ L"Steve" } : settings.username;
        int minMem = settings.minMemMb;
        int maxMem = settings.maxMemMb;

        if (auto* card = FindCard(id))
        {
            card->prog.Visibility(Visibility::Visible);
            card->prog.IsIndeterminate(true);
            card->progText.Visibility(Visibility::Visible);
            card->progText.Text(L"Starting...");
        }

        auto fail = [this, id](hstring const& msg) {
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
            auto* card = FindCard(id);
            if (!card)
                return;
            card->prog.IsIndeterminate(total == 0);
            if (total > 0)
                card->prog.Value(100.0 * static_cast<double>(done) / static_cast<double>(total));
            wchar_t buf[256]{};
            if (total > 0)
                swprintf_s(buf, L"%s %s / %s (%s)", std::wstring{ file }.c_str(),
                    FormatBytes(done).c_str(), FormatBytes(total).c_str(), FormatSpeed(bps).c_str());
            else
                swprintf_s(buf, L"%s %s (%s)", std::wstring{ file }.c_str(),
                    FormatBytes(done).c_str(), FormatSpeed(bps).c_str());
            card->progText.Text(buf);
        };
        Downloader::PrepareAsync(
            inst.mcVersion, inst.loader, inst.loaderVersion, gameDir, logCb, progCb,
            [this, id, settings, username, minMem, maxMem, fail](
                bool ok, Downloader::PreparedGame game, hstring error) {
                if (!ok)
                {
                    fail(hstring{ L"Prepare failed: " } + error);
                    return;
                }
                hstring javaExe = settings.javaPath;
                if (!javaExe.empty())
                {
                    auto v = Java::Verify(javaExe);
                    if (v.major < game.javaMajor)
                    {
                        SetStatus(hstring{ L"Configured Java is too old (need " } +
                            to_hstring(game.javaMajor) + L"+). Auto-detecting...");
                        javaExe = L"";
                    }
                }
                if (javaExe.empty())
                {
                    SetStatus(hstring{ L"Locating Java " } + to_hstring(game.javaMajor) + L"+...");
                    javaExe = Java::Pick(game.javaMajor);
                }
                if (javaExe.empty())
                {
                    fail(hstring{ L"No Java " } + to_hstring(game.javaMajor) +
                        L"+ found. Install one or set a path in Settings.");
                    return;
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
            });
    }

    fire_and_forget InstancesPage::InstallFabricApi(hstring id)
    {
        SetStatus(L"Resolving Fabric API...");
        hstring mc;
        for (auto const& i : LoadInstances())
        {
            if (i.id == id)
            {
                mc = i.mcVersion;
                break;
            }
        }
        if (mc.empty())
        {
            SetStatus(L"Instance is gone.");
            co_return;
        }
        auto settings = LoadSettings();
        hstring msg;
        try
        {
            msg = co_await Modrinth::EnsureFabricApiAsync(
                std::filesystem::path{ std::wstring{ EffectiveGameDir(settings) } }.wstring(), mc);
        }
        catch (...)
        {
            msg = L"Fabric API install failed.";
        }
        SetStatus(msg);
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
