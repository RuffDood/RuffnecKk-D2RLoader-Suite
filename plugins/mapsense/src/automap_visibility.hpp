#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

namespace RuffnecKk::MapSense {

auto InitializeAutomapVisibility(
    const D2RL::PluginContext* context,
    bool diagnostics) noexcept -> bool;
void ShutdownAutomapVisibility() noexcept;
void ResetAutomapVisibility() noexcept;
auto IsNativeAutomapVisible() noexcept -> bool;
auto GetAutomapRenderPulses() noexcept -> std::uint64_t;

} // namespace RuffnecKk::MapSense
