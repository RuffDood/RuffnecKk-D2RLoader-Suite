#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

enum class NativeSettingsAction {
    RevealLevel,
    RevealAct,
    ToggleRevealAll,
    DisableRevealAll,
};

using NativeSettingsActionCallback = void (*)(
    NativeSettingsAction action,
    void* userData) noexcept;

struct NativeSettingsPanelCallbacks {
    NativeSettingsActionCallback onAction{};
    void* userData{};
};

inline constexpr std::string_view NativeSettingsMessageTarget =
    "PanelManager";
inline constexpr std::string_view NativeSettingsMessageCommand =
    "OpenPanel";

struct NativeSettingsMessageBinding {
    std::string_view text;
    NativeSettingsAction action;
};

inline constexpr std::array NativeSettingsMessageBindings{
    NativeSettingsMessageBinding{
        "RuffnecKkMapSenseRevealLevel",
        NativeSettingsAction::RevealLevel,
    },
    NativeSettingsMessageBinding{
        "RuffnecKkMapSenseRevealAct",
        NativeSettingsAction::RevealAct,
    },
    NativeSettingsMessageBinding{
        "RuffnecKkMapSenseRevealAll",
        NativeSettingsAction::ToggleRevealAll,
    },
    NativeSettingsMessageBinding{
        "RuffnecKkMapSenseRevealOff",
        NativeSettingsAction::DisableRevealAll,
    },
};

[[nodiscard]] constexpr auto ClassifyNativeSettingsMessage(
        std::string_view target,
        std::string_view command,
        std::string_view text) noexcept
        -> std::optional<NativeSettingsAction> {
    if (target != NativeSettingsMessageTarget
        || command != NativeSettingsMessageCommand) {
        return std::nullopt;
    }
    for (const auto& binding : NativeSettingsMessageBindings) {
        if (binding.text == text) return binding.action;
    }
    return std::nullopt;
}

struct NativeSettingsPanelStatus {
    bool ready{};
    bool open{};
};

// Register the SDK-owned resource, panel, and private UI-message listener.
// Call during D2RLoaderLoadPlugin while the plugin owner is active.
auto InitializeNativeSettingsPanel(
    const D2RL::PluginContext* context,
    NativeSettingsPanelCallbacks callbacks = {}) noexcept -> bool;

// Explicitly unregister every owner-scoped SDK object. Call during unload.
void ShutdownNativeSettingsPanel() noexcept;

// Toggle the registered panel. The caller must already be on D2RLoader's UI
// thread (for example, from MapSense's existing UI-thread action drain).
auto ToggleNativeSettingsPanel() noexcept -> bool;

[[nodiscard]] auto GetNativeSettingsPanelStatus() noexcept
    -> NativeSettingsPanelStatus;

} // namespace RuffnecKk::MapSense
