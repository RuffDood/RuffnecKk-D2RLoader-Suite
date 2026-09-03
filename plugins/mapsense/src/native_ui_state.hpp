#pragma once

#include <D2RLPlugin/api.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::MapSense {

inline constexpr std::size_t NativeUiStateCount = 32U;
inline constexpr std::size_t NativeUiQuestPanelState = 14U;
inline constexpr std::uint64_t NativeUiPanelVisibilityLifetimeMilliseconds =
    500U;
inline constexpr std::uint32_t NativeUiGameStateMask =
    std::uint32_t{1U} << 0U;
inline constexpr std::uint32_t NativeUiAutomapStateMask =
    std::uint32_t{1U} << 10U;

// MapSense-owned map pixels may be submitted only while D2R proves that both
// the gameplay world and its native automap are active. Loading screens clear
// at least one of these states, so stale render caches cannot bleed across a
// waypoint or act transition.
[[nodiscard]] constexpr auto IsNativeGameplayAutomapFrame(
        std::uint32_t activeMask) noexcept -> bool {
    constexpr auto requiredMask = NativeUiGameStateMask
        | NativeUiAutomapStateMask;
    return (activeMask & requiredMask) == requiredMask;
}

[[nodiscard]] constexpr auto ShouldDrawMapSenseOwnedVisualFrame(
        bool gameplayReady,
        std::uint32_t activeMask,
        bool localPlayerAlive) noexcept -> bool {
    return gameplayReady
        && IsNativeGameplayAutomapFrame(activeMask)
        && localPlayerAlive;
}

// D2R's 32-byte interface-state table mixes modal panels with a small set of
// world/HUD states. The native automap is submitted before those panels and is
// therefore hidden by their later draw. MapSense is submitted at Present, so
// it must reproduce that occlusion explicitly. The launcher/settings panel is
// deliberately independent from this map-pixel policy.
[[nodiscard]] constexpr auto IsNativeUiPanelState(
        std::size_t state) noexcept -> bool {
    switch (state) {
        case 0U:  // in-game/world state
        case 10U: // native automap
        case 12U: // item labels on the ground
        case 18U: // party portraits
        case 20U: // persistent world/HUD state observed with normal gameplay
        case 26U: // belt HUD
        case 28U: // avatar HUD
        case 29U: // persistent world/HUD state observed with normal gameplay
            return false;
        default:
            return state < NativeUiStateCount;
    }
}

[[nodiscard]] constexpr auto ShouldDrawMapSenseSettingsMenu(
        bool gameplayReady,
        bool) noexcept -> bool {
    // Native panels never make the MapSense launcher disappear. The second
    // argument is explicit so this exception remains covered by policy tests.
    return gameplayReady;
}

[[nodiscard]] constexpr auto ShouldDrawMapSenseOwnedMapOverlay(
        bool gameplayReady,
        bool) noexcept -> bool {
    // Side panels constrain map pixels later. They must not make all MapSense
    // content disappear from the free region.
    return gameplayReady;
}

enum class NativeUiMapPanelCoverage : std::uint8_t {
    None,
    Left,
    Right,
    Full,
};

struct NativeUiMapHorizontalClip final {
    std::int32_t left{};
    std::int32_t right{};
    NativeUiMapPanelCoverage coverage{NativeUiMapPanelCoverage::None};
};

// These are the non-panel states proven by live D2R 3.3 snapshots or by the
// governed native UI enum. They may coexist with the automap without hiding
// MapSense pixels.
inline constexpr std::uint32_t NativeUiWorldHudStateMask =
    NativeUiGameStateMask
    | NativeUiAutomapStateMask
    | (std::uint32_t{1U} << 12U)
    | (std::uint32_t{1U} << 18U)
    | (std::uint32_t{1U} << 20U)
    | (std::uint32_t{1U} << 26U)
    | (std::uint32_t{1U} << 28U)
    | (std::uint32_t{1U} << 29U);

inline constexpr std::uint32_t NativeUiLeftPanelStateMask =
    (std::uint32_t{1U} << 2U)  // Character Stats
    | (std::uint32_t{1U} << NativeUiQuestPanelState); // Quest Log

inline constexpr std::uint32_t NativeUiRightPanelStateMask =
    (std::uint32_t{1U} << 1U)  // Player Inventory
    | (std::uint32_t{1U} << 4U); // Skill Tree

[[nodiscard]] constexpr auto ClassifyNativeUiMapPanelCoverage(
        std::uint32_t activeMask) noexcept -> NativeUiMapPanelCoverage {
    constexpr auto classifiedMask = NativeUiWorldHudStateMask
        | NativeUiLeftPanelStateMask
        | NativeUiRightPanelStateMask;
    if ((activeMask & ~classifiedMask) != 0U) {
        // Central, full-screen and not-yet-classified panels fail closed.
        return NativeUiMapPanelCoverage::Full;
    }

    const auto hasLeft = (activeMask & NativeUiLeftPanelStateMask) != 0U;
    const auto hasRight = (activeMask & NativeUiRightPanelStateMask) != 0U;
    if (hasLeft && hasRight) return NativeUiMapPanelCoverage::Full;
    if (hasLeft) return NativeUiMapPanelCoverage::Left;
    if (hasRight) return NativeUiMapPanelCoverage::Right;
    return NativeUiMapPanelCoverage::None;
}

// Quest Log is exceptional in D2R 3.3: its top-level widget remains visible
// while interface-state 0x0E is not kept asserted, and the local-player
// automap pass stops refreshing. A recent UI-thread visibility observation may
// therefore retain the last value-only projection, but only while the native
// automap is still asserted and no incompatible panel is active.
[[nodiscard]] constexpr auto ShouldRetainNativeAutomapProjectionForQuest(
        std::uint32_t activeMask,
        bool questVisibilityKnown,
        bool questPanelVisible,
        std::uint64_t questVisibilityTick,
        std::uint64_t currentTick) noexcept -> bool {
    if (!questVisibilityKnown || !questPanelVisible
        || questVisibilityTick == 0U || currentTick < questVisibilityTick
        || currentTick - questVisibilityTick
            > NativeUiPanelVisibilityLifetimeMilliseconds
        || (activeMask & (std::uint32_t{1U} << 10U)) == 0U) {
        return false;
    }
    const auto effectiveMask = activeMask
        | (std::uint32_t{1U} << NativeUiQuestPanelState);
    return ClassifyNativeUiMapPanelCoverage(effectiveMask)
        == NativeUiMapPanelCoverage::Left;
}

// D2R's HD in-game side panels use a governed 2560x1440 layout space. The
// standard left panel ends at x=81+1250 and the standard right panel begins at
// screenWidth-1342. Scale by the limiting display dimension, exactly as the HD
// UI profile does. A small inward margin prevents antialiased MapSense pixels
// from touching panel artwork. Unknown/full coverage returns false so callers
// emit no MapSense map pixels for that frame.
[[nodiscard]] constexpr auto TryResolveNativeUiMapHorizontalClip(
        std::int32_t nativeWidth,
        std::int32_t nativeHeight,
        std::uint32_t activeMask,
        NativeUiMapHorizontalClip& output) noexcept -> bool {
    if (nativeWidth <= 0 || nativeWidth > 32768
        || nativeHeight <= 0 || nativeHeight > 32768) {
        return false;
    }

    const auto coverage = ClassifyNativeUiMapPanelCoverage(activeMask);
    if (coverage == NativeUiMapPanelCoverage::Full) return false;

    constexpr std::int64_t referenceWidth = 2560;
    constexpr std::int64_t referenceHeight = 1440;
    constexpr std::int64_t leftPanelFarEdge = 1331;
    constexpr std::int64_t rightPanelNearEdgeOffset = 1342;
    constexpr std::int32_t safetyMargin = 8;
    const auto widthLimited = static_cast<std::int64_t>(nativeWidth)
        * referenceHeight
        <= static_cast<std::int64_t>(nativeHeight) * referenceWidth;
    const auto scaleNumerator = widthLimited
        ? static_cast<std::int64_t>(nativeWidth)
        : static_cast<std::int64_t>(nativeHeight);
    const auto scaleDenominator = widthLimited
        ? referenceWidth
        : referenceHeight;
    const auto ceilingScaled = [scaleNumerator, scaleDenominator](
            std::int64_t value) constexpr noexcept -> std::int32_t {
        return static_cast<std::int32_t>(
            (value * scaleNumerator + scaleDenominator - 1)
            / scaleDenominator);
    };

    auto left = 0;
    auto right = nativeWidth;
    if (coverage == NativeUiMapPanelCoverage::Left) {
        left = ceilingScaled(leftPanelFarEdge) + safetyMargin;
    } else if (coverage == NativeUiMapPanelCoverage::Right) {
        right = nativeWidth
            - ceilingScaled(rightPanelNearEdgeOffset)
            - safetyMargin;
    }
    if (right <= left) return false;
    output = {
        .left = left,
        .right = right,
        .coverage = coverage,
    };
    return true;
}

[[nodiscard]] constexpr auto NativeUiStateMask(
        const std::array<std::uint8_t, NativeUiStateCount>& states) noexcept
        -> std::uint32_t {
    std::uint32_t mask{};
    for (std::size_t state = 0U; state < states.size(); ++state) {
        if (states[state] != 0U) {
            mask |= std::uint32_t{1U} << state;
        }
    }
    return mask;
}

[[nodiscard]] constexpr auto NativeUiBlockingPanelMask(
        const std::array<std::uint8_t, NativeUiStateCount>& states) noexcept
        -> std::uint32_t {
    std::uint32_t mask{};
    for (std::size_t state = 0U; state < states.size(); ++state) {
        if (states[state] != 0U && IsNativeUiPanelState(state)) {
            mask |= std::uint32_t{1U} << state;
        }
    }
    return mask;
}

struct NativeUiStateStatus final {
    bool active{};
    bool questVisibilityKnown{};
    bool questPanelVisible{};
    bool retainAutomapProjection{};
    std::uint32_t activeMask{};
    std::uint32_t blockingPanelMask{};
    std::uint64_t readFailures{};
    std::uint64_t questVisibilityReadFailures{};
};

[[nodiscard]] auto InitializeNativeUiState(
    const D2RL::PluginContext* context) noexcept -> bool;
void ShutdownNativeUiState() noexcept;
// Must be called through D2RLoader's UI-thread dispatcher. It resolves the
// current Quest Log root by native layout name, copies only its visibility bit,
// and never retains the returned widget pointer.
[[nodiscard]] auto RefreshNativeUiPanelVisibilityOnUiThread() noexcept -> bool;
void ResetNativeUiPanelVisibility() noexcept;

// Returns true on every read/fingerprint failure. This is diagnostic telemetry
// only; renderer safety is enforced by the copied native automap viewport.
[[nodiscard]] auto HasBlockingNativeUiPanel() noexcept -> bool;
// Reads one current, copied UI-state snapshot. It never returns a borrowed D2R
// pointer. A failure is fail-closed for MapSense map pixels only.
[[nodiscard]] auto AcquireNativeUiStateStatus(
    NativeUiStateStatus& status) noexcept -> bool;
[[nodiscard]] auto GetNativeUiStateStatus() noexcept -> NativeUiStateStatus;

} // namespace RuffnecKk::MapSense
