#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.xaml.g.cpp")
#include "MainWindow.xaml.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::PretClient::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"PretClient");
    }

    int32_t MainWindow::MyProperty()
    {
        return m_myProperty;
    }

    void MainWindow::MyProperty(int32_t value)
    {
        m_myProperty = value;
    }

    void MainWindow::myButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        myButton().Content(box_value(L"Clicked"));
    }
}
