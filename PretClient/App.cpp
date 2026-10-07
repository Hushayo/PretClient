#include "pch.h"
#include "App.h"
#include "UI/Theme.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Markup;
using namespace Windows::UI::Xaml::Interop;

namespace winrt::PretClient
{
    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        Theme::ApplyBranding();
        Resources().MergedDictionaries().Append(XamlControlsResources());
        m_window = make<MainWindow>();
        m_window.Activate();
    }

    IXamlType App::GetXamlType(TypeName const& type)
    {
        return m_provider.GetXamlType(type);
    }

    IXamlType App::GetXamlType(hstring const& fullname)
    {
        return m_provider.GetXamlType(fullname);
    }

    com_array<XmlnsDefinition> App::GetXmlnsDefinitions()
    {
        return m_provider.GetXmlnsDefinitions();
    }
}
