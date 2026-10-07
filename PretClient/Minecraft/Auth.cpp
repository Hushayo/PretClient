#include "pch.h"
#include "Auth.h"
#include "Http.h"
#include "Launcher.h"
#include "../Paths.h"
#include "../Settings.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <wincrypt.h>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Auth
{
    namespace
    {
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";
        constexpr wchar_t kDeviceCode[] =
            L"https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
        constexpr wchar_t kToken[] = L"https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
        constexpr wchar_t kScope[] = L"XboxLive.signin offline_access";
        constexpr wchar_t kXbox[] = L"https://user.auth.xboxlive.com/user/authenticate";
        constexpr wchar_t kXsts[] = L"https://xsts.auth.xboxlive.com/xsts/authorize";
        constexpr wchar_t kMcLogin[] = L"https://api.minecraftservices.com/authentication/login_with_xbox";
        constexpr wchar_t kEntitlements[] = L"https://api.minecraftservices.com/entitlements/mcstore";
        constexpr wchar_t kProfile[] = L"https://api.minecraftservices.com/minecraft/profile";
        constexpr wchar_t kJson[] = L"application/json";
        constexpr wchar_t kForm[] = L"application/x-www-form-urlencoded";
        // Mojang's own public Minecraft app. Authorized for Minecraft Services,
        // so sign-in works with zero setup; a custom Azure ID can override it.
        constexpr wchar_t kBuiltInClientId[] = L"00000000402b5328";

        struct Account
        {
            bool linked = false;
            hstring name{};
            hstring uuid{}; // dashed
            hstring xuid{};
            hstring mcToken{};
            long long mcExpiry = 0; // unix seconds
            hstring clientId{}; // azure app used at sign-in (needed to refresh)
            std::string msRefresh{}; // DPAPI-unprotected in memory only
        };

        std::filesystem::path StoreFile()
        {
            return Paths::DataDir() / L"account.json";
        }

        long long UnixNow()
        {
            FILETIME ft{};
            GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER u{};
            u.LowPart = ft.dwLowDateTime;
            u.HighPart = ft.dwHighDateTime;
            return static_cast<long long>(u.QuadPart / 10000000ULL - 11644473600ULL);
        }

        hstring OptStr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o && o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::String)
                    return o.GetNamedString(key);
            }
            catch (...)
            {
            }
            return hstring{};
        }

        // "HTTP <status> <body>" -> status code, 0 when no prefix.
        int HttpCode(std::wstring const& msg)
        {
            try
            {
                if (msg.rfind(L"HTTP ", 0) != 0)
                    return 0;
                return std::stoi(msg.substr(5));
            }
            catch (...)
            {
            }
            return 0;
        }

        std::wstring HttpBody(std::wstring const& msg)
        {
            auto sp = msg.find(L' ', 5);
            if (sp == std::wstring::npos)
                return L"";
            auto sp2 = msg.find(L' ', sp + 1);
            if (sp2 == std::wstring::npos)
                return L"";
            return msg.substr(sp2 + 1);
        }

        std::string FormEscape(std::string const& s)
        {
            return to_string(Http::Escape(to_hstring(s)));
        }

        hstring DashUuid(std::string const& plain)
        {
            try
            {
                std::string p;
                for (char c : plain)
                {
                    if (c != '-')
                        p += c;
                }
                if (p.size() != 32)
                    return hstring{};
                p.insert(20, "-");
                p.insert(16, "-");
                p.insert(12, "-");
                p.insert(8, "-");
                return to_hstring(p);
            }
            catch (...)
            {
            }
            return hstring{};
        }

        bool Protect(std::string const& plain, std::wstring& b64out)
        {
            b64out.clear();
            DATA_BLOB in{ static_cast<DWORD>(plain.size()),
                reinterpret_cast<BYTE*>(const_cast<char*>(plain.data())) };
            DATA_BLOB out{};
            if (!CryptProtectData(&in, L"PretClient MS refresh", nullptr, nullptr,
                    nullptr, 0, &out))
                return false;
            bool ok = false;
            DWORD cch = 0;
            if (CryptBinaryToStringW(out.pbData, out.cbData,
                    CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &cch) &&
                cch > 1)
            {
                std::wstring s(cch, 0);
                DWORD have = cch;
                if (CryptBinaryToStringW(out.pbData, out.cbData,
                        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, s.data(), &have) &&
                    have > 1)
                {
                    s.resize(have - 1); // trailing null counted in have
                    b64out = s;
                    ok = true;
                }
            }
            LocalFree(out.pbData);
            return ok;
        }

        bool Unprotect(std::wstring const& b64, std::string& plainOut)
        {
            plainOut.clear();
            DWORD cb = 0;
            if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &cb, nullptr, nullptr) ||
                cb == 0)
                return false;
            std::vector<BYTE> raw(cb);
            DWORD have = cb;
            if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, raw.data(), &have, nullptr, nullptr))
                return false;
            DATA_BLOB in{ have, raw.data() };
            DATA_BLOB out{};
            if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
                return false;
            plainOut.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
            LocalFree(out.pbData);
            return !plainOut.empty();
        }

        Account LoadAccount()
        {
            Account a{};
            try
            {
                std::error_code ec;
                if (!std::filesystem::exists(StoreFile(), ec))
                    return a;
                std::ifstream f(StoreFile(), std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (text.empty())
                    return a;
                auto o = JsonObject::Parse(to_hstring(text));
                a.name = OptStr(o, L"name");
                a.uuid = OptStr(o, L"uuid");
                a.xuid = OptStr(o, L"xuid");
                a.mcToken = OptStr(o, L"mcToken");
                a.clientId = OptStr(o, L"clientId");
                try
                {
                    if (o.HasKey(L"mcExpiry"))
                        a.mcExpiry = static_cast<long long>(o.GetNamedNumber(L"mcExpiry"));
                }
                catch (...)
                {
                }
                std::string rt;
                if (Unprotect(std::wstring{ OptStr(o, L"msRefresh") }, rt))
                    a.msRefresh = rt;
                a.linked = !a.name.empty() && !a.uuid.empty() && !a.msRefresh.empty();
            }
            catch (...)
            {
            }
            return a;
        }

        void SaveAccount(Account const& a)
        {
            try
            {
                JsonObject o{};
                o.SetNamedValue(L"name", JsonValue::CreateStringValue(a.name));
                o.SetNamedValue(L"uuid", JsonValue::CreateStringValue(a.uuid));
                o.SetNamedValue(L"xuid", JsonValue::CreateStringValue(a.xuid));
                o.SetNamedValue(L"mcToken", JsonValue::CreateStringValue(a.mcToken));
                o.SetNamedValue(L"clientId", JsonValue::CreateStringValue(a.clientId));
                o.SetNamedValue(L"mcExpiry", JsonValue::CreateNumberValue(static_cast<double>(a.mcExpiry)));
                std::wstring b64;
                if (Protect(a.msRefresh, b64))
                    o.SetNamedValue(L"msRefresh", JsonValue::CreateStringValue(hstring{ b64 }));
                std::ofstream f(StoreFile(), std::ios::binary | std::ios::trunc);
                f << to_string(o.Stringify());
            }
            catch (...)
            {
            }
        }

        hstring UhsOf(JsonObject const& o)
        {
            try
            {
                auto claims = o.GetNamedObject(L"DisplayClaims");
                auto xui = claims.GetNamedArray(L"xui");
                if (xui.Size() > 0)
                    return OptStr(xui.GetObjectAt(0), L"uhs");
            }
            catch (...)
            {
            }
            return hstring{};
        }

        long long ExpiresIn(JsonObject const& o, long long fallback)
        {
            try
            {
                if (o && o.HasKey(L"expires_in"))
                    return static_cast<long long>(o.GetNamedNumber(L"expires_in"));
            }
            catch (...)
            {
            }
            return fallback;
        }

        IAsyncOperation<JsonObject> DeviceCodeAsync(hstring clientId)
        {
            JsonObject out{ nullptr };
            std::string body = "client_id=" + to_string(clientId) +
                "&scope=" + FormEscape(to_string(hstring{ kScope }));
            out = JsonObject::Parse(co_await Http::PostStringAsync(kDeviceCode, body, kForm, kUA));
            co_return out;
        }

        // Polls until the user approves (or denies / the code expires).
        IAsyncOperation<JsonObject> PollDeviceTokenAsync(
            hstring clientId, hstring deviceCode, int intervalSecs, long long deadline)
        {
            JsonObject out{ nullptr };
            std::string body = "grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=" +
                to_string(clientId) + "&device_code=" + FormEscape(to_string(deviceCode));
            for (;;)
            {
                co_await winrt::resume_after(std::chrono::seconds(intervalSecs));
                if (UnixNow() >= deadline)
                    throw hresult_error(E_FAIL, L"The login code expired. Try again.");
                try
                {
                    out = JsonObject::Parse(co_await Http::PostStringAsync(kToken, body, kForm, kUA));
                    break; // approved
                }
                catch (hresult_error const& e)
                {
                    std::wstring msg{ e.message() };
                    if (msg.find(L"authorization_pending") != std::wstring::npos ||
                        msg.find(L"slow_down") != std::wstring::npos)
                        continue;
                    if (msg.find(L"expired_token") != std::wstring::npos)
                        throw hresult_error(E_FAIL, L"The login code expired. Try again.");
                    if (msg.find(L"access_denied") != std::wstring::npos)
                        throw hresult_error(E_FAIL, L"Microsoft login was denied. Try again.");
                    if (msg.find(L"invalid_grant") != std::wstring::npos)
                        throw hresult_error(E_FAIL, L"Microsoft login failed. Try again.");
                    throw hresult_error(E_FAIL, hstring{ L"Microsoft login failed: " } +
                        (HttpBody(msg).empty() ? msg : HttpBody(msg).substr(0, 200)));
                }
            }
            co_return out;
        }

        IAsyncOperation<JsonObject> RefreshMsAsync(hstring clientId, std::string refresh)
        {
            JsonObject out{ nullptr };
            try
            {
                std::string body = "client_id=" + to_string(clientId) + "&refresh_token=" +
                    FormEscape(refresh) + "&grant_type=refresh_token&scope=" +
                    FormEscape(to_string(hstring{ kScope }));
                out = JsonObject::Parse(co_await Http::PostStringAsync(kToken, body, kForm, kUA));
            }
            catch (...)
            {
                // Caller maps to "sign in again".
            }
            co_return out;
        }

        IAsyncOperation<JsonObject> XboxAsync(std::string msAccess)
        {
            JsonObject out{ nullptr };
            std::string body = "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":"
                               "\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=" +
                msAccess + "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";
            out = JsonObject::Parse(co_await Http::PostStringAsync(kXbox, body, kJson, kUA));
            co_return out;
        }

        hstring XErrMessage(long long xerr)
        {
            switch (xerr)
            {
            case 2148916233:
                return L"This Microsoft account has no Xbox profile yet. Sign in once at "
                       L"minecraft.net or the Xbox app, then try again.";
            case 2148916238:
                return L"Child account: it must join an adult's Xbox family before sign-in works.";
            case 2148916227:
                return L"This account is banned from Xbox.";
            case 2148916235:
                return L"Xbox Live is not available in this account's country.";
            case 2148916236:
            case 2148916237:
                return L"This account needs adult verification on the Xbox site first.";
            default:
                break;
            }
            wchar_t buf[96]{};
            swprintf_s(buf, L"Xbox sign-in refused this account (code %lld).", xerr);
            return hstring{ buf };
        }

        IAsyncOperation<JsonObject> XstsAsync(std::string xblToken)
        {
            JsonObject out{ nullptr };
            try
            {
                std::string body = "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" +
                    xblToken + "\"]},\"RelyingParty\":\"rp://api.minecraftservices.com/\","
                                "\"TokenType\":\"JWT\"}";
                out = JsonObject::Parse(co_await Http::PostStringAsync(kXsts, body, kJson, kUA));
            }
            catch (hresult_error const& e)
            {
                long long xerr = 0;
                try
                {
                    auto o = JsonObject::Parse(hstring{ HttpBody(std::wstring{ e.message() }) });
                    if (o.HasKey(L"XErr"))
                        xerr = static_cast<long long>(o.GetNamedNumber(L"XErr"));
                }
                catch (...)
                {
                }
                if (xerr != 0)
                    throw hresult_error(E_FAIL, XErrMessage(xerr));
                throw;
            }
            co_return out;
        }

        IAsyncOperation<JsonObject> McLoginAsync(std::string xstsToken, std::string uhs)
        {
            JsonObject out{ nullptr };
            try
            {
                std::string body = "{\"identityToken\":\"XBL3.0 x=" + uhs + ";" + xstsToken + "\"}";
                out = JsonObject::Parse(co_await Http::PostStringAsync(kMcLogin, body, kJson, kUA));
            }
            catch (hresult_error const& e)
            {
                if (HttpCode(std::wstring{ e.message() }) == 403)
                    throw hresult_error(E_FAIL,
                        L"Minecraft rejected this app registration (403). New Azure apps must be "
                        L"approved for Minecraft Services: apply at aka.ms/mce-reviewappid, then try again.");
                throw;
            }
            co_return out;
        }

        IAsyncOperation<bool> OwnsGameAsync(hstring mcToken)
        {
            bool owns = false;
            try
            {
                auto text = co_await Http::GetStringAuthAsync(
                    kEntitlements, kUA, hstring{ L"Bearer " } + mcToken);
                auto root = JsonObject::Parse(text);
                if (root.HasKey(L"items"))
                {
                    for (auto const& v : root.GetNamedArray(L"items"))
                    {
                        try
                        {
                            if (v.ValueType() == JsonValueType::Object &&
                                OptStr(v.GetObject(), L"name") == L"game_minecraft")
                            {
                                owns = true;
                                break;
                            }
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
            co_return owns;
        }

        IAsyncOperation<JsonObject> FetchProfileAsync(hstring mcToken)
        {
            JsonObject out{ nullptr };
            try
            {
                out = JsonObject::Parse(co_await Http::GetStringAuthAsync(
                    kProfile, kUA, hstring{ L"Bearer " } + mcToken));
            }
            catch (hresult_error const& e)
            {
                std::wstring msg{ e.message() };
                if (msg.find(L"NOT_FOUND") != std::wstring::npos)
                    throw hresult_error(E_FAIL,
                        L"No Minecraft profile on this account. Game Pass users: open the official "
                        L"launcher once to set up your username, then try again.");
                throw hresult_error(E_FAIL, L"Could not fetch the Minecraft profile.");
            }
            co_return out;
        }

        // Full Xbox chain off one MS access token. Throws friendly errors.
        IAsyncOperation<JsonObject> McAuthChainAsync(std::string msAccess, std::string& uhsOut)
        {
            JsonObject mc{ nullptr };
            auto xbox = co_await XboxAsync(msAccess);
            std::string xbl = to_string(OptStr(xbox, L"Token"));
            std::string uhs = to_string(UhsOf(xbox));
            if (xbl.empty() || uhs.empty())
                throw hresult_error(E_FAIL, L"Xbox sign-in returned no token.");
            auto xsts = co_await XstsAsync(xbl);
            std::string xt = to_string(OptStr(xsts, L"Token"));
            if (xt.empty())
                throw hresult_error(E_FAIL, L"Xbox authorization returned no token.");
            mc = co_await McLoginAsync(xt, uhs);
            uhsOut = uhs;
            co_return mc;
        }

        void CopyToClipboard(hstring const& text)
        {
            try
            {
                std::wstring s{ text };
                size_t bytes = (s.size() + 1) * sizeof(wchar_t);
                if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
                {
                    memcpy(GlobalLock(mem), s.c_str(), bytes);
                    GlobalUnlock(mem);
                    if (OpenClipboard(nullptr))
                    {
                        EmptyClipboard();
                        SetClipboardData(CF_UNICODETEXT, mem);
                        CloseClipboard();
                    }
                    else
                    {
                        GlobalFree(mem);
                    }
                }
            }
            catch (...)
            {
            }
        }
    } // namespace

    bool HasMicrosoft()
    {
        try
        {
            return LoadAccount().linked;
        }
        catch (...)
        {
        }
        return false;
    }

    hstring AccountName()
    {
        try
        {
            return LoadAccount().name;
        }
        catch (...)
        {
        }
        return hstring{};
    }

    Windows::Foundation::IAsyncOperation<bool> EnsureSessionAsync()
    {
        bool ok = false;
        try
        {
            auto acc = LoadAccount();
            if (!acc.linked)
                co_return false;
            if (UnixNow() < acc.mcExpiry - 300 && !to_string(acc.mcToken).empty())
                co_return true;
            if (acc.clientId.empty() || acc.msRefresh.empty())
                co_return false;
            auto ms = co_await RefreshMsAsync(acc.clientId, acc.msRefresh);
            std::string access = to_string(OptStr(ms, L"access_token"));
            std::string refresh = to_string(OptStr(ms, L"refresh_token"));
            if (access.empty() || refresh.empty())
                co_return false;
            std::string uhs;
            auto mc = co_await McAuthChainAsync(access, uhs);
            std::string mcToken = to_string(OptStr(mc, L"access_token"));
            if (mcToken.empty())
                co_return false;
            acc.mcToken = to_hstring(mcToken);
            acc.mcExpiry = UnixNow() + ExpiresIn(mc, 86400);
            acc.msRefresh = refresh;
            if (!uhs.empty())
                acc.xuid = to_hstring(uhs);
            SaveAccount(acc);
            ok = true;
        }
        catch (...)
        {
        }
        co_return ok;
    }

    Session LaunchSession()
    {
        Session s{};
        try
        {
            auto acc = LoadAccount();
            if (acc.linked && !to_string(acc.mcToken).empty() && UnixNow() < acc.mcExpiry - 60)
            {
                s.microsoft = true;
                s.username = acc.name;
                s.uuid = acc.uuid;
                s.token = acc.mcToken;
                s.xuid = acc.xuid;
                s.userType = L"msa";
                return s;
            }
        }
        catch (...)
        {
        }
        // Offline: classic non-premium session, unchanged behavior.
        auto st = LoadSettings();
        s.username = st.username.empty() ? hstring{ L"Steve" } : st.username;
        s.uuid = Launcher::OfflineUuid(s.username);
        s.token = L"0";
        s.userType = L"legacy";
        return s;
    }

    fire_and_forget SignInAsync(hstring clientId, StatusFn status, DoneFn done)
    {
        auto ui = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        auto say = [ui, status](hstring msg) {
            try
            {
                ui.TryEnqueue([status, msg] {
                    try
                    {
                        if (status)
                            status(msg);
                    }
                    catch (...)
                    {
                    }
                });
            }
            catch (...)
            {
            }
        };
        auto finish = [ui, done](bool ok, hstring msg) {
            try
            {
                ui.TryEnqueue([done, ok, msg] {
                    try
                    {
                        if (done)
                            done(ok, msg);
                    }
                    catch (...)
                    {
                    }
                });
            }
            catch (...)
            {
            }
        };
        // Empty = built-in shared Minecraft app: zero setup.
        if (to_string(clientId).empty())
            clientId = kBuiltInClientId;
        try
        {
            say(L"Requesting a login code...");
            co_await winrt::resume_background();
            auto dc = co_await DeviceCodeAsync(clientId);
            hstring userCode = OptStr(dc, L"user_code");
            hstring deviceCode = OptStr(dc, L"device_code");
            hstring verifyUri = OptStr(dc, L"verification_uri");
            long long expires = 900;
            int interval = 5;
            try
            {
                if (dc.HasKey(L"expires_in"))
                    expires = static_cast<long long>(dc.GetNamedNumber(L"expires_in"));
                if (dc.HasKey(L"interval"))
                    interval = static_cast<int>(dc.GetNamedNumber(L"interval"));
            }
            catch (...)
            {
            }
            if (userCode.empty() || deviceCode.empty())
                throw hresult_error(E_FAIL, L"Microsoft refused the login request. Check the client ID.");
            if (verifyUri.empty())
                verifyUri = L"https://www.microsoft.com/link";
            CopyToClipboard(userCode);
            say(hstring{ L"Code " } + userCode + L" copied - enter it at " + verifyUri + L", then approve.");
            auto ms = co_await PollDeviceTokenAsync(
                clientId, deviceCode, (std::max)(5, interval), UnixNow() + expires - 30);
            std::string access = to_string(OptStr(ms, L"access_token"));
            std::string refresh = to_string(OptStr(ms, L"refresh_token"));
            if (access.empty() || refresh.empty())
                throw hresult_error(E_FAIL, L"Microsoft returned no tokens.");
            say(L"Checking Xbox...");
            std::string uhs;
            auto mc = co_await McAuthChainAsync(access, uhs);
            std::string mcToken = to_string(OptStr(mc, L"access_token"));
            if (mcToken.empty())
                throw hresult_error(E_FAIL, L"Minecraft login returned no token.");
            say(L"Checking game ownership...");
            if (!co_await OwnsGameAsync(to_hstring(mcToken)))
                throw hresult_error(E_FAIL,
                    L"This account does not own Minecraft Java Edition.");
            say(L"Fetching profile...");
            auto prof = co_await FetchProfileAsync(to_hstring(mcToken));
            hstring name = OptStr(prof, L"name");
            hstring uuid = DashUuid(to_string(OptStr(prof, L"id")));
            if (name.empty() || uuid.empty())
                throw hresult_error(E_FAIL, L"Could not read the Minecraft profile.");
            Account acc{};
            acc.linked = true;
            acc.name = name;
            acc.uuid = uuid;
            acc.xuid = to_hstring(uhs);
            acc.mcToken = to_hstring(mcToken);
            acc.mcExpiry = UnixNow() + ExpiresIn(mc, 86400);
            acc.clientId = clientId;
            acc.msRefresh = refresh;
            SaveAccount(acc);
            finish(true, hstring{ L"Signed in as " } + name);
        }
        catch (hresult_error const& e)
        {
            finish(false, e.message().empty() ? hstring{ L"Sign-in failed." } : e.message());
        }
        catch (...)
        {
            finish(false, L"Sign-in failed.");
        }
    }

    void SignOut()
    {
        try
        {
            std::error_code ec;
            std::filesystem::remove(StoreFile(), ec);
        }
        catch (...)
        {
        }
    }
}
