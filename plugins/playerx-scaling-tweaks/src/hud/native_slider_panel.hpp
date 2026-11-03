// Adapted from MadMike616/players-hud-slider, MIT (2026 MadMike616). See LICENSE.
#pragma once

#include "hud_player_slider_config.hpp"

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/panels.h>
#include <D2RLPlugin/resources.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace players_hud_slider {

class NativeSliderPanel {
public:
    auto Start(
            const D2RL::PluginContext* context,
            const HudPlayerSliderConfig& config,
            std::int32_t maximumPlayers) noexcept -> bool;
    void Stop() noexcept;

    auto Open() noexcept -> bool;
    void Close() noexcept;
    auto IsOpen() const noexcept -> bool;
    void ShowHudButton() noexcept;
    void HideHudButton() noexcept;
    auto IsHudButtonOpen() const noexcept -> bool;
    void SetTooltipValue(std::int32_t players) noexcept;
    void QueueTooltipValue(std::int32_t players) noexcept;

private:
    static constexpr char PanelLocalId[] = "PlayerCountSlider";
    static constexpr char PanelQualifiedName[] =
        "ruffneckk-playerx-scaling-tweaks/PlayerCountSlider";
    static constexpr char LayoutResourcePath[] =
        "data/global/ui/layouts/ruffneckk-playerx-scaling-tweaks/PlayerCountSliderhd.json";
    static constexpr char TrackSpriteResourcePath[] =
        "data/hd/global/ui/d2rloader/ruffneckk-playerx-scaling-tweaks/native-track.sprite";
    static constexpr char MarkerSpriteResourcePath[] =
        "data/hd/global/ui/d2rloader/ruffneckk-playerx-scaling-tweaks/native-marker.sprite";
    static constexpr char TooltipSpriteResourcePath[] =
        "data/hd/global/ui/d2rloader/ruffneckk-playerx-scaling-tweaks/native-tooltip.sprite";
    static constexpr char HudButtonPanelLocalId[] = "HudButton";
    static constexpr char HudButtonPanelQualifiedName[] =
        "ruffneckk-playerx-scaling-tweaks/HudButton";
    static constexpr char HudButtonLayoutResourcePath[] =
        "data/global/ui/layouts/ruffneckk-playerx-scaling-tweaks/HudButtonhd.json";
    static constexpr char HudButtonSpriteResourcePath[] =
        "data/hd/global/ui/d2rloader/ruffneckk-playerx-scaling-tweaks/native-button.sprite";

    const D2RL::PluginContext* context_{};
    const D2RL::ResourceServiceV1* resources_{};
    const D2RL::PanelServiceV1* panels_{};
    const D2RL::ThreadServiceV1* threads_{};
    const D2RL::WidgetServiceV1* widgets_{};
    D2RL::Panels::RegistrationHandle panelHandle_{
        D2RL::Panels::InvalidHandle};
    D2RL::Panels::RegistrationHandle hudButtonPanelHandle_{
        D2RL::Panels::InvalidHandle};
    D2RL::Resources::RegistrationHandle layoutHandle_{
        D2RL::Resources::InvalidHandle};
    D2RL::Resources::RegistrationHandle hudButtonLayoutHandle_{
        D2RL::Resources::InvalidHandle};
    std::array<D2RL::Resources::RegistrationHandle, 4> spriteHandles_{};
    std::int32_t maximumPlayers_{1};
    std::int32_t minimumPlayers_{1};
    std::int32_t tooltipValue_{};
    std::atomic<std::int32_t> pendingTooltipValue_{};
    std::atomic_bool pendingHudButtonVisible_{};
    std::atomic_bool tooltipUpdateQueued_{};
    std::atomic_bool hudButtonUpdateQueued_{};
    bool tooltipWidgetsInitialized_{};
    bool started_{};

    auto RegisterResource(
            const char* path,
            const void* bytes,
            std::uint64_t byteCount,
            D2RL::Resources::RegistrationHandle& handle) noexcept -> bool;
    auto BuildLayout(
            const HudPlayerSliderConfig& config,
            std::int32_t maximumPlayers,
            std::string& output) noexcept -> bool;
    auto BuildHudButtonLayout(
            const HudPlayerSliderConfig& config,
            std::string& output) noexcept -> bool;
    void SetActualDigits(std::int32_t players, bool visible) noexcept;
    void SetTooltipWidgetsVisible(
            std::int32_t players,
            bool visible) noexcept;
    void HideAllTooltipWidgets() noexcept;
    void SetHudButtonVisible(bool visible) noexcept;
    void QueueHudButtonVisibility(bool visible) noexcept;
    void Cleanup() noexcept;

    static void __cdecl OnQueuedTooltipValue(
            const D2RL::PluginContext* context,
            void* userData) noexcept;
    static void __cdecl OnQueuedHudButtonVisibility(
            const D2RL::PluginContext* context,
            void* userData) noexcept;
};

} // namespace players_hud_slider
