#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace MapSeedSelector::NativeDropdownArt {

struct SpriteBundle {
    std::vector<std::uint8_t> dropdown{};
    std::vector<std::uint8_t> dropdownLowEnd{};
    std::vector<std::uint8_t> optionTile{};
    std::vector<std::uint8_t> optionTileLowEnd{};
};

// Must run from the DataTablesLoaded callback, on the game thread.
auto Load(const D2RL::PluginContext* context, SpriteBundle& sprites,
        std::string& error) noexcept -> bool;

} // namespace MapSeedSelector::NativeDropdownArt
