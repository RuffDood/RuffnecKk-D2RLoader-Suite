// Adapted from MadMike616/players-hud-slider, MIT (2026 MadMike616). See LICENSE.
#pragma once

#include <cstdint>

namespace players_hud_slider {

inline constexpr std::int32_t MaximumHudPlayerCount = 65535;

struct HudElementConfig {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{1};
    std::int32_t height{1};
};

struct HudPlayerSliderConfig {
    bool enabled{true};
    std::int32_t minimumPlayers{1};
    HudElementConfig button{1571, 1708, 58, 58};
    HudElementConfig track{960, 1612, 1280, 34};
    // x/y are the p1 marker's top-left position. Its rightmost position is
    // derived from the configured track width so the steps stay evenly spaced.
    HudElementConfig marker{970, 1593, 54, 72};
    // Tooltip x/y are offsets from the marker's top-left position.
    HudElementConfig tooltip{-32, -68, 118, 48};
    // Tooltip text x/y are offsets from the tooltip plaque's top-left.
    HudElementConfig tooltipText{0, 0, 118, 48};
    // Tick x/y are offsets from the marker center and track center.
    HudElementConfig tick{0, 0, 2, 10};
};

} // namespace players_hud_slider
