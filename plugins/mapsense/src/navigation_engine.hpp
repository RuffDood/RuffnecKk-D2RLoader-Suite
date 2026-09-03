#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::int32_t UnknownNavigationLevelId = -1;
inline constexpr std::size_t MaximumNavigationDestinations = 256U;

enum class NavigationLineKind : std::uint8_t {
    Waypoint,
    Progression,
    CustomLevel,
    Quest,
};

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
        bool inTown) noexcept -> bool {
    return observation == NavigationAutomapObservationResult::LevelChanged
        && !inTown;
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
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t startX{};
    std::int32_t startY{};
    std::int32_t endX{};
    std::int32_t endY{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    NavigationLineKind kind{NavigationLineKind::Waypoint};
};

void InitializeNavigationEngine() noexcept;
void ShutdownNavigationEngine() noexcept;

// Lifecycle invalidation. LevelChanged is the authoritative reset for a
// resolver; act transitions are covered by their corresponding level event.
void ResetNavigationSession(std::uint64_t sessionGeneration) noexcept;
void ResetNavigationLevel(
    std::uint64_t sessionGeneration,
    std::int32_t levelId) noexcept;

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
    std::size_t destinationCount) noexcept -> bool;

// Called only from the existing native automap hook when D2R renders the local
// player. All subtile-to-client conversion and automap projection occurs
// synchronously in this call.
[[nodiscard]] auto ObserveNavigationAutomapPass(
    const NavigationAutomapPass& pass) noexcept
    -> NavigationAutomapObservationResult;

[[nodiscard]] auto AcquireNavigationLineSnapshots(
    std::vector<NavigationLineSnapshot>& snapshots,
    bool retainCurrentProjection = false) noexcept -> std::size_t;

// Drops only the renderer-facing projection. Resolver destinations, level and
// session state remain intact so the next native automap pass can republish
// exact lines without repeating destination discovery.
void InvalidateNavigationProjection() noexcept;

[[nodiscard]] auto WantsNavigationLineFrame(
    bool retainCurrentProjection = false) noexcept -> bool;

[[nodiscard]] auto GetNavigationEngineStatus() noexcept
    -> NavigationEngineStatus;

} // namespace RuffnecKk::MapSense
