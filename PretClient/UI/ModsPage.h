#pragma once

#include "../Minecraft/Instance.h"
#include "../Minecraft/Modrinth.h"

// Mods page: live Modrinth search with loader/MC filters, per-row install
// into the selected instance's mods folder.
namespace winrt::PretClient
{
    struct ModsPage
    {
        ModsPage();
        Microsoft::UI::Xaml::Controls::StackPanel Root() const
        {
            return m_root;
        }
        void RefreshInstances();

    private:
        winrt::fire_and_forget OnSearch();
        winrt::fire_and_forget OnInstall(Modrinth::ModHit hit);
        winrt::fire_and_forget InstallOneFile(Modrinth::ModFile file, std::wstring modsDir);
        void SetStatus(hstring const& line);

        Microsoft::UI::Xaml::Controls::StackPanel m_root{};
        Microsoft::UI::Xaml::Controls::TextBox m_query{};
        Microsoft::UI::Xaml::Controls::ComboBox m_loader{};
        Microsoft::UI::Xaml::Controls::TextBox m_mc{};
        Microsoft::UI::Xaml::Controls::ComboBox m_target{};
        Microsoft::UI::Xaml::Controls::StackPanel m_results{};
        Microsoft::UI::Xaml::Controls::TextBlock m_status{};
        std::vector<Instance> m_targets{};
    };
}
