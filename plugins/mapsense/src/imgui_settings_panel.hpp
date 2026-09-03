#pragma once

#include "mapsense_config.hpp"

#include <cstdint>

namespace RuffnecKk::MapSense {

enum class ImGuiSettingsAction : std::uint8_t {
    ToggleRevealMap,
};

using ImGuiSettingsActionCallback = void (*)(ImGuiSettingsAction) noexcept;

// Describes the complete screen-space region owned by the panel this frame.
// Input routing can use Contains() to keep mouse messages inside that region
// from reaching D2R.
struct ImGuiSettingsBounds {
    bool visible{};
    bool expanded{};
    bool saveRequested{};
    float left{};
    float top{};
    float right{};
    float bottom{};

    [[nodiscard]] auto Contains(float x, float y) const noexcept -> bool {
        return visible
            && x >= left
            && x < right
            && y >= top
            && y < bottom;
    }
};

// Draws the persistent MapSense launcher or its expanded settings panel.
// Closing the expanded window collapses it back to the launcher.
[[nodiscard]] auto DrawImGuiSettingsPanel(
    Config& config,
    bool& expanded,
    bool revealMapEnabled,
    ImGuiSettingsActionCallback actionCallback) noexcept
    -> ImGuiSettingsBounds;

} // namespace RuffnecKk::MapSense
