# PretClient — compact Minecraft launcher (WinUI 3, C++)

Offline-first, unpackaged exe + Inno Setup installer. No MSIX, no admin.

- Instances: vanilla / Fabric / Quilt / Forge / NeoForge, offline play, per-process CPU/RAM + system GPU + log while running, per-instance mods
- Mods: Modrinth search, version-locked to instance, one-click install / update check
- Settings: username, game folder, Java auto-detect, memory
- Self-update: polls GitHub Releases, one-click `PretClient-Setup.exe` download + install

See `workflow.md` — do not build locally, push to CI.
