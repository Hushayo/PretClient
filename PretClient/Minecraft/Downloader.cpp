#include "pch.h"
#include "Downloader.h"
#include "Fabric.h"
#include "Forge.h"
#include "Http.h"
#include "Java.h"
#include "Modrinth.h"
#include "NeoForge.h"
#include "Quilt.h"
#include "Rules.h"
#include "Versions.h"
#include <algorithm>
#include <coroutine>
#include <fstream>
#include <set>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Downloader
{
    namespace
    {
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        hstring OptStr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::String)
                    return o.GetNamedString(key);
            }
            catch (...)
            {
            }
            return hstring{};
        }

        // Legacy launchwrapper (pre-1.6 vanilla, old Forge: effective
        // mainClass net.minecraft.launchwrapper.Launch) casts the app class
        // loader to URLClassLoader, which throws on Java 9+. Mojang marks
        // these javaVersion.component "jre-legacy" and ships Java 8 for
        // them. Mirror that: pin major == max == 8 so the picker and the
        // Temurin auto-download land on 8 instead of the newest install.
        void ApplyJavaRule(PreparedGame& game)
        {
            int major = 8;
            hstring component;
            try
            {
                if (game.versionJson && game.versionJson.HasKey(L"javaVersion"))
                {
                    auto jv = game.versionJson.GetNamedObject(L"javaVersion");
                    if (jv.HasKey(L"majorVersion"))
                        major = static_cast<int>(jv.GetNamedNumber(L"majorVersion"));
                    component = OptStr(jv, L"component");
                }
            }
            catch (...)
            {
            }
            if (major < 8)
                major = 8;
            game.javaMajor = major;
            bool legacy = (component == hstring{ L"jre-legacy" });
            try
            {
                hstring main;
                if (game.versionJson && game.versionJson.HasKey(L"mainClass"))
                    main = game.versionJson.GetNamedString(L"mainClass");
                if (game.loaderProfile && game.loaderProfile.HasKey(L"mainClass"))
                {
                    hstring lm = game.loaderProfile.GetNamedString(L"mainClass");
                    if (!lm.empty())
                        main = lm;
                }
                if (main == hstring{ L"net.minecraft.launchwrapper.Launch" })
                    legacy = true;
            }
            catch (...)
            {
            }
            game.javaMax = legacy ? 8 : 0;
        }

        // A probed java.exe is usable when it meets the floor and (for
        // legacy launches) the ceiling.
        bool JavaFits(int major, int req, int max)
        {
            if (major < req)
                return false;
            if (max > 0 && major > max)
                return false;
            return major > 0;
        }

        hstring DescribeException(std::exception const& e)
        {
            return to_hstring(e.what());
        }

        hstring DescribeException(...)
        {
            return L"unknown error";
        }

        bool FileOk(std::filesystem::path const& p, long long wantSize, hstring const& wantSha1)
        {
            try
            {
                std::error_code ec;
                if (!std::filesystem::exists(p, ec))
                    return false;
                if (wantSize > 0 && static_cast<long long>(std::filesystem::file_size(p, ec)) != wantSize)
                    return false;
                if (!wantSha1.empty())
                {
                    std::wstring hex;
                    if (!Http::Sha1OfFile(p, hex) ||
                        _wcsicmp(hex.c_str(), std::wstring{ wantSha1 }.c_str()) != 0)
                        return false;
                }
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        IAsyncOperation<hstring> FetchFile(hstring url, std::filesystem::path const& dest,
            long long size, hstring sha1, hstring what, LogFn log, FileProgFn prog,
            bool quiet = false)
        {
            if (FileOk(dest, size, sha1))
            {
                if (!quiet)
                    log(L"  ok " + what);
                co_return hstring{};
            }
            if (url.empty())
                co_return hstring{ L"Missing URL for " } + what;
            if (!quiet)
                log(L"  + " + what);
            auto fileProg = [prog, what](unsigned long long done, unsigned long long total, double bps) {
                if (prog)
                    prog(what, done, total, bps);
            };
            if (auto err = co_await Http::DownloadToFileAsync(url, dest, kUA, fileProg); !err.empty())
                co_return err;
            if (size > 0)
            {
                std::error_code ec;
                auto have = static_cast<long long>(std::filesystem::file_size(dest, ec));
                if (!ec && have != size)
                    co_return hstring{ L"Size mismatch: " } + what;
            }
            if (!sha1.empty() && !FileOk(dest, size, sha1))
                co_return hstring{ L"Checksum mismatch: " } + what;
            co_return hstring{};
        }

        bool IsNativesEntry(std::wstring const& name)
        {
            auto has = [&](wchar_t const* t) { return name.find(t) != std::wstring::npos; };
            if (name.find(L"natives") == std::wstring::npos)
                return false;
            if (has(L"natives-windows-x86") || has(L"natives-windows-arm64"))
                return false;
            return true;
        }

        struct WaitGroup
        {
            size_t pending = 0;
            std::coroutine_handle<> waiter{};

            void Add()
            {
                ++pending;
            }
            void Done()
            {
                if (pending == 0)
                    return;
                if (--pending == 0 && waiter)
                {
                    auto h = waiter;
                    waiter = {};
                    h.resume();
                }
            }
        };

        struct WaitAwaiter
        {
            WaitGroup* wg;
            bool await_ready() const noexcept
            {
                return wg->pending == 0;
            }
            void await_suspend(std::coroutine_handle<> h) noexcept
            {
                wg->waiter = h;
            }
            void await_resume() const noexcept
            {
            }
        };

        struct FetchJob
        {
            hstring url{};
            std::filesystem::path dest{};
            long long size = 0;
            hstring sha1{};
            hstring what{};
            unsigned long long lastDone = 0;
        };

        struct FetchBatch
        {
            hstring label{ L"files" };
            std::vector<FetchJob> jobs{};
            size_t cursor = 0;
            size_t completed = 0;
            hstring error{};
            unsigned long long doneBytes = 0;
            unsigned long long totalBytes = 0;
            std::chrono::steady_clock::time_point t0{};
            std::chrono::steady_clock::time_point lastReport{};
            LogFn log{};
            FileProgFn prog{};
            WaitGroup wg{};

            void Add(hstring url, std::filesystem::path dest, long long size, hstring sha1, hstring what)
            {
                FetchJob j{};
                j.url = url;
                j.dest = std::move(dest);
                j.size = size;
                j.sha1 = sha1;
                j.what = what;
                totalBytes += size > 0 ? static_cast<unsigned long long>(size) : 0;
                jobs.push_back(std::move(j));
            }

            void Report(bool force = false)
            {
                if (!prog)
                    return;
                auto now = std::chrono::steady_clock::now();
                if (!force && lastReport.time_since_epoch().count() != 0 &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count() < 100)
                    return;
                lastReport = now;
                double secs = std::chrono::duration<double>(now - t0).count();
                wchar_t buf[96]{};
                swprintf_s(buf, L"%s %zu/%zu", std::wstring{ label }.c_str(),
                    completed, jobs.size());
                prog(buf, doneBytes, totalBytes, secs > 0.05 ? doneBytes / secs : 0.0);
            }
        };

        fire_and_forget FetchWorker(FetchBatch* b);

        void StartFetchBatch(FetchBatch& b, size_t workers)
        {
            if (b.jobs.empty())
                return;
            b.t0 = std::chrono::steady_clock::now();
            size_t n = (std::min)(workers, b.jobs.size());
            for (size_t w = 0; w < n; ++w)
            {
                b.wg.Add();
                FetchWorker(&b);
            }
        }

        fire_and_forget FetchWorker(FetchBatch* b)
        {
            for (;;)
            {
                size_t i = b->cursor++;
                if (i >= b->jobs.size())
                    break;
                auto& job = b->jobs[i];
                FileProgFn sub = [b, i](hstring const&, unsigned long long done,
                    unsigned long long, double) {
                    auto& j = b->jobs[i];
                    if (done > j.lastDone)
                    {
                        b->doneBytes += done - j.lastDone;
                        j.lastDone = done;
                    }
                    b->Report();
                };
                auto err = co_await FetchFile(job.url, job.dest, job.size, job.sha1,
                    job.what, b->log, sub, /*quiet=*/true);
                if (!err.empty())
                {
                    if (b->error.empty())
                        b->error = err;
                    break;
                }
                ++b->completed;
                // Credit bytes for files that were already present+valid:
                // FetchFile reports no progress for those, so without this a
                // fully-cached batch reads "81/81 0 KB / 86 MB" and the bar
                // never fills.
                if (job.size > 0 &&
                    static_cast<unsigned long long>(job.size) > job.lastDone)
                {
                    b->doneBytes += static_cast<unsigned long long>(job.size) - job.lastDone;
                    job.lastDone = static_cast<unsigned long long>(job.size);
                }
            }
            b->wg.Done();
        }

        // Runs a forge/neoforge installer jar headless:
        //   "<java>" -jar <installer> --installClient <gameDir>
        // stdout/stderr go to logFile. The process is polled (never a
        // blocking wait: this coroutine lives on the UI thread), so the
        // window stays responsive through the minutes-long install.
        // Returns the process exit code, or -1 when it could not start.
        IAsyncOperation<int> RunInstallerAsync(hstring javaExe,
            std::filesystem::path installerJar, std::filesystem::path gameDir,
            std::filesystem::path logFile)
        {
            int code = -1;
            try
            {
                std::error_code ec;
                std::filesystem::create_directories(logFile.parent_path(), ec);
                HANDLE logH = CreateFileW(logFile.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                std::wstring line = L"\"" + std::wstring{ javaExe } + L"\" -jar \"" +
                    installerJar.wstring() + L"\" --installClient \"" +
                    gameDir.wstring() + L"\"";
                STARTUPINFOW si{ sizeof(si) };
                si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
                si.wShowWindow = SW_HIDE;
                si.hStdOutput = logH ? logH : GetStdHandle(STD_OUTPUT_HANDLE);
                si.hStdError = si.hStdOutput;
                si.hStdInput = nullptr;
                PROCESS_INFORMATION pi{};
                BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                    CREATE_NO_WINDOW, nullptr, gameDir.wstring().c_str(), &si, &pi);
                if (logH)
                    CloseHandle(logH);
                if (!ok)
                    co_return -1;
                CloseHandle(pi.hThread);
                for (;;)
                {
                    DWORD wait = WaitForSingleObject(pi.hProcess, 0);
                    if (wait != WAIT_TIMEOUT)
                        break;
                    co_await winrt::resume_after(std::chrono::seconds{ 1 });
                }
                DWORD exit = 1;
                GetExitCodeProcess(pi.hProcess, &exit);
                CloseHandle(pi.hProcess);
                code = static_cast<int>(exit);
            }
            catch (...)
            {
                code = -1;
            }
            co_return code;
        }

        JsonObject ReadProfileFile(std::filesystem::path const& p)
        {
            JsonObject o{ nullptr };
            try
            {
                std::ifstream f(p, std::ios::binary);
                if (!f.good())
                    return o;
                std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (!text.empty())
                    o = JsonObject::Parse(to_hstring(text));
            }
            catch (...)
            {
                o = JsonObject{ nullptr };
            }
            return o;
        }

        bool HasLoaderMain(JsonObject const& profile)
        {
            try
            {
                return profile && profile.HasKey(L"mainClass");
            }
            catch (...)
            {
                return false;
            }
        }

        // The installer names its output versions/<id>/<id>.json, but the id
        // scheme has changed across eras -- if the expected file is missing,
        // scan for any usable forge/neoforge profile for this MC instead of
        // failing outright.
        JsonObject FindInstalledProfile(std::filesystem::path const& versionsRoot,
            hstring mcVersion, hstring kindSub)
        {
            JsonObject found{ nullptr };
            try
            {
                std::wstring kind{ kindSub };
                std::wstring mc{ mcVersion };
                int best = 0;
                std::error_code ec;
                if (!std::filesystem::exists(versionsRoot, ec))
                    return found;
                for (auto const& e : std::filesystem::directory_iterator(versionsRoot, ec))
                {
                    try
                    {
                        if (!e.is_directory(ec))
                            continue;
                        auto dir = e.path();
                        auto cand = dir / (dir.filename().wstring() + L".json");
                        std::error_code ec2;
                        if (!std::filesystem::exists(cand, ec2))
                            continue;
                        auto o = ReadProfileFile(cand);
                        if (!HasLoaderMain(o))
                            continue;
                        int score = 0;
                        try
                        {
                            hstring id = o.HasKey(L"id")
                                ? o.GetNamedString(L"id")
                                : hstring{ dir.filename().wstring() };
                            std::wstring s{ id };
                            if (s.find(kind) == std::wstring::npos)
                                continue;
                            score = (s.rfind(mc, 0) == 0) ? 2 : 1;
                        }
                        catch (...)
                        {
                            continue;
                        }
                        if (score > best)
                        {
                            best = score;
                            found = o;
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
            catch (...)
            {
            }
            return found;
        }
        // Offline launch cache: the vanilla version JSON is saved on every
        // successful online prepare to versions/<id>/<id>.json, and
        // fabric/quilt loader profiles to
        // versions/<mc>/pretclient-<loader>-<loaderVer>.json. Forge/NeoForge
        // already cache via their installer (versions/<id>/<id>.json), so no
        // extra file is needed for them. With these on disk, PrepareAsync can
        // build a launchable PreparedGame with zero network traffic.
        std::filesystem::path VanillaJsonPath(
            std::filesystem::path const& gamePath, hstring const& vanillaId)
        {
            return gamePath / L"versions" /
                std::filesystem::path{ std::wstring{ vanillaId } } /
                (std::wstring{ vanillaId } + L".json");
        }

        std::wstring SanitizeFilePart(std::wstring s)
        {
            for (auto& c : s)
            {
                bool ok = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') ||
                    (c >= L'a' && c <= L'z') || c == L'-' || c == L'_' || c == L'.';
                if (!ok)
                    c = L'_';
            }
            if (s.empty())
                s = L"cached";
            return s;
        }

        std::filesystem::path LoaderCachePath(
            std::filesystem::path const& gamePath, hstring const& mcVersion,
            hstring const& loader, hstring const& loaderVersion)
        {
            std::wstring name = L"pretclient-" + std::wstring{ loader } + L"-" +
                SanitizeFilePart(std::wstring{ loaderVersion }) + L".json";
            return gamePath / L"versions" /
                std::filesystem::path{ std::wstring{ mcVersion } } / name;
        }

        bool WriteTextFile(std::filesystem::path const& p, hstring const& text)
        {
            try
            {
                std::error_code ec;
                std::filesystem::create_directories(p.parent_path(), ec);
                std::ofstream f(p, std::ios::binary | std::ios::trunc);
                if (!f.good())
                    return false;
                auto utf8 = to_string(text);
                f.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
                return static_cast<bool>(f);
            }
            catch (...)
            {
                return false;
            }
        }

        // Any cached fabric/quilt profile for this MC, newest filename first.
        std::vector<std::filesystem::path> FindCachedLoaderFiles(
            std::filesystem::path const& gamePath, hstring const& mcVersion,
            hstring const& loader)
        {
            std::vector<std::filesystem::path> out;
            try
            {
                auto dir = gamePath / L"versions" /
                    std::filesystem::path{ std::wstring{ mcVersion } };
                std::error_code ec;
                if (!std::filesystem::exists(dir, ec))
                    return out;
                std::wstring prefix = L"pretclient-" + std::wstring{ loader } + L"-";
                for (auto const& e : std::filesystem::directory_iterator(dir, ec))
                {
                    try
                    {
                        if (!e.is_regular_file(ec))
                            continue;
                        auto fn = e.path().filename().wstring();
                        if (fn.rfind(prefix, 0) != 0)
                            continue;
                        if (fn.size() < 6 ||
                            fn.compare(fn.size() - 5, 5, L".json") != 0)
                            continue;
                        // Must parse as JSON with a loader entry point.
                        if (!HasLoaderMain(ReadProfileFile(e.path())))
                            continue;
                        out.push_back(e.path());
                    }
                    catch (...)
                    {
                    }
                }
                std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
                    return a.wstring() > b.wstring();
                });
            }
            catch (...)
            {
            }
            return out;
        }

        hstring LoaderVersionFromCacheFile(std::filesystem::path const& p, hstring const& loader)
        {
            try
            {
                std::wstring fn = p.filename().wstring();
                std::wstring prefix = L"pretclient-" + std::wstring{ loader } + L"-";
                if (fn.rfind(prefix, 0) != 0)
                    return L"cached";
                auto ver = fn.substr(prefix.size());
                if (ver.size() >= 5 && ver.compare(ver.size() - 5, 5, L".json") == 0)
                    ver.erase(ver.size() - 5);
                if (ver.empty())
                    return L"cached";
                return hstring{ ver };
            }
            catch (...)
            {
                return hstring{ L"cached" };
            }
        }

        // Offline assembly: versionJson (+ loaderProfile when modded) are
        // already loaded from disk. Verifies the client jar + every library
        // the launch classpath needs, re-extracts natives, and fills the
        // PreparedGame. Returns "" on success, otherwise a user-facing reason
        // (first missing file) so Play can tell the user to go online once.
        hstring BuildOfflineGame(PreparedGame& game,
            std::filesystem::path const& gamePath, std::filesystem::path const& libsDir,
            std::filesystem::path const& versionsDir, LogFn const& log)
        {
            try
            {
                if (!game.versionJson)
                    return L"Cached version data is missing. Connect once to download it.";
                ApplyJavaRule(game);
                try
                {
                    game.assetIndexId = OptStr(
                        game.versionJson.GetNamedObject(L"assetIndex"), L"id");
                }
                catch (...)
                {
                }
                game.gameDir = game.gameDir.empty()
                    ? hstring{ gamePath.wstring() }
                    : game.gameDir;
                game.cacheDir = hstring{ gamePath.wstring() };
                game.assetsDir = hstring{ (gamePath / L"assets").wstring() };
                game.nativesDir = hstring{ (versionsDir / L"natives").wstring() };
                game.clientJar = hstring{
                    (versionsDir / (std::wstring{ game.vanillaId } + L".jar")).wstring()
                };
                std::error_code ec;
                std::filesystem::create_directories(versionsDir, ec);
                std::filesystem::remove_all(
                    std::filesystem::path{ std::wstring{ game.nativesDir } }, ec);
                std::filesystem::create_directories(
                    std::filesystem::path{ std::wstring{ game.nativesDir } }, ec);

                std::filesystem::path clientPath{ std::wstring{ game.clientJar } };
                if (!std::filesystem::exists(clientPath, ec))
                    return hstring{ L"Client jar is not cached for " } +
                        game.vanillaId +
                        hstring{ L". Connect once to download it." };

                // Collect vanilla natives + verify every classpath artifact.
                std::vector<std::filesystem::path> nativeZips;
                if (game.versionJson.HasKey(L"libraries"))
                {
                    for (auto const& lv : game.versionJson.GetNamedArray(L"libraries"))
                    {
                        if (lv.ValueType() != JsonValueType::Object)
                            continue;
                        auto lib = lv.GetObject();
                        if (!Rules::EntryAllowed(lib) || !lib.HasKey(L"downloads"))
                            continue;
                        hstring libName = OptStr(lib, L"name");
                        auto dl = lib.GetNamedObject(L"downloads");
                        if (dl.HasKey(L"artifact"))
                        {
                            try
                            {
                                auto art = dl.GetNamedObject(L"artifact");
                                hstring rel = OptStr(art, L"path");
                                if (rel.empty())
                                    continue;
                                auto dest = libsDir /
                                    std::filesystem::path{ std::wstring{ rel } };
                                if (!std::filesystem::exists(dest, ec))
                                    return hstring{ L"Missing library " } +
                                        (libName.empty() ? rel : libName) +
                                        hstring{ L". Connect once to download it." };
                                if (IsNativesEntry(std::wstring{ libName }))
                                    nativeZips.push_back(dest);
                            }
                            catch (...)
                            {
                            }
                        }
                        if (dl.HasKey(L"classifiers"))
                        {
                            try
                            {
                                auto cls = dl.GetNamedObject(L"classifiers");
                                hstring key = L"natives-windows";
                                if (lib.HasKey(L"natives"))
                                {
                                    try
                                    {
                                        auto natives = lib.GetNamedObject(L"natives");
                                        if (natives.HasKey(L"windows"))
                                            key = natives.GetNamedString(L"windows");
                                    }
                                    catch (...)
                                    {
                                    }
                                }
                                if (cls.HasKey(key))
                                {
                                    auto art = cls.GetNamedObject(key);
                                    hstring rel = OptStr(art, L"path");
                                    auto dest = libsDir /
                                        std::filesystem::path{ std::wstring{
                                            rel.empty() ? L"natives-legacy.jar" : rel } };
                                    if (!std::filesystem::exists(dest, ec))
                                        return L"Missing natives library. Connect once to download it.";
                                    nativeZips.push_back(dest);
                                }
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                }
                // Loader libraries become extraClasspath (same resolution as
                // the online path: vanilla-style artifact or maven coords).
                if (game.loaderProfile && game.loaderProfile.HasKey(L"libraries"))
                {
                    try
                    {
                        for (auto const& lv :
                            game.loaderProfile.GetNamedArray(L"libraries"))
                        {
                            if (lv.ValueType() != JsonValueType::Object)
                                continue;
                            auto lib = lv.GetObject();
                            if (!Rules::EntryAllowed(lib))
                                continue;
                            hstring name = OptStr(lib, L"name");
                            hstring base = OptStr(lib, L"url");
                            hstring rel;
                            bool have = false;
                            try
                            {
                                if (lib.HasKey(L"downloads"))
                                {
                                    auto dl = lib.GetNamedObject(L"downloads");
                                    if (dl.HasKey(L"artifact"))
                                    {
                                        auto art = dl.GetNamedObject(L"artifact");
                                        rel = OptStr(art, L"path");
                                        have = !rel.empty();
                                    }
                                }
                            }
                            catch (...)
                            {
                                have = false;
                            }
                            if (!have && !name.empty() && !base.empty())
                            {
                                hstring mrel = Fabric::MavenJarPath(name);
                                if (mrel.empty())
                                    continue;
                                rel = mrel;
                                have = true;
                            }
                            if (!have)
                                continue;
                            auto dest = libsDir /
                                std::filesystem::path{ std::wstring{ rel } };
                            if (!std::filesystem::exists(dest, ec))
                                return hstring{ L"Missing loader library " } +
                                    (name.empty() ? rel : name) +
                                    hstring{ L". Connect once to download it." };
                            game.extraClasspath.push_back(hstring{ dest.wstring() });
                        }
                    }
                    catch (...)
                    {
                    }
                }
                for (auto const& zip : nativeZips)
                {
                    if (!Http::UnzipWithTar(zip,
                            std::filesystem::path{ std::wstring{ game.nativesDir } }))
                        return L"Natives extract failed.";
                }
                // Asset index: warn-only. The game still starts with missing
                // objects (silent sounds / pink textures), which beats
                // refusing to launch at all on a plane with no wifi.
                try
                {
                    if (!game.assetIndexId.empty())
                    {
                        auto idx = gamePath / L"assets" / L"indexes" /
                            (std::wstring{ game.assetIndexId } + L".json");
                        if (!std::filesystem::exists(idx, ec) && log)
                            log(L"Offline: asset index is not cached (game may miss sounds).");
                    }
                }
                catch (...)
                {
                }
                // Logging config: optional. Launch without it when uncached.
                game.loggingPath = L"";
                try
                {
                    if (game.versionJson.HasKey(L"logging"))
                    {
                        auto file = game.versionJson.GetNamedObject(L"logging")
                            .GetNamedObject(L"client")
                            .GetNamedObject(L"file");
                        hstring id = OptStr(file, L"id");
                        if (!id.empty())
                        {
                            auto dest = versionsDir /
                                std::filesystem::path{ std::wstring{ id } };
                            if (std::filesystem::exists(dest, ec))
                                game.loggingPath = hstring{ dest.wstring() };
                        }
                    }
                }
                catch (...)
                {
                }
                return hstring{};
            }
            catch (...)
            {
                return L"Offline launch failed (cached files unreadable).";
            }
        }
    } // namespace

    fire_and_forget PrepareAsync(
        hstring mcVersion, hstring loader, hstring loaderVersion,
        std::wstring cacheDir, std::wstring instanceGameDir, hstring javaPathHint,
        LogFn log, FileProgFn prog, DoneFn done)
    {
        auto fail = [&](hstring const& msg) { done(false, PreparedGame{}, msg); };
        PreparedGame game{};
        // Per-instance work dir is known up front; the shared cache holds
        // everything versioned. Both are created here so later steps (and
        // offline builds) can rely on them existing.
        try
        {
            std::error_code ec0;
            std::filesystem::create_directories(std::filesystem::path{ instanceGameDir }, ec0);
            game.gameDir = hstring{ instanceGameDir };
            game.cacheDir = hstring{ cacheDir };
        }
        catch (...)
        {
        }
        try
        {
            bool isFabric = (loader == L"fabric");
            bool isQuilt = (loader == L"quilt");
            bool isForge = (loader == L"forge");
            bool isNeoForge = (loader == L"neoforge");
            bool isModded = isFabric || isQuilt || isForge || isNeoForge;
            hstring kindName = isForge ? hstring{ L"Forge" }
                : isNeoForge           ? hstring{ L"NeoForge" }
                : isQuilt              ? hstring{ L"Quilt" }
                                       : hstring{ L"Fabric" };
            hstring useLoader = loaderVersion;
            std::filesystem::path gamePathEarly{ cacheDir };
            if (isModded && useLoader.empty())
            {
                log(hstring{ L"Resolving " } + loader + hstring{ L" loader..." });
                try
                {
                    if (isFabric)
                        useLoader = co_await Fabric::GetLatestLoader(mcVersion);
                    else if (isQuilt)
                        useLoader = co_await Quilt::GetLatestLoader(mcVersion);
                    else if (isForge)
                        useLoader = co_await Forge::GetLatestForge(mcVersion);
                    else
                        useLoader = co_await NeoForge::GetLatestNeoForge(mcVersion);
                }
                catch (...)
                {
                }
                if (useLoader.empty())
                {
                    // Offline: reuse whatever loader is already cached so a
                    // previous online run can still launch with no internet.
                    bool haveCached = false;
                    try
                    {
                        if (isFabric || isQuilt)
                        {
                            auto cached = FindCachedLoaderFiles(gamePathEarly, mcVersion, loader);
                            if (!cached.empty())
                            {
                                useLoader = LoaderVersionFromCacheFile(cached.front(), loader);
                                haveCached = true;
                            }
                        }
                        else
                        {
                            auto prof = FindInstalledProfile(
                                gamePathEarly / L"versions", mcVersion,
                                isForge ? hstring{ L"forge" } : hstring{ L"neoforge" });
                            haveCached = HasLoaderMain(prof);
                            if (haveCached)
                                useLoader = L"cached";
                        }
                    }
                    catch (...)
                    {
                    }
                    if (!haveCached)
                    {
                        fail(hstring{ L"No " } + loader + hstring{ L" loader for " } +
                            mcVersion +
                            hstring{ L". Connect to the internet once to resolve it." });
                        co_return;
                    }
                    log(kindName + hstring{ L" loader " } + useLoader + hstring{ L" (cached, offline)" });
                }
                else
                {
                    log(kindName + hstring{ L" loader " } + useLoader);
                }
            }
            if (isFabric || isQuilt)
            {
                log(kindName + hstring{ L" profile..." });
                JsonObject prof{ nullptr };
                try
                {
                    if (isFabric)
                        prof = co_await Fabric::GetProfile(mcVersion, useLoader);
                    else
                        prof = co_await Quilt::GetProfile(mcVersion, useLoader);
                }
                catch (...)
                {
                }
                game.loaderProfile = prof;
                if (game.loaderProfile)
                {
                    // Cache for offline launches.
                    try
                    {
                        if (!useLoader.empty() && useLoader != hstring{ L"cached" })
                            WriteTextFile(
                                LoaderCachePath(gamePathEarly, mcVersion, loader, useLoader),
                                game.loaderProfile.Stringify());
                    }
                    catch (...)
                    {
                    }
                }
                if (!game.loaderProfile)
                {
                    // Offline fallback: any cached profile for this MC+loader.
                    try
                    {
                        JsonObject cached{ nullptr };
                        if (!useLoader.empty() && useLoader != hstring{ L"cached" })
                            cached = ReadProfileFile(
                                LoaderCachePath(gamePathEarly, mcVersion, loader, useLoader));
                        if (!HasLoaderMain(cached))
                        {
                            auto files = FindCachedLoaderFiles(gamePathEarly, mcVersion, loader);
                            if (!files.empty())
                            {
                                cached = ReadProfileFile(files.front());
                                try
                                {
                                    useLoader = LoaderVersionFromCacheFile(files.front(), loader);
                                }
                                catch (...)
                                {
                                }
                            }
                        }
                        game.loaderProfile = cached;
                    }
                    catch (...)
                    {
                    }
                    if (!HasLoaderMain(game.loaderProfile))
                    {
                        fail(kindName +
                            hstring{ L" profile fetch failed. Connect once to download it, then you can play offline." });
                        co_return;
                    }
                    log(kindName + hstring{ L" profile (cached, offline)" });
                }
            }

            hstring vanillaId = mcVersion;
            if ((isFabric || isQuilt) && game.loaderProfile.HasKey(L"inheritsFrom"))
            {
                try
                {
                    vanillaId = game.loaderProfile.GetNamedString(L"inheritsFrom");
                }
                catch (...)
                {
                }
            }
            game.vanillaId = vanillaId;

            Versions::McVersion entry{};
            entry.id = vanillaId;
            bool versionFromCache = false;
            try
            {
                Versions::Manifest manifest{};
                bool haveManifest = false;
                auto text = co_await Http::GetStringAsync(
                    L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json", kUA);
                auto root = JsonObject::Parse(text);
                if (root.HasKey(L"versions"))
                {
                    for (auto const& v : root.GetNamedArray(L"versions"))
                    {
                        if (v.ValueType() != JsonValueType::Object)
                            continue;
                        auto o = v.GetObject();
                        hstring id;
                        try
                        {
                            if (o.HasKey(L"id"))
                                id = o.GetNamedString(L"id");
                        }
                        catch (...)
                        {
                        }
                        if (id == vanillaId)
                        {
                            try
                            {
                                if (o.HasKey(L"url"))
                                    entry.url = o.GetNamedString(L"url");
                            }
                            catch (...)
                            {
                            }
                            haveManifest = true;
                            break;
                        }
                    }
                }
                (void)haveManifest;
            }
            catch (...)
            {
                // Offline below: entry.url stays empty on purpose.
            }
            if (!entry.url.empty())
            {
                log(hstring{ L"Fetching " } + vanillaId + L" package...");
                try
                {
                    auto version = co_await Versions::FetchVersionJsonAsync(entry);
                    if (version)
                    {
                        game.versionJson = version;
                        try
                        {
                            WriteTextFile(
                                VanillaJsonPath(gamePathEarly, vanillaId),
                                game.versionJson.Stringify());
                        }
                        catch (...)
                        {
                        }
                    }
                }
                catch (...)
                {
                }
            }
            if (!game.versionJson)
            {
                // Offline fallback: reuse the version JSON saved by the last
                // online run. Without it there is nothing to launch from.
                try
                {
                    game.versionJson = ReadProfileFile(
                        VanillaJsonPath(gamePathEarly, vanillaId));
                    if (!game.versionJson && vanillaId != mcVersion)
                        game.versionJson = ReadProfileFile(
                            VanillaJsonPath(gamePathEarly, mcVersion));
                }
                catch (...)
                {
                }
                if (!game.versionJson)
                {
                    if (entry.url.empty())
                        fail(hstring{ L"No internet connection and " } + vanillaId +
                            hstring{ L" was never downloaded. Connect once to download it, then you can play offline." });
                    else
                        fail(L"Version package fetch failed. Check your connection and retry.");
                    co_return;
                }
                versionFromCache = true;
                log(hstring{ L"Offline: using cached " } + vanillaId);
            }

            // Floor + legacy ceiling (jre-legacy / launchwrapper -> Java 8).
            // Re-applied after the forge/neoforge profile lands so a
            // launchwrapper loader entry point also pins to 8.
            ApplyJavaRule(game);

            std::filesystem::path gamePath{ cacheDir };
            std::filesystem::path versionsDir = gamePath / L"versions" / std::filesystem::path{ std::wstring{ vanillaId } };
            std::filesystem::path libsDir = gamePath / L"libraries";
            // Re-assert the split: cache holds versions/libraries, instance dir
            // is the work dir. BuildOfflineGame verifies cache files only.
            game.gameDir = hstring{ instanceGameDir };
            game.cacheDir = hstring{ cacheDir };

            if (versionFromCache)
            {
                // No network from here on: assemble the launch from disk.
                // Forge/NeoForge profiles live in versions/<id>/<id>.json
                // (written by their installer), so resolve them the same way.
                if (isForge || isNeoForge)
                {
                    hstring kindSub = isForge ? hstring{ L"forge" } : hstring{ L"neoforge" };
                    hstring expectedId{};
                    try
                    {
                        if (!useLoader.empty() && useLoader != hstring{ L"cached" })
                            expectedId = isForge
                                ? Forge::ExpectedVersionId(mcVersion, useLoader)
                                : NeoForge::ExpectedVersionId(mcVersion, useLoader);
                    }
                    catch (...)
                    {
                    }
                    JsonObject prof{ nullptr };
                    try
                    {
                        if (!expectedId.empty())
                        {
                            auto cand = gamePath / L"versions" /
                                std::filesystem::path{ std::wstring{ expectedId } } /
                                (std::wstring{ expectedId } + L".json");
                            prof = ReadProfileFile(cand);
                        }
                        if (!HasLoaderMain(prof))
                            prof = FindInstalledProfile(
                                gamePath / L"versions", mcVersion, kindSub);
                    }
                    catch (...)
                    {
                    }
                    game.loaderProfile = prof;
                    if (!HasLoaderMain(game.loaderProfile))
                    {
                        fail(kindName +
                            hstring{ L" is not installed for " } + mcVersion +
                            hstring{ L". Connect once to run the installer, then you can play offline." });
                        co_return;
                    }
                    log(kindName + hstring{ L" profile (cached, offline)" });
                }
                if (auto offlineErr = BuildOfflineGame(game, gamePath, libsDir, versionsDir, log);
                    !offlineErr.empty())
                {
                    fail(offlineErr);
                    co_return;
                }
                log(L"Ready (offline).");
                done(true, std::move(game), hstring{});
                co_return;
            }
            game.assetsDir = hstring{ (gamePath / L"assets").wstring() };
            game.gameDir = hstring{ instanceGameDir };
            game.cacheDir = hstring{ cacheDir };
            game.nativesDir = hstring{ (versionsDir / L"natives").wstring() };
            std::error_code ec;
            std::filesystem::create_directories(versionsDir, ec);
            std::filesystem::remove_all(game.nativesDir.c_str(), ec);
            std::filesystem::create_directories(std::filesystem::path{ std::wstring{ game.nativesDir } }, ec);

            if (isForge || isNeoForge)
            {
                // The installer writes versions/<id>/<id>.json (libraries +
                // mainClass + args); the profile below feeds the same library
                // and launch merging as the fabric/quilt profiles.
                hstring installerUrl = isForge
                    ? Forge::InstallerUrl(mcVersion, useLoader)
                    : NeoForge::InstallerUrl(useLoader);
                hstring expectedId = isForge
                    ? Forge::ExpectedVersionId(mcVersion, useLoader)
                    : NeoForge::ExpectedVersionId(mcVersion, useLoader);
                hstring kindSub = isForge ? hstring{ L"forge" } : hstring{ L"neoforge" };
                hstring installLogName =
                    isForge ? hstring{ L"forge-install.log" } : hstring{ L"neoforge-install.log" };
                std::filesystem::path loaderDir =
                    gamePath / L"versions" / std::filesystem::path{ std::wstring{ expectedId } };
                std::filesystem::path loaderJson =
                    loaderDir / (std::wstring{ expectedId } + L".json");
                std::filesystem::path installerJar = loaderDir / L"installer.jar";
                if (!HasLoaderMain(ReadProfileFile(loaderJson)))
                {
                    // Fast path missed: maybe a previous run installed under a
                    // different id scheme.
                    game.loaderProfile =
                        FindInstalledProfile(gamePath / L"versions", mcVersion, kindSub);
                }
                else
                {
                    game.loaderProfile = ReadProfileFile(loaderJson);
                }
                if (!HasLoaderMain(game.loaderProfile))
                {
                    game.loaderProfile = JsonObject{ nullptr };
                    if (useLoader == hstring{ L"cached" })
                    {
                        fail(kindName +
                            hstring{ L" is not installed for " } + mcVersion +
                            hstring{ L". Connect once to run the installer, then you can play offline." });
                        co_return;
                    }
                    if (installerUrl.empty() || expectedId.empty())
                    {
                        fail(kindName + hstring{ L" version not recognized for " } +
                            mcVersion + hstring{ L"." });
                        co_return;
                    }
                    log(kindName + hstring{ L" installer..." });
                    {
                        std::error_code ec2;
                        std::filesystem::remove(installerJar, ec2); // always a fresh copy
                    }
                    if (auto err = co_await FetchFile(installerUrl, installerJar, 0, hstring{},
                            kindName + hstring{ L" installer" }, log, prog);
                        !err.empty())
                    {
                        fail(err);
                        co_return;
                    }
                    log(hstring{ L"Locating Java for the installer..." });
                    hstring javaExe = javaPathHint;
                    if (!javaExe.empty() && !JavaFits(Java::Verify(javaExe).major, game.javaMajor, game.javaMax))
                        javaExe = L"";
                    if (javaExe.empty())
                    {
                        try
                        {
                            javaExe = Java::PickCapped(game.javaMajor, game.javaMax).path;
                        }
                        catch (...)
                        {
                        }
                    }
                    if (javaExe.empty())
                    {
                        // Forge/NeoForge installers need Java too: download a
                        // managed copy rather than failing the whole install.
                        // Legacy installers pin to Java 8 like the game does.
                        if (game.javaMax > 0)
                            log(hstring{ L"Java " } + to_hstring(game.javaMax) +
                                L" not found - downloading (one-time)...");
                        else
                            log(hstring{ L"Java " } + to_hstring(game.javaMajor) +
                                L"+ not found - downloading (one-time)...");
                        auto jprog = [prog](unsigned long long done, unsigned long long total, double bps) {
                            try
                            {
                                if (prog)
                                    prog(L"Java", done, total, bps);
                            }
                            catch (...)
                            {
                            }
                        };
                        try
                        {
                            javaExe = co_await Java::EnsureAsync(game.javaMajor, log, jprog, game.javaMax);
                        }
                        catch (...)
                        {
                            javaExe = L"";
                        }
                    }
                    if (javaExe.empty())
                    {
                        if (game.javaMax > 0)
                            fail(hstring{ L"This old version needs Java " } + to_hstring(game.javaMax) +
                                hstring{ L" for the " } + kindName +
                                hstring{ L" installer and auto-download failed. Check your connection or install a 64-bit Java 8 manually." });
                        else
                            fail(hstring{ L"No Java " } + to_hstring(game.javaMajor) +
                                hstring{ L"+ found for the " } + kindName +
                                hstring{ L" installer and auto-download failed. Check your connection or install a 64-bit Java manually." });
                        co_return;
                    }
                    try // the installer refuses to run without a profiles file
                    {
                        auto prof = gamePath / L"launcher_profiles.json";
                        std::error_code ec2;
                        if (!std::filesystem::exists(prof, ec2))
                        {
                            std::ofstream f(prof, std::ios::binary | std::ios::trunc);
                            if (f.good())
                                f << "{\"profiles\":{}}";
                        }
                    }
                    catch (...)
                    {
                    }
                    log(kindName + hstring{ L" installer running (can take a few minutes)..." });
                    prog(kindName + hstring{ L" installer" }, 0, 0, 0.0);
                    auto installLog = gamePath / L"logs-pretclient" / std::wstring{ installLogName };
                    int exit = co_await RunInstallerAsync(javaExe, installerJar, gamePath, installLog);
                    game.loaderProfile = ReadProfileFile(loaderJson);
                    if (!HasLoaderMain(game.loaderProfile))
                        game.loaderProfile =
                            FindInstalledProfile(gamePath / L"versions", mcVersion, kindSub);
                    if (!HasLoaderMain(game.loaderProfile))
                    {
                        fail(kindName + hstring{ L" installer failed (exit " } +
                            to_hstring(exit) + hstring{ L"). See " } +
                            hstring{ installLog.wstring() });
                        co_return;
                    }
                    try
                    {
                        std::error_code ec2;
                        std::filesystem::remove(installerJar, ec2);
                    }
                    catch (...)
                    {
                    }
                    log(kindName + hstring{ L" " } + useLoader + hstring{ L" ready." });
                }
                else
                {
                    log(kindName + hstring{ L" profile ready." });
                }
            }
            // Loader profile is final now: a launchwrapper entry point
            // (old Forge) pins the whole launch to Java 8 here.
            ApplyJavaRule(game);

            hstring clientUrl, clientSha1;
            long long clientSize = 0;
            try
            {
                auto dl = game.versionJson.GetNamedObject(L"downloads").GetNamedObject(L"client");
                clientUrl = OptStr(dl, L"url");
                clientSha1 = OptStr(dl, L"sha1");
                if (dl.HasKey(L"size"))
                    clientSize = static_cast<long long>(dl.GetNamedNumber(L"size"));
            }
            catch (...)
            {
            }
            game.clientJar = hstring{ (versionsDir / (std::wstring{ vanillaId } + L".jar")).wstring() };
            log(L"Client jar...");
            if (auto err = co_await FetchFile(clientUrl, std::filesystem::path{ std::wstring{ game.clientJar } },
                    clientSize, clientSha1, L"client.jar", log, prog);
                !err.empty())
            {
                fail(err);
                co_return;
            }

            log(L"Libraries...");
            std::vector<std::filesystem::path> nativeZips;
            std::set<std::wstring> seenLibs; // vanilla + loader share one download set
            FetchBatch libBatch{};
            libBatch.label = L"libraries";
            libBatch.log = log;
            libBatch.prog = prog;
            if (game.versionJson.HasKey(L"libraries"))
            {
                for (auto const& lv : game.versionJson.GetNamedArray(L"libraries"))
                {
                    if (lv.ValueType() != JsonValueType::Object)
                        continue;
                    auto lib = lv.GetObject();
                    if (!Rules::EntryAllowed(lib))
                        continue;
                    hstring name = OptStr(lib, L"name");
                    if (name.empty() || !lib.HasKey(L"downloads"))
                        continue;
                    auto dl = lib.GetNamedObject(L"downloads");
                    if (dl.HasKey(L"artifact"))
                    {
                        try
                        {
                            auto art = dl.GetNamedObject(L"artifact");
                            hstring rel = OptStr(art, L"path");
                            if (rel.empty())
                                continue;
                            hstring url = OptStr(art, L"url");
                            hstring sha1 = OptStr(art, L"sha1");
                            long long size = 0;
                            if (art.HasKey(L"size"))
                                size = static_cast<long long>(art.GetNamedNumber(L"size"));
                            auto dest = libsDir / std::filesystem::path{ std::wstring{ rel } };
                            libBatch.Add(url, dest, size, sha1,
                                hstring{ L"lib " } + std::wstring{ name });
                            seenLibs.insert(dest.wstring());
                            if (IsNativesEntry(std::wstring{ name }))
                                nativeZips.push_back(dest);
                        }
                        catch (...)
                        {
                        }
                    }
                    if (dl.HasKey(L"classifiers"))
                    {
                        try
                        {
                            auto cls = dl.GetNamedObject(L"classifiers");
                            hstring key = L"natives-windows";
                            if (lib.HasKey(L"natives"))
                            {
                                try
                                {
                                    auto natives = lib.GetNamedObject(L"natives");
                                    if (natives.HasKey(L"windows"))
                                        key = natives.GetNamedString(L"windows");
                                }
                                catch (...)
                                {
                                }
                            }
                            if (cls.HasKey(key))
                            {
                                auto art = cls.GetNamedObject(key);
                                hstring url = OptStr(art, L"url");
                                hstring sha1 = OptStr(art, L"sha1");
                                hstring rel = OptStr(art, L"path");
                                long long size = 0;
                                if (art.HasKey(L"size"))
                                    size = static_cast<long long>(art.GetNamedNumber(L"size"));
                                auto dest = libsDir / std::filesystem::path{ std::wstring{ rel.empty() ? L"natives-legacy.jar" : rel } };
                                libBatch.Add(url, dest, size, sha1, L"legacy natives");
                                seenLibs.insert(dest.wstring());
                                nativeZips.push_back(dest);
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }

            // Loader libraries: fabric/quilt maven entries plus forge/neoforge
            // installed entries (maven-style or vanilla-style artifacts).
            // Vanilla already queued its own set above, so anything already
            // covered is skipped here instead of being hashed twice.
            if (isModded && game.loaderProfile && game.loaderProfile.HasKey(L"libraries"))
            {
                log(kindName + hstring{ L" libraries..." });
                try
                {
                    for (auto const& lv : game.loaderProfile.GetNamedArray(L"libraries"))
                    {
                        if (lv.ValueType() != JsonValueType::Object)
                            continue;
                        auto lib = lv.GetObject();
                        if (!Rules::EntryAllowed(lib))
                            continue;
                        hstring name = OptStr(lib, L"name");
                        hstring base = OptStr(lib, L"url");
                        hstring rel, url, sha1;
                        long long size = 0;
                        bool have = false;
                        try
                        {
                            if (lib.HasKey(L"downloads"))
                            {
                                auto dl = lib.GetNamedObject(L"downloads");
                                if (dl.HasKey(L"artifact"))
                                {
                                    auto art = dl.GetNamedObject(L"artifact");
                                    rel = OptStr(art, L"path");
                                    url = OptStr(art, L"url");
                                    sha1 = OptStr(art, L"sha1");
                                    if (art.HasKey(L"size"))
                                        size = static_cast<long long>(art.GetNamedNumber(L"size"));
                                    have = !rel.empty() && !url.empty();
                                }
                            }
                        }
                        catch (...)
                        {
                            have = false;
                        }
                        if (!have && !name.empty() && !base.empty())
                        {
                            hstring mrel = Fabric::MavenJarPath(name);
                            if (mrel.empty())
                                continue;
                            if (base.back() != L'/')
                                base = base + L"/";
                            url = base + mrel;
                            rel = mrel;
                            sha1 = OptStr(lib, L"sha1");
                            size = 0;
                            try
                            {
                                if (lib.HasKey(L"size"))
                                    size = static_cast<long long>(lib.GetNamedNumber(L"size"));
                            }
                            catch (...)
                            {
                            }
                            have = true;
                        }
                        if (!have)
                            continue;
                        auto dest = libsDir / std::filesystem::path{ std::wstring{ rel } };
                        if (seenLibs.find(dest.wstring()) != seenLibs.end())
                            continue;
                        seenLibs.insert(dest.wstring());
                        libBatch.Add(url, dest, size, sha1,
                            hstring{ L"loader " } + (name.empty() ? rel : name));
                        game.extraClasspath.push_back(hstring{ dest.wstring() });
                    }
                }
                catch (...)
                {
                }
            }

            if (!libBatch.jobs.empty())
            {
                wchar_t nbuf[64]{};
                swprintf_s(nbuf, L"  %zu library file(s) to fetch", libBatch.jobs.size());
                log(nbuf);
                StartFetchBatch(libBatch, 10);
                co_await WaitAwaiter{ &libBatch.wg };
                libBatch.Report(true);
                if (!libBatch.error.empty())
                {
                    fail(libBatch.error);
                    co_return;
                }
            }

            log(L"Extracting natives...");
            for (auto const& zip : nativeZips)
            {
                if (!Http::UnzipWithTar(zip, std::filesystem::path{ std::wstring{ game.nativesDir } }))
                {
                    fail(L"Natives extract failed.");
                    co_return;
                }
            }

            hstring assetId;
            hstring assetUrl, assetSha1;
            long long assetSize = 0;
            try
            {
                auto ai = game.versionJson.GetNamedObject(L"assetIndex");
                assetId = OptStr(ai, L"id");
                assetUrl = OptStr(ai, L"url");
                assetSha1 = OptStr(ai, L"sha1");
                if (ai.HasKey(L"size"))
                    assetSize = static_cast<long long>(ai.GetNamedNumber(L"size"));
            }
            catch (...)
            {
            }
            if (assetId.empty())
            {
                fail(L"Version has no asset index.");
                co_return;
            }
            game.assetIndexId = assetId;
            auto indexPath = gamePath / L"assets" / L"indexes" / (std::wstring{ assetId } + L".json");
            log(L"Asset index...");
            if (auto err = co_await FetchFile(assetUrl, indexPath, assetSize, assetSha1, L"assets index", log, prog);
                !err.empty())
            {
                fail(err);
                co_return;
            }
            log(L"Asset objects...");
            try
            {
                std::ifstream f(indexPath, std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                auto index = JsonObject::Parse(to_hstring(text));
                auto objects = index.GetNamedObject(L"objects");
                size_t total = 0;
                for (auto const& kv : objects)
                    (void)kv, total++;
                FetchBatch assetBatch{};
                assetBatch.label = L"assets";
                assetBatch.log = log;
                assetBatch.prog = prog;
                for (auto const& kv : objects)
                {
                    auto o = kv.Value().GetObject();
                    hstring hash = OptStr(o, L"hash");
                    long long size = 0;
                    try
                    {
                        if (o.HasKey(L"size"))
                            size = static_cast<long long>(o.GetNamedNumber(L"size"));
                    }
                    catch (...)
                    {
                    }
                    if (hash.size() < 2)
                        continue;
                    std::wstring hs{ hash };
                    auto dest = gamePath / L"assets" / L"objects" / hs.substr(0, 2) / hs;
                    std::error_code ec2;
                    bool present = std::filesystem::exists(dest, ec2) &&
                        (size <= 0 || static_cast<long long>(std::filesystem::file_size(dest, ec2)) == size);
                    if (!present)
                    {
                        hstring url = hstring{ L"https://resources.download.minecraft.net/" } +
                            hs.substr(0, 2) + L"/" + hs;
                        assetBatch.Add(url, dest, size, hash,
                            hstring{ L"asset " } + std::wstring{ hash }.substr(0, 8));
                    }
                }
                wchar_t mbuf[96]{};
                swprintf_s(mbuf, L"  %zu of %zu asset(s) to fetch", assetBatch.jobs.size(), total);
                log(mbuf);
                if (!assetBatch.jobs.empty())
                {
                    StartFetchBatch(assetBatch, 10);
                    co_await WaitAwaiter{ &assetBatch.wg };
                    assetBatch.Report(true);
                    if (!assetBatch.error.empty())
                    {
                        fail(assetBatch.error);
                        co_return;
                    }
                }
            }
            catch (const std::exception& e)
            {
                fail(hstring{ L"Asset index parse failed: " } + DescribeException(e));
                co_return;
            }
            catch (...)
            {
                fail(L"Asset index parse failed: unknown error");
                co_return;
            }

            game.loggingPath = L"";
            try
            {
                if (game.versionJson.HasKey(L"logging"))
                {
                    auto file = game.versionJson.GetNamedObject(L"logging").GetNamedObject(L"client").GetNamedObject(L"file");
                    hstring id = OptStr(file, L"id");
                    hstring url = OptStr(file, L"url");
                    hstring sha1 = OptStr(file, L"sha1");
                    long long size = 0;
                    if (file.HasKey(L"size"))
                        size = static_cast<long long>(file.GetNamedNumber(L"size"));
                    if (!id.empty() && !url.empty())
                    {
                        auto dest = versionsDir / std::filesystem::path{ std::wstring{ id } };
                        if (auto err = co_await FetchFile(url, dest, size, sha1, L"logging config", log, prog);
                            !err.empty())
                        {
                            fail(err);
                            co_return;
                        }
                        game.loggingPath = hstring{ dest.wstring() };
                    }
                }
            }
            catch (...)
            {
            }

            if (isFabric)
            {
                log(L"Fabric API...");
                std::wstring target =
                    (std::filesystem::path{ std::wstring{ game.gameDir } } / L"mods").wstring();
                hstring msg = co_await Modrinth::EnsureFabricApiAsync(target, vanillaId);
                log(hstring{ L"  " } + msg);
            }

            log(L"Ready.");
        }
        catch (const std::exception& e)
        {
            fail(hstring{ L"Unexpected prepare failure: " } + DescribeException(e));
            co_return;
        }
        catch (...)
        {
            fail(L"Unexpected prepare failure: unknown error");
            co_return;
        }
        done(true, std::move(game), hstring{});
    }
}
