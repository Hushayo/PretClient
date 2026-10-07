#pragma once

#include "../Minecraft/Instance.h"
#include "../System/Stats.h"

// Instances page: long cards with loader badge, version, live CPU/RAM/GPU,
// game log, progress bar with speed, Play / Stop / Restart, Fabric API,
// per-instance mods (fabric only), delete, profile switching, New dialog fed
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
            Microsoft::UI::Xaml::Controls::ProgressBar prog{};
            Microsoft::UI::Xaml::Controls::TextBlock progText{};
            Microsoft::UI::Xaml::Controls::TextBox gamelog{};
            Microsoft::UI::Xaml::Controls::StackPanel modsBox{};
        };

        Card* FindCard(hstring const& id);
        void SetStatus(hstring const& line);
        void UpdateStats();
        void RefreshMods(Card& card, Instance const& inst);
        static hstring TailText(std::filesystem::path const& file);
        winrt::fire_and_forget PlayInstance(hstring id);
        winrt::fire_and_forget AddDialog();
        winrt::fire_and_forget ShowCreateDialog(
            Microsoft::UI::Xaml::Controls::ContentDialog dialog,
            Microsoft::UI::Xaml::Controls::TextBox nameBox,
            Microsoft::UI::Xaml::Controls::ComboBox versionBox,
            Microsoft::UI::Xaml::Controls::ComboBox loaderBox,
            Microsoft::UI::Xaml::Controls::TextBox loaderVerBox);
        winrt::fire_and_forget InstallFabricApi(hstring id);
        winrt::fire_and_forget ProfileDialog();

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::StackPanel m_cards{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        Microsoft::UI::Xaml::Controls::Button m_profile{};
        std::vector<Card> m_cardList{};
        SystemStats::Sampler m_sampler{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
    };
}
