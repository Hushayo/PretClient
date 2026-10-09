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
                // Bool may be stored as Boolean or Number (older builds).
                auto getBool = [&](wchar_t const* k, bool fb) {
                    try
                    {
                        if (!o.HasKey(k))
                            return fb;
                        auto v = o.GetNamedValue(k);
                        if (v.ValueType() == JsonValueType::Boolean)
                            return v.GetBoolean();
                        if (v.ValueType() == JsonValueType::Number)
                            return v.GetNumber() != 0.0;
                    }
                    catch (...)
                    {
                    }
                    return fb;
                };
                s.fpsBoost = getBool(L"fpsBoost", true);
                s.highPriority = getBool(L"highPriority", true);
            }
            catch (...)
            {
            }
            s.extraJvmArgs = str(L"extraJvmArgs", L"");
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
        o.SetNamedValue(L"fpsBoost", JsonValue::CreateBooleanValue(s.fpsBoost));
        o.SetNamedValue(L"highPriority", JsonValue::CreateBooleanValue(s.highPriority));
        o.SetNamedValue(L"extraJvmArgs", JsonValue::CreateStringValue(s.extraJvmArgs));
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

    std::filesystem::path InstanceGameDir(Settings const& s, hstring const& instanceId)
    {
        return InstanceDir(s, instanceId) / L"game";
    }

    std::filesystem::path InstanceModsDir(Settings const& s, hstring const& instanceId)
    {
        return InstanceGameDir(s, instanceId) / L"mods";
    }

    std::filesystem::path InstanceResourcePacksDir(Settings const& s, hstring const& instanceId)
    {
        return InstanceGameDir(s, instanceId) / L"resourcepacks";
    }

    void EnsureInstanceGameDir(Settings const& s, hstring const& instanceId)
    {
        try
        {
            std::error_code ec;
            auto root = InstanceDir(s, instanceId);
            auto game = InstanceGameDir(s, instanceId);
            std::filesystem::create_directories(game, ec);
            std::filesystem::create_directories(InstanceModsDir(s, instanceId), ec);
            std::filesystem::create_directories(InstanceResourcePacksDir(s, instanceId), ec);
            // Migrate legacy layout (<instance>/mods -> <instance>/game/mods).
            auto migrateDir = [&](std::filesystem::path const& oldDir,
                                  std::filesystem::path const& newDir) {
                try
                {
                    std::error_code ec2;
                    if (!std::filesystem::exists(oldDir, ec2) || std::filesystem::exists(newDir, ec2))
                    {
                        // newDir always exists after create_directories above,
                        // so only migrate when it is still empty.
                        bool empty = true;
                        for (auto const& e : std::filesystem::directory_iterator(newDir, ec2))
                        {
                            (void)e;
                            empty = false;
                            break;
                        }
                        if (!empty)
                            return;
                    }
                    else
                    {
                        std::filesystem::create_directories(newDir, ec2);
                    }
                    for (auto const& e : std::filesystem::directory_iterator(oldDir, ec2))
                    {
                        try
                        {
                            auto dest = newDir / e.path().filename();
                            std::error_code ec3;
                            if (std::filesystem::exists(dest, ec3))
                                continue;
                            std::filesystem::rename(e.path(), dest, ec3);
                            if (ec3)
                            {
                                // Cross-volume fallback: copy then remove.
                                if (e.is_directory(ec3))
                                    std::filesystem::copy(e.path(), dest,
                                        std::filesystem::copy_options::recursive |
                                            std::filesystem::copy_options::overwrite_existing,
                                        ec3);
                                else
                                    std::filesystem::copy_file(e.path(), dest,
                                        std::filesystem::copy_options::overwrite_existing, ec3);
                                if (!ec3)
                                {
                                    std::error_code ec4;
                                    if (e.is_directory(ec4))
                                        std::filesystem::remove_all(e.path(), ec4);
                                    else
                                        std::filesystem::remove(e.path(), ec4);
                                }
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
                catch (...)
                {
                }
            };
            migrateDir(root / L"mods", InstanceModsDir(s, instanceId));
            migrateDir(root / L"resourcepacks", InstanceResourcePacksDir(s, instanceId));
        }
        catch (...)
        {
        }
    }
}
