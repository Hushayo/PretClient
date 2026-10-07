#pragma once

#include <deque>
#include <filesystem>

// Local Server page: Create-a-Server entry point. The Create button opens
// a dialog (researched server-software list, per-software MC versions, live
// CPU / memory / storage graphs); confirming downloads + installs the
// server jar into <DataDir>/servers and lists it on the page.
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
        // (Named to dodge the Win32 CreateDialog macro: winuser.h rewrites
        // CreateDialog to CreateDialogW, which breaks the declaration.)
        winrt::fire_and_forget OpenCreateDialog();
        void LoadVersions(hstring softwareId);
        // Resolve -> download (+verify) -> installer/BuildTools when needed
        // -> run.bat + servers.json record. Progress lives on the page.
        void InstallServer(hstring name, hstring software, hstring mc);
        void RefreshServers();
        void SetStatus(hstring const& line);
        void WorkDone();

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
    };
}
