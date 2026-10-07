#include "pch.h"
#include "Forge.h"
#include "Http.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::PretClient::Forge
{
    namespace
    {
        constexpr wchar_t kPromos[] =
            L"https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json";
        constexpr wchar_t kMaven[] = L"https://maven.minecraftforge.net/net/minecraftforge/forge/";
        constexpr wchar_t kUA[] = L"PretClient/0.0.1 (github.com/Hushayo/PretClient)";

        struct Promo
        {
            hstring recommended{};
            hstring latest{};
        };

        IAsyncOperation<Windows::Data::Json::JsonObject> FetchPromosJsonAsync()
        {
            JsonObject promos{ nullptr };
            try
            {
                auto text = co_await Http::GetStringAsync(kPromos, kUA);
                auto root = JsonObject::Parse(text);
                if (root.HasKey(L"promos"))
                    promos = root.GetNamedObject(L"promos");
            }
            catch (...)
            {
            }
            co_return promos;
        }

        Promo PickPromo(JsonObject const& promos, hstring const& mcVersion)
        {
            Promo p{};
            try
            {
                if (!promos)
                    return p;
                std::wstring mc{ mcVersion };
                for (auto const& kv : promos)
                {
                    try
                    {
                        if (kv.Value().ValueType() != JsonValueType::String)
                            continue;
                        std::wstring key{ kv.Key() };
                        hstring val = kv.Value().GetString();
                        if (key == mc + L"-recommended")
                            p.recommended = val;
                        else if (key == mc + L"-latest")
                            p.latest = val;
                    }
                    catch (...)
                    {
                    }
                }
            }
            catch (...)
            {
            }
            return p;
        }
    } // namespace

    hstring ArtifactId(hstring mcVersion, hstring forgeVersion)
    {
        try
        {
            std::wstring mc{ mcVersion };
            std::wstring fv{ forgeVersion };
            if (fv.empty() || mc.empty())
                return hstring{};
            if (fv.rfind(mc + L"-", 0) == 0)
                return hstring{ fv }; // full artifact already ("1.20.1-47.2.0")
            std::wstring art = mc + L"-" + fv;
            if (mc.rfind(L"1.7.", 0) == 0 || mc.rfind(L"1.8.", 0) == 0)
                art += L"-" + mc; // ancient maven layout
            return hstring{ art };
        }
        catch (...)
        {
            return hstring{};
        }
    }

    hstring InstallerUrl(hstring mcVersion, hstring forgeVersion)
    {
        try
        {
            std::wstring art{ ArtifactId(mcVersion, forgeVersion) };
            if (art.empty())
                return hstring{};
            return hstring{ std::wstring{ kMaven } + art + L"/forge-" + art + L"-installer.jar" };
        }
        catch (...)
        {
            return hstring{};
        }
    }

    hstring ExpectedVersionId(hstring mcVersion, hstring forgeVersion)
    {
        try
        {
            std::wstring mc{ mcVersion };
            std::wstring art{ ArtifactId(mcVersion, forgeVersion) };
            if (mc.empty() || art.empty())
                return hstring{};
            std::wstring part = art;
            if (art.rfind(mc + L"-", 0) == 0)
                part = art.substr(mc.size() + 1);
            std::wstring tail = L"-" + mc; // strip legacy trailing MC
            if (part.size() > tail.size() &&
                part.compare(part.size() - tail.size(), tail.size(), tail) == 0)
                part = part.substr(0, part.size() - tail.size());
            if (part.empty())
                return hstring{};
            return hstring{ mc + L"-forge-" + part };
        }
        catch (...)
        {
            return hstring{};
        }
    }

    fire_and_forget GetLoaderVersions(hstring mcVersion, LoaderListFn done)
    {
        std::vector<hstring> out;
        try
        {
            auto p = PickPromo(co_await FetchPromosJsonAsync(), mcVersion);
            if (!p.recommended.empty())
                out.push_back(p.recommended);
            if (!p.latest.empty() && p.latest != p.recommended)
                out.push_back(p.latest);
        }
        catch (...)
        {
        }
        done(std::move(out));
    }

    IAsyncOperation<hstring> GetLatestForge(hstring mcVersion)
    {
        hstring latest{};
        try
        {
            auto p = PickPromo(co_await FetchPromosJsonAsync(), mcVersion);
            latest = !p.recommended.empty() ? p.recommended : p.latest;
        }
        catch (...)
        {
        }
        co_return latest;
    }
}
