#pragma once

#include "../Minecraft/Instance.h"
#include "../System/Stats.h"
#include <atomic>

// Instances page: long cards with loader badge, version, live CPU/RAM/GPU,
// game log, progress bar with speed, Play / Stop / Restart, per-instance
// mods manager (fabric only: enable/disable, remove, update check), delete,
// profile switching, New dialog fed by the real piston-meta manifest
// (release/snapshot/beta/alpha).
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
        };

        Card* FindCard(hstring const& id);
        void SetStatus(hstring const& line);
        // Samples CPU/RAM/GPU + log tails on a background thread (PDH GPU
        // queries can stall for seconds on flaky drivers and must never run
        // on the UI thread), then applies the text updates on top of it.
        winrt::fire_and_forget UpdateStatsAsync();
        static hstring TailText(std::filesystem::path const& file);
        // Copy the instance's enabled mods into <gameDir>/mods right before
        // launch (Fabric only reads that folder, game files stay shared).
        static void StageMods(std::filesystem::path const& instanceMods,
            std::filesystem::path const& gameMods);
        winrt::fire_and_forget PlayInstance(hstring id);
        winrt::fire_and_forget ModsDialog(hstring id);
        winrt::fire_and_forget AddDialog();
        winrt::fire_and_forget ShowCreateDialog(
            Microsoft::UI::Xaml::Controls::ContentDialog dialog,
            Microsoft::UI::Xaml::Controls::TextBox nameBox,
            Microsoft::UI::Xaml::Controls::ComboBox versionBox,
            Microsoft::UI::Xaml::Controls::ComboBox loaderBox,
            Microsoft::UI::Xaml::Controls::TextBox loaderVerBox);
        winrt::fire_and_forget ProfileDialog();

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::StackPanel m_cards{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        Microsoft::UI::Xaml::Controls::Button m_profile{};
        std::vector<Card> m_cardList{};
        SystemStats::Sampler m_sampler{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // Overlap guard: if one sample is still stuck (slow disk/PDH), the
        // next tick skips instead of stacking up.
        std::atomic<bool> m_statsBusy{ false };
    };
}
