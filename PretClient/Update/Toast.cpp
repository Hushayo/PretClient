#include "pch.h"
#include "Toast.h"
#include <algorithm>
#include <winrt/Microsoft.Windows.AppNotifications.h>

using namespace winrt;
using namespace Microsoft::Windows::AppNotifications;

namespace winrt::PretClient::Update::Toast
{
    namespace
    {
        constexpr wchar_t kTag[] = L"pretclient-update";
        constexpr wchar_t kGroup[] = L"updates";

        Microsoft::UI::Dispatching::DispatcherQueue g_dispatcher{ nullptr };
        ActionFn g_onAction{};
        bool g_registered = false;

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

        void Show(hstring const& xml)
        {
            try
            {
                auto manager = AppNotificationManager::Default();
                try
                {
                    manager.RemoveByTagAsync(kTag);
                }
                catch (...)
                {
                }
                AppNotification toast{ xml };
                toast.Tag(kTag);
                toast.Group(kGroup);
                manager.Show(toast);
            }
            catch (...)
            {
            }
        }

        void ShowSimple(hstring const& title, hstring const& body)
        {
            std::wstring xml =
                L"<toast>"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>" + Escape(std::wstring{ title }) + L"</text>"
                L"<text>" + Escape(std::wstring{ body }) + L"</text>"
                L"</binding></visual>"
                L"</toast>";
            Show(hstring{ xml });
        }

        // Progress data takes its (non-zero) sequence number at construction;
        // the shell keeps the greatest one it has seen.
        static unsigned int NextSeq()
        {
            static unsigned int seq = 0;
            return ++seq;
        }

        void PushProgress(hstring const& status, double value01, hstring const& valueText)
        {
            try
            {
                AppNotificationProgressData data{ NextSeq() };
                data.Title(L"");
                data.Value((std::max)(0.0, (std::min)(1.0, value01)));
                data.ValueStringOverride(valueText);
                data.Status(status);
                AppNotificationManager::Default().UpdateAsync(data, kTag, kGroup);
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

        hstring ReleaseTagUrl(hstring const& latest)
        {
            return hstring{ L"https://github.com/Hushayo/PretClient/releases/tag/" } + latest;
        }
    } // namespace

    void EnsureRegistered(ActionFn onAction)
    {
        if (g_registered)
            return;
        g_registered = true;
        g_onAction = std::move(onAction);
        try
        {
            g_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        }
        catch (...)
        {
        }
        try
        {
            auto manager = AppNotificationManager::Default();
            manager.NotificationInvoked([onActionCopy = g_onAction](
                    AppNotificationManager const&, AppNotificationActivatedEventArgs const& args) {
                try
                {
                    hstring argument;
                    try
                    {
                        argument = args.Argument();
                    }
                    catch (...)
                    {
                    }
                    auto run = [onActionCopy, argument]() {
                        try
                        {
                            if (onActionCopy && !argument.empty())
                                onActionCopy(argument);
                        }
                        catch (...)
                        {
                        }
                    };
                    if (g_dispatcher)
                        g_dispatcher.TryEnqueue(run);
                    else
                        run();
                }
                catch (...)
                {
                }
            });
            manager.Register();
        }
        catch (...)
        {
        }
    }

    void ShowAvailable(hstring const& current, hstring const& latest)
    {
        try
        {
            hstring notesArg = hstring{ L"action=notes;" } + ReleaseTagUrl(latest);
            std::wstring xml =
                L"<toast>"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>PretClient update available</text>"
                L"<text>" + Escape(std::wstring{ current } + L" \u2192 " + std::wstring{ latest }) + L"</text>"
                L"</binding></visual>"
                L"<actions>"
                L"<action content=\"Install now\" arguments=\"action=install\"/>"
                L"<action content=\"Release notes\" arguments=\"" + Escape(std::wstring{ notesArg }) + L"\"/>"
                L"</actions>"
                L"</toast>";
            Show(hstring{ xml });
        }
        catch (...)
        {
        }
    }

    void ShowDownloading(hstring const& latest)
    {
        try
        {
            std::wstring xml =
                L"<toast>"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>" + Escape(std::wstring{ hstring{ L"Downloading PretClient " } + latest }) + L"</text>"
                L"<text>" + Escape(std::wstring{ L"Starting\u2026" }) + L"</text>"
                L"<progress title=\"\" value=\"{progressValue}\" valueStringOverride=\"{progressValueString}\" status=\"{progressStatus}\"/>"
                L"</binding></visual>"
                L"</toast>";
            Show(hstring{ xml });
            PushProgress(L"Starting\u2026", 0.0, L"\u2026");
        }
        catch (...)
        {
        }
    }

    void ShowProgress(hstring const& status, unsigned long long done, unsigned long long total)
    {
        double v = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
        hstring text = total > 0
            ? hstring{ ByteText(done) } + L" / " + ByteText(total)
            : ByteText(done);
        PushProgress(status, v, text);
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
            AppNotificationManager::Default().RemoveByTagAsync(kTag);
        }
        catch (...)
        {
        }
    }
}
