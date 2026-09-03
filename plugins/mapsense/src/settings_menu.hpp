#pragma once

#include "mapsense_config.hpp"

namespace RuffnecKk::MapSense {

using SettingsSaveCallback = void (*)(const Config&) noexcept;

struct SettingsSurfaceBounds {
    bool visible{};
    float left{};
    float top{};
    float right{};
    float bottom{};
};

// Applies the MapSense black, brown, and gold presentation to the current
// Dear ImGui context. Call once after creating the context.
void ApplyD2RStyle() noexcept;

// Draws one movable settings surface inside the full click-through canvas.
// The title-bar chevron collapses the panel to a small persistent launcher.
// The returned bounds describe the only screen region that may consume input.
auto DrawSettingsSurface(
    Config& draft,
    bool& expanded,
    SettingsSaveCallback saveCallback) noexcept -> SettingsSurfaceBounds;

} // namespace RuffnecKk::MapSense
