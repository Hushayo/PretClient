#pragma once

#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include "MainWindow.h"

namespace winrt::PretClient
{
    struct App : Microsoft::UI::Xaml::ApplicationT<App, Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
    {
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type);
        Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(hstring const& fullname);
        com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions();

    private:
        Microsoft::UI::Xaml::Window m_window{ nullptr };
        Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider m_provider;
    };
}
