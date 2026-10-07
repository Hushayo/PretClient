#include "pch.h"
#include "InstancesPage.h"
#include "Theme.h"
#include "../Minecraft/Downloader.h"
#include "../Minecraft/Fabric.h"
#include "../Minecraft/Java.h"
#include "../Minecraft/Launcher.h"
#include "../Minecraft/Modrinth.h"
#include "../Minecraft/Versions.h"
#include "../Settings.h"

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

        hstring Pct(double v)
        {
            if (v < 0)
                return L"…";
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

        Button add{};
        add.Content(box_value(L"+ New instance"));
        add.HorizontalAlignment(HorizontalAlignment::Left);
        add.Click([this](IInspectable const&, RoutedEventArgs const&) { AddDialog(); });
        m_root.Children().Append(add);

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

    void InstancesPage::SetStatus(hstring const& line)
    {
        m_status.Text(line);
    }

    void InstancesPage::Refresh()
    {
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
            auto c0 = GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star);
            auto c1 = GridLengthHelper::Auto();
            grid.ColumnDefinitions().Append(ColumnDefinition{});
            grid.ColumnDefinitions().Append(ColumnDefinition{});
            grid.ColumnDefinitions().GetAt(0).Width(c0);
            grid.ColumnDefinitions().GetAt(1).Width(c1);

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
            stats.Text(running ? L"● starting…" : L"Idle");
            left.Children().Append(stats);

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
            play.Content(box_value(L"▶ Play"));
            play.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
            play.IsEnabled(!running);
            play.Click([this, id](IInspectable const&, RoutedEventArgs const&) { PlayInstance(id); });

            Button stop{};
            stop.Content(box_value(L"■ Stop"));
            stop.IsEnabled(running);
            stop.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                Launcher::Stop(id);
                SetStatus(L"Stopped.");
                Refresh();
            });

            Button restart{};
            restart.Content(box_value(L"↻ Restart"));
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
                if (!all.empty())
                {
                    SaveInstances(all);
                    SetStatus(L"Instance deleted.");
                    Refresh();
                }
                else
                {
                    SetStatus(L"Keep at least one instance.");
                }
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
            m_cardList.push_back(std::move(c));
        }
        UpdateStats();
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
                    continue;
                }
                void* h = Launcher::RawHandle(card.id);
                double cpu = m_sampler.PollProcessCpu(h);
                auto ram = m_sampler.ProcessPrivateBytes(h);
                wchar_t buf[160]{};
                swprintf_s(buf, L"● pid %lu · CPU %s · RAM %s · GPU %s",
                    Launcher::Pid(card.id),
                    Pct(cpu).c_str(), FormatBytes(ram).c_str(), Pct(sys.gpuPercent).c_str());
                card.stats.Text(buf);
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget InstancesPage::PlayInstance(hstring id)
    {
        SetStatus(L"Preparing…");
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

        auto fail = [this](hstring const& msg) {
            SetStatus(msg);
            Refresh();
        };

        if (inst.loader == L"fabric" && inst.loaderVersion.empty())
        {
            SetStatus(L"Resolving fabric loader…");
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

        SetStatus(hstring{ L"Preparing " } + inst.mcVersion + L"… (this downloads game files on first run)");
        auto logCb = [this](hstring const& line) { SetStatus(line); };
        Downloader::PrepareAsync(
            inst.mcVersion, inst.loader, inst.loaderVersion, gameDir, logCb,
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
                            to_hstring(game.javaMajor) + L"+). Auto-detecting…");
                        javaExe = L"";
                    }
                }
                if (javaExe.empty())
                {
                    SetStatus(hstring{ L"Locating Java " } + to_hstring(game.javaMajor) + L"+…");
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
                        L"). Game log: logs-pretclient/latest.txt");
                else
                    SetStatus(hstring{ L"Launch failed: " } + err);
                Refresh();
            });
    }

    fire_and_forget InstancesPage::InstallFabricApi(hstring id)
    {
        SetStatus(L"Resolving Fabric API…");
        try
        {
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
            auto msg = co_await Modrinth::EnsureFabricApiAsync(
                std::filesystem::path{ std::wstring{ EffectiveGameDir(settings) } }.wstring(), mc);
            SetStatus(msg);
        }
        catch (...)
        {
            SetStatus(L"Fabric API install failed.");
        }
    }

    fire_and_forget InstancesPage::AddDialog()
    {
        SetStatus(L"Loading version list…");

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

        // Fill versions first (callback), then show.
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
        Microsoft::UI::Xaml::Controls::ContentDialog dialog,
        Microsoft::UI::Xaml::Controls::TextBox nameBox,
        Microsoft::UI::Xaml::Controls::ComboBox versionBox,
        Microsoft::UI::Xaml::Controls::ComboBox loaderBox,
        Microsoft::UI::Xaml::Controls::TextBox loaderVerBox)
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
}
