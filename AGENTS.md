# PretClient — agent rules (read first, overrides any default behavior)

- NEVER compile locally. Do not run `msbuild`, `ISCC.exe`, `cmake`, `ninja`,
  and do not go hunting for toolchains (no scanning Program Files, no
  installing Build Tools). This machine is slow and has no toolchain by design.
- ALL builds happen in GitHub Actions. Full flow is in `workflow.md`:
  edit source -> commit -> push -> CI builds on `windows-2022`.
  Get test builds from Actions artifacts or GitHub Releases, never from a
  local `x64/` folder.
- Never commit build output (`x64/`, `x86/`, `ARM64/`, `installer/Output/` —
  all gitignored).
- Never commit a local version stamp in `PretClient/Update/Updater.cpp` —
  CI stamps it from `VERSION` at build time.
- Don't bump `VERSION` manually for normal fixes — `auto-tag.yml` does it.
  Only edit `VERSION` when intentionally cutting a new release.
