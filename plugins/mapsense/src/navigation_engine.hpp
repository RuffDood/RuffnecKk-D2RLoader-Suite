#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "gps_route_walk_grid.hpp"

namespace RuffnecKk::MapSense {

inline constexpr std::int32_t UnknownNavigationLevelId = -1;
inline constexpr std::size_t MaximumNavigationDestinations = 256U;
inline constexpr std::size_t MaximumNavigationGpsRoutePaths = 64U;
inline constexpr std::size_t MaximumNavigationGpsRoutePoints = 65'536U;
inline constexpr std::int32_t MaximumNavigationGpsGoalSnapDistance = 48;
inline constexpr std::size_t NativeUnitClassIdOffset = 0x04U;
inline constexpr std::uintptr_t NativeUnitIdentityLayoutWitnessRva = 0x34B7B2U;
inline constexpr std::array<std::uint8_t, 20>
    NativeUnitIdentityLayoutWitness{
        0x8B, 0x01, 0x89, 0x44, 0x24, 0x60, 0x8B, 0x41,
        0x04, 0x89, 0x44, 0x24, 0x58, 0x8B, 0x41, 0x0C,
        0x89, 0x44, 0x24, 0x68};

// Bind And Summon owns the callable UNITS_GetClassId entry at 0x349860.
// Read the governed D2UnitStrc field directly after validating the separate,
// unowned identity-tuple witness above so plugin load order cannot affect it.
[[nodiscard]] inline auto ReadNativeUnitClassId(
        const void* unit) noexcept -> std::int32_t {
    if (unit == nullptr) return -1;
    return *reinterpret_cast<const std::int32_t*>(
        static_cast<const std::uint8_t*>(unit) + NativeUnitClassIdOffset);
}

enum class NavigationLineKind : std::uint8_t {
    Waypoint,
    Progression,
    CustomLevel,
    Quest,
};

inline constexpr std::size_t NavigationLineKindCount = 4U;

enum class NavigationLineMode : std::uint8_t {
    Direct,
    GpsWalk,
    GpsTeleport,
};

struct NavigationLinePolicy final {
    bool enabled{};
    NavigationLineMode mode{NavigationLineMode::Direct};

    constexpr auto operator==(const NavigationLinePolicy&) const noexcept
        -> bool = default;
};

struct NavigationLinePolicySnapshot final {
    std::uint64_t revision{1U};
    std::array<NavigationLinePolicy, NavigationLineKindCount> families{};
};

[[nodiscard]] constexpr auto NavigationLineKindIndex(
        NavigationLineKind kind) noexcept -> std::size_t {
    switch (kind) {
        case NavigationLineKind::Waypoint: return 0U;
        case NavigationLineKind::Progression: return 1U;
        case NavigationLineKind::CustomLevel: return 2U;
        case NavigationLineKind::Quest: return 3U;
    }
    return NavigationLineKindCount;
}

[[nodiscard]] constexpr auto NavigationLineKindMask(
        NavigationLineKind kind) noexcept -> std::uint8_t {
    switch (kind) {
        case NavigationLineKind::Waypoint: return 1U;
        case NavigationLineKind::Progression: return 2U;
        case NavigationLineKind::Quest: return 4U;
        case NavigationLineKind::CustomLevel: return 8U;
    }
    return 0U;
}

[[nodiscard]] constexpr auto IsNavigationLineKindEnabled(
        std::uint8_t enabledMask,
        NavigationLineKind kind) noexcept -> bool {
    return (enabledMask & NavigationLineKindMask(kind)) != 0U;
}

enum class NavigationDestinationSelection : std::uint8_t {
    All,
    NearestToPlayer,
};

// Stable value identity for one physical outdoor seam. Room pointers are
// deliberately excluded: they are valid only during one resolver refresh,
// while these four coordinates survive safely in the reveal-wide label
// catalog and normalize both sides of the same generated boundary.
enum class NavigationBoundaryAxis : std::uint8_t {
    None,
    Vertical,
    Horizontal,
};

struct NavigationExitBoundaryIdentity final {
    NavigationBoundaryAxis axis{NavigationBoundaryAxis::None};
    std::int32_t fixedSubtile{-1};
    std::int32_t startSubtile{-1};
    std::int32_t endSubtile{-1};

    [[nodiscard]] constexpr auto Valid() const noexcept -> bool {
        return axis != NavigationBoundaryAxis::None
            && fixedSubtile >= 0
            && startSubtile >= 0
            && endSubtile > startSubtile;
    }

    constexpr auto operator==(
        const NavigationExitBoundaryIdentity&) const noexcept -> bool = default;
};

// Immutable destination description produced by a resolver on D2R's game/UI
// thread. It deliberately contains no D2R pointer or renderer state.
struct NavigationSubtileDestination final {
    std::uint64_t destinationId{};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    NavigationLineKind kind{NavigationLineKind::Waypoint};
    std::int32_t exactClientX{};
    std::int32_t exactClientY{};
    bool useExactClientCoordinates{};
    NavigationDestinationSelection selection{
        NavigationDestinationSelection::All};

    constexpr auto operator==(
        const NavigationSubtileDestination&) const noexcept -> bool = default;
};

struct NavigationNativePoint final {
    std::int32_t x{};
    std::int32_t y{};
};

using NavigationProjectClientFn = bool(*)(
    void* borrowedAutomapContext,
    std::int32_t clientX,
    std::int32_t clientY,
    NavigationNativePoint& output) noexcept;

// D2R keeps resolver destinations in world subtiles, while the native automap
// projector consumes client/dimetric coordinates. Keep this conversion at the
// projection boundary so the two coordinate spaces cannot be mixed.
[[nodiscard]] auto ConvertNavigationSubtileToClientCoordinates(
    std::int32_t subtileX,
    std::int32_t subtileY,
    NavigationNativePoint& output) noexcept -> bool;

// Static native Units expose the exact client coordinates used by D2R's own
// automap renderer. This checked inverse is used only to retain a validated
// world-subtile witness alongside those authoritative client coordinates.
[[nodiscard]] auto ConvertNavigationClientToSubtileCoordinates(
    std::int32_t clientX,
    std::int32_t clientY,
    NavigationNativePoint& output) noexcept -> bool;

// Diagnostic witness emitted synchronously from the native automap pass.
// It contains values only: no D2R pointer survives the callback.
struct NavigationProjectionDiagnostic final {
    std::int32_t currentLevelId{UnknownNavigationLevelId};
    std::int32_t playerClientX{};
    std::int32_t playerClientY{};
    NavigationNativePoint projectedPlayer{};
    bool playerProjected{};
    NavigationSubtileDestination destination{};
    NavigationNativePoint destinationClient{};
    bool destinationConverted{};
    NavigationNativePoint projectedDestination{};
    bool destinationProjected{};
    NavigationNativePoint clippedStart{};
    NavigationNativePoint clippedEnd{};
    bool lineClipped{};
    std::int32_t clipLeft{};
    std::int32_t clipTop{};
    std::int32_t clipWidth{};
    std::int32_t clipHeight{};
};

using NavigationProjectionDiagnosticFn = void(*)(
    const NavigationProjectionDiagnostic& diagnostic,
    void* userData) noexcept;

// This input is valid only for the duration of ObserveNavigationAutomapPass.
// The engine invokes projectClient synchronously and never retains either the
// callback or borrowedAutomapContext.
struct NavigationAutomapPass final {
    std::int32_t currentLevelId{UnknownNavigationLevelId};
    bool inTown{};
    std::int32_t playerClientX{};
    std::int32_t playerClientY{};
    // Authoritative dynamic-path integer position; never inferred from drawing
    // coordinates. A missing witness disables GPS without disabling Direct.
    bool hasPlayerSubtile{};
    NavigationNativePoint playerSubtile{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    std::int32_t clipLeft{};
    std::int32_t clipTop{};
    std::int32_t clipWidth{};
    std::int32_t clipHeight{};
    NavigationProjectClientFn projectClient{};
    void* borrowedAutomapContext{};
    NavigationProjectionDiagnosticFn diagnostic{};
    void* diagnosticUserData{};
};

enum class NavigationAutomapObservationResult : std::uint8_t {
    Ignored,
    Projected,
    LevelChanged,
};

[[nodiscard]] constexpr auto ShouldRequestNavigationRefresh(
        NavigationAutomapObservationResult observation,
        bool inTown, bool allowTownShortcuts = false) noexcept -> bool {
    return observation == NavigationAutomapObservationResult::LevelChanged
        && (!inTown || allowTownShortcuts);
}

struct NavigationEngineStatus final {
    std::uint64_t sessionGeneration{};
    std::uint64_t destinationRevision{};
    std::uint64_t observedLevelChanges{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::size_t destinationCount{};
    std::size_t projectedLineCount{};
};

// Immutable projected line consumed by Present. No D2R pointer, callback or
// automap context crosses the native automap thread boundary.
struct NavigationLineSnapshot final {
    std::uint64_t destinationId{};
    std::uint64_t sessionGeneration{};
    std::uint64_t destinationRevision{};
    std::uint64_t policyRevision{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t startX{};
    std::int32_t startY{};
    std::int32_t endX{};
    std::int32_t endY{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    NavigationLineKind kind{NavigationLineKind::Waypoint};
};

// GPS route diagnostics are values only. The helper never receives a D2R
// pointer and the render thread never projects a world coordinate itself.
enum class NavigationGpsRouteMode : std::uint8_t {
    Walk,
    Teleport,
};

struct NavigationGpsRoutePoint final {
    std::int32_t subtileX{};
    std::int32_t subtileY{};
};

struct NavigationGpsRoutePath final {
    std::uint64_t destinationId{};
    std::uint64_t policyRevision{};
    NavigationLineKind kind{NavigationLineKind::Waypoint};
    NavigationGpsRouteMode mode{NavigationGpsRouteMode::Walk};
    std::vector<NavigationGpsRoutePoint> points;
    std::shared_ptr<const GpsRouteWalkGrid> walkGrid;
};

struct NavigationGpsRouteSegmentSnapshot final {
    std::uint64_t destinationId{};
    std::uint64_t sessionGeneration{};
    std::uint64_t destinationRevision{};
    std::uint64_t policyRevision{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t startX{};
    std::int32_t startY{};
    std::int32_t endX{};
    std::int32_t endY{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    NavigationLineKind kind{NavigationLineKind::Waypoint};
    NavigationGpsRouteMode mode{NavigationGpsRouteMode::Walk};
};

// Captured synchronously from the native automap pass. It contains current
// player and resolver values only, so it may safely cross to the helper worker.
struct NavigationGpsRouteSourceSnapshot final {
    std::uint64_t sessionGeneration{};
    std::uint64_t destinationRevision{};
    NavigationLinePolicySnapshot policy{};
    std::int32_t levelId{UnknownNavigationLevelId};
    NavigationGpsRoutePoint player{};
    std::vector<NavigationSubtileDestination> destinations;
};

// Diagnostic only: does not control destination-refresh scheduling. This is
// the most recently sampled observer outcome, not a per-session counter.
enum class NavigationAutomapObservationReason : std::uint8_t {
    Unobserved, Inactive, Contended, NoSession, UnknownLevel, LevelChanged,
    Town, NoDestinations, NoNativeOrigin, InvalidProjectionContext,
    InvalidNativeDimensions, InvalidClip, PlayerProjectionFailed, Projected,
};

[[nodiscard]] auto GetNavigationAutomapObservationReason() noexcept
    -> NavigationAutomapObservationReason;

[[nodiscard]] constexpr auto NavigationAutomapObservationReasonName(
        NavigationAutomapObservationReason reason) noexcept -> const char* {
    switch (reason) {
        case NavigationAutomapObservationReason::Unobserved: return "unobserved";
        case NavigationAutomapObservationReason::Inactive: return "inactive";
        case NavigationAutomapObservationReason::Contended: return "contended";
        case NavigationAutomapObservationReason::NoSession: return "no-session";
        case NavigationAutomapObservationReason::UnknownLevel: return "unknown-level";
        case NavigationAutomapObservationReason::LevelChanged: return "level-changed";
        case NavigationAutomapObservationReason::Town: return "town";
        case NavigationAutomapObservationReason::NoDestinations: return "no-destinations";
        case NavigationAutomapObservationReason::NoNativeOrigin: return "no-native-origin";
        case NavigationAutomapObservationReason::InvalidProjectionContext: return "invalid-projection-context";
        case NavigationAutomapObservationReason::InvalidNativeDimensions: return "invalid-native-dimensions";
        case NavigationAutomapObservationReason::InvalidClip: return "invalid-clip";
        case NavigationAutomapObservationReason::PlayerProjectionFailed: return "player-projection-failed";
        case NavigationAutomapObservationReason::Projected: return "projected";
    }
    return "unknown";
}

enum class NavigationGpsSourceReadiness : std::uint8_t {
    Ready,
    Inactive,
    Contended,
    UnknownLevel,
    NoDestinations,
    NoPlayer,
    Exception,
};

struct NavigationGpsSourceDiagnostics final {
    NavigationGpsSourceReadiness readiness{
        NavigationGpsSourceReadiness::Inactive};
    bool stateAvailable{};
    std::uint64_t sessionGeneration{};
    std::uint64_t destinationRevision{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::size_t destinationCount{};
    bool hasPlayer{};
};

struct NavigationGpsRouteDestination final {
    NavigationSubtileDestination destination{};
    NavigationGpsRouteMode mode{NavigationGpsRouteMode::Walk};
};

// Filters GPS-selected families before NearestToPlayer reduction and the
// caller's bounded output cap. The result is ordered by destination identity.
[[nodiscard]] auto SelectNavigationGpsRouteDestinations(
    std::span<const NavigationSubtileDestination> source,
    NavigationGpsRoutePoint player,
    const NavigationLinePolicySnapshot& policy,
    std::span<NavigationGpsRouteDestination> output) noexcept -> std::size_t;

void SetNavigationTownShortcutsEnabled(bool enabled) noexcept;
[[nodiscard]] auto AreNavigationTownShortcutsEnabled() noexcept -> bool;
void InitializeNavigationEngine() noexcept;
void ShutdownNavigationEngine() noexcept;

// Lifecycle invalidation. LevelChanged is the authoritative reset for a
// resolver; act transitions are covered by their corresponding level event.
void ResetNavigationSession(std::uint64_t sessionGeneration) noexcept;
void ResetNavigationLevel(
    std::uint64_t sessionGeneration,
    std::int32_t levelId) noexcept;

// Publishes the four-family visibility/mode policy without waiting on the
// native projection lock. Only enabled/mode changes advance the revision;
// colors remain a renderer concern. A changed policy revokes both projections.
[[nodiscard]] auto PublishNavigationLinePolicy(
    std::array<NavigationLinePolicy, NavigationLineKindCount> families) noexcept
    -> bool;
[[nodiscard]] auto AcquireNavigationLinePolicySnapshot() noexcept
    -> NavigationLinePolicySnapshot;

// Binds an initially unknown level without clearing already published
// same-level destinations. It never changes a different known level.
[[nodiscard]] auto BindNavigationLevelForPublish(
    std::uint64_t sessionGeneration,
    std::int32_t levelId) noexcept -> bool;

// Atomically replaces every destination for one exact session and level.
// Stale resolver work, unknown levels, invalid kinds and oversized batches are
// rejected without disturbing the currently published state.
[[nodiscard]] auto PublishNavigationDestinations(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    const NavigationSubtileDestination* destinations,
    std::size_t destinationCount, bool allowTownShortcut = false) noexcept -> bool;

// Called only from the existing native automap hook when D2R renders the local
// player. All subtile-to-client conversion and automap projection occurs
// synchronously in this call.
[[nodiscard]] auto ObserveNavigationAutomapPass(
    const NavigationAutomapPass& pass) noexcept
    -> NavigationAutomapObservationResult;

[[nodiscard]] auto AcquireNavigationLineSnapshots(
    std::vector<NavigationLineSnapshot>& snapshots,
    bool retainCurrentProjection = false) noexcept -> std::size_t;

[[nodiscard]] auto AcquireNavigationGpsRouteSourceSnapshot(
    NavigationGpsRouteSourceSnapshot& snapshot,
    NavigationGpsSourceDiagnostics* diagnostics = nullptr) noexcept -> bool;

// Atomically replaces all GPS paths for one exact resolver publication. Each
// path must begin at the coordinator's authenticated request origin and end
// within the bounded standable approach to its current destination. Ordinary
// movement after submission is soft state; stale hard identity, cross-level,
// and oversized publications fail closed.
[[nodiscard]] auto PublishNavigationGpsRoutes(
    std::uint64_t sessionGeneration,
    std::uint64_t destinationRevision,
    std::uint64_t policyRevision,
    std::int32_t levelId,
    const NavigationGpsRoutePath* paths,
    std::size_t pathCount,
    std::uint64_t* acceptedContentEpoch = nullptr) noexcept -> bool;

[[nodiscard]] auto GetNavigationGpsRouteContentEpoch() noexcept -> std::uint64_t;

[[nodiscard]] auto AcquireNavigationGpsRouteSegmentSnapshots(
    std::vector<NavigationGpsRouteSegmentSnapshot>& snapshots,
    bool retainCurrentProjection = false) noexcept -> std::size_t;

[[nodiscard]] auto WantsNavigationGpsRouteFrame(
    bool retainCurrentProjection = false) noexcept -> bool;
void InvalidateNavigationGpsRoutes() noexcept;

// Drops only the renderer-facing projection. Resolver destinations, level and
// session state remain intact so the next native automap pass can republish
// exact lines without repeating destination discovery.
void InvalidateNavigationProjection() noexcept;

[[nodiscard]] auto WantsNavigationLineFrame(
    bool retainCurrentProjection = false) noexcept -> bool;

[[nodiscard]] auto GetNavigationEngineStatus() noexcept
    -> NavigationEngineStatus;

} // namespace RuffnecKk::MapSense
