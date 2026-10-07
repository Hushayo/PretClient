#pragma once

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>

// Self-update against GitHub Releases. No extra deps:
// HTTP via Windows.Web.Http, JSON via Windows.Data.Json.
namespace winrt::PretClient::Update
{
// Latest published release, or nullptr when there is none / offline.
winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::JsonObject> GetLatestReleaseAsync();

// Local tag, e.g. L"v0.0.0-dev". Bump when tagging a release.
winrt::hstring CurrentVersionTag();

// True when latest is a higher version than current.
bool IsNewerTag(winrt::hstring const& current, winrt::hstring const& latest);

// Installer asset (.msix/.msixbundle/.exe) or the release page URL.
winrt::hstring DownloadUrlFor(winrt::Windows::Data::Json::JsonObject const& release);

// The setup exe asset with its published size + sha256 digest (from the
// release API's per-asset `size` and `digest` fields). url is "" when the
// release has no setup asset.
struct ReleaseAsset
{
    winrt::hstring name{};
    winrt::hstring url{};
    unsigned long long size = 0;
    winrt::hstring sha256{}; // lowercase hex, no "sha256:" prefix ("" when unpublished)
};
ReleaseAsset FindSetupAsset(winrt::Windows::Data::Json::JsonObject const& release);

// True when the URL is a setup exe we can download + run ourselves
// (as opposed to the release-page fallback, which still opens a browser).
bool IsInstallerUrl(winrt::hstring const& url);

// Open a URL in the default browser.
void OpenUrl(winrt::hstring const& url);
}
