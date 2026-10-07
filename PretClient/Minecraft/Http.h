#pragma once

#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <vector>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

// Tiny HTTP + hashing + unzip kit for the game/mod downloaders.
// Throws winrt::hresult_error on failure; callers translate to status text.
namespace winrt::PretClient::Http
{
    // One client per User-Agent, shared by every fetch. Connection reuse
    // (keep-alive) across thousands of tiny asset/library files: a fresh
    // client per file meant a full TCP+TLS handshake per file, which both
    // crawled and pegged a CPU core. All callers run on the UI thread today;
    // the mutex keeps it safe if that ever changes.
    inline Windows::Web::Http::HttpClient& ClientFor(hstring const& userAgent)
    {
        static std::mutex m;
        static std::map<std::wstring, Windows::Web::Http::HttpClient> clients;
        std::lock_guard<std::mutex> lk(m);
        std::wstring key{ userAgent };
        auto it = clients.find(key);
        if (it == clients.end())
        {
            Windows::Web::Http::HttpClient c;
            try
            {
                c.DefaultRequestHeaders().Append(L"User-Agent", userAgent);
            }
            catch (...)
            {
            }
            it = clients.emplace(std::move(key), std::move(c)).first;
        }
        return it->second;
    }

    inline Windows::Foundation::IAsyncOperation<Windows::Web::Http::HttpResponseMessage> GetAsync(
        hstring url, hstring userAgent)
    {
        co_return co_await ClientFor(userAgent).GetAsync(Windows::Foundation::Uri{ url });
    }

    inline Windows::Foundation::IAsyncOperation<hstring> GetStringAsync(hstring url, hstring userAgent)
    {
        auto resp = co_await GetAsync(url, userAgent);
        resp.EnsureSuccessStatusCode();
        co_return co_await resp.Content().ReadAsStringAsync();
    }

    // GET with an Authorization header (Minecraft profile/entitlements).
    // Throws hresult_error("HTTP <status> <body>") so callers can map errors.
    inline Windows::Foundation::IAsyncOperation<hstring> GetStringAuthAsync(
        hstring url, hstring userAgent, hstring authHeader)
    {
        try
        {
            Windows::Web::Http::HttpRequestMessage msg(
                Windows::Web::Http::HttpMethod::Get(), Windows::Foundation::Uri{ url });
            msg.Headers().Append(L"Authorization", authHeader);
            auto resp = co_await ClientFor(userAgent).SendRequestAsync(msg);
            hstring body = co_await resp.Content().ReadAsStringAsync();
            if (!resp.IsSuccessStatusCode())
            {
                wchar_t code[32]{};
                swprintf_s(code, L"HTTP %d ", static_cast<int>(resp.StatusCode()));
                throw hresult_error(E_FAIL, hstring{ code } + body);
            }
            co_return body;
        }
        catch (hresult_error const&)
        {
            throw;
        }
        catch (...)
        {
            throw hresult_error(E_FAIL, L"Request failed.");
        }
    }

    // POST a UTF-8 body (JSON or form-encoded) and return the response text.
    // Throws hresult_error("HTTP <status> <body>") so callers can map errors.
    inline Windows::Foundation::IAsyncOperation<hstring> PostStringAsync(
        hstring url, std::string const& utf8Body, hstring contentType, hstring userAgent)
    {
        try
        {
            Windows::Web::Http::HttpStringContent content(to_hstring(utf8Body),
                Windows::Storage::Streams::UnicodeEncoding::Utf8, contentType);
            auto resp = co_await ClientFor(userAgent).PostAsync(Windows::Foundation::Uri{ url }, content);
            hstring body = co_await resp.Content().ReadAsStringAsync();
            if (!resp.IsSuccessStatusCode())
            {
                wchar_t code[32]{};
                swprintf_s(code, L"HTTP %d ", static_cast<int>(resp.StatusCode()));
                throw hresult_error(E_FAIL, hstring{ code } + body);
            }
            co_return body;
        }
        catch (hresult_error const&)
        {
            throw;
        }
        catch (...)
        {
            throw hresult_error(E_FAIL, L"Request failed.");
        }
    }

    inline Windows::Foundation::IAsyncOperation<Windows::Storage::Streams::IBuffer> GetBufferAsync(
        hstring url, hstring userAgent)
    {
        auto resp = co_await GetAsync(url, userAgent);
        resp.EnsureSuccessStatusCode();
        co_return co_await resp.Content().ReadAsBufferAsync();
    }

    inline std::vector<std::uint8_t> BufferToVector(Windows::Storage::Streams::IBuffer const& buf)
    {
        std::vector<std::uint8_t> out(buf.Length());
        if (!out.empty())
            Windows::Storage::Streams::DataReader::FromBuffer(buf).ReadBytes(out);
        return out;
    }

    inline hstring Escape(hstring s)
    {
        return Windows::Foundation::Uri::EscapeComponent(s);
    }

    // Lowercase hex digest of a file with a CNG hash algorithm
    // (BCRYPT_SHA1_ALGORITHM / BCRYPT_SHA256_ALGORITHM). False on IO/crypto failure.
    inline bool HashFile(std::filesystem::path const& path, wchar_t const* algorithm,
        size_t digestLen, std::wstring& outHex)
    {
        outHex.clear();
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<std::uint8_t> digest(digestLen);
        std::ifstream f(path, std::ios::binary);
        if (!f.good())
            return false;
        bool ok = false;
        if (BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0) != 0)
            return false;
        DWORD objLen = 0;
        DWORD read = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &read, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(alg, 0);
            return false;
        }
        std::vector<std::uint8_t> obj(objLen);
        if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(alg, 0);
            return false;
        }
        char chunk[65536];
        bool readOk = true;
        for (;;)
        {
            f.read(chunk, sizeof(chunk));
            auto n = static_cast<ULONG>(f.gcount());
            if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk), n, 0) != 0)
            {
                readOk = false;
                break;
            }
            if (n < sizeof(chunk))
                break; // EOF (or empty file): finalize below.
        }
        if (readOk && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0)
        {
            std::wstring hex;
            wchar_t pair[3]{};
            for (size_t i = 0; i < digest.size(); ++i)
            {
                swprintf_s(pair, L"%02x", digest[i]);
                hex += pair;
            }
            outHex = hex;
            ok = true;
        }
        if (hash)
            BCryptDestroyHash(hash);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);
        return ok;
    }

    // Lowercase hex SHA1 of a file. Returns false on any IO/crypto failure.
    inline bool Sha1OfFile(std::filesystem::path const& path, std::wstring& outHex)
    {
        return HashFile(path, BCRYPT_SHA1_ALGORITHM, 20, outHex);
    }

    // Lowercase hex SHA256 of a file. Returns false on any IO/crypto failure.
    inline bool Sha256OfFile(std::filesystem::path const& path, std::wstring& outHex)
    {
        return HashFile(path, BCRYPT_SHA256_ALGORITHM, 32, outHex);
    }

    using ProgFn = std::function<void(unsigned long long done, unsigned long long total, double bytesPerSec)>;

    // Stream a URL straight to disk (no full-file RAM buffering) with
    // progress + speed callbacks. Returns "" on success, error text otherwise.
    inline Windows::Foundation::IAsyncOperation<hstring> DownloadToFileAsync(
        hstring url, std::filesystem::path const& dest, hstring userAgent, ProgFn prog)
    {
        try
        {
            auto resp = co_await GetAsync(url, userAgent);
            resp.EnsureSuccessStatusCode();
            unsigned long long total = 0;
            try
            {
                if (auto len = resp.Content().Headers().ContentLength())
                    total = len.Value();
            }
            catch (...)
            {
            }
            auto stream = co_await resp.Content().ReadAsInputStreamAsync();
            std::error_code ec;
            std::filesystem::create_directories(dest.parent_path(), ec);
            std::ofstream f(dest, std::ios::binary | std::ios::trunc);
            if (!f.good())
                co_return L"Cannot write file.";
            Windows::Storage::Streams::DataReader reader(stream);
            const std::uint32_t CH = 65536;
            std::vector<std::uint8_t> scratch(CH);
            unsigned long long done = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (;;)
            {
                std::uint32_t got = co_await reader.LoadAsync(CH);
                if (got == 0)
                    break;
                reader.ReadBytes({ scratch.data(), got });
                f.write(reinterpret_cast<char const*>(scratch.data()), got);
                if (!f)
                    co_return L"Write failed.";
                done += got;
                if (prog)
                {
                    double secs = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - t0)
                                          .count();
                    prog(done, total, secs > 0.05 ? done / secs : 0.0);
                }
            }
            f.close();
            try
            {
                reader.DetachStream();
            }
            catch (...)
            {
            }
            co_return hstring{};
        }
        catch (...)
        {
            co_return L"Download failed.";
        }
    }

    // Extract a zip (natives jar) with the inbox tar.exe, skipping META-INF/.
    inline bool UnzipWithTar(std::filesystem::path const& zip, std::filesystem::path const& dest)
    {
        try
        {
            std::error_code ec;
            std::filesystem::create_directories(dest, ec);
            std::wstring cmd = L"tar -xf \"" + zip.wstring() + L"\" -C \"" + dest.wstring() + L"\" --exclude=META-INF";
            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            std::wstring mutableCmd = cmd;
            if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                    nullptr, nullptr, &si, &pi))
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

    // Same as below but keeps raw bytes (used for mod icon PNGs).
    inline bool ZipEntryToBytes(std::filesystem::path const& zip,
        std::wstring const& entry, std::vector<std::uint8_t>& out)
    {
        try
        {
            out.clear();
            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE rd = nullptr;
            HANDLE wr = nullptr;
            if (!CreatePipe(&rd, &wr, &sa, 0))
                return false;
            SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si{ sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = wr;
            si.hStdInput = nullptr;
            si.hStdError = nullptr;
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"tar -xOf \"" + zip.wstring() + L"\" " + entry;
            std::wstring mutableCmd = cmd;
            bool ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            CloseHandle(wr);
            wr = nullptr;
            if (!ok)
            {
                CloseHandle(rd);
                return false;
            }
            char buf[8192];
            DWORD got = 0;
            while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0)
                out.insert(out.end(), buf, buf + got);
            CloseHandle(rd);
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return code == 0 && !out.empty();
        }
        catch (...)
        {
            return false;
        }
    }

    // Read one entry out of a zip (jars are zips) via the inbox tar.exe,
    // streaming to stdout through a pipe. Used to read mod metadata
    // (fabric.mod.json, quilt.mod.json).
    inline bool ZipEntryToString(std::filesystem::path const& zip,
        std::wstring const& entry, std::string& out)
    {
        out.clear();
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE rd = nullptr;
        HANDLE wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0))
            return false;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = wr;
        si.hStdInput = nullptr;
        si.hStdError = nullptr;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"tar -xOf \"" + zip.wstring() + L"\" " + entry;
        std::wstring mutableCmd = cmd;
        bool ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(wr);
        wr = nullptr;
        if (!ok)
        {
            CloseHandle(rd);
            return false;
        }
        char buf[8192];
        DWORD got = 0;
        while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0)
            out.append(buf, got);
        CloseHandle(rd);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return code == 0 && !out.empty();
    }

    inline bool WriteFile(std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes)
    {
        try
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f.good())
                return false;
            if (!bytes.empty())
                f.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            return static_cast<bool>(f);
        }
        catch (...)
        {
            return false;
        }
    }
}
