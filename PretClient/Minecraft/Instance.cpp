#include "pch.h"
#include "Instance.h"
#include "../Paths.h"
#include <fstream>

using namespace winrt;
using namespace Windows::Data::Json;

namespace winrt::PretClient
{
    namespace
    {
        std::filesystem::path StoreFile()
        {
            return Paths::DataDir() / L"instances.json";
        }

        hstring GetStr(JsonObject const& o, wchar_t const* key, hstring fallback)
        {
            try
            {
                if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::String)
                    return o.GetNamedString(key);
            }
            catch (...)
            {
            }
            return fallback;
        }

        JsonObject ToJson(Instance const& in)
        {
            JsonObject o{};
            o.SetNamedValue(L"id", JsonValue::CreateStringValue(in.id));
            o.SetNamedValue(L"name", JsonValue::CreateStringValue(in.name));
            o.SetNamedValue(L"mcVersion", JsonValue::CreateStringValue(in.mcVersion));
            o.SetNamedValue(L"loader", JsonValue::CreateStringValue(in.loader));
            o.SetNamedValue(L"loaderVersion", JsonValue::CreateStringValue(in.loaderVersion));
            o.SetNamedValue(L"maxMemMb", JsonValue::CreateNumberValue(in.maxMemMb));
            return o;
        }

        Instance FromJson(JsonObject const& o)
        {
            Instance in{};
            in.id = GetStr(o, L"id", L"");
            in.name = GetStr(o, L"name", L"Instance");
            in.mcVersion = GetStr(o, L"mcVersion", L"1.21.4");
            in.loader = GetStr(o, L"loader", L"vanilla");
            in.loaderVersion = GetStr(o, L"loaderVersion", L"");
            try
            {
                if (o.HasKey(L"maxMemMb"))
                    in.maxMemMb = static_cast<int>(o.GetNamedNumber(L"maxMemMb"));
            }
            catch (...)
            {
            }
            if (in.maxMemMb <= 0)
                in.maxMemMb = 2048;
            return in;
        }
    } // namespace

    std::vector<Instance> LoadInstances()
    {
        std::vector<Instance> out;
        try
        {
            std::ifstream f(StoreFile());
            if (!f.good())
                return out;
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (text.empty())
                return out;
            auto arr = JsonArray::Parse(to_hstring(text));
            for (auto const& v : arr)
            {
                if (v.ValueType() == JsonValueType::Object)
                {
                    auto in = FromJson(v.GetObject());
                    if (!in.id.empty())
                        out.push_back(std::move(in));
                }
            }
        }
        catch (...)
        {
        }
        if (out.empty())
        {
            Instance def{};
            def.id = L"default";
            def.name = L"Default";
            def.mcVersion = L"1.21.4";
            def.loader = L"vanilla";
            out.push_back(std::move(def));
            try
            {
                SaveInstances(out);
            }
            catch (...)
            {
            }
        }
        return out;
    }

    void SaveInstances(std::vector<Instance> const& instances)
    {
        JsonArray arr{};
        for (auto const& in : instances)
            arr.Append(ToJson(in));
        std::ofstream f(StoreFile(), std::ios::binary | std::ios::trunc);
        f << to_string(arr.Stringify());
    }

    hstring NewInstanceId()
    {
        GUID g{};
        CoCreateGuid(&g);
        wchar_t buf[64]{};
        swprintf_s(
            buf, L"inst-%08lX-%04hX-%04hX", g.Data1 ^ GetTickCount(), g.Data2, g.Data3);
        return hstring{ buf };
    }
}
