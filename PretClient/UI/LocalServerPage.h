#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include "../System/Stats.h"

// Local Server page: server list (in-app start/stop) + a per-server detail
// view with live console, resource graphs, Modrinth plugins/mods, file
// management and tar.gz backups. The Create button opens a dialog
// (researched server-software list, per-software MC versions, live
// CPU / memory / storage graphs) that downloads + installs the jar.
namespace winrt::PretClient
{
    struct LocalServerPage
    {
        LocalServerPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
        {
            return m_root;
        }
        void Refresh();

    private:
        winrt::fire_and_forget OpenCreateDialog();
        void LoadVersions(hstring softwareId);
        // Resolve -> download (+verify) -> installer/BuildTools when needed
        // -> run.bat + servers.json record. Progress lives on the page.
        void InstallServer(hstring name, hstring software, hstring mc);
        void RefreshServers();
        void SetStatus(hstring const& line);
        void WorkDone();

        // Detail view (one server at a time).
        void OpenDetail(hstring id);
        void ShowList();
        void BuildDetail();
        void UpdateDetailStatus();
        winrt::fire_and_forget StartServer(hstring id);
        void SendDetailCommand();
        void SearchServerContent(bool plugins);
        void InstallServerContent(bool plugins, hstring slug, hstring title);
        void RefreshDetailFiles();
        void RefreshDetailBackups();
        winrt::fire_and_forget BackupNow();
        winrt::fire_and_forget RestoreBackup(std::filesystem::path file);
        void AppendConsole(hstring id, std::string const& line, bool exited, int exitCode);

        // Detail resource sampler (background thread, UI-marshalled).
        void StartDetailSampler();
        void StopDetailSampler();

        void StartGraphs();
        void StopGraphs();
        void SampleGraphs();
        static void PaintGraph(Microsoft::UI::Xaml::Shapes::Polyline const& line,
            std::deque<double> const& hist);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::Button m_create{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        Microsoft::UI::Xaml::Controls::ProgressBar m_progress{};
        Microsoft::UI::Xaml::Controls::StackPanel m_servers{};
        Microsoft::UI::Xaml::Controls::StackPanel m_detail{};
        Microsoft::UI::Dispatching::DispatcherQueue m_ui{ nullptr };
        bool m_working = false;

        // Create-dialog widgets. Rebuilt on every open and held here so the
        // 1s graph timer can repaint them until the dialog closes.
        Microsoft::UI::Xaml::Controls::TextBox m_name{};
        Microsoft::UI::Xaml::Controls::ComboBox m_software{};
        Microsoft::UI::Xaml::Controls::TextBlock m_softwareDesc{};
        Microsoft::UI::Xaml::Controls::ComboBox m_versions{};
        Microsoft::UI::Xaml::Controls::TextBlock m_cpuValue{};
        Microsoft::UI::Xaml::Controls::TextBlock m_memValue{};
        Microsoft::UI::Xaml::Controls::TextBlock m_diskValue{};
        Microsoft::UI::Xaml::Shapes::Polyline m_cpuLine{};
        Microsoft::UI::Xaml::Shapes::Polyline m_memLine{};
        Microsoft::UI::Xaml::Shapes::Polyline m_diskLine{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_graphTimer{ nullptr };
        std::deque<double> m_cpuHist{};
        std::deque<double> m_memHist{};
        std::deque<double> m_diskHist{};
        std::filesystem::path m_diskPath{};
        bool m_cpuSeeded = false;
        unsigned long long m_lastIdle = 0;
        unsigned long long m_lastKernel = 0;
        unsigned long long m_lastUser = 0;

        // Detail widgets (rebuilt per opened server).
        hstring m_detailId{};
        std::wstring m_detailRel{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dTitle{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dStatus{};
        Microsoft::UI::Xaml::Controls::Button m_dStart{};
        Microsoft::UI::Xaml::Controls::Button m_dStop{};
        Microsoft::UI::Xaml::Controls::TextBox m_dLog{};
        Microsoft::UI::Xaml::Controls::TextBox m_dInput{};
        Microsoft::UI::Xaml::Controls::ScrollViewer m_dScroll{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dProc{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dCpuValue{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dMemValue{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dDiskValue{};
        Microsoft::UI::Xaml::Shapes::Polyline m_dCpuLine{};
        Microsoft::UI::Xaml::Shapes::Polyline m_dMemLine{};
        Microsoft::UI::Xaml::Shapes::Polyline m_dDiskLine{};
        std::deque<double> m_dCpuHist{};
        std::deque<double> m_dMemHist{};
        std::deque<double> m_dDiskHist{};
        Microsoft::UI::Xaml::Controls::TextBox m_dPluginQuery{};
        Microsoft::UI::Xaml::Controls::StackPanel m_dPluginResults{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dPluginStatus{};
        Microsoft::UI::Xaml::Controls::TextBox m_dModQuery{};
        Microsoft::UI::Xaml::Controls::StackPanel m_dModResults{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dModStatus{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dFilePath{};
        Microsoft::UI::Xaml::Controls::StackPanel m_dFileList{};
        Microsoft::UI::Xaml::Controls::StackPanel m_dBackupList{};
        Microsoft::UI::Xaml::Controls::TextBlock m_dBackupStatus{};

        SystemStats::Sampler m_sampler{};
        std::mutex m_samplerMutex{};
        std::atomic<int> m_detailGen{ 0 };
        std::atomic<bool> m_detailStop{ false };
        std::map<std::wstring, std::wstring> m_backlog{}; // console text per server
    };
}
