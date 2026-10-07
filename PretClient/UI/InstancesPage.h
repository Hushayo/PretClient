#pragma once

#include "../Minecraft/Instance.h"
#include "../System/Stats.h"

// Instances page: long cards with loader badge, version, live CPU/RAM/GPU,
// Play / Stop / Restart, Fabric API install, delete, and a New dialog fed
// by the real piston-meta manifest (release/snapshot/beta/alpha).
namespace winrt::PretClient
{
    struct InstancesPage
    {
        InstancesPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
        {
            return m_root;
        }
        void Refresh();

    private:
        struct Card
        {
            hstring id{};
            Microsoft::UI::Xaml::Controls::TextBlock stats{};
            Microsoft::UI::Xaml::Controls::Button play{};
            Microsoft::UI::Xaml::Controls::Button stop{};
            Microsoft::UI::Xaml::Controls::Button restart{};
        };

        void SetStatus(hstring const& line);
        void UpdateStats();
        winrt::fire_and_forget PlayInstance(hstring id);
        winrt::fire_and_forget AddDialog();
        winrt::fire_and_forget ShowCreateDialog(
            Microsoft::UI::Xaml::Controls::ContentDialog dialog,
            Microsoft::UI::Xaml::Controls::TextBox nameBox,
            Microsoft::UI::Xaml::Controls::ComboBox versionBox,
            Microsoft::UI::Xaml::Controls::ComboBox loaderBox,
            Microsoft::UI::Xaml::Controls::TextBox loaderVerBox);
        winrt::fire_and_forget InstallFabricApi(hstring id);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::StackPanel m_cards{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        std::vector<Card> m_cardList{};
        SystemStats::Sampler m_sampler{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
    };
}
