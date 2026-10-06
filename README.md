# PretClient — WinUI 3 (C++/WinRT)

Blank packaged WinUI 3 desktop app in C++. `App.OnLaunched` creates and activates `MainWindow` (`PretClient/MainWindow.xaml:1`).

## Layout
- `PretClient.sln` — solution (x86/x64/ARM64, Debug/Release)
- `PretClient/PretClient.vcxproj` — C++20, `EnableXamlGeneratedMain`, WASDK 1.8.260921001, CppWinRT 2.0.250303.1
- `PretClient/Package.appxmanifest` — packaged identity, min 10.0.17763.0
- `PretClient/App.xaml`, `App.idl`, `App.xaml.h`, `App.xaml.cpp` — app entry
- `PretClient/MainWindow.xaml`, `MainWindow.idl`, `MainWindow.xaml.h`, `MainWindow.xaml.cpp` — window, button + text
- `PretClient/Assets/` — placeholder logos (replace with real art)

## Build requirements (your machine is missing these)
You have Build Tools 2022 only (no `cl.exe`, no Windows SDK `Include/`). Install:
1. Visual Studio 2022 17.8+ with workloads:
   - Desktop development with C++
   - Windows application development
   - .NET Desktop Development (required by VS for WinUI even for C++)
   - Windows SDK 10.0.26100 or 10.0.19041+
2. Windows App SDK C++ templates / VSIX (via VS Installer) + enable Developer Mode
3. Open `PretClient.sln`, set x64 Debug, Build. VS will prompt to create a test certificate (`PretClient_TemporaryKey.pfx`) for sideloading — accept.

## Run
F5 in VS (packaged deploy). The window title is set in `PretClient/MainWindow.xaml.cpp:18`.
Button handler is `MainWindow::myButton_Click` in `PretClient/MainWindow.xaml.cpp:30`.
