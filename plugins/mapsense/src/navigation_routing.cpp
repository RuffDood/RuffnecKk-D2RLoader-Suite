#include "navigation_routing.hpp"
#include "navigation_policy.hpp"

#include <algorithm>
#include <atomic>
#include <optional>

namespace RuffnecKk::MapSense {
namespace {
std::atomic<std::shared_ptr<const NavigationRoutingOptions>> PublishedRoutingOptions{};
using RouteTargets = std::array<std::int32_t, MaximumMainProgressionTargets>;

// The canonical graphs describe intent. Only the loaded direct connections
// describe which of those destinations the current mod can actually reach.
[[nodiscard]] auto Reachable(std::int32_t from, std::int32_t to,
        NavigationLineKind kind) noexcept -> bool {
    if (from == to) return true;
    std::array<std::int32_t, 256> queue{};
    std::size_t count = 1U;
    queue[0] = from;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto level = queue[index];
        // Dynamic portals are quest gates, not paths through which to infer
        // later shortcuts. The native object resolver retains their ownership.
        if (kind == NavigationLineKind::Progression
                && HasDynamicMainProgressionTargetFor(level)) continue;
        RouteTargets next{};
        const auto nextCount = kind == NavigationLineKind::Quest
            ? StaticQuestRouteTargetsFor(level, next)
            : MainProgressionTargetsFor(level, next);
        for (std::size_t edge = 0U; edge < nextCount; ++edge) {
            const auto target = next[edge];
            // Great Marsh's legacy return-to-Forest fallback is not forward.
            if (kind == NavigationLineKind::Progression && level == 77 && target == 76) continue;
            if (target == to) return true;
            if (count < queue.size()
                    && !HasNavigationRouteTarget(std::span(queue).first(count), target)) {
                queue[count++] = target;
            }
        }
    }
    return false;
}

[[nodiscard]] auto OverrideFor(std::int32_t from, NavigationLineKind kind,
        const NavigationRoutingOptions& options) noexcept -> const NavigationRouteOverride* {
    const auto found = std::find_if(options.overrides.begin(), options.overrides.end(),
        [from, kind](const NavigationRouteOverride& route) noexcept {
            return route.fromLevelId == from && route.kind == kind;
        });
    return found == options.overrides.end() ? nullptr : &*found;
}

[[nodiscard]] auto FurthestTarget(std::int32_t from,
        std::span<const std::int32_t> neighbours, NavigationLineKind kind,
        bool& ambiguous, std::int32_t excludedLevel) noexcept -> std::optional<std::int32_t> {
    std::optional<std::int32_t> selected;
    ambiguous = false;
    for (const auto target : neighbours) {
        if (target <= 0 || target == excludedLevel || !Reachable(from, target, kind)) continue;
        if (!selected || Reachable(*selected, target, kind)) {
            selected = target;
        }
    }
    if (selected) {
        for (const auto target : neighbours) {
            if (target > 0 && target != excludedLevel && Reachable(from, target, kind)
                    && !Reachable(target, *selected, kind)) {
                ambiguous = true;
                return std::nullopt;
            }
        }
    }
    return selected;
}
}

auto PublishNavigationRoutingOptions(const NavigationRoutingOptions& options) noexcept -> bool {
    try {
        PublishedRoutingOptions.store(std::make_shared<const NavigationRoutingOptions>(options),
            std::memory_order_release);
        return true;
    } catch (...) {
        return false;
    }
}

auto AcquireNavigationRoutingOptions() noexcept -> std::shared_ptr<const NavigationRoutingOptions> {
    return PublishedRoutingOptions.load(std::memory_order_acquire);
}

auto HasNavigationRouteTarget(std::span<const std::int32_t> targets,
        std::int32_t targetLevelId) noexcept -> bool {
    return std::find(targets.begin(), targets.end(), targetLevelId) != targets.end();
}

auto SelectPlannedMainProgressionTarget(const NavigationRoutePlan& plan,
        std::span<const NavigationExitCandidate> exits) noexcept -> std::optional<std::int32_t> {
    for (std::size_t index = 0U; index < plan.progressionCount; ++index) {
        const auto target = plan.progression[index];
        if (std::any_of(exits.begin(), exits.end(),
                [target](const NavigationExitCandidate& exit) noexcept {
                    return exit.targetLevelId == target;
                })) return target;
    }
    return std::nullopt;
}

auto BuildAdaptiveNavigationPreparationTargets(const NavigationRoutePlan& plan,
        std::span<const std::int32_t> customTargets,
        std::span<std::int32_t> output) noexcept -> std::size_t {
    std::size_t count{};
    const auto append = [&output, &count](std::int32_t target) noexcept {
        if (target > 0 && count < output.size()
                && !HasNavigationRouteTarget(output.first(count), target)) output[count++] = target;
    };
    if (plan.progressionRequiresExit || plan.allowTownShortcut) {
        for (std::size_t index = 0U; index < plan.progressionCount; ++index) append(plan.progression[index]);
    }
    for (std::size_t index = 0U; index < plan.questCount; ++index) append(plan.quests[index]);
    if (!plan.allowTownShortcut) {
        for (const auto target : customTargets) append(target);
    }
    return count;
}

auto BuildAdaptiveNavigationRoutePlan(std::int32_t currentLevelId,
        std::span<const std::int32_t> neighbours, const NavigationRoutingOptions& options,
        bool inTown) noexcept -> NavigationRoutePlan {
    NavigationRoutePlan plan{};
    if (currentLevelId <= 0) return plan;
    RouteTargets legacy{};
    const auto legacyCount = MainProgressionTargetsFor(currentLevelId, legacy);
    if (const auto* route = OverrideFor(currentLevelId, NavigationLineKind::Progression, options)) {
        if (route->toLevelId > 0 && route->toLevelId != currentLevelId
                && HasNavigationRouteTarget(neighbours, route->toLevelId)) {
            plan.progression[plan.progressionCount++] = route->toLevelId;
            plan.progressionRequiresExit = true;
            plan.allowTownShortcut = inTown && options.townShortcuts;
        }
    } else if (options.adaptToModConnections && currentLevelId == 40
            && HasNavigationRouteTarget(neighbours, 74)) {
        // Lut Gholein's mandatory palace branch is separate from its outdoor
        // campaign chain. QoL can expose Arcane directly as a physical Vis exit;
        // this endpoint is recognized without inferring through its portal.
        plan.progression[plan.progressionCount++] = 74;
        plan.progressionRequiresExit = true;
        plan.allowTownShortcut = inTown && options.townShortcuts;
    } else if (options.adaptToModConnections) {
        const auto selected = FurthestTarget(currentLevelId, neighbours,
            NavigationLineKind::Progression, plan.ambiguousProgression, currentLevelId);
        if (selected) {
            plan.progression[plan.progressionCount++] = *selected;
            plan.progressionRequiresExit = true;
            plan.allowTownShortcut = inTown && options.townShortcuts
                && !HasNavigationRouteTarget(std::span(legacy).first(legacyCount), *selected);
        } else if (!plan.ambiguousProgression) {
            // Preserve opportunistic, proven dynamic portals and the exact
            // Great Marsh fallback without guessing a missing static exit.
            for (std::size_t index = 0U; index < legacyCount; ++index) {
                if (HasDynamicMainProgressionTargetFor(currentLevelId)
                        || HasNavigationRouteTarget(neighbours, legacy[index])) {
                    plan.progression[plan.progressionCount++] = legacy[index];
                }
            }
            plan.progressionRequiresExit = plan.progressionCount != 0U
                && !HasDynamicMainProgressionTargetFor(currentLevelId);
        }
    } else {
        plan.progressionCount = MainProgressionTargetsFor(currentLevelId, plan.progression);
        plan.progressionRequiresExit = plan.progressionCount != 0U
            && !HasDynamicMainProgressionTargetFor(currentLevelId);
    }

    if (const auto* route = OverrideFor(currentLevelId, NavigationLineKind::Quest, options)) {
        if (route->toLevelId > 0 && route->toLevelId != currentLevelId
                && HasNavigationRouteTarget(neighbours, route->toLevelId)) {
            plan.quests[plan.questCount++] = route->toLevelId;
        }
    } else if (options.adaptToModConnections) {
        RouteTargets branches{};
        const auto branchCount = StaticQuestRouteTargetsFor(
            currentLevelId == 40 ? 47 : currentLevelId, branches);
        for (std::size_t branch = 0U; branch < branchCount; ++branch) {
            bool ambiguous{};
            const auto selected = FurthestTarget(branches[branch], neighbours,
                NavigationLineKind::Quest, ambiguous, currentLevelId);
            if (selected && !HasNavigationRouteTarget(
                    std::span(plan.quests).first(plan.questCount), *selected)) {
                plan.quests[plan.questCount++] = *selected;
            }
        }
    } else {
        plan.questCount = StaticQuestRouteTargetsFor(currentLevelId, plan.quests);
    }
    if (inTown) {
        if (!plan.allowTownShortcut) {
            plan.progressionCount = 0U;
            plan.progressionRequiresExit = false;
        }
        plan.allowTownShortcut = options.townShortcuts
            && (plan.allowTownShortcut || plan.questCount != 0U);
        if (!plan.allowTownShortcut) plan.questCount = 0U;
    }
    return plan;
}

} // namespace RuffnecKk::MapSense
