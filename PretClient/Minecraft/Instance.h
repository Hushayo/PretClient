#pragma once

#include <vector>

// One launcher instance: a named MC version + loader combo.
// Game files live in the shared roaming .minecraft folder.
namespace winrt::PretClient
{
    struct Instance
    {
        hstring id{};
        hstring name{};
        hstring mcVersion{ L"1.21.4" };
        hstring loader{ L"vanilla" }; // "vanilla" | "fabric" | "quilt" | "forge" | "neoforge"
        hstring loaderVersion{};
        int maxMemMb = 2048;
    };

    std::vector<Instance> LoadInstances();
    void SaveInstances(std::vector<Instance> const& instances);
    hstring NewInstanceId();
}
