#include "pch.h"
#include "Settings.h"
#include "Paths.h"
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
        o.SetNamedValue(L"gameDir", JsonValue::CreateStringValue(s.gameDir));
        o.SetNamedValue(L"javaPath", JsonValue::CreateStringValue(s.javaPath));
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
}
