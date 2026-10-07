#include "pch.h"
#include "NeoForge.h"
#include "Http.h"
#include <algorithm>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::NeoForge
{
    namespace
    {
        constexpr wchar_t kVersions[] =
            L"https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        // Same numeric-core ordering as the quilt module (stable wins ties).
        struct VerKey
        {
            std::vector<int> nums{};
            bool stable = true;
        };

        VerKey KeyOf(std::wstring s)
        {
            VerKey k{};
            if (auto dash = s.find(L'-'); dash != std::wstring::npos)
            {
                k.stable = false;
                s = s.substr(0, dash);
            }
            size_t start = 0;
            while (start <= s.size())
            {
                auto dot = s.find(L'.', start);
                auto token = s.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start);
                try
                {
                    k.nums.push_back(token.empty() ? 0 : std::stoi(token));
                }
                catch (...)
                {
                    k.nums.push_back(0);
                }
                if (dot == std::wstring::npos)
                    break;
                start = dot + 1;
            }
            return k;
        }

        bool VerLess(VerKey const& a, VerKey const& b)
        {
            size_t n = (std::max)(a.nums.size(), b.nums.size());
            for (size_t i = 0; i < n; ++i)
            {
                int x = i < a.nums.size() ? a.nums[i] : 0;
                int y = i < b.nums.size() ? b.nums[i] : 0;
                if (x != y)
                    return x < y;
            }
            if (a.stable != b.stable)
                return b.stable && !a.stable;
            return false;
        }

        // "1.20.4" -> "20.4." ; "26.1.2" -> "26.1.2." (post-1.x numbering).
        std::wstring McPrefix(hstring const& mcVersion)
        {
            std::wstring s{ mcVersion };
            if (s.rfind(L"1.", 0) == 0)
                return s.substr(2) + L".";
            return s + L".";
        }

        IAsyncOperation<JsonObject> FetchRootJsonAsync()
        {
            JsonObject root{ nullptr };
            try
            {
                auto text = co_await Http::GetStringAsync(kVersions, kUA);
                root = JsonObject::Parse(text);
            }
            catch (...)
            {
            }
            co_return root;
        }

        struct Item
        {
            hstring version{};
            VerKey key{};
        };

        // Newest-first stable builds for this MC (WinRT types only cross the
        // coroutine boundary; the plain-C++ list is built by the caller).
        IAsyncOperation<JsonArray> FetchMatchingJsonAsync(hstring mcVersion)
        {
            JsonArray out{};
            try
            {
                auto root = co_await FetchRootJsonAsync();
                if (root && root.HasKey(L"versions"))
                {
                    std::wstring prefix = McPrefix(mcVersion);
                    std::vector<Item> items;
                    for (auto const& vv : root.GetNamedArray(L"versions"))
                    {
                        if (vv.ValueType() != JsonValueType::String)
                            continue;
                        hstring ver = vv.GetString();
                        std::wstring s{ ver };
                        if (s.rfind(prefix, 0) != 0)
                            continue;
                        if (s.find(L'-') != std::wstring::npos)
                            continue; // betas: typeable explicitly, never auto-picked
                        items.push_back(Item{ ver, KeyOf(s) });
                    }
                    std::sort(items.begin(), items.end(),
                        [](Item const& a, Item const& b) { return VerLess(b.key, a.key); });
                    for (size_t i = 0; i < items.size() && i < 20; ++i)
                        out.Append(JsonValue::CreateStringValue(items[i].version));
                }
            }
            catch (...)
            {
            }
            co_return out;
        }
    } // namespace

    fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done)
    {
        std::vector<hstring> out;
        try
        {
            auto arr = co_await FetchMatchingJsonAsync(mcVersion);
            if (arr)
            {
                for (auto const& vv : arr)
                {
                    if (vv.ValueType() == JsonValueType::String)
                        out.push_back(vv.GetString());
                }
            }
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    IAsyncOperation<hstring> GetLatestNeoForge(hstring mcVersion)
    {
        hstring latest{};
        try
        {
            auto arr = co_await FetchMatchingJsonAsync(mcVersion);
            if (arr && arr.Size() > 0)
                latest = arr.GetStringAt(0);
        }
        catch (...)
        {
        }
        co_return latest;
    }

    hstring InstallerUrl(hstring neoVersion)
    {
        try
        {
            std::wstring v{ neoVersion };
            if (v.empty())
                return hstring{};
            return hstring{
                L"https://maven.neoforged.net/releases/net/neoforged/neoforge/" + v +
                L"/neoforge-" + v + L"-installer.jar"
            };
        }
        catch (...)
        {
            return hstring{};
        }
    }

    hstring ExpectedVersionId(hstring mcVersion, hstring neoVersion)
    {
        try
        {
            std::wstring mc{ mcVersion };
            std::wstring v{ neoVersion };
            if (mc.empty() || v.empty())
                return hstring{};
            return hstring{ mc + L"-neoforge-" + v };
        }
        catch (...)
        {
            return hstring{};
        }
    }
}
