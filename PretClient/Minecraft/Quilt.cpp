#include "pch.h"
#include "Quilt.h"
#include "Http.h"
#include <algorithm>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Quilt
{
    namespace
    {
        constexpr wchar_t kMeta[] = L"https://meta.quiltmc.org/v3/versions/loader";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        // Numeric core comparison with stable > prerelease on ties, so
        // "0.28.1" wins over "0.28.1-beta.2" and "0.28.1" over "0.27.1".
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

        IAsyncOperation<JsonArray> LoaderListJsonAsync(hstring mcVersion)
        {
            JsonArray out{ nullptr };
            try
            {
                hstring url = hstring{ kMeta } + hstring{ L"/" } + mcVersion;
                auto text = co_await Http::GetStringAsync(url, kUA);
                out = JsonArray::Parse(text);
            }
            catch (...)
            {
            }
            co_return out;
        }

        // Per-MC entries look like { "loader": { "version": "0.28.1", ... }, ... }.
        std::vector<hstring> ParseLoaderList(JsonArray const& arr)
        {
            std::vector<hstring> out;
            try
            {
                if (!arr)
                    return out;
                for (auto const& v : arr)
                {
                    if (v.ValueType() != JsonValueType::Object)
                        continue;
                    try
                    {
                        auto o = v.GetObject();
                        if (!o.HasKey(L"loader"))
                            continue;
                        auto loader = o.GetNamedObject(L"loader");
                        if (loader.HasKey(L"version"))
                            out.push_back(loader.GetNamedString(L"version"));
                    }
                    catch (...)
                    {
                    }
                }
            }
            catch (...)
            {
            }
            return out;
        }
    } // namespace

    fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done)
    {
        std::vector<hstring> out;
        try
        {
            out = ParseLoaderList(co_await LoaderListJsonAsync(mcVersion));
            std::sort(out.begin(), out.end(),
                [](hstring const& a, hstring const& b) {
                    return VerLess(KeyOf(std::wstring{ b }), KeyOf(std::wstring{ a }));
                });
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    IAsyncOperation<hstring> GetLatestLoader(hstring mcVersion)
    {
        hstring latest{};
        try
        {
            auto all = ParseLoaderList(co_await LoaderListJsonAsync(mcVersion));
            hstring bestStable{};
            hstring bestAny{};
            VerKey bestStableKey{};
            VerKey bestAnyKey{};
            bool haveStable = false;
            bool haveAny = false;
            for (auto const& v : all)
            {
                VerKey k = KeyOf(std::wstring{ v });
                if (!haveAny || VerLess(bestAnyKey, k))
                {
                    bestAny = v;
                    bestAnyKey = k;
                    haveAny = true;
                }
                if (k.stable && (!haveStable || VerLess(bestStableKey, k)))
                {
                    bestStable = v;
                    bestStableKey = k;
                    haveStable = true;
                }
            }
            latest = haveStable ? bestStable : bestAny;
        }
        catch (...)
        {
        }
        co_return latest;
    }

    IAsyncOperation<JsonObject> GetProfile(hstring mcVersion, hstring loader)
    {
        JsonObject out{ nullptr };
        try
        {
            hstring url = hstring{ kMeta } + hstring{ L"/" } + mcVersion +
                hstring{ L"/" } + loader + hstring{ L"/profile/json" };
            auto text = co_await Http::GetStringAsync(url, kUA);
            out = JsonObject::Parse(text);
        }
        catch (...)
        {
        }
        co_return out;
    }
}
