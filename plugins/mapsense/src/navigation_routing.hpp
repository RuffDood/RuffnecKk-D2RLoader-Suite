#pragma once

#include "navigation_engine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <vector>

namespace RuffnecKk::MapSense {

struct NavigationExitCandidate;

struct NavigationRouteOverride final {
    std::int32_t fromLevelId{};
    std::int32_t toLevelId{};
    NavigationLineKind kind{NavigationLineKind::Progression};
};

struct NavigationRoutingOptions final {
    bool adaptToModConnections{true};
    bool townShortcuts{};
    std::vector<NavigationRouteOverride> overrides{};
};

// The menu owns mutable settings on Present. Gameplay refreshes retain one
// immutable snapshot, including its override list, for the whole native call.
[[nodiscard]] auto PublishNavigationRoutingOptions(
    const NavigationRoutingOptions& options) noexcept -> bool;
[[nodiscard]] auto AcquireNavigationRoutingOptions() noexcept
    -> std::shared_ptr<const NavigationRoutingOptions>;

inline constexpr std::size_t MaximumAdaptiveRouteTargets = 16U;

// An immutable routing decision built from the current loaded Vis neighbours.
// These ids guide preparation; only observed physical exit points can be drawn.
struct NavigationRoutePlan final {
    std::array<std::int32_t, MaximumAdaptiveRouteTargets> progression{};
    std::size_t progressionCount{};
    std::array<std::int32_t, MaximumAdaptiveRouteTargets> quests{};
    std::size_t questCount{};
    bool allowTownShortcut{};
    bool progressionRequiresExit{};
    bool ambiguousProgression{};
};

[[nodiscard]] auto BuildAdaptiveNavigationRoutePlan(
    std::int32_t currentLevelId,
    std::span<const std::int32_t> activeNeighbours,
    const NavigationRoutingOptions& options,
    bool inTown) noexcept -> NavigationRoutePlan;

[[nodiscard]] auto HasNavigationRouteTarget(
    std::span<const std::int32_t> targets,
    std::int32_t targetLevelId) noexcept -> bool;

[[nodiscard]] auto SelectPlannedMainProgressionTarget(
    const NavigationRoutePlan& plan,
    std::span<const NavigationExitCandidate> exits) noexcept -> std::optional<std::int32_t>;

[[nodiscard]] auto BuildAdaptiveNavigationPreparationTargets(
    const NavigationRoutePlan& plan,
    std::span<const std::int32_t> customTargets,
    std::span<std::int32_t> output) noexcept -> std::size_t;

} // namespace RuffnecKk::MapSense
