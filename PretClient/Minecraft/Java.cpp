#include "pch.h"
#include "Java.h"
#include "Http.h"
#include "../Paths.h"
#include <coroutine>
#include <future>

using namespace winrt;

namespace winrt::PretClient::Java
{
    namespace
    {
        void AddUnique(std::vector<hstring>& out, hstring const& p)
        {
            if (p.empty())
                return;
            for (auto const& q : out)
            {
                if (_wcsicmp(q.c_str(), p.c_str()) == 0)
                    return;
            }
            out.push_back(p);
        }

        std::wstring ReadOutput(HANDLE childOut, HANDLE childErr)
        {
            std::string raw;
            char buf[4096];
            DWORD n = 0;
            // java prints -version to stderr; read both ends until the process ends.
            for (;;)
            {
                DWORD avail = 0;
                BOOL okOut = PeekNamedPipe(childOut, nullptr, 0, nullptr, &avail, nullptr);
                if (okOut && avail > 0)
                {
                    DWORD got = 0;
                    if (ReadFile(childOut, buf, sizeof(buf), &got, nullptr) && got > 0)
                        raw.append(buf, got);
                }
                DWORD availErr = 0;
                BOOL okErr = PeekNamedPipe(childErr, nullptr, 0, nullptr, &availErr, nullptr);
                if (okErr && availErr > 0)
                {
                    DWORD got = 0;
                    if (ReadFile(childErr, buf, sizeof(buf), &got, nullptr) && got > 0)
                        raw.append(buf, got);
                }
                if ((!okOut || avail == 0) && (!okErr || availErr == 0))
                    break;
            }
            // Drain once more after exit is handled by the caller loop; convert loosely.
            std::wstring out;
            for (char c : raw)
                out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
            return out;
        }
    } // namespace

    Install Verify(hstring const& exePath)
    {
        Install bad{};
        try
        {
            if (exePath.empty())
                return bad;
            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
            if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0))
                return bad;
            SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = outW;
            si.hStdError = errW;
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + std::wstring{ exePath } + L"\" -version";
            BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                nullptr, nullptr, &si, &pi);
            CloseHandle(outW);
            CloseHandle(errW);
            if (!ok)
            {
                CloseHandle(outR);
                CloseHandle(errR);
                return bad;
            }
            WaitForSingleObject(pi.hProcess, 15000);
            std::wstring text = ReadOutput(outR, errR);
            // Drain remainder.
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(outR);
            CloseHandle(errR);

            Install in{};
            in.path = exePath;
            in.is64Bit = text.find(L"64-Bit") != std::wstring::npos;
            // `java version "1.8.0_421"` -> 8 ; `openjdk version "17.0.9"` -> 17
            auto q1 = text.find(L'"');
            if (q1 != std::wstring::npos)
            {
                auto q2 = text.find(L'"', q1 + 1);
                std::wstring ver = text.substr(q1 + 1, q2 == std::wstring::npos ? std::wstring::npos : q2 - q1 - 1);
                if (ver.rfind(L"1.", 0) == 0)
                {
                    in.major = 8;
                }
                else
                {
                    try
                    {
                        in.major = std::stoi(ver.substr(0, ver.find(L'.')));
                    }
                    catch (...)
                    {
                    }
                }
            }
            if (in.major > 0)
                return in;
        }
        catch (...)
        {
        }
        return bad;
    }

    std::vector<Install> FindAll()
    {
        std::vector<hstring> paths;
        try
        {
            // 1. Mojang bundled runtimes (any component the user ever downloaded).
            std::error_code ec;
            auto runtimes = Paths::DefaultGameDir() / L"runtime";
            if (std::filesystem::exists(runtimes, ec))
            {
                for (auto const& comp : std::filesystem::directory_iterator(runtimes, ec))
                {
                    if (!comp.is_directory(ec))
                        continue;
                    auto java = comp.path() / L"windows-x64" / comp.path().filename() / L"bin" / L"java.exe";
                    if (std::filesystem::exists(java, ec))
                        AddUnique(paths, hstring{ java.wstring() });
                }
            }
            // 2. JAVA_HOME.
            if (DWORD need = GetEnvironmentVariableW(L"JAVA_HOME", nullptr, 0); need > 1)
            {
                std::wstring home(need, L'\0');
                GetEnvironmentVariableW(L"JAVA_HOME", home.data(), need);
                home.resize(need - 1);
                auto java = std::filesystem::path{ home } / L"bin" / L"java.exe";
                if (std::filesystem::exists(java, ec))
                    AddUnique(paths, hstring{ java.wstring() });
            }
            // 3. PATH (`where java`).
            {
                SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
                HANDLE r = nullptr, w = nullptr;
                if (CreatePipe(&r, &w, &sa, 0))
                {
                    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
                    STARTUPINFOW si{ sizeof(si) };
                    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;
                    si.hStdOutput = w;
                    PROCESS_INFORMATION pi{};
                    std::wstring cmd = L"where java.exe";
                    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi))
                    {
                        WaitForSingleObject(pi.hProcess, 10000);
                        // Close OUR copy of the write end first: otherwise the
                        // read loop below can never see EOF and blocks forever
                        // (this hung Java detection on every launch).
                        CloseHandle(w);
                        w = nullptr;
                        char buf[4096];
                        DWORD got = 0;
                        std::string raw;
                        while (ReadFile(r, buf, sizeof(buf), &got, nullptr) && got > 0)
                            raw.append(buf, got);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        size_t pos = 0;
                        while (pos < raw.size())
                        {
                            auto nl = raw.find_first_of("\r\n", pos);
                            std::string line = raw.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                                line.pop_back();
                            if (!line.empty())
                            {
                                std::wstring wide(line.begin(), line.end());
                                if (std::filesystem::exists(wide, ec))
                                    AddUnique(paths, hstring{ wide });
                            }
                            if (nl == std::string::npos)
                                break;
                            pos = nl + 1;
                            if (paths.size() >= 8)
                                break;
                        }
                    }
                    if (w)
                        CloseHandle(w);
                    CloseHandle(r);
                }
            }
            // 4. Registry (Oracle + Adoptium + Microsoft), 64-bit and 32-bit views.
            const wchar_t* keys[] = {
                L"SOFTWARE\\JavaSoft\\JDK", L"SOFTWARE\\JavaSoft\\JRE",
                L"SOFTWARE\\Eclipse Adoptium\\JDK", L"SOFTWARE\\Eclipse Foundation\\JDK",
                L"SOFTWARE\\Microsoft\\JDK",
            };
            for (auto view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY })
            {
                for (auto key : keys)
                {
                    HKEY h = nullptr;
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | view, &h) != ERROR_SUCCESS)
                        continue;
                    DWORD idx = 0;
                    wchar_t sub[256];
                    DWORD subLen;
                    while (paths.size() < 16)
                    {
                        subLen = 256;
                        if (RegEnumKeyExW(h, idx++, sub, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                            break;
                        HKEY hs = nullptr;
                        if (RegOpenKeyExW(h, sub, 0, KEY_READ | view, &hs) != ERROR_SUCCESS)
                            continue;
                        wchar_t home[1024]{};
                        DWORD homeLen = sizeof(home);
                        if (RegQueryValueExW(hs, L"JavaHome", nullptr, nullptr,
                                reinterpret_cast<LPBYTE>(home), &homeLen) == ERROR_SUCCESS)
                        {
                            auto java = std::filesystem::path{ home } / L"bin" / L"java.exe";
                            if (std::filesystem::exists(java, ec))
                                AddUnique(paths, hstring{ java.wstring() });
                        }
                        RegCloseKey(hs);
                    }
                    RegCloseKey(h);
                }
            }
            // 5. Common install roots.
            const wchar_t* roots[] = {
                L"C:\\Program Files\\Java", L"C:\\Program Files\\Eclipse Adoptium",
                L"C:\\Program Files\\Microsoft", L"C:\\Program Files\\Zulu",
            };
            for (auto root : roots)
            {
                if (!std::filesystem::exists(root, ec))
                    continue;
                for (auto const& d : std::filesystem::directory_iterator(root, ec))
                {
                    if (!d.is_directory(ec))
                        continue;
                    auto java = d.path() / L"bin" / L"java.exe";
                    if (std::filesystem::exists(java, ec))
                        AddUnique(paths, hstring{ java.wstring() });
                    if (paths.size() >= 16)
                        break;
                }
            }
        }
        catch (...)
        {
        }
        std::vector<Install> out;
        // Probe candidates concurrently: each Verify spawns a process with
        // a long timeout, and sequential probing could stall for minutes
        // when several installs are present (or one hangs).
        std::vector<std::future<Install>> pending;
        for (auto const& p : paths)
            pending.push_back(std::async(std::launch::async, [p] { return Verify(p); }));
        for (auto& f : pending)
        {
            try
            {
                auto in = f.get();
                if (in.major > 0)
                    out.push_back(std::move(in));
            }
            catch (...)
            {
            }
            if (out.size() >= 12)
                break;
        }
        return out;
    }

    PickResult PickDetailed(int requiredMajor)
    {
        PickResult r{};
        try
        {
            auto all = FindAll();
            r.checked = static_cast<int>(all.size());
            Install const* best = nullptr;
            for (auto const& in : all)
            {
                if (in.major > r.bestMajor)
                    r.bestMajor = in.major;
                if (in.major < requiredMajor)
                    continue;
                if (!in.is64Bit && requiredMajor >= 17)
                    continue;
                if (!best || in.major < best->major)
                    best = &in;
            }
            if (best)
                r.path = best->path;
        }
        catch (...)
        {
        }
        return r;
    }

    hstring Pick(int requiredMajor)
    {
        return PickDetailed(requiredMajor).path;
    }

    int FeatureFor(int requiredMajor)
    {
        if (requiredMajor <= 8)
            return 8;
        if (requiredMajor <= 17)
            return 17;
        return 21;
    }

    std::filesystem::path ManagedRoot()
    {
        return Paths::DataDir() / L"java";
    }

    std::filesystem::path ManagedHome(int feature)
    {
        return ManagedRoot() / (L"temurin-" + std::to_wstring(feature));
    }

    hstring ManagedJava(int requiredMajor)
    {
        try
        {
            int need = requiredMajor < 8 ? 8 : requiredMajor;
            auto exe = ManagedHome(FeatureFor(need)) / L"bin" / L"java.exe";
            std::error_code ec;
            if (!std::filesystem::exists(exe, ec))
                return L"";
            auto v = Verify(hstring{ exe.wstring() });
            if (v.major >= need && !v.path.empty())
                return v.path;
        }
        catch (...)
        {
        }
        return L"";
    }

    namespace
    {
        constexpr wchar_t kJavaUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        bool ExtractZipWithTar(std::filesystem::path const& zip, std::filesystem::path const& dest)
        {
            try
            {
                std::error_code ec;
                std::filesystem::create_directories(dest, ec);
                std::wstring cmd = L"tar -xf \"" + zip.wstring() + L"\" -C \"" + dest.wstring() + L"\"";
                STARTUPINFOW si{ sizeof(si) };
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi{};
                std::wstring mutableCmd = cmd;
                if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                    return false;
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD code = 1;
                GetExitCodeProcess(pi.hProcess, &code);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return code == 0;
            }
            catch (...)
            {
                return false;
            }
        }

        // All bin/java.exe candidates under an extracted tree.
        std::vector<std::filesystem::path> FindJavaExes(std::filesystem::path const& root)
        {
            std::vector<std::filesystem::path> out;
            try
            {
                std::error_code ec;
                for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
                    it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                {
                    if (ec)
                        break;
                    try
                    {
                        auto const& p = it->path();
                        if (_wcsicmp(p.filename().c_str(), L"java.exe") != 0)
                            continue;
                        if (_wcsicmp(p.parent_path().filename().c_str(), L"bin") != 0)
                            continue;
                        out.push_back(p);
                        if (out.size() >= 8)
                            break;
                    }
                    catch (...)
                    {
                    }
                }
            }
            catch (...)
            {
            }
            return out;
        }

        // Blocking (tar + java -version probes): caller must already be off
        // the UI thread. Returns a verified java.exe under ManagedHome, or "".
        hstring InstallZip(std::filesystem::path const& zip, int need, int feature)
        {
            std::error_code ec;
            auto root = ManagedRoot();
            auto target = ManagedHome(feature);
            auto tmp = root / (L"temurin-" + std::to_wstring(feature) + L"-tmp");
            std::filesystem::remove_all(tmp, ec);
            std::filesystem::create_directories(tmp, ec);
            if (!ExtractZipWithTar(zip, tmp))
            {
                std::filesystem::remove_all(tmp, ec);
                std::filesystem::remove(zip, ec); // corrupt/incomplete: re-download next time
                return L"";
            }
            auto candidates = FindJavaExes(tmp);
            Install best{};
            std::filesystem::path bestExe{};
            for (auto const& exe : candidates)
            {
                try
                {
                    auto v = Verify(hstring{ exe.wstring() });
                    if (v.major <= 0)
                        continue;
                    if (v.major < need)
                    {
                        if (best.major <= 0 || v.major > best.major)
                        {
                            best = v;
                            bestExe = exe; // keep newest-but-too-old as fallback info
                        }
                        continue;
                    }
                    if (bestExe.empty() || v.major < best.major || best.major < need)
                    {
                        best = v;
                        bestExe = exe;
                    }
                }
                catch (...)
                {
                }
            }
            if (bestExe.empty() || best.major < need)
            {
                std::filesystem::remove_all(tmp, ec);
                std::filesystem::remove(zip, ec);
                return L"";
            }
            std::filesystem::path home;
            try
            {
                home = bestExe.parent_path().parent_path(); // .../bin/java.exe -> home
            }
            catch (...)
            {
                std::filesystem::remove_all(tmp, ec);
                return L"";
            }
            std::filesystem::remove_all(target, ec);
            std::error_code renameEc;
            std::filesystem::rename(home, target, renameEc);
            if (renameEc)
            {
                // Cross-volume or locked rename: copy the tree instead.
                try
                {
                    std::filesystem::create_directories(target, ec);
                    std::filesystem::copy(home, target,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        renameEc);
                }
                catch (...)
                {
                    renameEc = std::make_error_code(std::errc::io_error);
                }
                if (renameEc)
                {
                    std::filesystem::remove_all(tmp, ec);
                    return L"";
                }
            }
            std::filesystem::remove_all(tmp, ec);
            std::filesystem::remove(zip, ec); // save ~50-200MB once installed
            auto finalExe = target / L"bin" / L"java.exe";
            try
            {
                auto v = Verify(hstring{ finalExe.wstring() });
                if (v.major >= need && !v.path.empty())
                    return v.path;
            }
            catch (...)
            {
            }
            return L"";
        }
    } // namespace

    Windows::Foundation::IAsyncOperation<hstring> EnsureAsync(
        int requiredMajor, LogFn log, ProgFn prog)
    {
        auto say = [log](hstring const& s) {
            try
            {
                if (log)
                    log(s);
            }
            catch (...)
            {
            }
        };
        int need = requiredMajor < 8 ? 8 : requiredMajor;
        // 1. Already-downloaded managed copy (survives updates, no probing).
        try
        {
            auto m = ManagedJava(need);
            if (!m.empty())
                co_return m;
        }
        catch (...)
        {
        }
        // 2. Anything usable already on the system (fast, no download).
        try
        {
            auto p = PickDetailed(need);
            if (!p.path.empty())
                co_return p.path;
        }
        catch (...)
        {
        }
        // 3. Fetch Temurin (JRE first: ~50MB vs ~190MB JDK).
        int feature = FeatureFor(need);
        say(hstring{ L"Java " } + to_hstring(need) + L" not found - downloading Temurin " +
            to_hstring(feature) + L" (one-time, ~1 min)...");
        std::filesystem::path zip;
        try
        {
            std::error_code ec;
            std::filesystem::create_directories(ManagedRoot(), ec);
            zip = ManagedRoot() / (L"temurin-" + std::to_wstring(feature) + L".zip");
        }
        catch (...)
        {
            say(L"Java download failed (cannot create data folder).");
            co_return hstring{};
        }
        bool haveZip = false;
        try
        {
            std::error_code ec;
            if (std::filesystem::exists(zip, ec) &&
                std::filesystem::file_size(zip, ec) > 5ull * 1024 * 1024)
                haveZip = true;
        }
        catch (...)
        {
        }
        if (!haveZip)
        {
            std::wstring feat = std::to_wstring(feature);
            hstring urls[] = {
                hstring{ L"https://api.adoptium.net/v3/binary/latest/" + feat +
                    L"/ga/windows/x64/jre/hotspot/normal/eclipse" },
                hstring{ L"https://api.adoptium.net/v3/binary/latest/" + feat +
                    L"/ga/windows/x64/jdk/hotspot/normal/eclipse" },
            };
            hstring dlErr{ L"Download failed." };
            for (auto const& url : urls)
            {
                try
                {
                    std::error_code ec;
                    std::filesystem::remove(zip, ec);
                    dlErr = co_await Http::DownloadToFileAsync(url, zip, kJavaUA, prog);
                }
                catch (...)
                {
                    dlErr = L"Download failed.";
                }
                try
                {
                    std::error_code ec;
                    if (dlErr.empty() && std::filesystem::exists(zip, ec) &&
                        std::filesystem::file_size(zip, ec) > 5ull * 1024 * 1024)
                    {
                        haveZip = true;
                        break;
                    }
                    if (dlErr.empty())
                        dlErr = L"Download failed.";
                }
                catch (...)
                {
                }
            }
            if (!haveZip)
            {
                say(hstring{ L"Java download failed. Check your connection, or install a 64-bit Java " } +
                    to_hstring(need) + L"+ manually and set java.exe in Settings. (" +
                    (dlErr.empty() ? hstring{ L"error" } : dlErr) + L")");
                co_return hstring{};
            }
        }
        say(L"Extracting Java (one-time)...");
        // tar + java -version probes block: never on the UI thread. No
        // log/prog calls below this hop (callbacks are only safe on the
        // calling thread); the caller reports the outcome after awaiting.
        co_await winrt::resume_background();
        try
        {
            co_return InstallZip(zip, need, feature);
        }
        catch (...)
        {
            co_return hstring{};
        }
    }
}
