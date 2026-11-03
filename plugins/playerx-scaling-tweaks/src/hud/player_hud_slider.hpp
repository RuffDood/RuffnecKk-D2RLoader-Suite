// Adapted from MadMike616/players-hud-slider, MIT (2026 MadMike616). See LICENSE.
#pragma once

#include "hud_player_slider_config.hpp"
#include "native_slider_panel.hpp"
#include "direct_click.hpp"

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace players_hud_slider {

class PlayerHudSlider {
public:
    using Read = std::int32_t(*)() noexcept;
    using CanApply = bool(*)() noexcept;
    using Prepare = bool(*)() noexcept;
    using Apply = bool(*)(std::int32_t) noexcept;

    auto Start(
            const D2RL::PluginContext* context,
            const HudPlayerSliderConfig& config,
            std::int32_t maximumPlayers,
            Read read,
            CanApply canApply,
            Prepare prepare,
            Apply apply) noexcept -> bool;
    void Stop() noexcept;
    void SetCurrentPlayers(std::int32_t players) noexcept;

private:
    static constexpr UINT_PTR RefreshTimerId = 0xD2B1;
    static constexpr UINT_PTR NativeDragTimerId = 0xD2B2;
    static constexpr UINT DetachMessage = WM_APP + 0x2B2;
    static constexpr std::int32_t VirtualLayoutHeight = 1801;

    const D2RL::PluginContext* context_{};
    const D2RL::LifecycleServiceV1* lifecycle_{};
    const D2RL::ThreadServiceV1* threads_{};
    std::atomic_bool pendingReady_{};
    static void __cdecl OnSessionTransition(const D2RL::PluginContext*, void*) noexcept;
    std::array<D2RL::Lifecycle::ListenerHandle, 3> listeners_{};
    HudPlayerSliderConfig config_{};
    std::int32_t maximumPlayers_{8};
    Read read_{};
    CanApply canApply_{};
    Prepare prepare_{};
    Apply apply_{};
    NativeSliderPanel nativePanel_{};
    std::atomic<std::int32_t> currentPlayers_{1};
    std::atomic_bool active_{};
    bool sessionReady_{};
    bool sliderOpen_{};
    bool nativeDragActive_{};
    DirectClick directClick_;
    std::int32_t selectedPlayers_{1};
    HWND hostWindow_{};
    HWND timerWindow_{};
    WNDPROC previousWindowProc_{};

    static void __cdecl OnGameplay(
        const D2RL::PluginContext* context,
            const D2RL::Lifecycle::GameplayEvent* event,
            void* userData) noexcept;
    static LRESULT CALLBACK WindowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    auto RegisterListeners() noexcept -> bool;
    void OnSessionReady() noexcept;
    void OnSessionEnded() noexcept;
    auto CreateInteractionTimer() noexcept -> bool;
    void DestroyInteractionTimer() noexcept;
    void ToggleSlider() noexcept;
    void PollPointer() noexcept;
    void ProcessPointerSample(bool down, bool pointerReady, POINT point, bool foreground) noexcept;
    auto LogicalPoint(POINT screenPoint, POINT& output) const noexcept -> bool;
    auto IsSliderPoint(POINT logicalPoint) const noexcept -> bool;
    auto MarkerLeft(std::int32_t players) const noexcept -> double;
    auto MarkerCenter(std::int32_t players) const noexcept -> double;
    auto ResolvePlayerCount(POINT logicalPoint) const noexcept -> std::int32_t;
};

} // namespace players_hud_slider
