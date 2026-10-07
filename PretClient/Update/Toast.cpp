#include "pch.h"
#include "Toast.h"
#include <algorithm>
#include <chrono>
#include <shlobj.h>
#include <propkey.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

using namespace winrt;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

namespace winrt::PretClient::Update::Toast
{
    namespace
    {
        constexpr wchar_t kAumid[] = L"Hushayo.PretClient";
        constexpr wchar_t kTag[] = L"pretclient-update";
        constexpr wchar_t kGroup[] = L"updates";

        std::wstring Escape(std::wstring s)
        {
            auto rep = [&](std::wstring const& from, std::wstring const& to) {
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::wstring::npos)
                {
                    s.replace(pos, from.size(), to);
                    pos += to.size();
                }
            };
            rep(L"&", L"&amp;");
            rep(L"<", L"&lt;");
            rep(L">", L"&gt;");
            rep(L"\"", L"&quot;");
            return s;
        }

        // Explicit identity for an unpackaged process. Once per process.
        void EnsureIdentity()
        {
            static bool done = false;
            if (done)
                return;
            done = true;
            try
            {
                SetCurrentProcessExplicitAppUserModelId(kAumid);
            }
            catch (...)
            {
            }
        }

        // The Start-menu shortcut must carry the same AppUserModelID or the
        // shell drops our toasts. Stamp it at runtime (best-effort): this
        // also repairs installs from before the protocol/identity work.
        // Verified state is cached; a missing shortcut is retried cheaply
        // (one exists-check) since a later install may create it.
        void EnsureShortcutAppId()
        {
            static bool stamped = false;
            if (stamped)
                return;
            try
            {
                PWSTR raw = nullptr;
                if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &raw)))
                    return;
                std::filesystem::path lnk{ raw };
                CoTaskMemFree(raw);
                lnk /= L"PretClient";
                lnk /= L"PretClient.lnk";
                std::error_code ec;
                if (!std::filesystem::exists(lnk, ec))
                    return;
                com_ptr<IShellLinkW> link;
                if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                        __uuidof(IShellLinkW), link.put_void())))
                    return;
                com_ptr<IPersistFile> persist = link.as<IPersistFile>();
                if (FAILED(persist->Load(lnk.c_str(), STGM_READWRITE)))
                    return;
                com_ptr<IPropertyStore> store = link.as<IPropertyStore>();
                PROPVARIANT current{};
                bool already = false;
                if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &current)))
                {
                    if (current.vt == VT_LPWSTR && current.pwszVal &&
                        wcscmp(current.pwszVal, kAumid) == 0)
                        already = true;
                    PropVariantClear(&current);
                }
                if (already)
                {
                    stamped = true;
                    return;
                }
                PROPVARIANT pv{};
                pv.vt = VT_LPWSTR;
                size_t len = wcslen(kAumid) + 1;
                pv.pwszVal = static_cast<wchar_t*>(CoTaskMemAlloc(len * sizeof(wchar_t)));
                if (!pv.pwszVal)
                    return;
                wcscpy_s(pv.pwszVal, len, kAumid);
                HRESULT hr = store->SetValue(PKEY_AppUserModel_ID, pv);
                PropVariantClear(&pv);
                if (FAILED(hr))
                    return;
                store->Commit();
                persist->Save(nullptr, TRUE);
                stamped = true;
            }
            catch (...)
            {
            }
        }

        ToastNotifier Notifier()
        {
            EnsureIdentity();
            EnsureShortcutAppId();
            return ToastNotificationManager::CreateToastNotifierWithId(kAumid);
        }

        XmlDocument DocFor(hstring const& xml)
        {
            XmlDocument doc{};
            doc.LoadXml(xml);
            return doc;
        }

        hstring ReleaseTagUrl(hstring const& latest)
        {
            return hstring{ L"https://github.com/Hushayo/PretClient/releases/tag/" } + latest;
        }

        void Show(XmlDocument const& doc)
        {
            try
            {
                auto notifier = Notifier();
                try
                {
                    ToastNotificationManager::History().Remove(kTag, kGroup);
                }
                catch (...)
                {
                }
                ToastNotification toast{ doc };
                toast.Tag(kTag);
                toast.Group(kGroup);
                toast.ExpirationTime(winrt::clock::now() + std::chrono::hours(72));
                notifier.Show(toast);
            }
            catch (...)
            {
            }
        }

        void ShowSimple(hstring const& title, hstring const& body)
        {
            std::wstring xml =
                L"<toast launch=\"pretclient://open\" activationType=\"protocol\">"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>" + Escape(std::wstring{ title }) + L"</text>"
                L"<text>" + Escape(std::wstring{ body }) + L"</text>"
                L"</binding></visual>"
                L"</toast>";
            Show(DocFor(xml));
        }

        void ShowWithProgress(hstring const& title, hstring const& status,
            double value01, hstring const& valueText)
        {
            wchar_t vbuf[32]{};
            swprintf_s(vbuf, L"%.4f", (std::max)(0.0, (std::min)(1.0, value01)));
            std::wstring xml =
                L"<toast launch=\"pretclient://open\" activationType=\"protocol\">"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>" + Escape(std::wstring{ title }) + L"</text>"
                L"<text>" + Escape(std::wstring{ status }) + L"</text>"
                L"<progress title=\"\" value=\"" + std::wstring{ vbuf } +
                L"\" valueStringOverride=\"" + Escape(std::wstring{ valueText }) +
                L"\" status=\"" + Escape(std::wstring{ status }) + L"\"/>"
                L"</binding></visual>"
                L"</toast>";
            Show(DocFor(xml));
            try
            {
                Windows::Foundation::Collections::StringMap data{};
                data.Insert(L"progressValue", hstring{ vbuf });
                data.Insert(L"progressValueString", valueText);
                data.Insert(L"progressStatus", status);
                Notifier().Update(NotificationData{ data, 1 }, kTag, kGroup);
            }
            catch (...)
            {
            }
        }

        hstring ByteText(unsigned long long b)
        {
            wchar_t buf[64]{};
            if (b >= 1024ull * 1024 * 1024)
                swprintf_s(buf, L"%.1f GB", b / 1073741824.0);
            else if (b >= 1024 * 1024)
                swprintf_s(buf, L"%.0f MB", b / 1048576.0);
            else if (b >= 1024)
                swprintf_s(buf, L"%llu KB", b / 1024);
            else
                swprintf_s(buf, L"%llu B", b);
            return hstring{ buf };
        }
    } // namespace

    void ShowAvailable(hstring const& current, hstring const& latest)
    {
        try
        {
            std::wstring xml =
                L"<toast launch=\"pretclient://open\" activationType=\"protocol\">"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>PretClient update available</text>"
                L"<text>" + Escape(std::wstring{ current } + L" \u2192 " + std::wstring{ latest }) + L"</text>"
                L"</binding></visual>"
                L"<actions>"
                L"<action content=\"Install now\" arguments=\"pretclient://update\" activationType=\"protocol\"/>"
                L"<action content=\"Release notes\" arguments=\"" + Escape(std::wstring{ ReleaseTagUrl(latest) }) +
                L"\" activationType=\"protocol\"/>"
                L"</actions>"
                L"</toast>";
            Show(DocFor(xml));
        }
        catch (...)
        {
        }
    }

    void ShowDownloading(hstring const& latest)
    {
        ShowWithProgress(hstring{ L"Downloading PretClient " } + latest, L"Starting\u2026", 0.0, L"\u2026");
    }

    void ShowProgress(hstring const& status, unsigned long long done, unsigned long long total)
    {
        try
        {
            double v = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
            hstring text = total > 0
                ? hstring{ ByteText(done) } + L" / " + ByteText(total)
                : ByteText(done);
            Windows::Foundation::Collections::StringMap data{};
            wchar_t vbuf[32]{};
            swprintf_s(vbuf, L"%.4f", (std::max)(0.0, (std::min)(1.0, v)));
            data.Insert(L"progressValue", hstring{ vbuf });
            data.Insert(L"progressValueString", text);
            data.Insert(L"progressStatus", status);
            static unsigned int seq = 1;
            Notifier().Update(NotificationData{ data, ++seq }, kTag, kGroup);
        }
        catch (...)
        {
        }
    }

    void ShowDone(hstring const& msg)
    {
        ShowSimple(L"PretClient update", msg);
    }

    void ShowError(hstring const& msg)
    {
        ShowSimple(L"PretClient update failed", msg);
    }

    void Hide()
    {
        try
        {
            EnsureIdentity();
            ToastNotificationManager::History().Remove(kTag, kGroup);
        }
        catch (...)
        {
        }
    }
}
