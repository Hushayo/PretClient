#pragma once

#include <functional>

// Microsoft (Xbox) authentication for online play, alongside the existing
// offline mode. Flow: browser auth-code + localhost redirect -> MS tokens ->
// Xbox Live -> XSTS -> Minecraft token -> profile. The MS refresh token is
// DPAPI-encrypted in account.json; the MC access token is short-lived (24h)
// and refreshed silently before launch.
namespace winrt::PretClient::Auth
{
    struct Session
    {
        bool microsoft = false;
        hstring username{}; // gamertag, or offline name
        hstring uuid{}; // dashed
        hstring token{}; // MC access token, or "0" offline
        hstring xuid{}; // xbox user hash, or "" offline
        hstring userType{}; // "msa" or "legacy"
    };

    bool HasMicrosoft(); // a Microsoft account is linked on disk
    hstring AccountName(); // cached gamertag, "" when none
    // Refresh the MC token when stale. False = sign in again (or stay offline).
    Windows::Foundation::IAsyncOperation<bool> EnsureSessionAsync();
    // Valid MS session when linked + fresh, otherwise offline from settings.
    Session LaunchSession();

    using StatusFn = std::function<void(hstring)>;
    using DoneFn = std::function<void(bool ok, hstring message)>;
    // Full browser sign-in. Status/done always run on the UI thread.
    winrt::fire_and_forget SignInAsync(hstring clientId, StatusFn status, DoneFn done);
    void SignOut();
}
