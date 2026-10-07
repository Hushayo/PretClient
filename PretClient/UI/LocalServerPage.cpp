#include "pch.h"
#include "LocalServerPage.h"
#include "Theme.h"
#include "../Minecraft/Http.h"
#include "../Minecraft/Java.h"
#include "../Minecraft/Server.h"
#include "../Paths.h"
#include <chrono>
#include <coroutine>
#include <shellapi.h>
#include <winrt/Windows.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
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
    } // namespace

    LocalServerPage::LocalServerPage()
    {
        m_ui = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        m_root.Spacing(12);
        m_root.Padding(ThicknessHelper::FromUniformLength(24));

        TextBlock head{};
        head.Text(L"Local Server");
        head.Style(Application::Current().Resources().Lookup(box_value(L"TitleLargeTextBlockStyle")).as<Style>());
        m_root.Children().Append(head);

        // Explicit blue (not AccentButtonStyle: the app tints the accent
        // ramp green, so accent would render green here).
        m_create.Content(box_value(L"Create a Server"));
        m_create.Background(Media::SolidColorBrush{
            Windows::UI::ColorHelper::FromArgb(0xFF, 0x00, 0x78, 0xD4) });
        m_create.Foreground(Media::SolidColorBrush{
            Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF) });
        m_create.Click([this](IInspectable const&, RoutedEventArgs const&) { CreateDialog(); });
        m_root.Children().Append(m_create);

        m_progress.Minimum(0);
        m_progress.Maximum(100);
        m_progress.Width(320);
        m_progress.HorizontalAlignment(HorizontalAlignment::Left);
        m_progress.Visibility(Visibility::Collapsed);
        m_root.Children().Append(m_progress);

        m_status.Opacity(0.7);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_root.Children().Append(m_status);

        m_servers.Spacing(8);
        m_root.Children().Append(m_servers);

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

                StackPanel row{};
                row.Orientation(Orientation::Horizontal);
                row.Spacing(8);
                row.VerticalAlignment(VerticalAlignment::Center);

                TextBlock name{};
                name.Text(s.name + L"  -  " + label + L" " + s.mcVersion);
                name.VerticalAlignment(VerticalAlignment::Center);
                name.Width(320);
                name.TextTrimming(TextTrimming::CharacterEllipsis);
                row.Children().Append(name);

                Button run{};
                run.Content(box_value(L"Run"));
                run.Click([this, dir](IInspectable const&, RoutedEventArgs const&) {
                    try
                    {
                        auto bat = dir / L"run.bat";
                        std::error_code ec;
                        if (std::filesystem::exists(bat, ec))
                        {
                            ShellExecuteW(nullptr, L"open", bat.wstring().c_str(),
                                nullptr, dir.wstring().c_str(), SW_SHOWNORMAL);
                        }
                        else
                        {
                            SetStatus(L"No run script yet in " + hstring{ dir.wstring() } +
                                L" - finish the install first.");
                        }
                    }
                    catch (...)
                    {
                    }
                });
                row.Children().Append(run);

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
                row.Children().Append(folder);

                m_servers.Children().Append(row);
            }
        }
        catch (...)
        {
        }
    }

    fire_and_forget LocalServerPage::CreateDialog()
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

            auto addGraph = [&](wchar_t const* name, TextBlock& value, Polyline& line,
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
                line = Polyline{};
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
                try
                {
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
                    SetStatus(L"Downloading " + hstring{ art.fileName } + L"...");
                    m_progress.IsIndeterminate(true);
                    hstring dlErr = co_await Http::DownloadToFileAsync(art.url, dest, kUA,
                        [this](unsigned long long done, unsigned long long total, double) {
                            try
                            {
                                if (total > 0)
                                {
                                    m_progress.IsIndeterminate(false);
                                    m_progress.Value(
                                        100.0 * static_cast<double>(done) / static_cast<double>(total));
                                    wchar_t buf[128]{};
                                    swprintf_s(buf, L"Downloading... %.0f%% (%s / %s)",
                                        100.0 * static_cast<double>(done) / static_cast<double>(total),
                                        MbText(done).c_str(), MbText(total).c_str());
                                    m_status.Text(buf);
                                }
                            }
                            catch (...)
                            {
                            }
                        });
                    if (!dlErr.empty())
                    {
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

                    auto all = Server::LoadServers();
                    Server::ServerEntry entry{};
                    entry.id = hstring{ dir.filename().wstring() };
                    entry.name = name;
                    entry.software = software;
                    entry.mcVersion = mc;
                    all.push_back(std::move(entry));
                    Server::SaveServers(all);
                    RefreshServers();
                    SetStatus(L"Server created in " + hstring{ dir.wstring() } +
                        L". Set eula=true in eula.txt, then press Run.");
                    WorkDone();
                }
                catch (...)
                {
                    try
                    {
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

    void LocalServerPage::PaintGraph(Polyline const& line, std::deque<double> const& hist)
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
}
