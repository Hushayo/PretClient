#pragma once

// Update toasts: the self-update banner replacement. Unpackaged apps can
// toast through an explicit AppUserModelID as long as the Start-menu
// shortcut carries the same ID (stamped here at runtime, best-effort).
// All calls are safe from any thread state and never throw: when toasts are
// unavailable (no shortcut, no shell) they silently do nothing and the
// Settings status line remains the fallback surface.
namespace winrt::PretClient::Update::Toast
{
    // "Update vX -> vY is ready" with Install-now (pretclient://update) and
    // Release-notes (https release page) actions.
    void ShowAvailable(hstring const& current, hstring const& latest);
    // Swap to the downloading layout (progress bar bound to NotificationData).
    void ShowDownloading(hstring const& latest);
    // Progress tick: status text + done/total bytes (total 0 = unknown).
    void ShowProgress(hstring const& status, unsigned long long done, unsigned long long total);
    void ShowDone(hstring const& msg);
    void ShowError(hstring const& msg);
    void Hide();
}
