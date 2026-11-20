#pragma once

#include "mapsense_config.hpp"
#if defined(RUFFNECKK_MAPSENSE_ENABLE_GPS_ROUTE_DIAGNOSTICS) \
    && RUFFNECKK_MAPSENSE_ENABLE_GPS_ROUTE_DIAGNOSTICS
#include "gps_route_artifact.hpp"
#endif

#include <array>
#include <cstdint>

namespace RuffnecKk::MapSense {

enum class ImGuiSettingsAction : std::uint8_t {
    ToggleRevealMap,
    ToggleRevealOnlyMapLabels,
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
// A new session closes the panel and resets its sections on the render thread.
[[nodiscard]] auto DrawImGuiSettingsPanel(
    Config& config,
    bool& expanded,
    std::uint64_t sessionGeneration,
    bool revealMapEnabled,
    bool revealOnlyMapLabelsEnabled,
    float menuScale,
    ImGuiSettingsActionCallback actionCallback
#if defined(RUFFNECKK_MAPSENSE_ENABLE_GPS_ROUTE_DIAGNOSTICS) \
    && RUFFNECKK_MAPSENSE_ENABLE_GPS_ROUTE_DIAGNOSTICS
    , std::array<const char*, NavigationLineKindCount> gpsRouteStatuses
#endif
    ) noexcept
    -> ImGuiSettingsBounds;

} // namespace RuffnecKk::MapSense
