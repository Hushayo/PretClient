// Unpackaged entry point. The Windows App SDK bootstrapper runs automatically
// via the auto-initializer objects linked from the NuGet packages, so all
// that remains here is apartment init + Application::Start.
#include "pch.h"
#include "App.h"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) { ::winrt::make<::winrt::PretClient::App>(); });

    return 0;
}
