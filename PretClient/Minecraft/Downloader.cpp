#include "pch.h"
#include "Downloader.h"
#include "Fabric.h"
#include "Http.h"
#include "Modrinth.h"
#include "Rules.h"
#include "Versions.h"
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

        // Download unless present+valid. Returns empty on success, error text otherwise.
        IAsyncOperation<hstring> FetchFile(hstring url, std::filesystem::path const& dest,
            long long size, hstring sha1, hstring what, LogFn log)
        {
            if (FileOk(dest, size, sha1))
            {
                log(L"  ok " + what);
                co_return hstring{};
            }
            if (url.empty())
                co_return hstring{ L"Missing URL for " } + what;
            log(L"  + " + what);
            std::vector<std::uint8_t> bytes;
            try
            {
                bytes = Http::BufferToVector(co_await Http::GetBufferAsync(url, kUA));
            }
            catch (...)
            {
                co_return hstring{ L"Download failed: " } + what;
            }
            if (size > 0 && static_cast<long long>(bytes.size()) != size)
                co_return hstring{ L"Size mismatch: " } + what;
            if (!Http::WriteFile(dest, bytes))
                co_return hstring{ L"Cannot write: " } + what;
            if (!sha1.empty() && !FileOk(dest, size, sha1))
                co_return hstring{ L"Checksum mismatch: " } + what;
            co_return hstring{};
        }

        bool IsNativesEntry(std::wstring const& name)
        {
            auto has = [&](wchar_t const* t) { return name.find(t) != std::wstring::npos; };
            if (name.find(L"natives") == std::wstring::npos)
                return false;
            // x64 build: bare natives-windows (+linux/osx variants are filtered by rules anyway).
            if (has(L"natives-windows-x86") || has(L"natives-windows-arm64"))
                return false;
            return true;
        }
    } // namespace

    fire_and_forget PrepareAsync(
        hstring mcVersion, hstring loader, hstring loaderVersion,
        std::wstring const& gameDir, LogFn log, DoneFn done)
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
                // Manifest fetch is callback-style; bridge with a shared slot.
                // (Simplest correct approach: fetch inline via the raw JSON.)
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
            }
            if (entry.url.empty())
            {
                fail(hstring{ L"Unknown Minecraft version " } + vanillaId);
                co_return;
            }

            log(hstring{ L"Fetching " } + vanillaId + L" package...");
            auto version = co_await Versions::FetchVersionJsonAsync(entry);
            if (!version)
            {
                fail(L"Version package fetch failed.");
                co_return;
            }
            game.versionJson = version;

            try
            {
                if (version.HasKey(L"javaVersion"))
                    game.javaMajor = static_cast<int>(version.GetNamedObject(L"javaVersion").GetNamedNumber(L"majorVersion"));
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
                auto dl = version.GetNamedObject(L"downloads").GetNamedObject(L"client");
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
                    clientSize, clientSha1, L"client.jar", log);
                !err.empty())
            {
                fail(err);
                co_return;
            }

            log(L"Libraries...");
            std::vector<std::filesystem::path> nativeZips;
            if (version.HasKey(L"libraries"))
            {
                for (auto const& lv : version.GetNamedArray(L"libraries"))
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
                            if (auto err = co_await FetchFile(url, dest, size, sha1,
                                    hstring{ L"lib " } + std::wstring{ name }, log);
                                !err.empty())
                            {
                                fail(err);
                                co_return;
                            }
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
                                if (auto err = co_await FetchFile(url, dest, size, sha1, L"legacy natives", log);
                                    !err.empty())
                                {
                                    fail(err);
                                    co_return;
                                }
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
                    if (auto err = co_await FetchFile(url, dest, size, sha1,
                            hstring{ L"fabric " } + std::wstring{ name }, log);
                        !err.empty())
                    {
                        fail(err);
                        co_return;
                    }
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
                auto ai = version.GetNamedObject(L"assetIndex");
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
            if (auto err = co_await FetchFile(assetUrl, indexPath, assetSize, assetSha1, L"assets index", log);
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
                size_t done = 0, total = 0;
                for (auto const& kv : objects)
                    (void)kv, total++;
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
                        hstring url = hstring{ L"https://resources.download.minecraft.net/" } + hs.substr(0, 2) + L"/" + hs;
                        std::vector<std::uint8_t> bytes;
                        try
                        {
                            bytes = Http::BufferToVector(co_await Http::GetBufferAsync(url, kUA));
                        }
                        catch (...)
                        {
                            fail(L"Asset fetch failed.");
                            co_return;
                        }
                        if (!Http::WriteFile(dest, bytes))
                        {
                            fail(L"Asset write failed.");
                            co_return;
                        }
                    }
                    if (++done % 200 == 0)
                        log(hstring{ L"  assets " } + to_hstring(done) + L"/" + to_hstring(total));
                }
            }
            catch (...)
            {
                fail(L"Asset index parse failed.");
                co_return;
            }

            game.loggingPath = L"";
            try
            {
                if (version.HasKey(L"logging"))
                {
                    auto file = version.GetNamedObject(L"logging").GetNamedObject(L"client").GetNamedObject(L"file");
                    hstring id = OptStr(file, L"id");
                    hstring url = OptStr(file, L"url");
                    hstring sha1 = OptStr(file, L"sha1");
                    long long size = 0;
                    if (file.HasKey(L"size"))
                        size = static_cast<long long>(file.GetNamedNumber(L"size"));
                    if (!id.empty() && !url.empty())
                    {
                        auto dest = versionsDir / std::filesystem::path{ std::wstring{ id } };
                        if (auto err = co_await FetchFile(url, dest, size, sha1, L"logging config", log);
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
                hstring msg = co_await Modrinth::EnsureFabricApiAsync(
                    std::filesystem::path{ std::wstring{ game.gameDir } }.wstring(), vanillaId);
                log(hstring{ L"  " } + msg);
            }

            log(L"Ready.");
        }
        catch (...)
        {
            fail(L"Unexpected prepare failure.");
            co_return;
        }
        done(true, std::move(game), hstring{});
    }
}
