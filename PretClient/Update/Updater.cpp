#include "pch.h"
#include "Updater.h"
#include <cwctype>
#include <shellapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;

namespace winrt::PretClient::Update
{
namespace
{
constexpr wchar_t kOwner[] = L"Hushayo";
constexpr wchar_t kRepo[] = L"PretClient";

std::vector<int> NumericParts(std::wstring s)
{
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V'))
        s = s.substr(1);
    if (auto dash = s.find(L'-'); dash != std::wstring::npos)
        s = s.substr(0, dash);
    std::vector<int> parts;
    size_t start = 0;
    while (start <= s.size())
    {
        auto dot = s.find(L'.', start);
        auto token = s.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start);
        try
        {
            parts.push_back(token.empty() ? 0 : std::stoi(token));
        }
        catch (...)
        {
            parts.push_back(0);
        }
        if (dot == std::wstring::npos)
            break;
        start = dot + 1;
    }
    return parts;
}

bool HasPrerelease(std::wstring const& s)
{
    auto t = s;
    if (!t.empty() && (t[0] == L'v' || t[0] == L'V'))
        t = t.substr(1);
    return t.find(L'-') != std::wstring::npos;
}
} // namespace

IAsyncOperation<JsonObject> GetLatestReleaseAsync()
{
    try
    {
        HttpClient client;
        client.DefaultRequestHeaders().Append(L"User-Agent", L"PretClient/1.0");
        client.DefaultRequestHeaders().Append(L"Accept", L"application/vnd.github+json");
        Uri uri{ std::wstring(L"https://api.github.com/repos/") + kOwner + L"/" + kRepo + L"/releases/latest" };
        hstring body = co_await client.GetStringAsync(uri);
        co_return JsonObject::Parse(body);
    }
    catch (...)
    {
        co_return nullptr;
    }
}

hstring CurrentVersionTag()
{
    return L"v1.0.0";
}

bool IsNewerTag(hstring const& current, hstring const& latest)
{
    if (latest.empty() || latest == current)
        return false;
    try
    {
        auto a = NumericParts(std::wstring{ current });
        auto b = NumericParts(std::wstring{ latest });
        auto n = (std::max)(a.size(), b.size());
        a.resize(n, 0);
        b.resize(n, 0);
        for (size_t i = 0; i < n; ++i)
        {
            if (b[i] != a[i])
                return b[i] > a[i];
        }
        // Same numbers: a real release beats a prerelease tag.
        return HasPrerelease(std::wstring{ current }) && !HasPrerelease(std::wstring{ latest });
    }
    catch (...)
    {
        return true;
    }
}

// Custom-setup flow: prefer the full installer (Setup.exe), then a
// portable zip, then any loose exe. Fall back to the release page.
hstring DownloadUrlFor(JsonObject const& release)
{
    hstring fallback;
    hstring setupUrl;
    hstring portableUrl;
    try
    {
        fallback = release.GetNamedString(L"html_url", L"");
        if (release.HasKey(L"assets"))
        {
            for (auto const& item : release.GetNamedArray(L"assets"))
            {
                auto asset = item.GetObject();
                auto name = asset.GetNamedString(L"name", L"");
                auto url = asset.GetNamedString(L"browser_download_url", L"");
                if (url.empty())
                    continue;
                std::wstring lower{ name };
                for (auto& c : lower)
                    c = static_cast<wchar_t>(towlower(c));
                auto ends = [&](wchar_t const* ext) {
                    size_t n = wcslen(ext);
                    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
                };
                bool isSetup = lower.find(L"setup") != std::wstring::npos && ends(L".exe");
                if (isSetup && setupUrl.empty())
                    setupUrl = url;
                else if ((ends(L".zip") || ends(L".exe")) && portableUrl.empty())
                    portableUrl = url;
            }
        }
    }
    catch (...)
    {
    }
    if (!setupUrl.empty())
        return setupUrl;
    if (!portableUrl.empty())
        return portableUrl;
    return fallback;
}

void OpenUrl(hstring const& url)
{
    if (!url.empty())
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool IsInstallerUrl(hstring const& url)
{
    std::wstring lower{ std::wstring{ url } };
    for (auto& c : lower)
        c = static_cast<wchar_t>(towlower(c));
    bool endsExe = lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0;
    return endsExe && lower.find(L"setup") != std::wstring::npos;
}
}
