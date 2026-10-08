#pragma once

#include "Modrinth.h"
#include "CurseForge.h"
#include <functional>
#include <string>
#include <vector>

// Automatic dependency installer for mod installs. After the Mods page
// downloads a mod file, it hands that build's dependency list here and this
// resolves + downloads every *required* dependency for the same MC
// version/loader, transitively (deps of deps), then reports what happened.
//
// Only required deps are installed: optional ones are skipped silently,
// incompatible/embedded ones are never touched. A visited set + depth cap
// stops cycles (A needs B needs A). Anything that can't be resolved to a
// matching file is collected into Summary::missing so the status line can
// name it instead of failing the whole install.
namespace winrt::PretClient::ModDeps
{
    struct Summary
    {
        std::vector<hstring> installed{}; // filenames downloaded this time
        std::vector<hstring> alreadyThere{}; // filenames already on disk
        std::vector<hstring> missing{}; // titles/files we could not resolve
    };

    using SummaryFn = std::function<void(Summary)>;

    winrt::fire_and_forget EnsureModrinthAsync(Modrinth::ModVersion version,
        hstring mcVersion, hstring loader, std::wstring modsDir, SummaryFn done);
    winrt::fire_and_forget EnsureCurseForgeAsync(CurseForge::ModFile file, int modId,
        hstring mcVersion, hstring loader, hstring apiKey, std::wstring modsDir, SummaryFn done);

    // One status line for the Mods page: main result plus a compact
    // "+ N deps: ..." / "(couldn't resolve: ...)" suffix when relevant.
    hstring FormatStatus(hstring const& mainStatus, Summary const& s);
}
