#pragma once

#include "../Minecraft/Instance.h"
#include "../System/Stats.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

// Instances page: long cards with loader badge, version, live CPU/RAM/GPU,
// game log, progress bar with speed, Play / Stop / Restart, per-instance
// mods manager (any loader: enable/disable, remove, update check),
// per-instance resource packs manager (all loaders incl. vanilla: add .zip,
// enable/disable, remove), delete, New dialog fed by the real piston-meta
// manifest (release/snapshot/beta/alpha). Profiles dialog is hosted here
// but opened from the MainWindow top-right round avatar.
namespace winrt::PretClient
{
    namespace Downloader
    {
        struct PreparedGame;
    }

    struct InstancesPage
    {
        InstancesPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
        {
            return m_root;
        }
        void Refresh();
        winrt::fire_and_forget ProfileDialog();
        void SetOnProfileChanged(std::function<void()> cb)
        {
            m_onProfileChanged = std::move(cb);
        }

    private:
        // Last known download progress per instance. Cards are rebuilt on
        // every Refresh (including tab switches), so progress is stashed
        // here and repainted onto the fresh card -- it never "poofs".
        struct DownloadState
        {
            hstring label{};
            unsigned long long done = 0;
            unsigned long long total = 0;
            double bps = 0.0;
        };

        struct Card
        {
            hstring id{};
            Microsoft::UI::Xaml::Controls::Border frame{ nullptr };
            Microsoft::UI::Xaml::Controls::Border rail{ nullptr };
            Microsoft::UI::Xaml::Shapes::Ellipse dot{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock stateLabel{ nullptr };
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
        // Single place that renders a DownloadState onto a card, so live
        // updates and Refresh-restores can never disagree (and the bar can
        // never jump backwards from two writers).
        static void PaintProgress(Card& card, hstring const& label,
            unsigned long long done, unsigned long long total, double bps);
        // Samples CPU/RAM/GPU + log tails on a background thread (PDH GPU
        // queries can stall for seconds on flaky drivers and must never run
        // on the UI thread), then applies the text updates on top of it.
        winrt::fire_and_forget UpdateStatsAsync();
        static hstring TailText(std::filesystem::path const& file);
        // Copy the instance's enabled mods into <gameDir>/mods right before
        // launch (loaders only read that folder, game files stay shared).
        static void StageMods(std::filesystem::path const& instanceMods,
            std::filesystem::path const& gameMods);
        // Copy the instance's enabled resource packs into
        // <gameDir>/resourcepacks right before launch (the game only reads
        // that folder, game files stay shared). Runs for every loader
        // including vanilla.
        static void StageResourcePacks(std::filesystem::path const& instancePacks,
            std::filesystem::path const& gamePacks);
        winrt::fire_and_forget PlayInstance(hstring id);
        // Post-prepare launch: mod/pack staging + Java probing run on a
        // background thread (process spawns with long waits must never block
        // the UI thread), then the build/start happens back on top of it.
        winrt::fire_and_forget FinishLaunch(hstring id, hstring username, hstring javaPath,
            int minMem, int maxMem, Downloader::PreparedGame game, bool isModded,
            std::filesystem::path instanceMods, std::filesystem::path gameMods,
            std::filesystem::path instancePacks, std::filesystem::path gamePacks,
            bool fpsBoost, hstring extraJvmArgs, bool highPriority);
        winrt::fire_and_forget ModsDialog(hstring id);
        winrt::fire_and_forget ResourcePacksDialog(hstring id);
        winrt::fire_and_forget AddDialog();
        winrt::fire_and_forget ShowCreateDialog(
            Microsoft::UI::Xaml::Controls::ContentDialog dialog,
            Microsoft::UI::Xaml::Controls::TextBox nameBox,
            Microsoft::UI::Xaml::Controls::ComboBox versionBox,
            Microsoft::UI::Xaml::Controls::ComboBox loaderBox,
            Microsoft::UI::Xaml::Controls::ComboBox loaderVerBox);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::StackPanel m_cards{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        std::function<void()> m_onProfileChanged{};
        std::vector<Card> m_cardList{};
        SystemStats::Sampler m_sampler{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // Overlap guard: if one sample is still stuck (slow disk/PDH), the
        // next tick skips instead of stacking up.
        std::atomic<bool> m_statsBusy{ false };
        std::map<std::wstring, DownloadState> m_downloads{};
        // Instances with a prepare/download currently in flight. Blocks a
        // second Play (which would start a duplicate download fighting over
        // the same progress bar) until the first finishes or fails.
        std::set<std::wstring> m_preparing{};
    };
}
