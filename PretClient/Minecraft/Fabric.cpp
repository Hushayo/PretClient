#include "pch.h"
#include "Fabric.h"
#include "Http.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Fabric
{
    namespace
    {
        constexpr wchar_t kMeta[] = L"https://meta.fabricmc.net";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        IAsyncOperation<JsonArray> LoaderListJsonAsync(hstring mcVersion)
        {
            JsonArray out{ nullptr };
            try
            {
                hstring url = hstring{ kMeta } + L"/v2/versions/loader/" + mcVersion;
                auto text = co_await Http::GetStringAsync(url, kUA);
                out = JsonArray::Parse(text);
            }
            catch (...)
            {
            }
            co_return out;
        }

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
                    auto o = v.GetObject();
                    if (!o.HasKey(L"loader"))
                        continue;
                    try
                    {
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
            if (!all.empty())
                latest = all.front(); // newest first
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
            hstring url = hstring{ kMeta } + L"/v2/versions/loader/" + mcVersion + L"/" + loader + L"/profile/json";
            auto text = co_await Http::GetStringAsync(url, kUA);
            out = JsonObject::Parse(text);
        }
        catch (...)
        {
        }
        co_return out;
    }

    hstring MavenJarPath(hstring name)
    {
        // group:artifact:version[:classifier] -> group/path/artifact/version/artifact-version[-classifier].jar
        try
        {
            std::wstring s{ name };
            std::vector<std::wstring> parts;
            size_t pos = 0;
            while (pos <= s.size())
            {
                auto c = s.find(L':', pos);
                parts.push_back(s.substr(pos, c == std::wstring::npos ? std::wstring::npos : c - pos));
                if (c == std::wstring::npos)
                    break;
                pos = c + 1;
            }
            if (parts.size() < 3)
                return hstring{};
            std::wstring group = parts[0];
            for (auto& ch : group)
            {
                if (ch == L'.')
                    ch = L'/';
            }
            std::wstring file = parts[1] + L"-" + parts[2];
            if (parts.size() >= 4)
                file += L"-" + parts[3];
            file += L".jar";
            return hstring{ group + L"/" + parts[1] + L"/" + parts[2] + L"/" + file };
        }
        catch (...)
        {
            return hstring{};
        }
    }
}
