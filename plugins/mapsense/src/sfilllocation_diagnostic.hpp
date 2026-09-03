#pragma once

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

// Validate both complete native branch witnesses before MapSense installs any
// hook that may materialize a client room.
[[nodiscard]] auto ValidateSFillLocationDiagnosticSuppression(
    const D2RL::PluginContext* context) noexcept -> bool;

// Suppress only the two guarded negative-index diagnostic CALLs. The patches are
// registered through D2RLoader so ownership and shutdown restoration remain
// attached to this plugin.
[[nodiscard]] auto InstallSFillLocationDiagnosticSuppression(
    const D2RL::PluginContext* context) noexcept -> bool;

void ShutdownSFillLocationDiagnosticSuppression() noexcept;

} // namespace RuffnecKk::MapSense
