#pragma once

#include <functional>

// Update toasts via the Windows App SDK AppNotifications API: the supported
// path for unpackaged apps (in-process button activation, no COM server, no
// protocol registration). All calls never throw; when notifications are
// unavailable the Settings status line remains the fallback surface.
namespace winrt::PretClient::Update::Toast
{
    using ActionFn = std::function<void(hstring const& action)>;

    // Call once on the UI thread (MainWindow ctor). Registers the notification
    // activator and routes button clicks to onAction ("action=install" or
    // "action=notes;<url>").
    void EnsureRegistered(ActionFn onAction);

    // "Update vX -> vY is ready" with Install-now and Release-notes buttons.
    void ShowAvailable(hstring const& current, hstring const& latest);
    // Swap to the downloading layout (progress bar bound to progress data).
    void ShowDownloading(hstring const& latest);
    // Progress tick: status text + done/total bytes (total 0 = unknown).
    void ShowProgress(hstring const& status, unsigned long long done, unsigned long long total);
    void ShowDone(hstring const& msg);
    void ShowError(hstring const& msg);
    void Hide();
}
