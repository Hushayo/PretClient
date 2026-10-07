# Workflow — do NOT compile locally

All builds happen in GitHub Actions. Do not run `msbuild` or `ISCC.exe` on your machine.

## Make a change

1. Edit the source files.
2. Commit and push — that's what triggers the build:
   ```powershell
   git add <files you changed>
   git commit -m "What you changed"
   git push origin main
   ```
   Working on something risky? Push to a branch and open a PR instead — PRs also get a CI build with no release attached.
3. Check the result under GitHub → **Actions** → **Build WinUI 3 (C++)**.
   Green = your code compiled on `windows-2022` (x64 Release).

## How CI handles it

- Push to `main` (or a PR): CI checks out, stamps the updater version from `VERSION`, runs `msbuild PretClient.sln /restore /p:Configuration=Release /p:Platform=x64`.
- Push touching `**.cpp / **.h / **.hpp / **.rc / *.sln / VERSION`: `auto-tag.yml` bumps `VERSION` if needed, creates tag `vX.Y.Z`, and triggers a release build.
- Tag `v*` / `release-*` (or manual dispatch with `build_installer=true`): CI also builds `installer/Output/PretClient-Setup.exe` with Inno Setup and publishes/updates the GitHub Release the in-app updater polls.

## Rules

- Never commit build output (`x64/`, `x86/`, `ARM64/`, `installer/Output/` — all gitignored).
- Never commit a local version stamp in `PretClient/Update/Updater.cpp` — CI stamps it from `VERSION` at build time.
- Don't bump `VERSION` manually for normal fixes — auto-tag does it. Only edit `VERSION` when you intentionally want a new release number.
- Get test builds from Actions artifacts (`PretClient-Setup`) or from GitHub Releases, not from a local `x64/Release` folder.
