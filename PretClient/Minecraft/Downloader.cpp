#include "pch.h"
#include "Downloader.h"
#include "Fabric.h"
#include "Http.h"
#include "Modrinth.h"
#include "Rules.h"
#include "Versions.h"
#include <algorithm>
#include <coroutine>
#include <fstream>

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
            }
            b->wg.Done();
        }
    } // namespace

    fire_and_forget PrepareAsync(
        hstring mcVersion, hstring loader, hstring loaderVersion,
        std::wstring gameDir, std::wstring modsDir,
        LogFn log, FileProgFn prog, DoneFn done)
    {
        auto fail = [&](hstring const& msg) { done(false, PreparedGame{}, msg); };
        PreparedGame game{};
        try
        {
            bool isFabric = (loader == L"fabric");
            hstring useLoader = loaderVersion;
            if (isFabric && useLoader.empty())
            {
                log(L"Resolving fabric loader...");
                useLoader = co_await Fabric::GetLatestLoader(mcVersion);
                if (useLoader.empty())
                {
                    fail(L"No fabric loader for this version.");
                    co_return;
                }
                log(hstring{ L"Fabric loader " } + useLoader);
            }
            if (isFabric)
            {
                log(L"Fetching fabric profile...");
                game.fabricProfile = co_await Fabric::GetProfile(mcVersion, useLoader);
                if (!game.fabricProfile)
                {
                    fail(L"Fabric profile fetch failed.");
                    co_return;
                }
            }

            hstring vanillaId = mcVersion;
            if (isFabric && game.fabricProfile.HasKey(L"inheritsFrom"))
            {
                try
                {
                    vanillaId = game.fabricProfile.GetNamedString(L"inheritsFrom");
                }
                catch (...)
                {
                }
            }
            game.vanillaId = vanillaId;

            Versions::McVersion entry{};
            entry.id = vanillaId;
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
            catch (const std::exception& e)
            {
                fail(hstring{ L"Version manifest fetch failed: " } + DescribeException(e));
                co_return;
            }
            catch (...)
            {
                fail(L"Version manifest fetch failed: unknown error");
                co_return;
            }
            if (entry.url.empty())
            {
                fail(hstring{ L"Unknown Minecraft version " } + vanillaId);
                co_return;
            }

            log(hstring{ L"Fetching " } + vanillaId + L" package...");
            try
            {
                auto version = co_await Versions::FetchVersionJsonAsync(entry);
                if (!version)
                {
                    fail(L"Version package fetch failed.");
                    co_return;
                }
                game.versionJson = version;
            }
            catch (const std::exception& e)
            {
                fail(hstring{ L"Version package fetch failed: " } + DescribeException(e));
                co_return;
            }
            catch (...)
            {
                fail(L"Version package fetch failed: unknown error");
                co_return;
            }

            try
            {
                if (game.versionJson.HasKey(L"javaVersion"))
                    game.javaMajor = static_cast<int>(game.versionJson.GetNamedObject(L"javaVersion").GetNamedNumber(L"majorVersion"));
            }
            catch (...)
            {
            }
            if (game.javaMajor < 8)
                game.javaMajor = 8;

            std::filesystem::path gamePath{ gameDir };
            std::filesystem::path versionsDir = gamePath / L"versions" / std::filesystem::path{ std::wstring{ vanillaId } };
            std::filesystem::path libsDir = gamePath / L"libraries";
            game.assetsDir = hstring{ (gamePath / L"assets").wstring() };
            game.gameDir = hstring{ gamePath.wstring() };
            game.nativesDir = hstring{ (versionsDir / L"natives").wstring() };
            std::error_code ec;
            std::filesystem::create_directories(versionsDir, ec);
            std::filesystem::remove_all(game.nativesDir.c_str(), ec);
            std::filesystem::create_directories(std::filesystem::path{ std::wstring{ game.nativesDir } }, ec);

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
                                nativeZips.push_back(dest);
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }

            if (isFabric && game.fabricProfile.HasKey(L"libraries"))
            {
                log(L"Fabric libraries...");
                for (auto const& lv : game.fabricProfile.GetNamedArray(L"libraries"))
                {
                    if (lv.ValueType() != JsonValueType::Object)
                        continue;
                    auto lib = lv.GetObject();
                    hstring name = OptStr(lib, L"name");
                    hstring base = OptStr(lib, L"url");
                    if (name.empty() || base.empty())
                        continue;
                    hstring rel = Fabric::MavenJarPath(name);
                    if (rel.empty())
                        continue;
                    if (base.back() != L'/')
                        base = base + L"/";
                    hstring url = base + rel;
                    hstring sha1 = OptStr(lib, L"sha1");
                    long long size = 0;
                    try
                    {
                        if (lib.HasKey(L"size"))
                            size = static_cast<long long>(lib.GetNamedNumber(L"size"));
                    }
                    catch (...)
                    {
                    }
                    auto dest = libsDir / std::filesystem::path{ std::wstring{ rel } };
                    libBatch.Add(url, dest, size, sha1, hstring{ L"fabric " } + std::wstring{ name });
                    game.extraClasspath.push_back(hstring{ dest.wstring() });
                }
                game.fabricMainClass = OptStr(game.fabricProfile, L"mainClass");
                if (game.fabricProfile.HasKey(L"arguments"))
                {
                    try
                    {
                        auto args = game.fabricProfile.GetNamedObject(L"arguments");
                        if (args.HasKey(L"jvm"))
                        {
                            for (auto const& jv : args.GetNamedArray(L"jvm"))
                            {
                                if (jv.ValueType() == JsonValueType::String)
                                    game.fabricJvmExtras.push_back(jv.GetString());
                            }
                        }
                    }
                    catch (...)
                    {
                    }
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
                std::wstring target = modsDir.empty()
                    ? (std::filesystem::path{ std::wstring{ game.gameDir } } / L"mods").wstring()
                    : modsDir;
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
