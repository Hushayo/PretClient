#include "pch.h"
#include "LocalServerPage.h"
#include "Theme.h"
#include "../Minecraft/Http.h"
#include "../Minecraft/Java.h"
#include "../Minecraft/Modrinth.h"
#include "../Minecraft/Server.h"
#include "../Paths.h"
#include "../Settings.h"
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <shellapi.h>
#include <thread>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Shapes;
using namespace Windows::Foundation;


namespace winrt::PretClient
{
    namespace
    {
        constexpr double kGraphW = 320.0;
        constexpr double kGraphH = 72.0;
        constexpr size_t kSamples = 60; // 60s of 1s samples
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        // Minimal foreground awaitable (same shape as InstancesPage): hops
        // the coroutine back onto the UI thread after background steps.
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

        unsigned long long ToU64(FILETIME ft)
        {
            ULARGE_INTEGER u{};
            u.LowPart = ft.dwLowDateTime;
            u.HighPart = ft.dwHighDateTime;
            return u.QuadPart;
        }

        hstring GbText(unsigned long long bytes)
        {
            wchar_t buf[32]{};
            swprintf_s(buf, L"%.1f GB", bytes / 1073741824.0);
            return hstring{ buf };
        }

        hstring MbText(unsigned long long bytes)
        {
            wchar_t buf[32]{};
            swprintf_s(buf, L"%.1f MB", bytes / 1048576.0);
            return hstring{ buf };
        }

        hstring FmtSize(unsigned long long bytes)
        {
            wchar_t buf[32]{};
            if (bytes >= 1073741824ULL)
                swprintf_s(buf, L"%.1f GB", bytes / 1073741824.0);
            else if (bytes >= 1048576ULL)
                swprintf_s(buf, L"%.1f MB", bytes / 1048576.0);
            else if (bytes >= 1024ULL)
                swprintf_s(buf, L"%llu KB", bytes / 1024ULL);
            else
                swprintf_s(buf, L"%llu B", bytes);
            return hstring{ buf };
        }

        // Modrinth loader for the server's mod tab, "" when N/A.
        hstring ModLoaderFor(hstring const& software)
        {
            std::wstring s{ software };
            if (s == L"fabric" || s == L"quilt" || s == L"forge" || s == L"neoforge")
                return software;
            return hstring{};
        }

        bool PluginsFor(hstring const& software)
        {
            std::wstring s{ software };
            return s == L"paper" || s == L"folia" || s == L"purpur" || s == L"leaves" || s == L"spigot";
        }

        void WriteRunBat(std::filesystem::path const& dir, std::wstring const& jar)
        {
            try
            {
                std::string narrow(jar.begin(), jar.end());
                std::ofstream f(dir / L"run.bat", std::ios::binary | std::ios::trunc);
                f << "@echo off\r\n"
                     "REM PretClient server - set eula=true in eula.txt first, then run.\r\n"
                     "java -Xmx2048M -jar \"" +
                        narrow + "\" nogui\r\npause\r\n";
            }
            catch (...)
            {
            }
        }

        // Step log for the server install flow (flushed per line): if the app
        // dies mid-install, the tail of server-debug.log shows exactly where.
        void DbgLog(std::string const& line)
        {
            try
            {
                std::ofstream f(Paths::DataDir() / L"server-debug.log",
                    std::ios::binary | std::ios::app);
                SYSTEMTIME st{};
                GetLocalTime(&st);
                auto two = [](unsigned n) {
                    std::string s = std::to_string(n);
                    return (s.size() < 2 ? "0" : "") + s;
                };
                f << "[" + two(st.wHour) + ":" + two(st.wMinute) + ":" + two(st.wSecond) + "] " +
                        line + "\r\n";
                f.flush();
            }
            catch (...)
            {
            }
        }
    } // namespace

    LocalServerPage::LocalServerPage()
    {
        m_ui = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        m_inner.Spacing(12);
        m_inner.Padding(ThicknessHelper::FromUniformLength(24));

        TextBlock head{};
        head.Text(L"Local Server");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_inner.Children().Append(head);

        // Explicit blue (not AccentButtonStyle: the app tints the accent
        // ramp green, so accent would render green here).
        m_create.Content(box_value(L"Create a Server"));
        m_create.Background(Media::SolidColorBrush{
            Windows::UI::ColorHelper::FromArgb(0xFF, 0x00, 0x78, 0xD4) });
        m_create.Foreground(Media::SolidColorBrush{
            Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF) });
        m_create.Click([this](IInspectable const&, RoutedEventArgs const&) { OpenCreateDialog(); });
        m_inner.Children().Append(m_create);

        m_progress.Minimum(0);
        m_progress.Maximum(100);
        m_progress.Width(320);
        m_progress.HorizontalAlignment(HorizontalAlignment::Left);
        m_progress.Visibility(Visibility::Collapsed);
        m_inner.Children().Append(m_progress);

        m_status.Opacity(0.7);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_inner.Children().Append(m_status);

        m_servers.Spacing(8);
        m_inner.Children().Append(m_servers);

        m_detail.Spacing(12);
        m_detail.Visibility(Visibility::Collapsed);
        m_inner.Children().Append(m_detail);

        // Outer scroll so the Manage detail view (console + resources +
        // plugins/mods + files + backups) stays reachable in short windows.
        // The console keeps its own inner ScrollViewer (MaxHeight 260).
        m_root.Content(m_inner);
        m_root.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_root.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_root.HorizontalScrollMode(ScrollMode::Disabled);

        // Pump server output into the backlog + live detail view.
        Server::SetConsoleSink(
            [this](hstring id, std::string line, bool exited, int code) {
                try
                {
                    m_ui.TryEnqueue([this, id, line, exited, code] {
                        try
                        {
                            AppendConsole(id, line, exited, code);
                        }
                        catch (...)
                        {
                        }
                    });
                }
                catch (...)
                {
                }
            });

        RefreshServers();
    }

    void LocalServerPage::Refresh()
    {
        try
        {
            m_root.Opacity(1.0);
        }
        catch (...)
        {
        }
        RefreshServers();
    }

    void LocalServerPage::SetStatus(hstring const& line)
    {
        try
        {
            m_status.Text(line);
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::WorkDone()
    {
        m_working = false;
        try
        {
            m_progress.Visibility(Visibility::Collapsed);
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::RefreshServers()
    {
        try
        {
            m_servers.Children().Clear();
            if (m_detail.Visibility() == Visibility::Visible)
                return; // detail view owns the page right now
            auto all = Server::AllSoftware();
            for (auto const& s : Server::LoadServers())
            {
                hstring label = s.software;
                for (auto const& a : all)
                {
                    if (a.id == s.software)
                    {
                        label = a.label;
                        break;
                    }
                }
                auto dir = Server::ServersDir() / std::wstring{ s.id };
                bool running = Server::ConsoleRunning(s.id);
                hstring shown = s.name.empty() ? s.id : s.name;

                Border card{};
                card.Background(Theme::CardBrush());
                card.BorderBrush(Theme::CardStroke());
                card.BorderThickness(ThicknessHelper::FromUniformLength(1));
                card.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
                card.Padding(ThicknessHelper::FromUniformLength(16));

                StackPanel col{};
                col.Spacing(8);

                StackPanel head{};
                head.Orientation(Orientation::Horizontal);
                head.Spacing(12);
                head.VerticalAlignment(VerticalAlignment::Center);
                TextBlock name{};
                name.Text(shown + L"  -  " + label + L" " + s.mcVersion);
                name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                name.VerticalAlignment(VerticalAlignment::Center);
                head.Children().Append(name);
                TextBlock state{};
                state.Text(running ? L"Running" : L"Stopped");
                state.Opacity(0.7);
                state.VerticalAlignment(VerticalAlignment::Center);
                head.Children().Append(state);
                col.Children().Append(head);

                StackPanel btns{};
                btns.Orientation(Orientation::Horizontal);
                btns.Spacing(8);

                Button manage{};
                manage.Content(box_value(L"Manage"));
                manage.Click([this, id = s.id](IInspectable const&, RoutedEventArgs const&) {
                    OpenDetail(id);
                });
                btns.Children().Append(manage);

                Button start{};
                start.Content(box_value(L"Start"));
                start.IsEnabled(!running);
                start.Click([this, id = s.id](IInspectable const&, RoutedEventArgs const&) {
                    StartServer(id);
                });
                btns.Children().Append(start);

                Button stop{};
                stop.Content(box_value(L"Stop"));
                stop.IsEnabled(running);
                stop.Click([this, id = s.id](IInspectable const&, RoutedEventArgs const&) {
                    Server::StopConsole(id);
                    SetStatus(L"Stopping...");
                    RefreshServers();
                });
                btns.Children().Append(stop);

                Button folder{};
                folder.Content(box_value(L"Folder"));
                folder.Click([this, dir](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        ShellExecuteW(nullptr, L"open", dir.wstring().c_str(),
                            nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    catch (...)
                    {
                    }
                });
                btns.Children().Append(folder);
                Button del{};
                del.Content(box_value(L"Delete"));
                del.Click([this, id = s.id](IInspectable const&, RoutedEventArgs const&) {
                    DeleteServer(id);
                });
                btns.Children().Append(del);
                col.Children().Append(btns);

                card.Child(col);
                m_servers.Children().Append(card);
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget LocalServerPage::OpenCreateDialog()
    {
        try
        {
            StackPanel panel{};
            panel.Spacing(10);
            panel.MinWidth(340);

            TextBlock nameHead{};
            nameHead.Text(L"Server name");
            panel.Children().Append(nameHead);
            m_name = TextBox{};
            m_name.Text(L"My Server");
            m_name.MaxLength(40);
            panel.Children().Append(m_name);

            TextBlock swHead{};
            swHead.Text(L"Server jar");
            swHead.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            panel.Children().Append(swHead);

            auto all = Server::AllSoftware();
            m_software = ComboBox{};
            for (auto const& s : all)
                m_software.Items().Append(box_value(s.label));
            m_softwareDesc = TextBlock{};
            m_softwareDesc.Opacity(0.7);
            m_softwareDesc.TextWrapping(TextWrapping::Wrap);
            m_software.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
                try
                {
                    auto list = Server::AllSoftware();
                    int i = m_software.SelectedIndex();
                    if (i >= 0 && static_cast<size_t>(i) < list.size())
                    {
                        m_softwareDesc.Text(list[i].desc);
                        LoadVersions(list[i].id);
                    }
                }
                catch (...)
                {
                }
            });
            panel.Children().Append(m_software);
            panel.Children().Append(m_softwareDesc);
            // Must exist before the initial selection fires LoadVersions.
            m_versions = ComboBox{};
            m_software.SelectedIndex(0); // fires handler: desc + versions

            TextBlock verHead{};
            verHead.Text(L"Minecraft version");
            panel.Children().Append(verHead);
            panel.Children().Append(m_versions);

            TextBlock resHead{};
            resHead.Text(L"Resources (live)");
            resHead.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            panel.Children().Append(resHead);

            auto addGraph = [&](wchar_t const* name, TextBlock& value,
                Microsoft::UI::Xaml::Shapes::Polyline& line,
                Windows::UI::Color color) {
                StackPanel row{};
                row.Spacing(4);
                StackPanel top{};
                top.Orientation(Orientation::Horizontal);
                top.Spacing(8);
                TextBlock label{};
                label.Text(hstring{ name });
                label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                value = TextBlock{};
                value.Opacity(0.7);
                top.Children().Append(label);
                top.Children().Append(value);
                row.Children().Append(top);
                line = Microsoft::UI::Xaml::Shapes::Polyline{};
                line.Stroke(Media::SolidColorBrush{ color });
                line.StrokeThickness(2);
                Canvas canvas{};
                canvas.Width(kGraphW);
                canvas.Height(kGraphH);
                canvas.Children().Append(line);
                Border frame{};
                frame.Child(canvas);
                frame.Background(Theme::LogBackgroundBrush());
                frame.BorderBrush(Theme::CardStroke());
                frame.BorderThickness(ThicknessHelper::FromUniformLength(1));
                frame.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                frame.Padding(ThicknessHelper::FromUniformLength(4));
                row.Children().Append(frame);
                panel.Children().Append(row);
            };
            addGraph(L"CPU", m_cpuValue, m_cpuLine,
                Windows::UI::ColorHelper::FromArgb(0xFF, 0x44, 0xBD, 0x32));
            addGraph(L"Memory", m_memValue, m_memLine,
                Windows::UI::ColorHelper::FromArgb(0xFF, 0x4C, 0xC2, 0xFF));
            addGraph(L"Storage", m_diskValue, m_diskLine,
                Windows::UI::ColorHelper::FromArgb(0xFF, 0xE0, 0xA6, 0x3C));

            ContentDialog dialog{};
            dialog.Title(box_value(L"Create a Server"));
            dialog.Content(panel);
            dialog.PrimaryButtonText(L"Create");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(ContentDialogButton::Primary);
            dialog.XamlRoot(m_root.XamlRoot());
            dialog.Closed([this](ContentDialog const&, ContentDialogClosedEventArgs const&) {
                StopGraphs();
            });

            StartGraphs();
            auto result = co_await dialog.ShowAsync();
            StopGraphs();
            if (result != ContentDialogResult::Primary)
                co_return;

            hstring name = m_name.Text().empty() ? hstring{ L"Server" } : m_name.Text();
            int si = m_software.SelectedIndex();
            if (si < 0 || static_cast<size_t>(si) >= all.size())
                co_return;
            hstring mc = unbox_value_or<hstring>(m_versions.SelectedItem(), hstring{});
            if (mc.empty())
                mc = Server::RecentMcVersions().front();
            InstallServer(name, all[static_cast<size_t>(si)].id, mc);
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::LoadVersions(hstring softwareId)
    {
        try
        {
            m_versions.Items().Clear();
            m_versions.Items().Append(box_value(L"Loading versions..."));
            m_versions.SelectedIndex(0);
            Server::FetchVersionsAsync(softwareId, [this](std::vector<hstring> list) {
                // FetchVersionsAsync resumes on a background thread: marshal
                // to the UI thread before touching XAML (cross-thread touches
                // AV the process past every catch(...) — Event 1000 0xc0000005).
                try
                {
                    m_ui.TryEnqueue([this, list = std::move(list)]() mutable {
                        try
                        {
                            if (!m_versions)
                                return;
                            m_versions.Items().Clear();
                            if (list.empty())
                                list = Server::RecentMcVersions();
                            for (auto const& v : list)
                                m_versions.Items().Append(box_value(v));
                            m_versions.SelectedIndex(0);
                        }
                        catch (...)
                        {
                        }
                    });
                }
                catch (...)
                {
                }
            });
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::InstallServer(hstring name, hstring software, hstring mc)
    {
        if (m_working)
        {
            SetStatus(L"Already working on a server - wait for it to finish.");
            return;
        }
        m_working = true;
        try
        {
            m_progress.IsIndeterminate(true);
            m_progress.Value(0);
            m_progress.Visibility(Visibility::Visible);
        }
        catch (...)
        {
        }
        hstring label = software;
        for (auto const& a : Server::AllSoftware())
        {
            if (a.id == software)
            {
                label = a.label;
                break;
            }
        }
        SetStatus(L"Resolving " + label + L" " + mc + L"...");
        DbgLog("install start sw=" + to_string(software) + " mc=" + to_string(mc));
        Server::ResolveArtifactAsync(software, mc,
            [this, name, software, mc](Server::Artifact art, hstring err) -> fire_and_forget {
                try
                {
                    if (!err.empty())
                    {
                        SetStatus(err);
                        WorkDone();
                        co_return;
                    }
                    auto dir = Server::ServersDir() / std::wstring{ Server::NewServerId() };
                    auto dest = dir / std::wstring{ art.fileName };
                    DbgLog("resolved kind=" + std::to_string(static_cast<int>(art.kind)) +
                        " dest=" + to_string(dest.wstring()));
                    SetStatus(L"Downloading " + hstring{ art.fileName } + L"...");
                    m_progress.IsIndeterminate(true);
                    hstring dlErr = co_await Http::DownloadToFileAsync(art.url, dest, kUA,
                        [this](unsigned long long doneBytes, unsigned long long total, double) {
                            try
                            {
                                if (total == 0)
                                    return;
                                double pct = 100.0 * static_cast<double>(doneBytes) /
                                    static_cast<double>(total);
                                // Progress fires on a background thread: marshal to UI.
                                m_ui.TryEnqueue([this, doneBytes, total, pct] {
                                    try
                                    {
                                        m_progress.IsIndeterminate(false);
                                        m_progress.Value(pct);
                                        wchar_t buf[128]{};
                                        swprintf_s(buf, L"Downloading... %.0f%% (%s / %s)", pct,
                                            MbText(doneBytes).c_str(), MbText(total).c_str());
                                        m_status.Text(buf);
                                    }
                                    catch (...)
                                    {
                                    }
                                });
                            }
                            catch (...)
                            {
                            }
                        });
                    DbgLog("download done");
                    // Everything below touches XAML: hop onto the UI thread first
                    // (continuations resume on a background thread; cross-thread
                    // XAML touches AV the process past every catch(...)).
                    co_await ForegroundAwait{ m_ui };
                    DbgLog("resumed on ui");
                    if (!dlErr.empty())
                    {
                        DbgLog("download error: " + to_string(dlErr));
                        SetStatus(L"Download failed: " + dlErr);
                        WorkDone();
                        co_return;
                    }
                    // Size + hash checks against the published metadata.
                    if (art.size > 0)
                    {
                        std::error_code ec;
                        auto have = std::filesystem::file_size(dest, ec);
                        if (ec || have != art.size)
                        {
                            std::filesystem::remove(dest, ec);
                            SetStatus(L"Download failed: size mismatch, please retry.");
                            WorkDone();
                            co_return;
                        }
                    }
                    if (!art.sha256.empty() || !art.sha1.empty())
                        SetStatus(L"Checking hash...");
                    DbgLog("verifying size/hash");
                    if (!art.sha256.empty())
                    {
                        std::wstring hex;
                        if (!Http::Sha256OfFile(dest, hex) ||
                            _wcsicmp(hex.c_str(), std::wstring{ art.sha256 }.c_str()) != 0)
                        {
                            std::error_code ec;
                            std::filesystem::remove(dest, ec);
                            SetStatus(L"Download failed: hash mismatch, please retry.");
                            WorkDone();
                            co_return;
                        }
                    }
                    else if (!art.sha1.empty())
                    {
                        std::wstring hex;
                        if (!Http::Sha1OfFile(dest, hex) ||
                            _wcsicmp(hex.c_str(), std::wstring{ art.sha1 }.c_str()) != 0)
                        {
                            std::error_code ec;
                            std::filesystem::remove(dest, ec);
                            SetStatus(L"Download failed: hash mismatch, please retry.");
                            WorkDone();
                            co_return;
                        }
                    }

                    if (art.kind == Server::InstallKind::Installer ||
                        art.kind == Server::InstallKind::BuildTools)
                    {
                        bool buildTools = (art.kind == Server::InstallKind::BuildTools);
                        hstring java = Java::Pick(Server::RequiredJava(mc));
                        if (java.empty())
                        {
                            SetStatus(L"No Java found for MC " + mc +
                                L". Install a 64-bit Java or set java.exe in Settings.");
                            WorkDone();
                            co_return;
                        }
                        hstring args = buildTools ? (L"--rev " + mc) : hstring{ L"--installServer" };
                        SetStatus(buildTools ? L"Compiling Spigot via BuildTools (needs git in PATH, "
                                               L"can take a long while)..."
                                             : L"Running installer (can take a few minutes)...");
                        m_progress.IsIndeterminate(true);
                        int exit = co_await Server::RunJavaJarAsync(
                            java, hstring{ dest.wstring() }, args, hstring{ dir.wstring() });
                        co_await ForegroundAwait{ m_ui };
                        if (exit != 0)
                        {
                            wchar_t buf[128]{};
                            swprintf_s(buf, L"Setup step failed (exit %d). See the server folder.", exit);
                            SetStatus(buf);
                            WorkDone();
                            co_return;
                        }
                        if (buildTools)
                            WriteRunBat(dir, L"spigot-" + std::wstring{ mc } + L".jar");
                    }
                    else
                    {
                        WriteRunBat(dir, std::wstring{ art.fileName });
                    }
                    DbgLog("files written");

                    auto all = Server::LoadServers();
                    Server::ServerEntry entry{};
                    entry.id = hstring{ dir.filename().wstring() };
                    entry.name = name;
                    entry.software = software;
                    entry.mcVersion = mc;
                    all.push_back(std::move(entry));
                    Server::SaveServers(all);
                    DbgLog("record saved");
                    RefreshServers();
                    DbgLog("list refreshed");
                    SetStatus(L"Server created in " + hstring{ dir.wstring() } +
                        L". Set eula=true in eula.txt, then press Run.");
                    WorkDone();
                }
                catch (...)
                {
                    try
                    {
                        DbgLog("EXCEPTION in install flow");
                        SetStatus(L"Server setup failed.");
                        WorkDone();
                    }
                    catch (...)
                    {
                    }
                }
            });
    }

    void LocalServerPage::StartGraphs()
    {
        try
        {
            m_cpuHist.clear();
            m_memHist.clear();
            m_diskHist.clear();
            m_cpuSeeded = false;
            m_diskPath = Paths::DataDir();
            SampleGraphs();
            m_graphTimer = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_graphTimer.Interval(std::chrono::seconds{ 1 });
            m_graphTimer.Tick([this](auto&&, auto&&) {
                try
                {
                    SampleGraphs();
                }
                catch (...)
                {
                }
            });
            m_graphTimer.Start();
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::StopGraphs()
    {
        try
        {
            if (m_graphTimer)
                m_graphTimer.Stop();
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::SampleGraphs()
    {
        // CPU: GetSystemTimes delta. First tick only seeds the baseline.
        double cpu = -1.0;
        FILETIME idle{}, kernel{}, user{};
        if (GetSystemTimes(&idle, &kernel, &user))
        {
            auto i = ToU64(idle);
            auto k = ToU64(kernel);
            auto u = ToU64(user);
            if (m_cpuSeeded)
            {
                auto idleD = i - m_lastIdle;
                auto totalD = (k - m_lastKernel) + (u - m_lastUser);
                if (totalD > 0)
                    cpu = 100.0 * (1.0 - static_cast<double>(idleD) / static_cast<double>(totalD));
            }
            m_lastIdle = i;
            m_lastKernel = k;
            m_lastUser = u;
            m_cpuSeeded = true;
        }
        if (cpu < 0.0)
            cpu = m_cpuHist.empty() ? 0.0 : m_cpuHist.back();
        m_cpuHist.push_back(cpu);
        while (m_cpuHist.size() > kSamples)
            m_cpuHist.pop_front();

        // Memory: used % of physical RAM.
        double memPct = 0.0;
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0)
            memPct = 100.0 * (1.0 - static_cast<double>(mem.ullAvailPhys) / static_cast<double>(mem.ullTotalPhys));
        m_memHist.push_back(memPct);
        while (m_memHist.size() > kSamples)
            m_memHist.pop_front();

        // Storage: used % of the drive hosting the app data dir.
        double diskPct = 0.0;
        ULARGE_INTEGER freeAvail{}, total{}, freeTotal{};
        if (GetDiskFreeSpaceExW(m_diskPath.c_str(), &freeAvail, &total, &freeTotal) && total.QuadPart > 0)
            diskPct = 100.0 * (1.0 - static_cast<double>(freeAvail.QuadPart) / static_cast<double>(total.QuadPart));
        m_diskHist.push_back(diskPct);
        while (m_diskHist.size() > kSamples)
            m_diskHist.pop_front();

        wchar_t buf[128]{};
        swprintf_s(buf, L"%.0f%%", cpu);
        m_cpuValue.Text(hstring{ buf });
        if (mem.ullTotalPhys > 0)
        {
            swprintf_s(buf, L"%.0f%% (%s / %s)", memPct,
                GbText(mem.ullTotalPhys - mem.ullAvailPhys).c_str(), GbText(mem.ullTotalPhys).c_str());
            m_memValue.Text(hstring{ buf });
        }
        if (total.QuadPart > 0)
        {
            swprintf_s(buf, L"%.0f%% used (%s free)", diskPct, GbText(freeAvail.QuadPart).c_str());
            m_diskValue.Text(hstring{ buf });
        }

        PaintGraph(m_cpuLine, m_cpuHist);
        PaintGraph(m_memLine, m_memHist);
        PaintGraph(m_diskLine, m_diskHist);
    }

    void LocalServerPage::PaintGraph(Microsoft::UI::Xaml::Shapes::Polyline const& line,
        std::deque<double> const& hist)
    {
        try
        {
            line.Points().Clear();
            // X grows with history so the trace fills left-to-right over 60s.
            for (size_t i = 0; i < hist.size(); ++i)
            {
                double v = (std::min)(100.0, (std::max)(0.0, hist[i]));
                float x = static_cast<float>(kGraphW * i / (kSamples - 1));
                float y = static_cast<float>(kGraphH - v / 100.0 * kGraphH);
                line.Points().Append(Point{ x, y });
            }
        }
        catch (...)
        {
        }
    }

    namespace
    {
        Server::ServerEntry FindEntry(hstring const& id)
        {
            try
            {
                for (auto const& s : Server::LoadServers())
                {
                    if (s.id == id)
                        return s;
                }
            }
            catch (...)
            {
            }
            return Server::ServerEntry{};
        }

        hstring EntryLabel(Server::ServerEntry const& s)
        {
            try
            {
                for (auto const& a : Server::AllSoftware())
                {
                    if (a.id == s.software)
                        return a.label;
                }
            }
            catch (...)
            {
            }
            return s.software;
        }

        Border DetailCard()
        {
            Border card{};
            card.Background(Theme::CardBrush());
            card.BorderBrush(Theme::CardStroke());
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
            card.Padding(ThicknessHelper::FromUniformLength(16));
            return card;
        }

        TextBlock SectionHead(wchar_t const* text)
        {
            TextBlock head{};
            head.Text(hstring{ text });
            head.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            return head;
        }
    } // namespace

    fire_and_forget LocalServerPage::DeleteServer(hstring id)
    {
        try
        {
            auto entry = FindEntry(id);
            if (entry.id.empty())
                co_return;
            hstring shown = entry.name.empty() ? entry.id : entry.name;
            ContentDialog confirm{};
            confirm.Title(box_value(L"Delete server?"));
            confirm.Content(box_value(
                L"Delete \"" + shown + L"\" and all its files? This cannot be undone."));
            confirm.PrimaryButtonText(L"Delete");
            confirm.CloseButtonText(L"Cancel");
            confirm.DefaultButton(ContentDialogButton::Close);
            try
            {
                confirm.XamlRoot(m_root.XamlRoot());
            }
            catch (...)
            {
            }
            if (co_await confirm.ShowAsync() != ContentDialogResult::Primary)
                co_return;
            try
            {
                Server::StopConsole(id);
            }
            catch (...)
            {
            }
            auto dir = Server::ServersDir() / std::wstring{ id };
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            auto all = Server::LoadServers();
            all.erase(std::remove_if(all.begin(), all.end(),
                          [&](Server::ServerEntry const& e) { return e.id == id; }),
                all.end());
            Server::SaveServers(all);
            if (id == m_detailId)
                ShowList();
            else
                RefreshServers();
            SetStatus(ec ? L"Server deleted (some files could not be removed)."
                         : L"Server deleted.");
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::ShowList()
    {
        StopDetailSampler();
        m_detailId = hstring{};
        m_detailRel.clear();
        try
        {
            m_detail.Children().Clear();
            m_detail.Visibility(Visibility::Collapsed);
            m_create.Visibility(Visibility::Visible);
            m_progress.Visibility(m_working ? Visibility::Visible : Visibility::Collapsed);
            m_status.Visibility(Visibility::Visible);
            m_servers.Visibility(Visibility::Visible);
        }
        catch (...)
        {
        }
        RefreshServers();
    }

    void LocalServerPage::OpenDetail(hstring id)
    {
        if (FindEntry(id).id.empty())
            return;
        m_detailId = id;
        m_detailRel.clear();
        try
        {
            m_create.Visibility(Visibility::Collapsed);
            m_progress.Visibility(Visibility::Collapsed);
            m_status.Visibility(Visibility::Collapsed);
            m_servers.Visibility(Visibility::Collapsed);
            m_detail.Visibility(Visibility::Visible);
        }
        catch (...)
        {
        }
        BuildDetail();
    }

    void LocalServerPage::BuildDetail()
    {
        try
        {
            StopDetailSampler();
            m_detail.Children().Clear();
            auto entry = FindEntry(m_detailId);
            if (entry.id.empty())
            {
                ShowList();
                return;
            }
            hstring label = EntryLabel(entry);
            hstring shown = entry.name.empty() ? entry.id : entry.name;
            auto dir = Server::ServersDir() / std::wstring{ entry.id };

            // Header card.
            Border headCard = DetailCard();
            StackPanel headCol{};
            headCol.Spacing(8);
            StackPanel titleRow{};
            titleRow.Orientation(Orientation::Horizontal);
            titleRow.Spacing(8);
            Button back{};
            back.Content(box_value(L"< Back"));
            back.Click([this](IInspectable const&, RoutedEventArgs const&) { ShowList(); });
            titleRow.Children().Append(back);
            TextBlock title{};
            title.Text(shown + L"  -  " + label + L" " + entry.mcVersion);
            title.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
            title.VerticalAlignment(VerticalAlignment::Center);
            title.TextWrapping(TextWrapping::Wrap);
            titleRow.Children().Append(title);
            headCol.Children().Append(titleRow);

            m_dStatus = TextBlock{};
            m_dStatus.Opacity(0.7);
            m_dStatus.TextWrapping(TextWrapping::Wrap);
            headCol.Children().Append(m_dStatus);

            StackPanel btns{};
            btns.Orientation(Orientation::Horizontal);
            btns.Spacing(8);
            m_dStart = Button{};
            m_dStart.Content(box_value(L"Start"));
            m_dStart.Click([this](IInspectable const&, RoutedEventArgs const&) {
                StartServer(m_detailId);
            });
            btns.Children().Append(m_dStart);
            m_dStop = Button{};
            m_dStop.Content(box_value(L"Stop"));
            m_dStop.Click([this](IInspectable const&, RoutedEventArgs const&) {
                Server::StopConsole(m_detailId);
                m_dStatus.Text(L"Stopping...");
            });
            btns.Children().Append(m_dStop);
            Button folder{};
            folder.Content(box_value(L"Folder"));
            folder.Click([dir](IInspectable const&, RoutedEventArgs const&) {
                try
                {
                    ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                catch (...)
                {
                }
            });
            btns.Children().Append(folder);
            Button del{};
            del.Content(box_value(L"Delete"));
            del.Click([this](IInspectable const&, RoutedEventArgs const&) {
                DeleteServer(m_detailId);
            });
            btns.Children().Append(del);
            headCol.Children().Append(btns);
            headCard.Child(headCol);
            m_detail.Children().Append(headCard);

            // Console card.
            Border conCard = DetailCard();
            StackPanel conCol{};
            conCol.Spacing(8);
            conCol.Children().Append(SectionHead(L"Console"));
            m_dScroll = ScrollViewer{};
            m_dScroll.MaxHeight(260);
            m_dScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
            m_dScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
            m_dLog = TextBox{};
            m_dLog.IsReadOnly(true);
            m_dLog.AcceptsReturn(true);
            m_dLog.TextWrapping(TextWrapping::Wrap);
            m_dLog.FontFamily(Media::FontFamily{ L"Consolas" });
            m_dLog.Background(Theme::LogBackgroundBrush());
            m_dScroll.Content(m_dLog);
            conCol.Children().Append(m_dScroll);
            StackPanel inRow{};
            inRow.Orientation(Orientation::Horizontal);
            inRow.Spacing(8);
            m_dInput = TextBox{};
            m_dInput.PlaceholderText(L"Server command (e.g. list, op Steve, stop)...");
            m_dInput.Width(420);
            m_dInput.KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& e) {
                try
                {
                    if (e.Key() == Windows::System::VirtualKey::Enter)
                        SendDetailCommand();
                }
                catch (...)
                {
                }
            });
            inRow.Children().Append(m_dInput);
            Button send{};
            send.Content(box_value(L"Send"));
            send.Click([this](IInspectable const&, RoutedEventArgs const&) { SendDetailCommand(); });
            inRow.Children().Append(send);
            conCol.Children().Append(inRow);
            conCard.Child(conCol);
            m_detail.Children().Append(conCard);

            // Resources card.
            Border resCard = DetailCard();
            StackPanel resCol{};
            resCol.Spacing(8);
            resCol.Children().Append(SectionHead(L"Resources"));
            m_dProc = TextBlock{};
            m_dProc.Opacity(0.7);
            m_dProc.TextWrapping(TextWrapping::Wrap);
            resCol.Children().Append(m_dProc);
            auto addGraph = [&](wchar_t const* name, TextBlock& value, Microsoft::UI::Xaml::Shapes::Polyline& line,
                Windows::UI::Color color) {
                StackPanel row{};
                row.Spacing(4);
                StackPanel top{};
                top.Orientation(Orientation::Horizontal);
                top.Spacing(8);
                TextBlock lab{};
                lab.Text(hstring{ name });
                lab.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                value = TextBlock{};
                value.Opacity(0.7);
                top.Children().Append(lab);
                top.Children().Append(value);
                row.Children().Append(top);
                line = Microsoft::UI::Xaml::Shapes::Polyline{};
                line.Stroke(Media::SolidColorBrush{ color });
                line.StrokeThickness(2);
                Canvas canvas{};
                canvas.Width(kGraphW);
                canvas.Height(kGraphH);
                canvas.Children().Append(line);
                Border frame{};
                frame.Child(canvas);
                frame.Background(Theme::LogBackgroundBrush());
                frame.BorderBrush(Theme::CardStroke());
                frame.BorderThickness(ThicknessHelper::FromUniformLength(1));
                frame.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                frame.Padding(ThicknessHelper::FromUniformLength(4));
                row.Children().Append(frame);
                resCol.Children().Append(row);
            };
            m_dCpuValue = TextBlock{};
            m_dMemValue = TextBlock{};
            m_dDiskValue = TextBlock{};
            m_dCpuLine = Microsoft::UI::Xaml::Shapes::Polyline{};
            m_dMemLine = Microsoft::UI::Xaml::Shapes::Polyline{};
            m_dDiskLine = Microsoft::UI::Xaml::Shapes::Polyline{};
            addGraph(L"CPU", m_dCpuValue, m_dCpuLine,
                Windows::UI::ColorHelper::FromArgb(0xFF, 0x44, 0xBD, 0x32));
            addGraph(L"Memory", m_dMemValue, m_dMemLine,
                Windows::UI::ColorHelper::FromArgb(0xFF, 0x4C, 0xC2, 0xFF));
            addGraph(L"Storage", m_dDiskValue, m_dDiskLine,
                Windows::UI::ColorHelper::FromArgb(0xFF, 0xE0, 0xA6, 0x3C));
            resCard.Child(resCol);
            m_detail.Children().Append(resCard);

            // Content cards: plugins and/or mods depending on software.
            hstring modLoader = ModLoaderFor(entry.software);
            if (PluginsFor(entry.software))
            {
                Border plugCard = DetailCard();
                StackPanel plugCol{};
                plugCol.Spacing(8);
                plugCol.Children().Append(SectionHead(L"Plugins (Modrinth)"));
                StackPanel searchRow{};
                searchRow.Orientation(Orientation::Horizontal);
                searchRow.Spacing(8);
                m_dPluginQuery = TextBox{};
                m_dPluginQuery.PlaceholderText(L"Search plugins...");
                m_dPluginQuery.Width(320);
                searchRow.Children().Append(m_dPluginQuery);
                Button go{};
                go.Content(box_value(L"Search"));
                go.Click([this](IInspectable const&, RoutedEventArgs const&) { SearchServerContent(true); });
                searchRow.Children().Append(go);
                plugCol.Children().Append(searchRow);
                m_dPluginStatus = TextBlock{};
                m_dPluginStatus.Opacity(0.7);
                plugCol.Children().Append(m_dPluginStatus);
                m_dPluginResults = StackPanel{};
                m_dPluginResults.Spacing(6);
                plugCol.Children().Append(m_dPluginResults);
                plugCard.Child(plugCol);
                m_detail.Children().Append(plugCard);
            }
            if (!modLoader.empty())
            {
                Border modCard = DetailCard();
                StackPanel modCol{};
                modCol.Spacing(8);
                modCol.Children().Append(SectionHead(L"Mods (Modrinth)"));
                StackPanel searchRow{};
                searchRow.Orientation(Orientation::Horizontal);
                searchRow.Spacing(8);
                m_dModQuery = TextBox{};
                m_dModQuery.PlaceholderText(L"Search mods...");
                m_dModQuery.Width(320);
                searchRow.Children().Append(m_dModQuery);
                Button go{};
                go.Content(box_value(L"Search"));
                go.Click([this](IInspectable const&, RoutedEventArgs const&) { SearchServerContent(false); });
                searchRow.Children().Append(go);
                modCol.Children().Append(searchRow);
                m_dModStatus = TextBlock{};
                m_dModStatus.Opacity(0.7);
                modCol.Children().Append(m_dModStatus);
                m_dModResults = StackPanel{};
                m_dModResults.Spacing(6);
                modCol.Children().Append(m_dModResults);
                modCard.Child(modCol);
                m_detail.Children().Append(modCard);
            }
            if (!PluginsFor(entry.software) && modLoader.empty())
            {
                Border note = DetailCard();
                TextBlock t{};
                t.Opacity(0.7);
                t.TextWrapping(TextWrapping::Wrap);
                t.Text(L"No Modrinth content for this software. Drop jars into the folder manually.");
                note.Child(t);
                m_detail.Children().Append(note);
            }

            // Files card.
            Border fileCard = DetailCard();
            StackPanel fileCol{};
            fileCol.Spacing(8);
            fileCol.Children().Append(SectionHead(L"Files"));
            m_dFilePath = TextBlock{};
            m_dFilePath.Opacity(0.7);
            fileCol.Children().Append(m_dFilePath);
            m_dFileList = StackPanel{};
            m_dFileList.Spacing(4);
            fileCol.Children().Append(m_dFileList);
            fileCard.Child(fileCol);
            m_detail.Children().Append(fileCard);

            // Backups card.
            Border bakCard = DetailCard();
            StackPanel bakCol{};
            bakCol.Spacing(8);
            bakCol.Children().Append(SectionHead(L"Backups"));
            Button bakNow{};
            bakNow.Content(box_value(L"Back up now"));
            bakNow.Click([this](IInspectable const&, RoutedEventArgs const&) { BackupNow(); });
            bakCol.Children().Append(bakNow);
            m_dBackupStatus = TextBlock{};
            m_dBackupStatus.Opacity(0.7);
            m_dBackupStatus.TextWrapping(TextWrapping::Wrap);
            bakCol.Children().Append(m_dBackupStatus);
            m_dBackupList = StackPanel{};
            m_dBackupList.Spacing(4);
            bakCol.Children().Append(m_dBackupList);
            bakCard.Child(bakCol);
            m_detail.Children().Append(bakCard);

            auto bit = m_backlog.find(std::wstring{ m_detailId });
            if (bit != m_backlog.end())
                m_dLog.Text(hstring{ bit->second });
            UpdateDetailStatus();
            RefreshDetailFiles();
            RefreshDetailBackups();
            StartDetailSampler();
            try
            {
                m_ui.TryEnqueue([this] {
                    try
                {
                    if (m_dScroll)
                        m_dScroll.ScrollToVerticalOffset(m_dScroll.ScrollableHeight());
                }
                catch (...)
                {
                }
                });
            }
            catch (...)
            {
            }
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::UpdateDetailStatus()
    {
        try
        {
            if (m_detailId.empty() || !m_dStatus)
                return;
            bool running = Server::ConsoleRunning(m_detailId);
            m_dStatus.Text(running ? L"Status: running" : L"Status: stopped");
            if (m_dStart)
                m_dStart.IsEnabled(!running);
            if (m_dStop)
                m_dStop.IsEnabled(running);
        }
        catch (...)
        {
        }
    }

    fire_and_forget LocalServerPage::StartServer(hstring id)
    {
        if (id.empty() || Server::ConsoleRunning(id))
            co_return;
        auto entry = FindEntry(id);
        if (entry.id.empty())
            co_return;
        auto dir = Server::ServersDir() / std::wstring{ id };
        auto jar = Server::FindServerJar(dir);
        if (jar.empty())
        {
            SetStatus(L"No server jar found - open the Folder and check the install.");
            if (id == m_detailId && m_dStatus)
                m_dStatus.Text(L"No server jar found - open the Folder and check the install.");
            co_return;
        }
        SetStatus(L"Starting server...");
        if (id == m_detailId && m_dStatus)
            m_dStatus.Text(L"Starting server...");
        int mem = 2048;
        try
        {
            auto st = LoadSettings();
            if (st.maxMemMb >= 512)
                mem = st.maxMemMb;
        }
        catch (...)
        {
        }
        co_await winrt::resume_background();
        hstring java = Java::Pick(Server::RequiredJava(entry.mcVersion));
        hstring err;
        bool ok = false;
        if (!java.empty())
            ok = Server::StartConsole(id, dir, jar, java, mem, err);
        else
            err = L"No Java found. Install a 64-bit Java or set java.exe in Settings.";
        co_await ForegroundAwait{ m_ui };
        if (ok)
        {
            DbgLog("console started " + to_string(std::wstring{ id }));
            SetStatus(L"Server running.");
        }
        else
        {
            SetStatus(L"Could not start: " + err);
            if (id == m_detailId && m_dStatus)
                m_dStatus.Text(L"Could not start: " + err);
        }
        RefreshServers();
        UpdateDetailStatus();
    }

    void LocalServerPage::SendDetailCommand()
    {
        try
        {
            if (m_detailId.empty())
                return;
            if (!Server::ConsoleRunning(m_detailId))
            {
                if (m_dStatus)
                    m_dStatus.Text(L"Server is not running.");
                return;
            }
            hstring cmd = m_dInput.Text();
            std::wstring line{ cmd };
            while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r' ||
                                     line.back() == L' ' || line.back() == L'\t'))
                line.pop_back();
            if (line.empty())
                return;
            Server::SendConsole(m_detailId, line);
            auto& back = m_backlog[std::wstring{ m_detailId }];
            back += L"> " + line + L"\n";
            if (back.size() > 220000)
            {
                auto pos = back.find(L'\n', 20000);
                if (pos != std::wstring::npos)
                    back.erase(0, pos + 1);
            }
            m_dInput.Text(L"");
            if (m_dLog)
                m_dLog.Text(hstring{ back });
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::AppendConsole(hstring id, std::string const& line, bool exited, int exitCode)
    {
        try
        {
            std::wstring key{ id };
            auto& back = m_backlog[key];
            if (!line.empty())
                back += to_hstring(line).c_str();
            if (!line.empty())
                back += L"\n";
            if (exited)
            {
                back += L"--- process exited (";
                back += std::to_wstring(exitCode);
                back += L") ---\n";
            }
            if (back.size() > 220000)
            {
                auto pos = back.find(L'\n', 20000);
                if (pos != std::wstring::npos)
                    back.erase(0, pos + 1);
            }
            if (id == m_detailId && m_detail.Visibility() == Visibility::Visible && m_dLog)
            {
                m_dLog.Text(hstring{ back });
                try
                {
                    if (m_dScroll)
                        m_dScroll.ScrollToVerticalOffset(m_dScroll.ScrollableHeight());
                }
                catch (...)
                {
                }
            }
            if (exited)
            {
                UpdateDetailStatus();
                RefreshServers();
            }
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::SearchServerContent(bool plugins)
    {
        try
        {
            auto entry = FindEntry(m_detailId);
            if (entry.id.empty())
                return;
            TextBox& query = plugins ? m_dPluginQuery : m_dModQuery;
            StackPanel& results = plugins ? m_dPluginResults : m_dModResults;
            TextBlock& status = plugins ? m_dPluginStatus : m_dModStatus;
            if (!query || !results || !status)
                return; // section not built for this software
            hstring q = query.Text();
            hstring loader = plugins ? hstring{} : ModLoaderFor(entry.software);
            hstring mc = entry.mcVersion;
            hstring sw = entry.software;
            status.Text(L"Searching Modrinth...");
            results.Children().Clear();
            Modrinth::SearchAsync(q, mc, loader, 0,
                [this, plugins, mc, sw](Modrinth::SearchResult r) {
                    try
                    {
                        StackPanel& results2 = plugins ? m_dPluginResults : m_dModResults;
                        TextBlock& status2 = plugins ? m_dPluginStatus : m_dModStatus;
                        if (!results2 || !status2)
                            return;
                        results2.Children().Clear();
                        if (r.hits.empty())
                        {
                            status2.Text(L"No results.");
                            return;
                        }
                        status2.Text(to_hstring(static_cast<long long>(r.hits.size())) + L" result(s)");
                        for (auto const& hit : r.hits)
                        {
                            StackPanel row{};
                            row.Orientation(Orientation::Horizontal);
                            row.Spacing(8);
                            row.VerticalAlignment(VerticalAlignment::Center);
                            TextBlock t{};
                            t.Text(hit.title);
                            t.Width(300);
                            t.TextTrimming(TextTrimming::CharacterEllipsis);
                            t.VerticalAlignment(VerticalAlignment::Center);
                            row.Children().Append(t);
                            Button inst{};
                            inst.Content(box_value(L"Install"));
                            inst.Click([this, plugins, slug = hit.slug, title = hit.title](
                                           IInspectable const&, RoutedEventArgs const&) {
                                InstallServerContent(plugins, slug, title);
                            });
                            row.Children().Append(inst);
                            results2.Children().Append(row);
                        }
                    }
                    catch (...)
                    {
                    }
                },
                plugins ? hstring{ L"plugin" } : hstring{ L"mod" });
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::InstallServerContent(bool plugins, hstring slug, hstring title)
    {
        auto entry = FindEntry(m_detailId);
        if (entry.id.empty())
            return;
        auto destDir = Server::ServersDir() / std::wstring{ entry.id } /
            (plugins ? L"plugins" : L"mods");
        try
        {
            std::error_code ec;
            std::filesystem::create_directories(destDir, ec);
        }
        catch (...)
        {
        }
        TextBlock& status = plugins ? m_dPluginStatus : m_dModStatus;
        if (status)
            status.Text(L"Resolving " + title + L"...");
        hstring mc = entry.mcVersion;
        Modrinth::PickFileAsync(slug, mc, L"",
            [this, plugins, destDir, title](Modrinth::ModFile f) -> fire_and_forget {
                try
                {
                    TextBlock& status2 = plugins ? m_dPluginStatus : m_dModStatus;
                    if (f.url.empty())
                    {
                        if (status2)
                            status2.Text(L"No compatible file found for this version.");
                        co_return;
                    }
                    if (status2)
                        status2.Text(L"Downloading " + f.filename + L"...");
                    hstring err = co_await Modrinth::DownloadFileAsync(f, destDir.wstring());
                    if (status2)
                        status2.Text(err.empty() ? (L"Installed " + f.filename) : err);
                }
                catch (...)
                {
                }
            });
    }

    void LocalServerPage::RefreshDetailFiles()
    {
        try
        {
            if (!m_dFilePath || !m_dFileList)
                return;
            auto entry = FindEntry(m_detailId);
            if (entry.id.empty())
                return;
            auto root = Server::ServersDir() / std::wstring{ entry.id };
            auto base = m_detailRel.empty() ? root : root / m_detailRel;
            m_dFilePath.Text(L"Files: /" + hstring{ m_detailRel });
            m_dFileList.Children().Clear();
            std::error_code ec;
            if (!std::filesystem::exists(base, ec))
                return;
            if (!m_detailRel.empty())
            {
                Button up{};
                up.Content(box_value(L".. (up)"));
                up.Click([this](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        auto p = std::filesystem::path{ m_detailRel }.parent_path().wstring();
                        if (!p.empty() && p != L"." && p != L"/")
                            m_detailRel = p;
                        RefreshDetailFiles();
                    }
                    catch (...)
                    {
                    }
                });
                m_dFileList.Children().Append(up);
            }
            std::vector<std::filesystem::directory_entry> dirs;
            std::vector<std::filesystem::directory_entry> files;
            for (auto const& e : std::filesystem::directory_iterator(base, ec))
            {
                try
                {
                    if (e.is_directory(ec))
                        dirs.push_back(e);
                    else if (e.is_regular_file(ec))
                        files.push_back(e);
                }
                catch (...)
                {
                }
            }
            auto byName = [](auto const& a, auto const& b) {
                return a.path().filename().wstring() < b.path().filename().wstring();
            };
            std::sort(dirs.begin(), dirs.end(), byName);
            std::sort(files.begin(), files.end(), byName);
            auto addRow = [this](std::filesystem::path p, bool isDir, unsigned long long size) {
                StackPanel row{};
                row.Orientation(Orientation::Horizontal);
                row.Spacing(8);
                row.VerticalAlignment(VerticalAlignment::Center);
                Button open{};
                open.Content(box_value(hstring{ (isDir ? L"[dir] " : L"") + p.filename().wstring() }));
                open.Click([this, p, isDir](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        if (isDir)
                        {
                            auto root2 = Server::ServersDir() / std::wstring{ m_detailId };
                            auto rel = std::filesystem::relative(p, root2).wstring();
                            if (!rel.empty() && rel != L".")
                                m_detailRel = rel;
                            RefreshDetailFiles();
                        }
                        else
                        {
                            ShellExecuteW(nullptr, L"open", p.wstring().c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
                        }
                    }
                    catch (...)
                    {
                    }
                });
                row.Children().Append(open);
                if (!isDir)
                {
                    TextBlock sz{};
                    sz.Text(FmtSize(size));
                    sz.Opacity(0.6);
                    sz.VerticalAlignment(VerticalAlignment::Center);
                    row.Children().Append(sz);
                }
                Button del{};
                del.Content(box_value(L"Delete"));
                del.Click([this, p, isDir](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        std::error_code ec2;
                        if (isDir)
                            std::filesystem::remove_all(p, ec2);
                        else
                            std::filesystem::remove(p, ec2);
                        if (ec2)
                            m_dStatus.Text(L"Delete failed.");
                        RefreshDetailFiles();
                    }
                    catch (...)
                    {
                    }
                });
                row.Children().Append(del);
                m_dFileList.Children().Append(row);
            };
            for (auto const& d : dirs)
                addRow(d.path(), true, 0);
            for (auto const& f : files)
            {
                std::error_code ec2;
                addRow(f.path(), false, std::filesystem::file_size(f.path(), ec2));
            }
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::RefreshDetailBackups()
    {
        try
        {
            if (!m_dBackupList)
                return;
            m_dBackupList.Children().Clear();
            auto entry = FindEntry(m_detailId);
            if (entry.id.empty())
                return;
            auto bakDir = Server::ServersDir() / std::wstring{ entry.id } / L"backups";
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            if (std::filesystem::exists(bakDir, ec))
            {
                for (auto const& e : std::filesystem::directory_iterator(bakDir, ec))
                {
                    try
                    {
                        if (e.is_regular_file(ec) && e.path().extension() == L".gz")
                            files.push_back(e.path());
                    }
                    catch (...)
                    {
                    }
                }
            }
            std::sort(files.begin(), files.end(),
                [](auto const& a, auto const& b) { return a.filename() > b.filename(); });
            if (files.empty())
            {
                TextBlock t{};
                t.Opacity(0.6);
                t.Text(L"No backups yet.");
                m_dBackupList.Children().Append(t);
                return;
            }
            for (auto const& f : files)
            {
                StackPanel row{};
                row.Orientation(Orientation::Horizontal);
                row.Spacing(8);
                row.VerticalAlignment(VerticalAlignment::Center);
                TextBlock t{};
                std::error_code ec2;
                t.Text(hstring{ f.filename().wstring() } + L"  (" +
                    FmtSize(std::filesystem::file_size(f, ec2)) + L")");
                t.VerticalAlignment(VerticalAlignment::Center);
                t.Width(320);
                t.TextTrimming(TextTrimming::CharacterEllipsis);
                row.Children().Append(t);
                Button restore{};
                restore.Content(box_value(L"Restore"));
                restore.Click([this, f](IInspectable const&, RoutedEventArgs const&) {
                    RestoreBackup(f);
                });
                row.Children().Append(restore);
                Button del{};
                del.Content(box_value(L"Delete"));
                del.Click([this, f](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        std::error_code ec3;
                        std::filesystem::remove(f, ec3);
                        RefreshDetailBackups();
                    }
                    catch (...)
                    {
                    }
                });
                row.Children().Append(del);
                m_dBackupList.Children().Append(row);
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget LocalServerPage::BackupNow()
    {
        auto entry = FindEntry(m_detailId);
        if (entry.id.empty())
            co_return;
        auto dir = Server::ServersDir() / std::wstring{ entry.id };
        hstring name = entry.name.empty() ? entry.id : entry.name;
        if (m_dBackupStatus)
            m_dBackupStatus.Text(L"Backing up...");
        hstring err = co_await Server::BackupServerAsync(dir, name);
        if (m_dBackupStatus)
            m_dBackupStatus.Text(err.empty() ? L"Backup complete." : err);
        RefreshDetailBackups();
    }

    fire_and_forget LocalServerPage::RestoreBackup(std::filesystem::path file)
    {
        auto entry = FindEntry(m_detailId);
        if (entry.id.empty())
            co_return;
        if (Server::ConsoleRunning(entry.id))
        {
            if (m_dBackupStatus)
                m_dBackupStatus.Text(L"Stop the server before restoring.");
            co_return;
        }
        auto dir = Server::ServersDir() / std::wstring{ entry.id };
        if (m_dBackupStatus)
            m_dBackupStatus.Text(L"Restoring...");
        hstring err = co_await Server::RestoreBackupAsync(dir, file);
        if (m_dBackupStatus)
            m_dBackupStatus.Text(err.empty() ? L"Restore complete." : err);
        RefreshDetailFiles();
        RefreshDetailBackups();
    }

    void LocalServerPage::StartDetailSampler()
    {
        try
        {
            StopDetailSampler();
            int gen = ++m_detailGen;
            m_detailStop = false;
            m_dCpuHist.clear();
            m_dMemHist.clear();
            m_dDiskHist.clear();
            hstring id = m_detailId;
            auto dir = Server::ServersDir() / std::wstring{ id };
            hstring cid = id;
            std::thread([this, gen, cid, dir] {
                try
                {
                    m_sampler.PollProcessCpu(Server::ConsoleHandle(cid)); // seed baseline
                    for (;;)
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        if (m_detailStop || gen != m_detailGen)
                            return;
                        SystemStats::SystemSnapshot snap{};
                        double pcpu = -1.0;
                        unsigned long long pmem = 0;
                        unsigned long long freeAv = 0;
                        unsigned long long total = 0;
                        unsigned long long memTot = 0;
                        unsigned long long memAv = 0;
                        bool diskOk = false;
                        {
                            std::lock_guard<std::mutex> lk(m_samplerMutex);
                            snap = m_sampler.PollSystem();
                            if (void* h = Server::ConsoleHandle(cid))
                            {
                                pcpu = m_sampler.PollProcessCpu(h);
                                pmem = m_sampler.ProcessPrivateBytes(h);
                            }
                        }
                        ULARGE_INTEGER freeA{}, tot{}, dummy{};
                        if (GetDiskFreeSpaceExW(dir.wstring().c_str(), &freeA, &tot, &dummy) &&
                            tot.QuadPart > 0)
                        {
                            diskOk = true;
                            freeAv = freeA.QuadPart;
                            total = tot.QuadPart;
                        }
                        memTot = snap.memTotalBytes;
                        memAv = snap.memAvailBytes;
                        try
                        {
                            m_ui.TryEnqueue(
                                [this, gen, snap, pcpu, pmem, freeAv, total, memTot, memAv, diskOk, cid] {
                                    try
                                    {
                                        if (gen != m_detailGen)
                                            return;
                                        double cpu = snap.cpuPercent < 0.0
                                            ? (m_dCpuHist.empty() ? 0.0 : m_dCpuHist.back())
                                            : snap.cpuPercent;
                                        double memPct = (memTot == 0) ? 0.0
                                            : 100.0 * (1.0 - static_cast<double>(memAv) /
                                                                  static_cast<double>(memTot));
                                        double diskPct = (!diskOk || total == 0) ? 0.0
                                            : 100.0 * (1.0 - static_cast<double>(freeAv) /
                                                                  static_cast<double>(total));
                                        m_dCpuHist.push_back(cpu);
                                        m_dMemHist.push_back(memPct);
                                        m_dDiskHist.push_back(diskPct);
                                        while (m_dCpuHist.size() > kSamples)
                                            m_dCpuHist.pop_front();
                                        while (m_dMemHist.size() > kSamples)
                                            m_dMemHist.pop_front();
                                        while (m_dDiskHist.size() > kSamples)
                                            m_dDiskHist.pop_front();
                                        wchar_t buf[160]{};
                                        swprintf_s(buf, L"%.0f%%", cpu);
                                        if (m_dCpuValue)
                                            m_dCpuValue.Text(buf);
                                        if (memTot > 0 && m_dMemValue)
                                        {
                                            swprintf_s(buf, L"%.0f%% (%s / %s)", memPct,
                                                GbText(memTot - memAv).c_str(),
                                                GbText(memTot).c_str());
                                            m_dMemValue.Text(buf);
                                        }
                                        if (diskOk && m_dDiskValue)
                                        {
                                            swprintf_s(buf, L"%.0f%% used (%s free)", diskPct,
                                                GbText(freeAv).c_str());
                                            m_dDiskValue.Text(buf);
                                        }
                                        if (m_dCpuLine)
                                            PaintGraph(m_dCpuLine, m_dCpuHist);
                                        if (m_dMemLine)
                                            PaintGraph(m_dMemLine, m_dMemHist);
                                        if (m_dDiskLine)
                                            PaintGraph(m_dDiskLine, m_dDiskHist);
                                        if (m_dProc)
                                        {
                                            if (pcpu < 0.0 && pmem == 0)
                                            {
                                                m_dProc.Text(Server::ConsoleRunning(cid)
                                                        ? L"Server process: starting..."
                                                        : L"Server process: stopped");
                                            }
                                            else
                                            {
                                                swprintf_s(buf, L"Server process: %.0f%% CPU, %s RAM",
                                                    (std::max)(0.0, pcpu), GbText(pmem).c_str());
                                                m_dProc.Text(buf);
                                            }
                                        }
                                    }
                                    catch (...)
                                    {
                                    }
                                });
                        }
                        catch (...)
                        {
                            return;
                        }
                    }
                }
                catch (...)
                {
                }
            }).detach();
        }
        catch (...)
        {
        }
    }

    void LocalServerPage::StopDetailSampler()
    {
        m_detailStop = true;
        ++m_detailGen;
    }
}
