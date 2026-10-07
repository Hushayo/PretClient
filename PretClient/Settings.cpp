#include "pch.h"
#include "Settings.h"
#include "Paths.h"
#include <cwctype>
#include <fstream>

using namespace winrt;
using namespace Windows::Data::Json;

namespace winrt::PretClient
{
    namespace
    {
        std::filesystem::path StoreFile()
        {
            return Paths::DataDir() / L"settings.json";
        }
    } // namespace

    Settings LoadSettings()
    {
        Settings s{};
        try
        {
            std::ifstream f(StoreFile());
            if (!f.good())
                return s;
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (text.empty())
                return s;
            auto o = JsonObject::Parse(to_hstring(text));
            auto str = [&](wchar_t const* k, hstring fb) {
                try
                {
                    if (o.HasKey(k) && o.GetNamedValue(k).ValueType() == JsonValueType::String)
                        return o.GetNamedString(k);
                }
                catch (...)
                {
                }
                return fb;
            };
            s.username = str(L"username", L"Steve");
            s.gameDir = str(L"gameDir", L"");
            s.javaPath = str(L"javaPath", L"");
            s.curseforgeKey = str(L"curseforgeKey", L"");
            try
            {
                if (o.HasKey(L"profiles") && o.GetNamedValue(L"profiles").ValueType() == JsonValueType::Array)
                {
                    for (auto const& pv : o.GetNamedArray(L"profiles"))
                    {
                        if (pv.ValueType() == JsonValueType::String)
                        {
                            hstring name = pv.GetString();
                            if (!name.empty())
                                s.profiles.push_back(name);
                        }
                    }
                }
            }
            catch (...)
            {
            }
            try
            {
                if (o.HasKey(L"maxMemMb"))
                    s.maxMemMb = static_cast<int>(o.GetNamedNumber(L"maxMemMb"));
                if (o.HasKey(L"minMemMb"))
                    s.minMemMb = static_cast<int>(o.GetNamedNumber(L"minMemMb"));
            }
            catch (...)
            {
            }
        }
        catch (...)
        {
        }
        if (s.username.empty())
            s.username = L"Steve";
        bool listed = false;
        for (auto const& p : s.profiles)
        {
            if (p == s.username)
            {
                listed = true;
                break;
            }
        }
        if (!listed)
            s.profiles.insert(s.profiles.begin(), s.username);
        if (s.maxMemMb < 512)
            s.maxMemMb = 2048;
        if (s.minMemMb < 256 || s.minMemMb > s.maxMemMb)
            s.minMemMb = 512;
        return s;
    }

    void SaveSettings(Settings const& s)
    {
        JsonObject o{};
        o.SetNamedValue(L"username", JsonValue::CreateStringValue(s.username));
        JsonArray profs{};
        for (auto const& p : s.profiles)
            profs.Append(JsonValue::CreateStringValue(p));
        o.SetNamedValue(L"profiles", profs);
        o.SetNamedValue(L"gameDir", JsonValue::CreateStringValue(s.gameDir));
        o.SetNamedValue(L"javaPath", JsonValue::CreateStringValue(s.javaPath));
        o.SetNamedValue(L"curseforgeKey", JsonValue::CreateStringValue(s.curseforgeKey));
        o.SetNamedValue(L"maxMemMb", JsonValue::CreateNumberValue(s.maxMemMb));
        o.SetNamedValue(L"minMemMb", JsonValue::CreateNumberValue(s.minMemMb));
        std::ofstream f(StoreFile(), std::ios::binary | std::ios::trunc);
        f << to_string(o.Stringify());
    }

    hstring EffectiveGameDir(Settings const& s)
    {
        if (!s.gameDir.empty())
            return s.gameDir;
        return hstring{ Paths::DefaultGameDir().wstring() };
    }

    std::filesystem::path InstanceDir(Settings const& s, hstring const& instanceId)
    {
        auto base = std::filesystem::path{ std::wstring{ EffectiveGameDir(s) } };
        std::wstring id{ instanceId };
        for (auto& c : id)
        {
            if (!(iswalnum(c) || c == L'-' || c == L'_'))
                c = L'_';
        }
        if (id.empty())
            id = L"instance";
        return base / L"instances" / id;
    }

    std::filesystem::path InstanceModsDir(Settings const& s, hstring const& instanceId)
    {
        return InstanceDir(s, instanceId) / L"mods";
    }

    std::filesystem::path InstanceResourcePacksDir(Settings const& s, hstring const& instanceId)
    {
        return InstanceDir(s, instanceId) / L"resourcepacks";
    }
}
