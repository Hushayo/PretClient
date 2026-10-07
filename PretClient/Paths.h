#pragma once

#include <filesystem>
#include <shlobj.h>

// Shared folders. Game files go to the classic %APPDATA%\.minecraft so the
// client overwrites the standard roaming folder instead of a custom one.
namespace winrt::PretClient::Paths
{
    inline std::filesystem::path RoamingBase()
    {
        PWSTR raw = nullptr;
        std::filesystem::path base;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)))
        {
            base = std::filesystem::path{ raw };
            CoTaskMemFree(raw);
        }
        return base;
    }

    inline std::filesystem::path DataDir()
    {
        auto dir = RoamingBase() / L"PretClient";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    inline std::filesystem::path DefaultGameDir()
    {
        return RoamingBase() / L".minecraft";
    }
}
