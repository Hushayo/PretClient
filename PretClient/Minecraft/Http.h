#pragma once

#include <vector>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

// Tiny HTTP + hashing + unzip kit for the game/mod downloaders.
// Throws winrt::hresult_error on failure; callers translate to status text.
namespace winrt::PretClient::Http
{
    inline Windows::Foundation::IAsyncOperation<Windows::Web::Http::HttpResponseMessage> GetAsync(
        hstring url, hstring userAgent)
    {
        Windows::Web::Http::HttpClient client;
        try
        {
            client.DefaultRequestHeaders().Append(L"User-Agent", userAgent);
        }
        catch (...)
        {
        }
        co_return co_await client.GetAsync(Windows::Foundation::Uri{ url });
    }

    inline Windows::Foundation::IAsyncOperation<hstring> GetStringAsync(hstring url, hstring userAgent)
    {
        auto resp = co_await GetAsync(url, userAgent);
        resp.EnsureSuccessStatusCode();
        co_return co_await resp.Content().ReadAsStringAsync();
    }

    // IBuffer is a WinRT type, so it can cross coroutine boundaries.
    // Convert with BufferToVector at the call site.
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

    // Lowercase hex SHA1 of a file. Returns false on any IO/crypto failure.
    inline bool Sha1OfFile(std::filesystem::path const& path, std::wstring& outHex)
    {
        outHex.clear();
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<std::uint8_t> obj;
        std::vector<std::uint8_t> digest(20);
        std::ifstream f(path, std::ios::binary);
        if (!f.good())
            return false;
        bool ok = false;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0)
            return false;
        DWORD objLen = 0;
        DWORD read = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &read, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(alg, 0);
            return false;
        }
        obj.resize(objLen);
        if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(alg, 0);
            return false;
        }
        char chunk[65536];
        while (f.good())
        {
            f.read(chunk, sizeof(chunk));
            auto n = static_cast<ULONG>(f.gcount());
            if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk), n, 0) != 0)
                break;
            if (n == 0)
            {
                if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0)
                {
                    wchar_t hex[41]{};
                    for (size_t i = 0; i < digest.size(); ++i)
                        swprintf_s(hex + i * 2, 3, L"%02x", digest[i]);
                    outHex = hex;
                    ok = true;
                }
                break;
            }
        }
        if (hash)
            BCryptDestroyHash(hash);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);
        return ok;
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
