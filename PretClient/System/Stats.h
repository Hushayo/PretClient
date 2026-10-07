#pragma once

// Live system + process stats for the instance cards.
// Everything is best-effort: -1 / 0 means "unavailable", never throws.
namespace winrt::PretClient::SystemStats
{
    struct SystemSnapshot
    {
        double cpuPercent = -1.0; // 0..100, -1 on first poll
        unsigned long long memTotalBytes = 0;
        unsigned long long memAvailBytes = 0;
        double gpuPercent = -1.0; // 0..100, -1 when PDH counters are missing
    };

    class Sampler
    {
    public:
        Sampler();
        ~Sampler();

        SystemSnapshot PollSystem();
        double PollProcessCpu(void* processHandle); // % of total machine CPU
        unsigned long long ProcessPrivateBytes(void* processHandle);
        void Forget(void* processHandle);

    private:
        unsigned long m_cores = 1;
        bool m_cpuSeeded = false;
        unsigned long long m_lastIdle = 0;
        unsigned long long m_lastKernel = 0;
        unsigned long long m_lastUser = 0;

        struct ProcSample
        {
            unsigned long long kernel = 0;
            unsigned long long user = 0;
            unsigned long long wall = 0;
        };
        std::map<std::uintptr_t, ProcSample> m_procs;

        void* m_gpuQuery = nullptr;
        void* m_gpuCounter = nullptr;
        bool m_gpuDead = false;
    };
}
