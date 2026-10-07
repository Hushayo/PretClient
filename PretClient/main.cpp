// Unpackaged entry point: bootstrap the Windows App SDK runtime,
// then start the WinUI application.
#include "pch.h"
#include "App.h"
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    const HRESULT bootstrapHr = ::MddBootstrapInitialize(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        { WINDOWSAPPSDK_RUNTIME_VERSION_UINT64 });
    if (FAILED(bootstrapHr))
    {
        MessageBoxW(
            nullptr,
            L"Failed to initialize the Windows App SDK runtime.\nReinstall PretClient and try again.",
            L"PretClient",
            MB_OK | MB_ICONERROR);
        return 0;
    }

    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) { ::winrt::make<::winrt::PretClient::App>(); });

    ::MddBootstrapShutdown();
    return 0;
}
