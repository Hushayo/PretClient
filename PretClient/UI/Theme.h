#pragma once

// Single source of truth for PretClient color. Dark-first: the app forces
// dark theme, tints the system accent ramp Minecraft-green, and builds cards
// from theme-aware brushes so nothing clashes in either theme.
namespace winrt::PretClient::Theme
{
    inline Windows::UI::Color AccentBase()
    {
        return Windows::UI::ColorHelper::FromArgb(0xFF, 0x44, 0xBD, 0x32);
    }

    inline void ApplyBranding()
    {
        using namespace Microsoft::UI::Xaml;
        auto app = Application::Current();
        app.RequestedTheme(ApplicationTheme::Dark);
        auto res = app.Resources();
        auto put = [&](wchar_t const* key, Windows::UI::Color color) {
            res.Insert(winrt::box_value(key), winrt::box_value(color));
        };
        // Coherent green ramp so hover/pressed/disabled states stay green.
        put(L"SystemAccentColor", Windows::UI::ColorHelper::FromArgb(0xFF, 0x44, 0xBD, 0x32));
        put(L"SystemAccentColorLight3", Windows::UI::ColorHelper::FromArgb(0xFF, 0xC9, 0xF2, 0xC4));
        put(L"SystemAccentColorLight2", Windows::UI::ColorHelper::FromArgb(0xFF, 0xA9, 0xE8, 0xA4));
        put(L"SystemAccentColorLight1", Windows::UI::ColorHelper::FromArgb(0xFF, 0x78, 0xD8, 0x72));
        put(L"SystemAccentColorDark1", Windows::UI::ColorHelper::FromArgb(0xFF, 0x37, 0x9E, 0x29));
        put(L"SystemAccentColorDark2", Windows::UI::ColorHelper::FromArgb(0xFF, 0x2B, 0x7A, 0x20));
        put(L"SystemAccentColorDark3", Windows::UI::ColorHelper::FromArgb(0xFF, 0x1F, 0x5C, 0x18));
    }

    inline Microsoft::UI::Xaml::Media::Brush CardBrush()
    {
        using namespace Microsoft::UI::Xaml;
        try
        {
            if (auto v = Application::Current().Resources().Lookup(winrt::box_value(L"CardBackgroundFillColorDefaultBrush")))
                return v.as<Media::Brush>();
        }
        catch (...)
        {
        }
        return Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x2B, 0x2B, 0x2B) };
    }

    inline Microsoft::UI::Xaml::Media::Brush CardStroke()
    {
        using namespace Microsoft::UI::Xaml;
        try
        {
            if (auto v = Application::Current().Resources().Lookup(winrt::box_value(L"CardStrokeColorDefaultBrush")))
                return v.as<Media::Brush>();
        }
        catch (...)
        {
        }
        return Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x3A, 0x3A, 0x3A) };
    }

    inline Microsoft::UI::Xaml::Media::SolidColorBrush GoodBrush()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x7B, 0xE3, 0x82) };
    }

    inline Microsoft::UI::Xaml::Media::SolidColorBrush DimBrush()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x9A, 0x9A, 0x9A) };
    }

    // Instance-card status colors. One hue per state so Idle / Preparing /
    // Running read at a glance from the rail, dot, pill and card border.
    inline Windows::UI::Color RailIdleColor()
    {
        return Windows::UI::ColorHelper::FromArgb(0xFF, 0x3D, 0x3D, 0x3D);
    }
    inline Windows::UI::Color RailRunningColor()
    {
        return Windows::UI::ColorHelper::FromArgb(0xFF, 0x44, 0xBD, 0x32);
    }
    inline Windows::UI::Color RailPreparingColor()
    {
        return Windows::UI::ColorHelper::FromArgb(0xFF, 0xE0, 0xA6, 0x3C);
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush RailIdleBrush()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ RailIdleColor() };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush RailRunningBrush()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ RailRunningColor() };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush RailPreparingBrush()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ RailPreparingColor() };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush PillIdleBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0x26, 0x9A, 0x9A, 0x9A) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush PillRunningBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0x2E, 0x44, 0xBD, 0x32) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush PillPreparingBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0x2E, 0xE0, 0xA6, 0x3C) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush PillRunningForeground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x8B, 0xE8, 0x8E) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush PillPreparingForeground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0xF2, 0xC8, 0x7A) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush IconFabricBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x24, 0x5C, 0x20) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush IconVanillaBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x38, 0x38, 0x38) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush IconForeground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0xF2, 0xF2, 0xF2) };
    }
    inline Microsoft::UI::Xaml::Media::Brush SubtleFillBrush()
    {
        using namespace Microsoft::UI::Xaml;
        try
        {
            if (auto v = Application::Current().Resources().Lookup(winrt::box_value(L"CardBackgroundFillColorSecondaryBrush")))
                return v.as<Media::Brush>();
        }
        catch (...)
        {
        }
        return Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x24, 0x24, 0x24) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush LogBackgroundBrush()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x1B, 0x1D, 0x1B) };
    }
}
