#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace players_hud_slider {
inline constexpr std::int32_t HudSelectionMaximum = 24;
constexpr auto SelectionMaximum(std::int32_t commandMaximum) noexcept {
    return std::clamp(commandMaximum, 1, HudSelectionMaximum);
}
inline auto CountAt(double fraction, std::int32_t minimum, std::int32_t maximum) noexcept {
    if (minimum > maximum) return minimum;
    return std::clamp(static_cast<std::int32_t>(std::lround(minimum
        + std::clamp(fraction, 0.0, 1.0) * (maximum - minimum))), minimum, maximum);
}
constexpr auto DisplayCount(std::int32_t actual) noexcept { return std::clamp(actual, 1, 65535); }
} // namespace players_hud_slider
