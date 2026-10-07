#include "pch.h"
#include "Launcher.h"
#include "Rules.h"
#include <bcrypt.h>
#include <mutex>

using namespace winrt;
using namespace Windows::Data::Json;

namespace winrt::PretClient::Launcher
{
    namespace
    {
        struct Session
        {
            HANDLE process = nullptr;
            DWORD pid = 0;
        };
        std::map<std::wstring, Session>& Sessions()
        {
            static std::map<std::wstring, Session> s;
            return s;
        }

        // Guards Sessions(): stats polling now runs on a background thread
        // while Start/Stop stay on the UI thread.
        std::mutex& SessionsMutex()
        {
            static std::mutex m;
            return m;
        }

        std::vector<std::uint8_t> Md5(std::string const& data)
        {
            std::vector<std::uint8_t> digest(16, 0);
            BCRYPT_ALG_HANDLE alg = nullptr;
            BCRYPT_HASH_HANDLE h = nullptr;
            if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0)
                return digest;
            DWORD objLen = 0;
            DWORD read = 0;
            if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &read, 0) == 0)
            {
                std::vector<std::uint8_t> obj(objLen);
                if (BCryptCreateHash(alg, &h, obj.data(), objLen, nullptr, 0, 0) == 0)
                {
                    BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                        static_cast<ULONG>(data.size()), 0);
                    BCryptFinishHash(h, digest.data(), static_cast<ULONG>(digest.size()), 0);
                    BCryptDestroyHash(h);
                }
            }
            BCryptCloseAlgorithmProvider(alg, 0);
            return digest;
        }

        void AppendArgs(std::vector<std::wstring>& out, JsonArray const& items,
            std::map<std::wstring, hstring> const& vars)
        {
            for (auto const& iv : items)
            {
                if (iv.ValueType() == JsonValueType::String)
                {
                    out.push_back(std::wstring{ Rules::Substitute(iv.GetString(), vars) });
                }
                else if (iv.ValueType() == JsonValueType::Object)
                {
                    try
                    {
                        auto o = iv.GetObject();
                        bool ok = true;
                        if (o.HasKey(L"rules"))
                            ok = Rules::RuleListAllows(o.GetNamedArray(L"rules"));
                        if (!ok || !o.HasKey(L"value"))
                            continue;
                        auto v = o.GetNamedValue(L"value");
                        if (v.ValueType() == JsonValueType::String)
                        {
                            out.push_back(std::wstring{ Rules::Substitute(v.GetString(), vars) });
                        }
                        else if (v.ValueType() == JsonValueType::Array)
                        {
                            for (auto const& sv : v.GetArray())
                            {
                                if (sv.ValueType() == JsonValueType::String)
                                    out.push_back(std::wstring{ Rules::Substitute(sv.GetString(), vars) });
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
        }

        std::wstring Quote(std::wstring const& s)
        {
            if (s.find(L' ') == std::wstring::npos && s.find(L'"') == std::wstring::npos)
                return s;
            std::wstring q = L"\"";
            for (auto c : s)
            {
                if (c == L'"')
                    q += L"\\\"";
                else
                    q += c;
            }
            return q + L"\"";
        }
    } // namespace

    hstring OfflineUuid(hstring username)
    {
        auto digest = Md5(to_string(username.empty() ? L"Steve" : username));
        // UUIDv3: version + variant bits (OfflinePlayer:<name> name-based).
        std::string src = "OfflinePlayer:" + to_string(username.empty() ? L"Steve" : username);
        digest = Md5(src);
        digest[6] = (digest[6] & 0x0F) | 0x30;
        digest[8] = (digest[8] & 0x3F) | 0x80;
        wchar_t buf[40]{};
        swprintf_s(buf,
            L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
            digest[6], digest[7], digest[8], digest[9],
            digest[10], digest[11], digest[12], digest[13], digest[14], digest[15]);
        return hstring{ buf };
    }

    Command BuildCommand(Downloader::PreparedGame const& game, hstring username, hstring uuid,
        int minMemMb, int maxMemMb, hstring javaExe)
    {
        Command cmd{};
        cmd.exe = javaExe;
        cmd.workDir = game.gameDir;

        std::wstring gameDir{ game.gameDir };
        std::wstring natives{ game.nativesDir };
        std::wstring assets{ game.assetsDir };
        std::wstring clientJar{ game.clientJar };

        // Classpath: vanilla libraries (allowed, non-native artifacts) in
        // manifest order, then loader extras, then the client jar last.
        std::wstring cp;
        auto pushCp = [&](std::wstring const& jar) {
            if (!cp.empty())
                cp += L";";
            cp += jar;
        };
        std::filesystem::path libsRoot = std::filesystem::path{ gameDir } / L"libraries";
        try
        {
            if (game.versionJson && game.versionJson.HasKey(L"libraries"))
            {
                for (auto const& lv : game.versionJson.GetNamedArray(L"libraries"))
                {
                    if (lv.ValueType() != JsonValueType::Object)
                        continue;
                    auto lib = lv.GetObject();
                    if (!Rules::EntryAllowed(lib) || !lib.HasKey(L"downloads"))
                        continue;
                    try
                    {
                        auto dl = lib.GetNamedObject(L"downloads");
                        if (!dl.HasKey(L"artifact"))
                            continue;
                        auto art = dl.GetNamedObject(L"artifact");
                        std::wstring rel;
                        try
                        {
                            rel = std::wstring{ art.GetNamedString(L"path") };
                        }
                        catch (...)
                        {
                            continue;
                        }
                        pushCp((libsRoot / rel).wstring());
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        catch (...)
        {
        }
        for (auto const& extra : game.extraClasspath)
            pushCp(std::wstring{ extra });
        pushCp(clientJar);

        hstring versionId = game.vanillaId;
        hstring assetIndex = game.assetIndexId;
        hstring versionType = L"release";
        hstring mainClass = L"net.minecraft.client.main.Main";
        try
        {
            if (game.versionJson.HasKey(L"type"))
                versionType = game.versionJson.GetNamedString(L"type");
            if (game.versionJson.HasKey(L"mainClass"))
                mainClass = game.versionJson.GetNamedString(L"mainClass");
        }
        catch (...)
        {
        }
        // Any loader profile (fabric/quilt/forge/neoforge) overrides the
        // entry point with its own Knot/Bootstrap launcher.
        try
        {
            if (game.loaderProfile && game.loaderProfile.HasKey(L"mainClass"))
            {
                hstring lm = game.loaderProfile.GetNamedString(L"mainClass");
                if (!lm.empty())
                    mainClass = lm;
            }
        }
        catch (...)
        {
        }

        std::map<std::wstring, hstring> vars{
            { L"auth_player_name", username },
            { L"version_name", versionId },
            { L"game_directory", hstring{ gameDir } },
            { L"assets_root", hstring{ assets } },
            { L"game_assets", hstring{ assets } },
            { L"assets_index_name", assetIndex },
            { L"auth_uuid", uuid },
            { L"auth_access_token", L"0" },
            { L"clientid", L"" },
            { L"auth_xuid", L"" },
            { L"user_type", L"legacy" },
            { L"version_type", versionType },
            { L"natives_directory", hstring{ natives } },
            { L"launcher_name", L"PretClient" },
            { L"launcher_version", L"0.0.1-dev" },
            { L"classpath", hstring{ cp } },
            { L"classpath_separator", L";" },
            { L"library_directory", hstring{ libsRoot.wstring() } },
            { L"primary_jar", hstring{ clientJar } },
        };

        std::vector<std::wstring> jvm;
        jvm.push_back(L"-Xms" + std::to_wstring(minMemMb) + L"M");
        jvm.push_back(L"-Xmx" + std::to_wstring(maxMemMb) + L"M");
        bool modern = false;
        try
        {
            modern = game.versionJson && game.versionJson.HasKey(L"arguments");
            if (modern)
            {
                auto args = game.versionJson.GetNamedObject(L"arguments");
                if (args.HasKey(L"jvm"))
                    AppendArgs(jvm, args.GetNamedArray(L"jvm"), vars);
            }
        }
        catch (...)
        {
        }
        // Loader JVM args (fabric's plain strings and forge/neoforge's
        // rules-aware entries alike) come after the vanilla ones.
        try
        {
            if (game.loaderProfile && game.loaderProfile.HasKey(L"arguments"))
            {
                auto largs = game.loaderProfile.GetNamedObject(L"arguments");
                if (largs.HasKey(L"jvm"))
                    AppendArgs(jvm, largs.GetNamedArray(L"jvm"), vars);
            }
        }
        catch (...)
        {
        }

        // -cp + main class (unless the JSON already carried them).
        bool hasCp = false;
        for (auto const& a : jvm)
        {
            if (a == L"-cp" || a == L"-classpath")
            {
                hasCp = true;
                break;
            }
        }
        std::vector<std::wstring> tail;
        if (!hasCp)
        {
            tail.push_back(L"-cp");
            tail.push_back(cp);
        }
        tail.push_back(std::wstring{ mainClass });

        std::vector<std::wstring> game_;
        try
        {
            if (modern)
            {
                auto args = game.versionJson.GetNamedObject(L"arguments");
                if (args.HasKey(L"game"))
                    AppendArgs(game_, args.GetNamedArray(L"game"), vars);
                // Loader game args (forge/neoforge launch targets; empty for
                // fabric/quilt) are appended after the vanilla ones.
                try
                {
                    if (game.loaderProfile && game.loaderProfile.HasKey(L"arguments"))
                    {
                        auto largs = game.loaderProfile.GetNamedObject(L"arguments");
                        if (largs.HasKey(L"game"))
                            AppendArgs(game_, largs.GetNamedArray(L"game"), vars);
                    }
                }
                catch (...)
                {
                }
            }
            else if (game.versionJson && game.versionJson.HasKey(L"minecraftArguments"))
            {
                // Legacy space-separated template. A loader template (old
                // forge tweakClass line) replaces the vanilla one outright:
                // it already carries the full argument set.
                hstring tplSrc = game.versionJson.GetNamedString(L"minecraftArguments");
                try
                {
                    if (game.loaderProfile && game.loaderProfile.HasKey(L"minecraftArguments"))
                    {
                        hstring lt = game.loaderProfile.GetNamedString(L"minecraftArguments");
                        if (!lt.empty())
                            tplSrc = lt;
                    }
                }
                catch (...)
                {
                }
                std::wstring tpl = std::wstring{ Rules::Substitute(tplSrc, vars) };
                size_t pos = 0;
                while (pos < tpl.size())
                {
                    while (pos < tpl.size() && tpl[pos] == L' ')
                        pos++;
                    if (pos >= tpl.size())
                        break;
                    size_t end = tpl.find(L' ', pos);
                    game_.push_back(tpl.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos));
                    if (end == std::wstring::npos)
                        break;
                    pos = end;
                }
                // Legacy builds synthesize what modern JSONs carry.
                jvm.insert(jvm.begin() + 2, L"-Djava.library.path=" + natives);
            }
        }
        catch (...)
        {
        }
        if (!game.loggingPath.empty())
        {
            jvm.push_back(L"-Dlog4j.configurationFile=" + std::wstring{ game.loggingPath });
        }

        std::wstring args;
        for (auto const& a : jvm)
            args += Quote(a) + L" ";
        for (auto const& a : tail)
            args += Quote(a) + L" ";
        for (auto const& a : game_)
        {
            if (a.empty())
                continue;
            args += Quote(a) + L" ";
        }
        if (!args.empty() && args.back() == L' ')
            args.pop_back();
        cmd.args = hstring{ args };
        return cmd;
    }

    bool Start(Command const& cmd, hstring const& instanceId, hstring& error)
    {
        error = L"";
        try
        {
            if (cmd.exe.empty())
            {
                error = L"No Java selected.";
                return false;
            }
            auto& sessions = Sessions();
            std::wstring key{ instanceId };
            {
                std::lock_guard<std::mutex> lk(SessionsMutex());
                auto it = sessions.find(key);
                if (it != sessions.end() && it->second.process)
                {
                    DWORD code = 0;
                    if (GetExitCodeProcess(it->second.process, &code) && code == STILL_ACTIVE)
                    {
                        error = L"Already running.";
                        return false;
                    }
                    CloseHandle(it->second.process);
                    sessions.erase(it);
                }
            }
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path{ std::wstring{ cmd.workDir } }, ec);
            auto logDir = std::filesystem::path{ std::wstring{ cmd.workDir } } / L"logs-pretclient";
            std::filesystem::create_directories(logDir, ec);
            auto logFile = logDir / L"latest.txt";
            HANDLE logH = CreateFileW(logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool logOk = (logH != nullptr && logH != INVALID_HANDLE_VALUE);
            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = logOk ? logH : GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError = si.hStdOutput;
            PROCESS_INFORMATION pi{};
            std::wstring line = Quote(std::wstring{ cmd.exe }) + L" " + std::wstring{ cmd.args };
            BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                nullptr, std::wstring{ cmd.workDir }.c_str(), &si, &pi);
            if (logOk)
                CloseHandle(logH);
            if (!ok)
            {
                error = L"Could not start Java.";
                return false;
            }
            CloseHandle(pi.hThread);
            {
                std::lock_guard<std::mutex> lk(SessionsMutex());
                Sessions()[key] = Session{ pi.hProcess, pi.dwProcessId };
            }
            return true;
        }
        catch (...)
        {
            error = L"Launch failed.";
            return false;
        }
    }

    void Stop(hstring const& instanceId)
    {
        try
        {
            std::lock_guard<std::mutex> lk(SessionsMutex());
            auto& sessions = Sessions();
            auto it = sessions.find(std::wstring{ instanceId });
            if (it == sessions.end())
                return;
            if (it->second.process)
            {
                TerminateProcess(it->second.process, 1);
                CloseHandle(it->second.process);
            }
            sessions.erase(it);
        }
        catch (...)
        {
        }
    }

    bool IsRunning(hstring const& instanceId)
    {
        try
        {
            std::lock_guard<std::mutex> lk(SessionsMutex());
            auto& sessions = Sessions();
            auto it = sessions.find(std::wstring{ instanceId });
            if (it == sessions.end() || !it->second.process)
                return false;
            DWORD code = 0;
            if (!GetExitCodeProcess(it->second.process, &code))
                return false;
            if (code != STILL_ACTIVE)
            {
                CloseHandle(it->second.process);
                sessions.erase(it);
                return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void* RawHandle(hstring const& instanceId)
    {
        try
        {
            std::lock_guard<std::mutex> lk(SessionsMutex());
            auto& sessions = Sessions();
            auto it = sessions.find(std::wstring{ instanceId });
            if (it == sessions.end())
                return nullptr;
            return it->second.process;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    unsigned long Pid(hstring const& instanceId)
    {
        try
        {
            std::lock_guard<std::mutex> lk(SessionsMutex());
            auto& sessions = Sessions();
            auto it = sessions.find(std::wstring{ instanceId });
            if (it == sessions.end())
                return 0;
            return it->second.pid;
        }
        catch (...)
        {
            return 0;
        }
    }
}
