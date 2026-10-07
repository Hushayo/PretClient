#include "pch.h"
#include "Versions.h"
#include "Http.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Versions
{
    namespace
    {
        constexpr wchar_t kManifestUrl[] = L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        hstring OptStr(JsonObject const& o, wchar_t const* key)
        {
            try
            {
                if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::String)
                    return o.GetNamedString(key);
            }
            catch (...)
            {
            }
            return hstring{};
        }
    } // namespace

    fire_and_forget FetchManifestAsync(ManifestFn done)
    {
        Manifest out{};
        try
        {
            auto text = co_await Http::GetStringAsync(kManifestUrl, kUA);
            auto root = JsonObject::Parse(text);
            if (root.HasKey(L"latest"))
            {
                auto latest = root.GetNamedObject(L"latest");
                out.latestRelease = OptStr(latest, L"release");
                out.latestSnapshot = OptStr(latest, L"snapshot");
            }
            if (root.HasKey(L"versions"))
            {
                for (auto const& v : root.GetNamedArray(L"versions"))
                {
                    if (v.ValueType() != JsonValueType::Object)
                        continue;
                    auto o = v.GetObject();
                    McVersion e{};
                    e.id = OptStr(o, L"id");
                    e.type = OptStr(o, L"type");
                    e.url = OptStr(o, L"url");
                    e.sha1 = OptStr(o, L"sha1");
                    if (!e.id.empty() && !e.url.empty())
                        out.entries.push_back(std::move(e));
                }
            }
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    IAsyncOperation<JsonObject> FetchVersionJsonAsync(McVersion const& entry)
    {
        JsonObject out{ nullptr };
        try
        {
            auto text = co_await Http::GetStringAsync(entry.url, kUA);
            out = JsonObject::Parse(text);
        }
        catch (...)
        {
        }
        co_return out;
    }
}
