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

        // The stock AccentButtonStyle froze the Light2 accent brush at
        // dictionary load, so changing the accent ramp alone left primary
        // buttons a washed-out mint next to the saturated brand green.
        // Replace the style itself with a flat brand template whose states
        // are explicit; this also keeps hover/pressed from drifting back to
        // the pale system brushes.
        constexpr wchar_t kAccentStyleXaml[] = LR"XAML(
<ResourceDictionary
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <Style x:Key="AccentButtonStyle" TargetType="Button">
    <Setter Property="Background" Value="#44BD32" />
    <Setter Property="Foreground" Value="#08140A" />
    <Setter Property="BorderBrush" Value="#44BD32" />
    <Setter Property="BorderThickness" Value="1" />
    <Setter Property="CornerRadius" Value="4" />
    <Setter Property="Padding" Value="12,6" />
    <Setter Property="MinHeight" Value="32" />
    <Setter Property="FontWeight" Value="SemiBold" />
    <Setter Property="HorizontalContentAlignment" Value="Center" />
    <Setter Property="VerticalContentAlignment" Value="Center" />
    <Setter Property="UseSystemFocusVisuals" Value="True" />
    <Setter Property="Template">
      <Setter.Value>
        <ControlTemplate TargetType="Button">
          <ContentPresenter
              x:Name="ContentPresenter"
              Background="{TemplateBinding Background}"
              BackgroundSizing="{TemplateBinding BackgroundSizing}"
              BorderBrush="{TemplateBinding BorderBrush}"
              BorderThickness="{TemplateBinding BorderThickness}"
              CornerRadius="{TemplateBinding CornerRadius}"
              Padding="{TemplateBinding Padding}"
              Content="{TemplateBinding Content}"
              ContentTemplate="{TemplateBinding ContentTemplate}"
              ContentTransitions="{TemplateBinding ContentTransitions}"
              Foreground="{TemplateBinding Foreground}"
              HorizontalContentAlignment="{TemplateBinding HorizontalContentAlignment}"
              VerticalContentAlignment="{TemplateBinding VerticalContentAlignment}"
              AutomationProperties.AccessibilityView="Raw">
            <ContentPresenter.BackgroundTransition>
              <BrushTransition Duration="0:0:0.083" />
            </ContentPresenter.BackgroundTransition>
            <VisualStateManager.VisualStateGroups>
              <VisualStateGroup x:Name="CommonStates">
                <VisualState x:Name="Normal" />
                <VisualState x:Name="PointerOver">
                  <Storyboard>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="Background">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#53CC40" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="BorderBrush">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#53CC40" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                  </Storyboard>
                </VisualState>
                <VisualState x:Name="Pressed">
                  <Storyboard>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="Background">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#369E27" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="BorderBrush">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#369E27" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                  </Storyboard>
                </VisualState>
                <VisualState x:Name="Disabled">
                  <Storyboard>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="Background">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#2C3A29" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="BorderBrush">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#2C3A29" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="ContentPresenter" Storyboard.TargetProperty="Foreground">
                      <DiscreteObjectKeyFrame KeyTime="0">
                        <DiscreteObjectKeyFrame.Value>
                          <SolidColorBrush Color="#6E7F6A" />
                        </DiscreteObjectKeyFrame.Value>
                      </DiscreteObjectKeyFrame>
                    </ObjectAnimationUsingKeyFrames>
                  </Storyboard>
                </VisualState>
              </VisualStateGroup>
            </VisualStateManager.VisualStateGroups>
          </ContentPresenter>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
</ResourceDictionary>
)XAML";

        try
        {
            auto dict = Markup::XamlReader::Load(hstring{ kAccentStyleXaml }).as<ResourceDictionary>();
            res.Insert(box_value(L"AccentButtonStyle"),
                dict.Lookup(box_value(L"AccentButtonStyle")));
        }
        catch (...)
        {
            // Keep the stock style if the template cannot be parsed; the
            // rest of the app is still usable with pale accent buttons.
        }
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
    inline Microsoft::UI::Xaml::Media::SolidColorBrush IconQuiltBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x3B, 0x2A, 0x5E) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush IconForgeBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x5E, 0x3D, 0x16) };
    }
    inline Microsoft::UI::Xaml::Media::SolidColorBrush IconNeoForgeBackground()
    {
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ Windows::UI::ColorHelper::FromArgb(0xFF, 0x5E, 0x20, 0x20) };
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
