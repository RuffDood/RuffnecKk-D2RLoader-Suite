#pragma once

#include "mapsense_config.hpp"

#include <cstdint>

namespace RuffnecKk::MapSense {

struct OverlayRuntimeStatus {
    bool threadRunning{};
    bool attachedToGame{};
    bool deviceReady{};
    bool menuOpen{};
    std::uint64_t frames{};
};

using OverlaySaveCallback = void(*)(const Config& config) noexcept;
using OverlayLogCallback = void(*)(const char* message) noexcept;

auto StartOverlayWindow(
    const Config& config,
    OverlaySaveCallback saveCallback,
    OverlayLogCallback infoCallback,
    OverlayLogCallback warningCallback) noexcept -> bool;
void StopOverlayWindow() noexcept;
void ToggleOverlayMenu() noexcept;
auto GetOverlayRuntimeStatus() noexcept -> OverlayRuntimeStatus;

} // namespace RuffnecKk::MapSense
