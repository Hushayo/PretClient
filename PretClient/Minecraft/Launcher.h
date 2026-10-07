#pragma once

#include <map>
#include "Downloader.h"

// Offline launch: offline UUID session, full java
// command from the prepared game (modern `arguments` and legacy
// `minecraftArguments`), plus process tracking for play / stop / restart.
namespace winrt::PretClient::Launcher
{
    struct Command
    {
        hstring exe{};
        hstring args{};
        hstring workDir{};
    };

    hstring OfflineUuid(hstring username);
    Command BuildCommand(Downloader::PreparedGame const& game, hstring username, hstring uuid,
        hstring accessToken, hstring userType, hstring xuid,
        int minMemMb, int maxMemMb, hstring javaExe,
        bool fpsBoost = true, hstring extraJvmArgs = L"");

    // Process sessions, keyed by instance id.
    bool Start(Command const& cmd, hstring const& instanceId, hstring& error,
        bool highPriority = true, bool preferDedicatedGpu = true);
    void Stop(hstring const& instanceId);
    bool IsRunning(hstring const& instanceId);
    void* RawHandle(hstring const& instanceId);
    unsigned long Pid(hstring const& instanceId);
}
