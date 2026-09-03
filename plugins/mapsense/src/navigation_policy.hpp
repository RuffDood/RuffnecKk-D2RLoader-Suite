#pragma once

#include "navigation_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace RuffnecKk::MapSense {

enum class NavigationResolutionCompleteness : std::uint8_t {
    Complete,
    PartialRetryable,
};

struct NavigationExitCandidate final {
    std::uint64_t destinationId{};
    std::int32_t targetLevelId{UnknownNavigationLevelId};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t exactClientX{};
    std::int32_t exactClientY{};
    bool useExactClientCoordinates{};
};

struct NavigationPointCandidate final {
    std::uint64_t destinationId{};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t exactClientX{};
    std::int32_t exactClientY{};
    bool useExactClientCoordinates{};
    NavigationDestinationSelection selection{
        NavigationDestinationSelection::All};
};

struct NavigationPolicyInput final {
    std::int32_t currentLevelId{UnknownNavigationLevelId};
    bool inTown{};
    std::span<const NavigationExitCandidate> exits{};
    const NavigationPointCandidate* waypoint{};
    std::span<const NavigationPointCandidate> questTargets{};
    std::span<const std::int32_t> customTargetLevelIds{};
    std::optional<std::int32_t> progressionTargetOverride{};
};

// Most levels have one forward exit. A small bounded alternative set covers
// layouts whose campaign route changes with the generated map, such as the
// Act III jungle bypass.
inline constexpr std::size_t MaximumMainProgressionTargets = 2U;

// Normal quest navigation is a static, opt-in POI policy. It deliberately
// ignores the quest log: several quests can be active at once, while these
// exact side routes remain useful and deterministic in every game. Two is the
// real route maximum (Kurast Bazaar).
inline constexpr std::size_t MaximumStaticQuestRouteTargets = 2U;

// Main progression is intentionally explicit. Raw Levels.txt Vis links mix
// optional dungeons with the campaign route (for example Cold Plains -> Cave),
// so they are not a safe source for the green line.
[[nodiscard]] auto MainProgressionTargetsFor(
    std::int32_t currentLevelId,
    std::span<std::int32_t> output) noexcept -> std::size_t;

// Returns the preferred target before generated-layout availability is known.
[[nodiscard]] auto MainProgressionTargetFor(
    std::int32_t currentLevelId) noexcept -> std::optional<std::int32_t>;

// Chooses the first configured target that has exact runtime exit evidence.
// Candidate order is policy order, not discovery order.
[[nodiscard]] auto SelectMainProgressionTargetFor(
    std::int32_t currentLevelId,
    std::span<const NavigationExitCandidate> exits) noexcept
    -> std::optional<std::int32_t>;

// Returns only normal-quest side routes. Farming, secret and Pandemonium
// destinations are excluded. When one of these edges is also present in the
// green graph (for example Halls of the Dead 1 -> 2), red owns that exact
// destination so two coincident lines are never published.
[[nodiscard]] auto StaticQuestRouteTargetsFor(
    std::int32_t currentLevelId,
    std::span<std::int32_t> output) noexcept -> std::size_t;

[[nodiscard]] auto IsStaticQuestRouteTarget(
    std::int32_t currentLevelId,
    std::int32_t targetLevelId) noexcept -> bool;

struct NavigationQuestPresetTarget final {
    NavigationDestinationSelection selection{
        NavigationDestinationSelection::All};
};

// Exact generated presets are the terminal POIs for ordinary quests. The
// policy is data-only and consumes no quest state. Repeated barbarian cages
// use NearestToPlayer so exactly one generated cage is rendered.
[[nodiscard]] auto StaticQuestPresetTargetFor(
    std::int32_t currentLevelId,
    std::uint32_t presetType,
    std::int32_t presetClassId) noexcept
    -> std::optional<NavigationQuestPresetTarget>;

// Dynamic progression portals are discovered from exact active object Units.
// The policy maps only proven level/class pairs so ordinary player portals can
// never become a green destination.
[[nodiscard]] auto DynamicMainProgressionTargetFor(
    std::int32_t currentLevelId,
    std::int32_t objectClassId) noexcept -> std::optional<std::int32_t>;

[[nodiscard]] auto HasDynamicMainProgressionTargetFor(
    std::int32_t currentLevelId) noexcept -> bool;

enum class NavigationPresetProgressionKind : std::uint8_t {
    QuestObject,
    Boss,
};

struct NavigationPresetProgressionTarget final {
    std::int32_t targetLevelId{UnknownNavigationLevelId};
    NavigationPresetProgressionKind kind{
        NavigationPresetProgressionKind::QuestObject};
};

// Some dynamic exits do not exist when a generated level is first entered.
// These exact preset witnesses provide a stable pre-spawn destination without
// weakening the active-object class policy above.
[[nodiscard]] auto PresetMainProgressionTargetFor(
    std::int32_t currentLevelId,
    std::uint32_t presetType,
    std::int32_t presetClassId) noexcept
    -> std::optional<NavigationPresetProgressionTarget>;

// Builds the bounded set of levels that the resolver should initialize before
// inspecting RoomTile links. Static campaign and quest destinations are
// first, followed by unique configured destinations; dynamic portals need no
// target-Level initialization. Invalid ids and the current level are ignored.
[[nodiscard]] auto BuildNavigationPreparationTargets(
    std::int32_t currentLevelId,
    std::span<const std::int32_t> customTargetLevelIds,
    std::span<std::int32_t> output) noexcept -> std::size_t;

// D2R DRLG coordinates are game tiles while MapSense destinations use world
// subtiles. This checked conversion is shared by native waypoint output
// and source-side exit presets; invalid coordinates never produce a line.
[[nodiscard]] auto CheckedNavigationSubtileCoordinate(
    std::int32_t gameTile,
    std::int32_t relativeSubtile,
    std::int32_t& output) noexcept -> bool;

// A static main-progression level with no exact matching exit remains
// retryable. A dynamic-only portal is opportunistic: its absence is complete
// until the active object appears, and no guessed point is ever published.
[[nodiscard]] auto EvaluateNavigationResolutionCompleteness(
    std::int32_t currentLevelId,
    std::span<const NavigationExitCandidate> exits) noexcept
    -> NavigationResolutionCompleteness;

// Builds an immutable batch for PublishNavigationDestinations. The resolver
// owns candidate discovery; this policy only classifies already-proven points.
[[nodiscard]] auto BuildNavigationDestinations(
    const NavigationPolicyInput& input,
    std::span<NavigationSubtileDestination> output) noexcept -> std::size_t;

} // namespace RuffnecKk::MapSense
