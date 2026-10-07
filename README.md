# PretClient — fancy Minecraft launcher (WinUI 3, C++, unpackaged)

Offline-first launcher client. No MSIX — plain exe + custom Inno Setup installer.

## Layout
- `PretClient.sln` — solution (x86/x64/ARM64, Debug/Release)
- `PretClient/PretClient.vcxproj` — C++20, `WindowsPackageType=None`, self-contained WASDK 1.8
- `PretClient/main.cpp` — bootstrap (`MddBootstrapInitialize`) + `Application::Start`
- `PretClient/App.h`, `App.cpp` — app object, merges `XamlControlsResources`
- `PretClient/MainWindow.h`, `MainWindow.cpp` — launcher window, built in code (no XAML):
  username box, version combo, Play button, progress, log, update banner
- `PretClient/Update/Updater.h`, `Updater.cpp` — checks GitHub Releases for a newer
  `PretClient-Setup.exe`, opens it in the browser (no manual re-download hunting)
- `installer/Setup.iss` — Inno Setup script, per-user install, no admin needed
- `.github/workflows/build.yml` — CI: MSBuild x64 Release, build setup, upload artifact

## Build locally
Requires: VS 2022 17.x Build Tools with MSVC v143 + Windows 11 SDK (what CI uses too).
Raw `msbuild` needs no VS IDE. From a dev shell:

```powershell
msbuild PretClient.sln /restore /p:Configuration=Release /p:Platform=x64
```

Run `x64\Release\PretClient.exe` directly — no deploy/install step.
Build the installer with Inno Setup 6: `ISCC.exe installer\Setup.iss`.

## V1 scope
- Fancy shell + offline (non-premium) usernames + self-update via Releases.
- Play is a stub (fake progress + log). Real work next: version manifest,
  Java locate, offline launch (`OfflinePlayer:<name>` UUID), then file downloads.
