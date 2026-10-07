#pragma once

#include <map>
#include <regex>

// Mojang rules evaluation (libraries + game/jvm arguments) and ${token}
// substitution. All features (demo/quickplay/resolution) are false.
namespace winrt::PretClient::Rules
{
    inline std::wstring RealOsVersion()
    {
        struct RtlVer
        {
            ULONG size = sizeof(RtlVer);
            ULONG major = 0;
            ULONG minor = 0;
            ULONG build = 0;
            ULONG platform = 0;
            WCHAR csd[128]{};
        };
        using Fn = LONG(NTAPI*)(RtlVer*);
        if (auto mod = GetModuleHandleW(L"ntdll.dll"))
        {
            if (auto fn = reinterpret_cast<Fn>(GetProcAddress(mod, "RtlGetVersion")))
            {
                RtlVer v{};
                if (fn(&v) == 0)
                    return std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.build);
            }
        }
        return L"10.0.0";
    }

    inline bool OsMatches(Windows::Data::Json::JsonObject const& os)
    {
        try
        {
            if (os.HasKey(L"name"))
            {
                std::wstring name{ os.GetNamedString(L"name") };
                for (auto& c : name)
                    c = static_cast<wchar_t>(towlower(c));
                if (name != L"windows")
                    return false;
            }
            if (os.HasKey(L"arch"))
            {
                std::wstring arch{ os.GetNamedString(L"arch") };
                // We ship x64 only: x86-only entries do not match.
                if (arch == L"x86")
                    return false;
            }
            if (os.HasKey(L"version"))
            {
                try
                {
                    std::wregex re{ std::wstring{ os.GetNamedString(L"version") } };
                    if (!std::regex_search(RealOsVersion(), re))
                        return false;
                }
                catch (...)
                {
                    return false;
                }
            }
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    // Last matching rule wins; no rules at all means include.
    inline bool RuleListAllows(Windows::Data::Json::JsonArray const& rules)
    {
        try
        {
            if (rules.Size() == 0)
                return true;
            bool allowed = false;
            for (auto const& rv : rules)
            {
                if (rv.ValueType() != Windows::Data::Json::JsonValueType::Object)
                    continue;
                auto rule = rv.GetObject();
                bool match = true;
                if (rule.HasKey(L"os"))
                {
                    try
                    {
                        match = OsMatches(rule.GetNamedObject(L"os"));
                    }
                    catch (...)
                    {
                        match = false;
                    }
                }
                if (match && rule.HasKey(L"features"))
                {
                    // All our feature flags are false: any required feature rejects.
                    match = false;
                }
                if (match)
                {
                    std::wstring action;
                    try
                    {
                        action = std::wstring{ rule.GetNamedString(L"action") };
                    }
                    catch (...)
                    {
                    }
                    allowed = (action == L"allow");
                }
            }
            return allowed;
        }
        catch (...)
        {
            return false;
        }
    }

    inline bool EntryAllowed(Windows::Data::Json::JsonObject const& entry)
    {
        try
        {
            if (!entry.HasKey(L"rules"))
                return true;
            return RuleListAllows(entry.GetNamedArray(L"rules"));
        }
        catch (...)
        {
            return false;
        }
    }

    inline hstring Substitute(hstring tmpl, std::map<std::wstring, hstring> const& vars)
    {
        std::wstring s{ tmpl };
        for (auto const& [key, value] : vars)
        {
            std::wstring token = L"${" + key + L"}";
            std::wstring rep{ value };
            size_t pos = 0;
            while ((pos = s.find(token, pos)) != std::wstring::npos)
            {
                s.replace(pos, token.size(), rep);
                pos += rep.size();
            }
        }
        return hstring{ s };
    }
}
