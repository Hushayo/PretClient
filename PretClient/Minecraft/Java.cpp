#include "pch.h"
#include "Java.h"
#include "../Paths.h"
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
}
