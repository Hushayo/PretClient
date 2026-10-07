#include "pch.h"
#include "Stats.h"
#include <map>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>

namespace winrt::PretClient::SystemStats
{
    namespace
    {
        unsigned long long ToU64(FILETIME ft)
        {
            ULARGE_INTEGER u{};
            u.LowPart = ft.dwLowDateTime;
            u.HighPart = ft.dwHighDateTime;
            return u.QuadPart;
        }
    } // namespace

    Sampler::Sampler()
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        if (si.dwNumberOfProcessors > 0)
            m_cores = si.dwNumberOfProcessors;
    }

    Sampler::~Sampler()
    {
        if (m_gpuQuery)
            PdhCloseQuery(static_cast<PDH_HQUERY>(m_gpuQuery));
    }

    SystemSnapshot Sampler::PollSystem()
    {
        SystemSnapshot snap{};
        try
        {
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
                        snap.cpuPercent = 100.0 * (1.0 - static_cast<double>(idleD) / static_cast<double>(totalD));
                }
                m_lastIdle = i;
                m_lastKernel = k;
                m_lastUser = u;
                m_cpuSeeded = true;
            }

            MEMORYSTATUSEX mem{};
            mem.dwLength = sizeof(mem);
            if (GlobalMemoryStatusEx(&mem))
            {
                snap.memTotalBytes = mem.ullTotalPhys;
                snap.memAvailBytes = mem.ullAvailPhys;
            }

            // GPU: sum of all 3D engine utilization counters, clamped to 100.
            if (!m_gpuDead)
            {
                if (!m_gpuQuery)
                {
                    PDH_HQUERY q = nullptr;
                    PDH_HCOUNTER c = nullptr;
                    if (PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS &&
                        PdhAddCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &c) == ERROR_SUCCESS &&
                        PdhCollectQueryData(q) == ERROR_SUCCESS)
                    {
                        m_gpuQuery = q;
                        m_gpuCounter = c;
                    }
                    else
                    {
                        if (q)
                            PdhCloseQuery(q);
                        m_gpuDead = true;
                    }
                }
                else if (PdhCollectQueryData(static_cast<PDH_HQUERY>(m_gpuQuery)) == ERROR_SUCCESS)
                {
                    DWORD size = 0;
                    DWORD count = 0;
                    if (PdhGetFormattedCounterArrayW(
                            static_cast<PDH_HCOUNTER>(m_gpuCounter), PDH_FMT_DOUBLE, &size, &count, nullptr) == PDH_MORE_DATA &&
                        size > 0)
                    {
                        std::vector<std::uint8_t> buf(size);
                        if (PdhGetFormattedCounterArrayW(
                                static_cast<PDH_HCOUNTER>(m_gpuCounter), PDH_FMT_DOUBLE, &size, &count,
                                reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buf.data())) == ERROR_SUCCESS)
                        {
                            double sum = 0.0;
                            auto items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buf.data());
                            for (DWORD n = 0; n < count; ++n)
                                sum += items[n].FmtValue.doubleValue;
                            snap.gpuPercent = (std::min)(100.0, (std::max)(0.0, sum));
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }
        return snap;
    }

    double Sampler::PollProcessCpu(void* processHandle)
    {
        try
        {
            HANDLE h = static_cast<HANDLE>(processHandle);
            if (!h)
                return -1.0;
            FILETIME created{}, exited{}, kernel{}, user{}, now{};
            if (!GetProcessTimes(h, &created, &exited, &kernel, &user))
                return -1.0;
            GetSystemTimeAsFileTime(&now);
            auto k = ToU64(kernel);
            auto u = ToU64(user);
            auto w = ToU64(now);
            auto key = reinterpret_cast<std::uintptr_t>(h);
            auto it = m_procs.find(key);
            if (it == m_procs.end())
            {
                m_procs[key] = ProcSample{ k, u, w };
                return -1.0;
            }
            auto procD = (k - it->second.kernel) + (u - it->second.user);
            auto wallD = w - it->second.wall;
            it->second = ProcSample{ k, u, w };
            if (wallD == 0 || m_cores == 0)
                return 0.0;
            return 100.0 * static_cast<double>(procD) / static_cast<double>(wallD) / static_cast<double>(m_cores);
        }
        catch (...)
        {
            return -1.0;
        }
    }

    unsigned long long Sampler::ProcessPrivateBytes(void* processHandle)
    {
        try
        {
            HANDLE h = static_cast<HANDLE>(processHandle);
            if (!h)
                return 0;
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
                return pmc.PrivateUsage;
        }
        catch (...)
        {
        }
        return 0;
    }

    void Sampler::Forget(void* processHandle)
    {
        m_procs.erase(reinterpret_cast<std::uintptr_t>(processHandle));
    }
}
