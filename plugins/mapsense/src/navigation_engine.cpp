#include "navigation_engine.hpp"
#include "navigation_gps_follow.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uint64_t NavigationSnapshotLifetimeMilliseconds = 250U;

std::atomic_bool Active{};
std::atomic<NavigationAutomapObservationReason> LastObservationReason{
    NavigationAutomapObservationReason::Unobserved};
std::atomic_flag StateLock = ATOMIC_FLAG_INIT;
std::array<NavigationSubtileDestination, MaximumNavigationDestinations>
    Destinations{};
std::size_t DestinationCount{};
bool TownShortcutBatch{};
std::atomic_bool TownShortcutsEnabled{};
std::array<NavigationLineSnapshot, MaximumNavigationDestinations>
    ProjectedLines{};
std::size_t ProjectedLineCount{};
std::uint64_t SessionGeneration{};
std::uint64_t DestinationRevision{1U};
std::uint64_t ProjectedRevision{};
std::uint64_t LastProjectionTick{};
std::atomic<std::shared_ptr<const std::vector<NavigationGpsRoutePath>>>
    GpsRoutes{};
std::atomic<std::uint64_t> GpsRouteContentEpoch{1U};
std::array<NavigationGpsFollowState, MaximumNavigationGpsRoutePaths> GpsFollowStates{};
// Value-only recent movement, scoped to this session and level. A late helper
// result may reconnect through these observations, but every edge is checked
// against that result's immutable grid before it can become visible.
constexpr std::size_t MaximumGpsPlayerTrailPoints = 128U;
std::array<NavigationGpsRoutePoint, MaximumGpsPlayerTrailPoints> GpsPlayerTrail{};
std::size_t GpsPlayerTrailCount{};
std::size_t GpsFollowEvaluationStart{};
std::vector<NavigationGpsRouteSegmentSnapshot> ProjectedGpsRouteSegments{};
std::uint64_t ProjectedGpsRouteRevision{};
std::uint64_t LastGpsRouteProjectionTick{};
std::uint64_t GpsRouteProjectionEpoch{};
NavigationGpsRoutePoint LatestPlayerSubtile{};
bool HasLatestPlayerSubtile{};
std::uint64_t LastPlayerObservationTick{};
std::int32_t LevelId{UnknownNavigationLevelId};
std::uint64_t ObservedLevelChanges{};
std::uint64_t ProjectedPolicyRevision{};
std::uint64_t ProjectedGpsRoutePolicyRevision{};
std::atomic<std::shared_ptr<const NavigationLinePolicySnapshot>> NavigationPolicy{};

std::atomic<std::uint64_t> PublishedProjectionTick{};
std::atomic<std::uint64_t> PublishedProjectionRevision{};
std::atomic<std::size_t> PublishedLineCount{};
std::atomic<std::size_t> PublishedGpsRouteSegmentCount{};
std::atomic<std::uint64_t> PublishedGpsRouteRevision{};
std::atomic<std::uint64_t> PublishedGpsRouteProjectionTick{};
std::atomic<std::uint64_t> PendingGpsRouteInvalidationEpoch{};
std::atomic<std::uint64_t> PendingNavigationProjectionInvalidationEpoch{};
std::uint64_t AppliedGpsRouteInvalidationEpoch{};
std::uint64_t AppliedNavigationProjectionInvalidationEpoch{};

static_assert(std::is_standard_layout_v<NavigationSubtileDestination>);
static_assert(std::is_trivially_copyable_v<NavigationSubtileDestination>);
static_assert(std::is_standard_layout_v<NavigationLineSnapshot>);
static_assert(std::is_trivially_copyable_v<NavigationLineSnapshot>);
static_assert(std::is_standard_layout_v<NavigationGpsRoutePoint>);
static_assert(std::is_trivially_copyable_v<NavigationGpsRoutePoint>);

class StateLockGuard final {
public:
    explicit StateLockGuard(bool wait) noexcept {
        if (!wait) {
            Acquired = !StateLock.test_and_set(std::memory_order_acquire);
            return;
        }
        while (StateLock.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        Acquired = true;
    }

    ~StateLockGuard() {
        if (Acquired) StateLock.clear(std::memory_order_release);
    }

    StateLockGuard(const StateLockGuard&) = delete;
    auto operator=(const StateLockGuard&) -> StateLockGuard& = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return Acquired;
    }

    void Release() noexcept {
        if (!Acquired) return;
        StateLock.clear(std::memory_order_release);
        Acquired = false;
    }

private:
    bool Acquired{};
};

[[nodiscard]] auto CurrentTickMilliseconds() noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] auto IsRecent(
        std::uint64_t tick,
        std::uint64_t currentTick) noexcept -> bool {
    return tick != 0U && currentTick >= tick
        && (currentTick - tick) <= NavigationSnapshotLifetimeMilliseconds;
}

[[nodiscard]] constexpr auto IsValidKind(
        NavigationLineKind kind) noexcept -> bool {
    switch (kind) {
        case NavigationLineKind::Waypoint:
        case NavigationLineKind::Progression:
        case NavigationLineKind::CustomLevel:
        case NavigationLineKind::Quest:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr auto IsValidLineMode(
        NavigationLineMode mode) noexcept -> bool {
    return mode == NavigationLineMode::Direct
        || mode == NavigationLineMode::GpsWalk
        || mode == NavigationLineMode::GpsTeleport;
}

[[nodiscard]] constexpr auto IsGpsLineMode(
        NavigationLineMode mode) noexcept -> bool {
    return mode == NavigationLineMode::GpsWalk
        || mode == NavigationLineMode::GpsTeleport;
}

[[nodiscard]] constexpr auto GpsModeForLineMode(
        NavigationLineMode mode) noexcept -> NavigationGpsRouteMode {
    return mode == NavigationLineMode::GpsTeleport
        ? NavigationGpsRouteMode::Teleport
        : NavigationGpsRouteMode::Walk;
}

[[nodiscard]] auto DefaultNavigationPolicy() noexcept
        -> NavigationLinePolicySnapshot {
    NavigationLinePolicySnapshot policy{};
    policy.revision = 1U;
    for (auto& family : policy.families) {
        family = {true, NavigationLineMode::Direct};
    }
    return policy;
}

[[nodiscard]] auto PolicyForKind(
        const NavigationLinePolicySnapshot& policy,
        NavigationLineKind kind) noexcept -> const NavigationLinePolicy* {
    const auto index = NavigationLineKindIndex(kind);
    if (index >= policy.families.size()) return nullptr;
    const auto& family = policy.families[index];
    return IsValidLineMode(family.mode) ? &family : nullptr;
}

[[nodiscard]] constexpr auto IsValidSelection(
        NavigationDestinationSelection selection) noexcept -> bool {
    switch (selection) {
        case NavigationDestinationSelection::All:
        case NavigationDestinationSelection::NearestToPlayer:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr auto UnsignedDifference(
        std::int64_t left,
        std::int64_t right) noexcept -> std::uint64_t {
    return left >= right
        ? static_cast<std::uint64_t>(left - right)
        : static_cast<std::uint64_t>(right - left);
}

// Generated quest presets stay immutable after discovery. For a repeated POI
// group (the generated captive cages), choose one endpoint from the current
// player coordinates during every automap pass instead of rescanning the DRLG.
[[nodiscard]] auto SelectNearestDestinationIndex(
        std::int32_t playerClientX,
        std::int32_t playerClientY,
        const NavigationLinePolicySnapshot& policy) noexcept -> std::optional<std::size_t> {
    const auto playerSubtileXNumerator =
        static_cast<std::int64_t>(playerClientX)
        + std::int64_t{2} * playerClientY;
    const auto playerSubtileYNumerator =
        std::int64_t{2} * playerClientY
        - static_cast<std::int64_t>(playerClientX);
    constexpr std::int64_t ClientToSubtileDivisor = 32;

    std::optional<std::size_t> selected{};
    std::uint64_t selectedDistance{};
    for (std::size_t index = 0U; index < DestinationCount; ++index) {
        const auto& destination = Destinations[index];
        const auto* family = PolicyForKind(policy, destination.kind);
        if (destination.selection
                != NavigationDestinationSelection::NearestToPlayer
            || family == nullptr || !family->enabled
            || family->mode != NavigationLineMode::Direct
            || destination.subtileX < 0 || destination.subtileY < 0) {
            continue;
        }
        const auto candidateXNumerator =
            static_cast<std::int64_t>(destination.subtileX)
            * ClientToSubtileDivisor;
        const auto candidateYNumerator =
            static_cast<std::int64_t>(destination.subtileY)
            * ClientToSubtileDivisor;
        const auto deltaX = UnsignedDifference(
                candidateXNumerator,
                playerSubtileXNumerator);
        const auto deltaY = UnsignedDifference(
            candidateYNumerator,
            playerSubtileYNumerator);
        // This bound makes the squared Euclidean metric overflow-proof. It is
        // over ninety million world subtiles after removing the x32 scale,
        // far beyond any valid generated D2R level.
        constexpr std::uint64_t MaximumSquaredComponent = 3'037'000'499U;
        if (deltaX > MaximumSquaredComponent
            || deltaY > MaximumSquaredComponent) {
            continue;
        }
        const auto distance = deltaX * deltaX + deltaY * deltaY;
        if (!selected.has_value()
            || distance < selectedDistance
            || (distance == selectedDistance
                && destination.destinationId
                    < Destinations[*selected].destinationId)) {
            selected = index;
            selectedDistance = distance;
        }
    }
    return selected;
}

void ClearProjectedLinesLocked() noexcept {
    ProjectedLineCount = 0U;
    ProjectedRevision = 0U;
    ProjectedPolicyRevision = 0U;
    LastProjectionTick = 0U;
    PublishedLineCount.store(0U, std::memory_order_release);
    PublishedProjectionRevision.store(0U, std::memory_order_release);
    PublishedProjectionTick.store(0U, std::memory_order_release);
}

void ClearGpsRouteProjectionLocked() noexcept {
    ++GpsRouteProjectionEpoch;
    ProjectedGpsRouteSegments.clear();
    ProjectedGpsRouteRevision = 0U;
    ProjectedGpsRoutePolicyRevision = 0U;
    LastGpsRouteProjectionTick = 0U;
    PublishedGpsRouteSegmentCount.store(0U, std::memory_order_release);
    PublishedGpsRouteRevision.store(0U, std::memory_order_release);
    PublishedGpsRouteProjectionTick.store(0U, std::memory_order_release);
}

void ClearGpsRoutesLocked() noexcept {
    if (GpsRoutes.exchange({}, std::memory_order_acq_rel) != nullptr) {
        GpsRouteContentEpoch.fetch_add(1U, std::memory_order_release);
    }
    GpsFollowStates = {};
    GpsFollowEvaluationStart = 0U;
    ClearGpsRouteProjectionLocked();
}

void ConsumePendingNavigationInvalidationsLocked() noexcept {
    const auto routeEpoch = PendingGpsRouteInvalidationEpoch.load(
        std::memory_order_acquire);
    if (routeEpoch != AppliedGpsRouteInvalidationEpoch) {
        ClearGpsRoutesLocked();
        AppliedGpsRouteInvalidationEpoch = routeEpoch;
    }
    const auto projectionEpoch = PendingNavigationProjectionInvalidationEpoch.load(
        std::memory_order_acquire);
    if (projectionEpoch != AppliedNavigationProjectionInvalidationEpoch) {
        ClearProjectedLinesLocked();
        ClearGpsRouteProjectionLocked();
        AppliedNavigationProjectionInvalidationEpoch = projectionEpoch;
    }
}

void ClearDestinationsLocked() noexcept {
    GpsPlayerTrailCount = 0U;
    DestinationCount = 0U;
    TownShortcutBatch = false;
    ++DestinationRevision;
    ClearProjectedLinesLocked();
    ClearGpsRoutesLocked();
    HasLatestPlayerSubtile = false;
    LastPlayerObservationTick = 0U;
}

[[nodiscard]] constexpr auto IsValidGpsRouteMode(
        NavigationGpsRouteMode mode) noexcept -> bool {
    return mode == NavigationGpsRouteMode::Walk
        || mode == NavigationGpsRouteMode::Teleport;
}

[[nodiscard]] constexpr auto IsWithinGpsGoalSnapDistance(
        std::int32_t actual, std::int32_t requested) noexcept -> bool {
    const auto difference = static_cast<std::int64_t>(actual)
        - static_cast<std::int64_t>(requested);
    return difference >= -MaximumNavigationGpsGoalSnapDistance
        && difference <= MaximumNavigationGpsGoalSnapDistance;
}

[[nodiscard]] auto SameGpsRoutes(
        std::span<const NavigationGpsRoutePath> incoming) noexcept -> bool {
    const auto currentRoutes = GpsRoutes.load(std::memory_order_acquire);
    if (currentRoutes == nullptr || currentRoutes->size() != incoming.size()) return false;
    for (std::size_t index = 0U; index < incoming.size(); ++index) {
        const auto& current = (*currentRoutes)[index];
        const auto& candidate = incoming[index];
        if (current.destinationId != candidate.destinationId
            || current.policyRevision != candidate.policyRevision
            || current.kind != candidate.kind
            || current.mode != candidate.mode
            || current.walkGrid != candidate.walkGrid
            || current.points.size() != candidate.points.size()) {
            return false;
        }
        for (std::size_t point = 0U; point < candidate.points.size(); ++point) {
            if (current.points[point].subtileX != candidate.points[point].subtileX
                || current.points[point].subtileY
                    != candidate.points[point].subtileY) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto ClipLineToAutomap(
        const NavigationNativePoint& start,
        const NavigationNativePoint& end,
        const NavigationAutomapPass& pass,
        NavigationNativePoint& clippedStart,
        NavigationNativePoint& clippedEnd) noexcept -> bool {
    if (pass.clipWidth <= 0 || pass.clipHeight <= 0) return false;

    const auto left = static_cast<double>(pass.clipLeft);
    const auto top = static_cast<double>(pass.clipTop);
    const auto right = left + static_cast<double>(pass.clipWidth) - 1.0;
    const auto bottom = top + static_cast<double>(pass.clipHeight) - 1.0;
    if (right < left || bottom < top) return false;

    const auto startX = static_cast<double>(start.x);
    const auto startY = static_cast<double>(start.y);
    const auto deltaX = static_cast<double>(end.x) - startX;
    const auto deltaY = static_cast<double>(end.y) - startY;
    auto entry = 0.0;
    auto exit = 1.0;

    const auto clip = [&entry, &exit](double p, double q) noexcept {
        if (p == 0.0) return q >= 0.0;
        const auto ratio = q / p;
        if (p < 0.0) {
            if (ratio > exit) return false;
            entry = std::max(entry, ratio);
        } else {
            if (ratio < entry) return false;
            exit = std::min(exit, ratio);
        }
        return true;
    };

    if (!clip(-deltaX, startX - left)
        || !clip(deltaX, right - startX)
        || !clip(-deltaY, startY - top)
        || !clip(deltaY, bottom - startY)
        || entry > exit) {
        return false;
    }

    const auto toCoordinate = [](double value) noexcept {
        return static_cast<std::int32_t>(std::llround(value));
    };
    clippedStart = {
        .x = toCoordinate(startX + entry * deltaX),
        .y = toCoordinate(startY + entry * deltaY),
    };
    clippedEnd = {
        .x = toCoordinate(startX + exit * deltaX),
        .y = toCoordinate(startY + exit * deltaY),
    };
    return clippedStart.x != clippedEnd.x || clippedStart.y != clippedEnd.y;
}

} // namespace

auto ConvertNavigationSubtileToClientCoordinates(
        std::int32_t subtileX,
        std::int32_t subtileY,
        NavigationNativePoint& output) noexcept -> bool {
    if (subtileX < 0 || subtileY < 0) return false;
    const auto x = static_cast<std::int64_t>(subtileX);
    const auto y = static_cast<std::int64_t>(subtileY);
    const auto clientX = std::int64_t{16} * (x - y);
    const auto clientY = std::int64_t{8} * (x + y);
    constexpr auto minimum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::min)());
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (clientX < minimum || clientX > maximum
        || clientY < minimum || clientY > maximum) {
        return false;
    }
    output = {
        .x = static_cast<std::int32_t>(clientX),
        .y = static_cast<std::int32_t>(clientY),
    };
    return true;
}

auto ConvertNavigationClientToSubtileCoordinates(
        std::int32_t clientX,
        std::int32_t clientY,
        NavigationNativePoint& output) noexcept -> bool {
    const auto x = static_cast<std::int64_t>(clientX);
    const auto y = static_cast<std::int64_t>(clientY);
    const auto subtileXNumerator = x + std::int64_t{2} * y;
    const auto subtileYNumerator = std::int64_t{2} * y - x;
    constexpr auto divisor = std::int64_t{32};
    if (subtileXNumerator % divisor != 0
        || subtileYNumerator % divisor != 0) {
        return false;
    }
    const auto subtileX = subtileXNumerator / divisor;
    const auto subtileY = subtileYNumerator / divisor;
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (subtileX < 0 || subtileY < 0
        || subtileX > maximum || subtileY > maximum) {
        return false;
    }
    output = {
        .x = static_cast<std::int32_t>(subtileX),
        .y = static_cast<std::int32_t>(subtileY),
    };
    return true;
}

void SetNavigationTownShortcutsEnabled(bool enabled) noexcept {
    TownShortcutsEnabled.store(enabled, std::memory_order_release);
}

auto AreNavigationTownShortcutsEnabled() noexcept -> bool {
    return TownShortcutsEnabled.load(std::memory_order_acquire);
}

void InitializeNavigationEngine() noexcept {
    StateLockGuard lock(true);
    Active.store(false, std::memory_order_release);
    NavigationPolicy.store(
        std::make_shared<const NavigationLinePolicySnapshot>(
            DefaultNavigationPolicy()),
        std::memory_order_release);
    TownShortcutsEnabled.store(false, std::memory_order_release);
    SessionGeneration = 0U;
    LevelId = UnknownNavigationLevelId;
    DestinationCount = 0U;
    TownShortcutBatch = false;
    DestinationRevision = 1U;
    ObservedLevelChanges = 0U;
    ClearProjectedLinesLocked();
    ClearGpsRoutesLocked();
    PendingGpsRouteInvalidationEpoch.store(0U, std::memory_order_release);
    PendingNavigationProjectionInvalidationEpoch.store(0U, std::memory_order_release);
    AppliedGpsRouteInvalidationEpoch = 0U;
    AppliedNavigationProjectionInvalidationEpoch = 0U;
    GpsPlayerTrailCount = 0U;
    HasLatestPlayerSubtile = false;
    LastPlayerObservationTick = 0U;
    LastObservationReason.store(NavigationAutomapObservationReason::Unobserved,
        std::memory_order_relaxed);
    Active.store(true, std::memory_order_release);
}

void ShutdownNavigationEngine() noexcept {
    Active.store(false, std::memory_order_release);
    NavigationPolicy.store({}, std::memory_order_release);
    StateLockGuard lock(true);
    SessionGeneration = 0U;
    LevelId = UnknownNavigationLevelId;
    ClearDestinationsLocked();
    HasLatestPlayerSubtile = false;
    LastPlayerObservationTick = 0U;
}

void ResetNavigationSession(std::uint64_t sessionGeneration) noexcept {
    StateLockGuard lock(true);
    SessionGeneration = sessionGeneration;
    LevelId = UnknownNavigationLevelId;
    ClearDestinationsLocked();
}

void ResetNavigationLevel(
        std::uint64_t sessionGeneration,
        std::int32_t levelId) noexcept {
    StateLockGuard lock(true);
    SessionGeneration = sessionGeneration;
    LevelId = levelId;
    ClearDestinationsLocked();
}

auto PublishNavigationLinePolicy(
        std::array<NavigationLinePolicy, NavigationLineKindCount> families) noexcept
        -> bool {
    for (const auto& family : families) {
        if (!IsValidLineMode(family.mode)) return false;
    }
    try {
        auto current = NavigationPolicy.load(std::memory_order_acquire);
        for (;;) {
            if (current != nullptr && current->families == families) return false;
            NavigationLinePolicySnapshot replacement{};
            replacement.revision = current != nullptr
                ? current->revision + 1U : 1U;
            if (replacement.revision == 0U) replacement.revision = 1U;
            replacement.families = families;
            const auto candidate = std::make_shared<const NavigationLinePolicySnapshot>(
                std::move(replacement));
            if (NavigationPolicy.compare_exchange_weak(
                    current, candidate, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                InvalidateNavigationProjection();
                InvalidateNavigationGpsRoutes();
                return true;
            }
        }
    } catch (...) {
        return false;
    }
}

auto AcquireNavigationLinePolicySnapshot() noexcept
        -> NavigationLinePolicySnapshot {
    const auto policy = NavigationPolicy.load(std::memory_order_acquire);
    return policy != nullptr ? *policy : DefaultNavigationPolicy();
}

auto SelectNavigationGpsRouteDestinations(
        std::span<const NavigationSubtileDestination> source,
        NavigationGpsRoutePoint player,
        const NavigationLinePolicySnapshot& policy,
        std::span<NavigationGpsRouteDestination> output) noexcept -> std::size_t {
    if (output.empty() || policy.revision == 0U) return 0U;
    std::array<NavigationGpsRouteDestination, MaximumNavigationDestinations> selected{};
    std::size_t selectedCount{};
    std::array<std::optional<std::uint64_t>, NavigationLineKindCount>
        nearestDestinations{};
    std::array<std::uint64_t, NavigationLineKindCount> nearestDistances{};
    const auto accepts = [&policy](const NavigationSubtileDestination& destination)
            noexcept -> bool {
        const auto* family = PolicyForKind(policy, destination.kind);
        return family != nullptr && family->enabled && IsGpsLineMode(family->mode)
            && IsValidSelection(destination.selection) && destination.destinationId != 0U
            && destination.subtileX >= 0 && destination.subtileY >= 0;
    };
    for (const auto& destination : source) {
        const auto familyIndex = NavigationLineKindIndex(destination.kind);
        if (!IsValidKind(destination.kind) || familyIndex >= nearestDestinations.size()
            || !accepts(destination)
            || destination.selection != NavigationDestinationSelection::NearestToPlayer) {
            continue;
        }
        const auto deltaX = static_cast<std::int64_t>(destination.subtileX)
            - static_cast<std::int64_t>(player.subtileX);
        const auto deltaY = static_cast<std::int64_t>(destination.subtileY)
            - static_cast<std::int64_t>(player.subtileY);
        const auto absoluteX = static_cast<std::uint64_t>(deltaX < 0 ? -deltaX : deltaX);
        const auto absoluteY = static_cast<std::uint64_t>(deltaY < 0 ? -deltaY : deltaY);
        constexpr std::uint64_t MaximumSquaredComponent = 3'037'000'499U;
        if (absoluteX > MaximumSquaredComponent || absoluteY > MaximumSquaredComponent) continue;
        const auto distance = absoluteX * absoluteX + absoluteY * absoluteY;
        if (!nearestDestinations[familyIndex].has_value()
            || distance < nearestDistances[familyIndex]
            || (distance == nearestDistances[familyIndex]
                && destination.destinationId < *nearestDestinations[familyIndex])) {
            nearestDestinations[familyIndex] = destination.destinationId;
            nearestDistances[familyIndex] = distance;
        }
    }
    for (const auto& destination : source) {
        const auto familyIndex = NavigationLineKindIndex(destination.kind);
        if (selectedCount >= selected.size() || !IsValidKind(destination.kind)
            || familyIndex >= nearestDestinations.size() || !accepts(destination)
            || (destination.selection == NavigationDestinationSelection::NearestToPlayer
                && (!nearestDestinations[familyIndex].has_value()
                    || destination.destinationId != *nearestDestinations[familyIndex]))) {
            continue;
        }
        const auto* family = PolicyForKind(policy, destination.kind);
        selected[selectedCount++] = {.destination = destination,
            .mode = GpsModeForLineMode(family->mode)};
    }
    std::sort(selected.begin(), selected.begin() + static_cast<std::ptrdiff_t>(selectedCount),
        [](const NavigationGpsRouteDestination& left,
                const NavigationGpsRouteDestination& right) noexcept {
            return left.destination.destinationId != right.destination.destinationId
                ? left.destination.destinationId < right.destination.destinationId
                : static_cast<std::uint8_t>(left.destination.kind)
                    < static_cast<std::uint8_t>(right.destination.kind);
        });
    const auto count = std::min(selectedCount, output.size());
    std::copy_n(selected.begin(), count, output.begin());
    return count;
}

auto BindNavigationLevelForPublish(
        std::uint64_t sessionGeneration,
        std::int32_t levelId) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || levelId == UnknownNavigationLevelId) {
        return false;
    }
    StateLockGuard lock(true);
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration != SessionGeneration) {
        return false;
    }
    if (LevelId == UnknownNavigationLevelId) {
        LevelId = levelId;
        ClearProjectedLinesLocked();
        return true;
    }
    return LevelId == levelId;
}

auto PublishNavigationDestinations(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        const NavigationSubtileDestination* destinations,
        std::size_t destinationCount, bool allowTownShortcut) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || levelId == UnknownNavigationLevelId
        || destinationCount > MaximumNavigationDestinations
        || (destinationCount != 0U && destinations == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < destinationCount; ++index) {
        if ((allowTownShortcut && destinations[index].kind != NavigationLineKind::Progression
                && destinations[index].kind != NavigationLineKind::Quest)
            || !IsValidKind(destinations[index].kind)
            || !IsValidSelection(destinations[index].selection)) {
            return false;
        }
    }

    StateLockGuard lock(true);
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration != SessionGeneration
        || levelId != LevelId) {
        return false;
    }
    if (allowTownShortcut == TownShortcutBatch && destinationCount == DestinationCount
        && (destinationCount == 0U
            || std::equal(destinations, destinations + destinationCount,
                Destinations.begin()))) {
        return true;
    }
    if (destinationCount != 0U) {
        std::copy_n(destinations, destinationCount, Destinations.begin());
    }
    DestinationCount = destinationCount;
    TownShortcutBatch = allowTownShortcut;
    ++DestinationRevision;
    ClearProjectedLinesLocked();
    ClearGpsRoutesLocked();
    return true;
}

auto GetNavigationAutomapObservationReason() noexcept
        -> NavigationAutomapObservationReason {
    return LastObservationReason.load(std::memory_order_relaxed);
}

auto ObserveNavigationAutomapPass(
        const NavigationAutomapPass& pass) noexcept
        -> NavigationAutomapObservationResult {
    const auto note = [](NavigationAutomapObservationReason reason) noexcept {
        LastObservationReason.store(reason, std::memory_order_relaxed);
    };
    if (!Active.load(std::memory_order_acquire)) {
        note(NavigationAutomapObservationReason::Inactive);
        return NavigationAutomapObservationResult::Ignored;
    }

    StateLockGuard lock(false);
    if (!lock) {
        note(NavigationAutomapObservationReason::Contended);
        return NavigationAutomapObservationResult::Ignored;
    }
    if (!Active.load(std::memory_order_acquire)) {
        note(NavigationAutomapObservationReason::Inactive);
        return NavigationAutomapObservationResult::Ignored;
    }
    ConsumePendingNavigationInvalidationsLocked();
    if (SessionGeneration == 0U || pass.currentLevelId == UnknownNavigationLevelId) {
        HasLatestPlayerSubtile = false;
        LastPlayerObservationTick = 0U;
        ClearProjectedLinesLocked();
        ClearGpsRouteProjectionLocked();
        note(SessionGeneration == 0U ? NavigationAutomapObservationReason::NoSession
            : NavigationAutomapObservationReason::UnknownLevel);
        return NavigationAutomapObservationResult::Ignored;
    }
    if (pass.currentLevelId != LevelId) {
        LevelId = pass.currentLevelId;
        ++ObservedLevelChanges;
        ClearDestinationsLocked();
        note(NavigationAutomapObservationReason::LevelChanged);
        return NavigationAutomapObservationResult::LevelChanged;
    }
    if (pass.inTown && !(TownShortcutBatch && AreNavigationTownShortcutsEnabled())) {
        if (DestinationCount != 0U) {
            ClearDestinationsLocked();
        } else {
            ClearProjectedLinesLocked();
            ClearGpsRouteProjectionLocked();
            HasLatestPlayerSubtile = false;
            LastPlayerObservationTick = 0U;
        }
        note(NavigationAutomapObservationReason::Town);
        return NavigationAutomapObservationResult::Ignored;
    }
    if (DestinationCount == 0U) {
        ClearProjectedLinesLocked();
        ClearGpsRouteProjectionLocked();
        note(NavigationAutomapObservationReason::NoDestinations);
        return NavigationAutomapObservationResult::Ignored;
    }

    // Routing consumes the admitted dynamic-path position. Client coordinates
    // may be off the integer subtile lattice and belong only to projection.
    constexpr auto maximumPathCoordinate = static_cast<std::int32_t>(
        (std::numeric_limits<std::uint16_t>::max)());
    const bool validOrigin = pass.hasPlayerSubtile
        && pass.playerSubtile.x >= 0 && pass.playerSubtile.y >= 0
        && pass.playerSubtile.x <= maximumPathCoordinate
        && pass.playerSubtile.y <= maximumPathCoordinate;
    if (!validOrigin) {
        HasLatestPlayerSubtile = false;
        LastPlayerObservationTick = 0U;
        ClearGpsRouteProjectionLocked();
    } else {
        LatestPlayerSubtile = {
            .subtileX = pass.playerSubtile.x,
            .subtileY = pass.playerSubtile.y,
        };
        HasLatestPlayerSubtile = true;
        LastPlayerObservationTick = CurrentTickMilliseconds();
        if (GpsPlayerTrailCount == 0U
            || !SameGpsPoint(GpsPlayerTrail[GpsPlayerTrailCount - 1U], LatestPlayerSubtile)) {
            if (GpsPlayerTrailCount == GpsPlayerTrail.size()) {
                std::move(GpsPlayerTrail.begin() + 1, GpsPlayerTrail.end(), GpsPlayerTrail.begin());
                --GpsPlayerTrailCount;
            }
            GpsPlayerTrail[GpsPlayerTrailCount++] = LatestPlayerSubtile;
        }
    }

    // A transient drawing failure must neither erase this valid world position
    // nor leave previously projected lines visible. Direct remains independent
    // of whether a GPS origin was available.
    auto projectionReason = NavigationAutomapObservationReason::Projected;
    if (pass.projectClient == nullptr || pass.borrowedAutomapContext == nullptr) {
        projectionReason = NavigationAutomapObservationReason::InvalidProjectionContext;
    } else if (pass.nativeWidth <= 0 || pass.nativeHeight <= 0
        || pass.nativeWidth > 32768 || pass.nativeHeight > 32768) {
        projectionReason = NavigationAutomapObservationReason::InvalidNativeDimensions;
    } else if (pass.clipWidth <= 0 || pass.clipHeight <= 0) {
        projectionReason = NavigationAutomapObservationReason::InvalidClip;
    }
    if (projectionReason != NavigationAutomapObservationReason::Projected) {
        ClearProjectedLinesLocked();
        ClearGpsRouteProjectionLocked();
        note(projectionReason);
        return NavigationAutomapObservationResult::Ignored;
    }

    const auto directPolicy = AcquireNavigationLinePolicySnapshot();
    NavigationNativePoint player{};
    if (!pass.projectClient(
            pass.borrowedAutomapContext,
            pass.playerClientX,
            pass.playerClientY,
            player)) {
        if (pass.diagnostic != nullptr) {
            pass.diagnostic(
                NavigationProjectionDiagnostic{
                    .currentLevelId = pass.currentLevelId,
                    .playerClientX = pass.playerClientX,
                    .playerClientY = pass.playerClientY,
                    .clipLeft = pass.clipLeft,
                    .clipTop = pass.clipTop,
                    .clipWidth = pass.clipWidth,
                    .clipHeight = pass.clipHeight,
                },
                pass.diagnosticUserData);
        }
        ClearProjectedLinesLocked();
        ClearGpsRouteProjectionLocked();
        note(NavigationAutomapObservationReason::PlayerProjectionFailed);
        return NavigationAutomapObservationResult::Ignored;
    }

    note(validOrigin ? NavigationAutomapObservationReason::Projected
        : NavigationAutomapObservationReason::NoNativeOrigin);
    const auto nearestDestination = SelectNearestDestinationIndex(
        pass.playerClientX,
        pass.playerClientY,
        directPolicy);
    std::size_t lineCount{};
    for (std::size_t index = 0U;
            index < DestinationCount;
            ++index) {
        const auto& destination = Destinations[index];
        const auto* family = PolicyForKind(directPolicy, destination.kind);
        if (family == nullptr || !family->enabled
            || family->mode != NavigationLineMode::Direct) {
            continue;
        }
        if (destination.selection
                == NavigationDestinationSelection::NearestToPlayer
            && (!nearestDestination.has_value()
                || index != *nearestDestination)) {
            continue;
        }
        NavigationProjectionDiagnostic diagnostic{
            .currentLevelId = pass.currentLevelId,
            .playerClientX = pass.playerClientX,
            .playerClientY = pass.playerClientY,
            .projectedPlayer = player,
            .playerProjected = true,
            .destination = destination,
            .clipLeft = pass.clipLeft,
            .clipTop = pass.clipTop,
            .clipWidth = pass.clipWidth,
            .clipHeight = pass.clipHeight,
        };
        NavigationNativePoint destinationClient{
            .x = destination.exactClientX,
            .y = destination.exactClientY,
        };
        if (!destination.useExactClientCoordinates) {
            if (!ConvertNavigationSubtileToClientCoordinates(
                    destination.subtileX,
                    destination.subtileY,
                    destinationClient)) {
                if (pass.diagnostic != nullptr) {
                    pass.diagnostic(diagnostic, pass.diagnosticUserData);
                }
                continue;
            }
        }
        diagnostic.destinationClient = destinationClient;
        diagnostic.destinationConverted = true;
        NavigationNativePoint projectedDestination{};
        if (!pass.projectClient(
                pass.borrowedAutomapContext,
                destinationClient.x,
                destinationClient.y,
                projectedDestination)) {
            if (pass.diagnostic != nullptr) {
                pass.diagnostic(diagnostic, pass.diagnosticUserData);
            }
            continue;
        }
        diagnostic.projectedDestination = projectedDestination;
        diagnostic.destinationProjected = true;

        NavigationNativePoint clippedStart{};
        NavigationNativePoint clippedEnd{};
        if (!ClipLineToAutomap(
                player,
                projectedDestination,
                pass,
                clippedStart,
                clippedEnd)) {
            if (pass.diagnostic != nullptr) {
                pass.diagnostic(diagnostic, pass.diagnosticUserData);
            }
            continue;
        }
        diagnostic.clippedStart = clippedStart;
        diagnostic.clippedEnd = clippedEnd;
        diagnostic.lineClipped = true;
        if (pass.diagnostic != nullptr) {
            pass.diagnostic(diagnostic, pass.diagnosticUserData);
        }

        ProjectedLines[lineCount++] = NavigationLineSnapshot{
            .destinationId = destination.destinationId,
            .sessionGeneration = SessionGeneration,
            .destinationRevision = DestinationRevision,
            .policyRevision = directPolicy.revision,
            .levelId = LevelId,
            .startX = clippedStart.x,
            .startY = clippedStart.y,
            .endX = clippedEnd.x,
            .endY = clippedEnd.y,
            .nativeWidth = pass.nativeWidth,
            .nativeHeight = pass.nativeHeight,
            .kind = destination.kind,
        };
    }

    if (AcquireNavigationLinePolicySnapshot().revision == directPolicy.revision) {
        ProjectedLineCount = lineCount;
        ProjectedRevision = DestinationRevision;
        ProjectedPolicyRevision = directPolicy.revision;
        LastProjectionTick = CurrentTickMilliseconds();
    } else {
        ClearProjectedLinesLocked();
    }
    PublishedProjectionRevision.store(
        ProjectedRevision,
        std::memory_order_release);
    PublishedProjectionTick.store(
        LastProjectionTick,
        std::memory_order_release);
    PublishedLineCount.store(ProjectedLineCount, std::memory_order_release);

    const auto gpsRoutes = GpsRoutes.load(std::memory_order_acquire);
    if (!HasLatestPlayerSubtile || gpsRoutes == nullptr || gpsRoutes->empty()) {
        return NavigationAutomapObservationResult::Projected;
    }
    const auto routeSessionGeneration = SessionGeneration;
    const auto routeDestinationRevision = DestinationRevision;
    const auto routeLevelId = LevelId;
    const auto routePlayer = LatestPlayerSubtile;
    const auto routeProjectionEpoch = GpsRouteProjectionEpoch;
    const auto routePolicyRevision = (*gpsRoutes).front().policyRevision;
    auto followStates = GpsFollowStates;
    const auto playerTrail = GpsPlayerTrail;
    const auto playerTrailCount = GpsPlayerTrailCount;
    const auto followStart = GpsFollowEvaluationStart % gpsRoutes->size();
    auto nextFollowStart = (followStart + 1U) % gpsRoutes->size();
    if (routePolicyRevision == 0U || !std::all_of(
            gpsRoutes->begin(), gpsRoutes->end(),
            [routePolicyRevision](const NavigationGpsRoutePath& route) noexcept {
                return route.policyRevision == routePolicyRevision;
            })) {
        ClearGpsRouteProjectionLocked();
        return NavigationAutomapObservationResult::Projected;
    }
    lock.Release();
    std::size_t totalSegments{};
    constexpr auto maximumProjectedSegments = MaximumNavigationGpsRoutePoints
        + GpsFollowMaximumPrefixPoints * MaximumNavigationGpsRoutePaths;
    std::vector<NavigationGpsRouteSegmentSnapshot> segments;
    try {
        std::size_t passCellChecks{};
        bool foundDeferredRoute{};
        for (std::size_t ordinal = 0U; ordinal < gpsRoutes->size(); ++ordinal) {
            const auto routeIndex = (followStart + ordinal) % gpsRoutes->size();
            const auto& route = (*gpsRoutes)[routeIndex];
            // A replacement route is computed asynchronously after the player
            // moves. Retain and reproject the last accepted route until that
            // replacement is published; clearing it here creates a visible
            // blank interval during ordinary movement. Publication itself
            // still requires an exact player origin.
            if (route.points.size() < 2U) {
                return NavigationAutomapObservationResult::Projected;
            }
            auto& following = followStates[routeIndex];
            std::size_t cellChecks{};
            const auto cellBudget = (std::min)(GpsFollowMaximumCellChecks,
                GpsFollowMaximumPassCellChecks - passCellChecks);
            if (cellBudget == 0U && !foundDeferredRoute) {
                nextFollowStart = routeIndex;
                foundDeferredRoute = true;
            }
            FollowNavigationGpsRoute(route, routePlayer, following, cellChecks,
                std::span<const NavigationGpsRoutePoint>{playerTrail.data(), playerTrailCount},
                cellBudget);
            passCellChecks += cellChecks;
            const auto prefixCount = following.prefixCount != 0U
                ? following.prefixCount : 2U;
            const auto routeSegments = route.points.size() - following.next + prefixCount;
            if (totalSegments > maximumProjectedSegments - routeSegments) {
                return NavigationAutomapObservationResult::Projected;
            }
            totalSegments += routeSegments;
        }
        // Allocate only for this immutable snapshot; native conversion below
        // runs without retaining the engine state lock.
        segments.reserve(totalSegments);
        totalSegments = 0U;
        std::size_t routeIndex{};
        for (const auto& route : *gpsRoutes) {
            auto& following = followStates[routeIndex++];
            // Retain progress for later reconnection, but never renew a
            // walking line whose certified start belongs to an older player
            // position. Other connected routes still project in this pass.
            if (route.mode == NavigationGpsRouteMode::Walk
                && route.walkGrid != nullptr
                && (following.prefixCount == 0U
                    || !SameGpsPoint(following.anchor, routePlayer))) {
                continue;
            }
            bool previousProjected{};
            NavigationNativePoint previous{};
            bool hasPreviousPoint{};
            NavigationGpsRoutePoint previousPoint{};
            const auto remaining = route.points.size() - following.next;
            const auto prefixCount = following.prefixCount != 0U
                ? following.prefixCount : 2U;
            for (std::size_t pointIndex = 0U; pointIndex < remaining + prefixCount; ++pointIndex) {
                const auto point = pointIndex < prefixCount
                    ? following.prefixCount != 0U ? following.prefix[pointIndex]
                        : pointIndex == 0U ? following.anchor : following.join
                    : route.points[following.next + pointIndex - prefixCount];
                if (hasPreviousPoint && SameGpsPoint(previousPoint, point)) continue;
                previousPoint = point;
                hasPreviousPoint = true;
                NavigationNativePoint client{};
                NavigationNativePoint projected{};
                const bool usePlayerProjection = pointIndex == 0U
                    && SameGpsPoint(point, routePlayer);
                bool projectedCurrent = usePlayerProjection;
                if (usePlayerProjection) {
                    projected = player;
                } else {
                    projectedCurrent = ConvertNavigationSubtileToClientCoordinates(
                        point.subtileX, point.subtileY, client)
                    && pass.projectClient(
                        pass.borrowedAutomapContext,
                        client.x,
                        client.y,
                        projected);
                }
                if (previousProjected && projectedCurrent) {
                    NavigationNativePoint clippedStart{};
                    NavigationNativePoint clippedEnd{};
                    if (ClipLineToAutomap(
                            previous,
                            projected,
                            pass,
                            clippedStart,
                            clippedEnd)) {
                        if (totalSegments >= maximumProjectedSegments) {
                            return NavigationAutomapObservationResult::Projected;
                        }
                        segments.push_back({
                            .destinationId = route.destinationId,
                            .sessionGeneration = routeSessionGeneration,
                            .destinationRevision = routeDestinationRevision,
                            .policyRevision = routePolicyRevision,
                            .levelId = routeLevelId,
                            .startX = clippedStart.x,
                            .startY = clippedStart.y,
                            .endX = clippedEnd.x,
                            .endY = clippedEnd.y,
                            .nativeWidth = pass.nativeWidth,
                            .nativeHeight = pass.nativeHeight,
                            .kind = route.kind,
                            .mode = route.mode,
                        });
                        ++totalSegments;
                    }
                }
                previous = projected;
                previousProjected = projectedCurrent;
            }
        }
    } catch (...) {
        return NavigationAutomapObservationResult::Projected;
    }
    StateLockGuard publishLock(false);
    if (publishLock) ConsumePendingNavigationInvalidationsLocked();
    if (!publishLock || !Active.load(std::memory_order_acquire)
        || SessionGeneration != routeSessionGeneration
        || DestinationRevision != routeDestinationRevision
        || LevelId != routeLevelId
        || AcquireNavigationLinePolicySnapshot().revision != routePolicyRevision
        || GpsRouteProjectionEpoch != routeProjectionEpoch
        || GpsRoutes.load(std::memory_order_acquire) != gpsRoutes
        || !HasLatestPlayerSubtile
        || LatestPlayerSubtile.subtileX != routePlayer.subtileX
        || LatestPlayerSubtile.subtileY != routePlayer.subtileY
        || !IsRecent(LastPlayerObservationTick, CurrentTickMilliseconds())) {
        return NavigationAutomapObservationResult::Projected;
    }
    try {
        GpsFollowStates = followStates;
        GpsFollowEvaluationStart = nextFollowStart;
        ProjectedGpsRouteSegments = std::move(segments);
    } catch (...) {
        ClearGpsRouteProjectionLocked();
        return NavigationAutomapObservationResult::Projected;
    }
    if (ProjectedGpsRouteSegments.empty()) {
        ClearGpsRouteProjectionLocked();
    } else {
        ProjectedGpsRouteRevision = routeDestinationRevision;
        ProjectedGpsRoutePolicyRevision = routePolicyRevision;
        LastGpsRouteProjectionTick = CurrentTickMilliseconds();
        PublishedGpsRouteSegmentCount.store(
            ProjectedGpsRouteSegments.size(), std::memory_order_release);
        PublishedGpsRouteRevision.store(
            ProjectedGpsRouteRevision, std::memory_order_release);
        PublishedGpsRouteProjectionTick.store(
            LastGpsRouteProjectionTick, std::memory_order_release);
    }
    return NavigationAutomapObservationResult::Projected;
}

auto AcquireNavigationLineSnapshots(
        std::vector<NavigationLineSnapshot>& snapshots,
        bool retainCurrentProjection) noexcept
        -> std::size_t {
    snapshots.clear();
    if (!Active.load(std::memory_order_acquire)
        || PublishedLineCount.load(std::memory_order_acquire) == 0U) {
        return 0U;
    }
    const auto currentTick = CurrentTickMilliseconds();
    if (!retainCurrentProjection && !IsRecent(
            PublishedProjectionTick.load(std::memory_order_acquire),
            currentTick)) {
        return 0U;
    }

    try {
        StateLockGuard lock(false);
        if (lock) ConsumePendingNavigationInvalidationsLocked();
        if (!lock || !Active.load(std::memory_order_acquire)
            || ProjectedLineCount == 0U
            || ProjectedRevision != DestinationRevision
            || ProjectedPolicyRevision != AcquireNavigationLinePolicySnapshot().revision
            || (!retainCurrentProjection
                && !IsRecent(LastProjectionTick, currentTick))) {
            return 0U;
        }
        snapshots.assign(
            ProjectedLines.begin(),
            ProjectedLines.begin()
                + static_cast<std::ptrdiff_t>(ProjectedLineCount));
        return snapshots.size();
    } catch (...) {
        snapshots.clear();
        return 0U;
    }
}

auto AcquireNavigationGpsRouteSourceSnapshot(
        NavigationGpsRouteSourceSnapshot& snapshot,
        NavigationGpsSourceDiagnostics* diagnostics) noexcept -> bool {
    snapshot = {};
    if (diagnostics != nullptr) *diagnostics = {};
    if (!Active.load(std::memory_order_acquire)) {
        return false;
    }
    try {
        StateLockGuard lock(false);
        if (!lock) {
            if (diagnostics != nullptr) {
                diagnostics->readiness =
                    NavigationGpsSourceReadiness::Contended;
            }
            return false;
        }
        ConsumePendingNavigationInvalidationsLocked();
        if (diagnostics != nullptr) {
            diagnostics->stateAvailable = true;
            diagnostics->sessionGeneration = SessionGeneration;
            diagnostics->destinationRevision = DestinationRevision;
            diagnostics->levelId = LevelId;
            diagnostics->destinationCount = DestinationCount;
            diagnostics->hasPlayer = HasLatestPlayerSubtile;
        }
        const auto fail = [diagnostics](
                NavigationGpsSourceReadiness readiness) noexcept -> bool {
            if (diagnostics != nullptr) diagnostics->readiness = readiness;
            return false;
        };
        if (!Active.load(std::memory_order_acquire)) {
            return fail(NavigationGpsSourceReadiness::Inactive);
        }
        if (LevelId == UnknownNavigationLevelId) {
            return fail(NavigationGpsSourceReadiness::UnknownLevel);
        }
        if (DestinationCount == 0U) {
            return fail(NavigationGpsSourceReadiness::NoDestinations);
        }
        if (!HasLatestPlayerSubtile) {
            return fail(NavigationGpsSourceReadiness::NoPlayer);
        }
        snapshot.sessionGeneration = SessionGeneration;
        snapshot.destinationRevision = DestinationRevision;
        snapshot.policy = AcquireNavigationLinePolicySnapshot();
        snapshot.levelId = LevelId;
        snapshot.player = LatestPlayerSubtile;
        snapshot.destinations.assign(
            Destinations.begin(),
            Destinations.begin() + static_cast<std::ptrdiff_t>(DestinationCount));
        if (diagnostics != nullptr) {
            diagnostics->readiness = NavigationGpsSourceReadiness::Ready;
        }
        return true;
    } catch (...) {
        snapshot = {};
        if (diagnostics != nullptr) {
            diagnostics->readiness = NavigationGpsSourceReadiness::Exception;
        }
        return false;
    }
}

auto PublishNavigationGpsRoutes(
        std::uint64_t sessionGeneration,
        std::uint64_t destinationRevision,
        std::uint64_t policyRevision,
        std::int32_t levelId,
        const NavigationGpsRoutePath* paths,
        std::size_t pathCount,
        std::uint64_t* acceptedContentEpoch) noexcept -> bool {
    if (acceptedContentEpoch != nullptr) *acceptedContentEpoch = 0U;
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration == 0U || policyRevision == 0U
        || levelId == UnknownNavigationLevelId
        || pathCount > MaximumNavigationGpsRoutePaths
        || (pathCount != 0U && paths == nullptr)) {
        return false;
    }
    std::size_t totalPoints{};
    for (std::size_t index = 0U; index < pathCount; ++index) {
        const auto& path = paths[index];
        if (!IsValidKind(path.kind) || !IsValidGpsRouteMode(path.mode)
            || path.policyRevision != policyRevision
            || path.points.size() < 2U
            || path.points.size() > MaximumNavigationGpsRoutePoints
            || totalPoints > MaximumNavigationGpsRoutePoints - path.points.size()) {
            return false;
        }
        totalPoints += path.points.size();
        for (const auto& point : path.points) {
            if (point.subtileX < 0 || point.subtileY < 0) return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (paths[previous].destinationId == path.destinationId
                && paths[previous].kind == path.kind) {
                return false;
            }
        }
    }

    try {
        // Construct the immutable snapshot before taking the state lock. This
        // entry point is reached from the automap observation coordinator, so
        // allocation and copies must not extend the native-state critical
        // section.
        std::vector<NavigationGpsRoutePath> replacement;
        replacement.reserve(pathCount);
        for (std::size_t index = 0U; index < pathCount; ++index) {
            replacement.push_back(paths[index]);
        }
        const auto replacementSnapshot =
            std::make_shared<const std::vector<NavigationGpsRoutePath>>(
                std::move(replacement));
        StateLockGuard lock(false);
        if (lock) ConsumePendingNavigationInvalidationsLocked();
        if (!lock) return false;
        const auto policy = AcquireNavigationLinePolicySnapshot();
        if (!Active.load(std::memory_order_acquire)
            || policy.revision != policyRevision
            || sessionGeneration != SessionGeneration
            || destinationRevision != DestinationRevision || levelId != LevelId
            || !HasLatestPlayerSubtile) {
            return false;
        }
        for (std::size_t index = 0U; index < pathCount; ++index) {
            const auto& path = paths[index];
            const auto destination = std::find_if(
                Destinations.begin(),
                Destinations.begin()
                    + static_cast<std::ptrdiff_t>(DestinationCount),
                [&path](const NavigationSubtileDestination& candidate) noexcept {
                    return candidate.destinationId == path.destinationId
                        && candidate.kind == path.kind;
                });
            const auto* family = PolicyForKind(policy, path.kind);
            if (destination == Destinations.begin()
                    + static_cast<std::ptrdiff_t>(DestinationCount)
                || family == nullptr || !family->enabled
                || !IsGpsLineMode(family->mode)
                || path.kind != destination->kind
                || path.mode != GpsModeForLineMode(family->mode)
                || !IsWithinGpsGoalSnapDistance(
                    path.points.back().subtileX, destination->subtileX)
                || !IsWithinGpsGoalSnapDistance(
                    path.points.back().subtileY, destination->subtileY)) {
                return false;
            }
        }
        if (SameGpsRoutes(std::span<const NavigationGpsRoutePath>{
                paths, pathCount})) {
            if (acceptedContentEpoch != nullptr) {
                *acceptedContentEpoch = GpsRouteContentEpoch.load(std::memory_order_acquire);
            }
            return true;
        }
        GpsRoutes.store(
            replacementSnapshot,
            std::memory_order_release);
        GpsFollowStates = {};
        const auto contentEpoch = GpsRouteContentEpoch.fetch_add(
            1U, std::memory_order_acq_rel) + 1U;
        if (acceptedContentEpoch != nullptr) *acceptedContentEpoch = contentEpoch;
        // Hard identity changes already invalidate projections. A compatible
        // soft replacement keeps the last frame until the next native pass;
        // the snapshot/epoch checks reject an old in-flight projection.
        ++GpsRouteProjectionEpoch;
        if (pathCount == 0U) ClearGpsRouteProjectionLocked();
        return true;
    } catch (...) {
        return false;
    }
}

auto GetNavigationGpsRouteContentEpoch() noexcept -> std::uint64_t {
    return GpsRouteContentEpoch.load(std::memory_order_acquire);
}

auto AcquireNavigationGpsRouteSegmentSnapshots(
        std::vector<NavigationGpsRouteSegmentSnapshot>& snapshots,
        bool retainCurrentProjection) noexcept -> std::size_t {
    snapshots.clear();
    if (!Active.load(std::memory_order_acquire)
        || PublishedGpsRouteSegmentCount.load(std::memory_order_acquire) == 0U) {
        return 0U;
    }
    const auto currentTick = CurrentTickMilliseconds();
    if (!retainCurrentProjection && !IsRecent(
            PublishedGpsRouteProjectionTick.load(std::memory_order_acquire),
            currentTick)) {
        return 0U;
    }
    try {
        StateLockGuard lock(false);
        if (lock) ConsumePendingNavigationInvalidationsLocked();
        if (!lock || ProjectedGpsRouteSegments.empty()
            || ProjectedGpsRouteRevision != DestinationRevision
            || ProjectedGpsRoutePolicyRevision != AcquireNavigationLinePolicySnapshot().revision
            || (!retainCurrentProjection && !IsRecent(
                LastGpsRouteProjectionTick, currentTick))) {
            return 0U;
        }
        snapshots = ProjectedGpsRouteSegments;
        return snapshots.size();
    } catch (...) {
        snapshots.clear();
        return 0U;
    }
}

auto WantsNavigationGpsRouteFrame(bool retainCurrentProjection) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || PublishedGpsRouteSegmentCount.load(std::memory_order_acquire) == 0U) {
        return false;
    }
    const auto currentTick = CurrentTickMilliseconds();
    return (retainCurrentProjection || IsRecent(
            PublishedGpsRouteProjectionTick.load(std::memory_order_acquire),
            currentTick))
        && PublishedGpsRouteRevision.load(std::memory_order_acquire) != 0U;
}

void InvalidateNavigationGpsRoutes() noexcept {
    (void)PendingGpsRouteInvalidationEpoch.fetch_add(1U, std::memory_order_acq_rel);
    PublishedGpsRouteSegmentCount.store(0U, std::memory_order_release);
    PublishedGpsRouteRevision.store(0U, std::memory_order_release);
    PublishedGpsRouteProjectionTick.store(0U, std::memory_order_release);
    StateLockGuard lock(false);
    if (!lock) return;
    ConsumePendingNavigationInvalidationsLocked();
}

void InvalidateNavigationProjection() noexcept {
    (void)PendingNavigationProjectionInvalidationEpoch.fetch_add(
        1U, std::memory_order_acq_rel);
    PublishedLineCount.store(0U, std::memory_order_release);
    PublishedProjectionRevision.store(0U, std::memory_order_release);
    PublishedProjectionTick.store(0U, std::memory_order_release);
    PublishedGpsRouteSegmentCount.store(0U, std::memory_order_release);
    PublishedGpsRouteRevision.store(0U, std::memory_order_release);
    PublishedGpsRouteProjectionTick.store(0U, std::memory_order_release);
    StateLockGuard lock(false);
    if (!lock) return;
    ConsumePendingNavigationInvalidationsLocked();
}

auto WantsNavigationLineFrame(bool retainCurrentProjection) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || PublishedLineCount.load(std::memory_order_acquire) == 0U) {
        return false;
    }
    const auto currentTick = CurrentTickMilliseconds();
    return (retainCurrentProjection || IsRecent(
            PublishedProjectionTick.load(std::memory_order_acquire),
            currentTick))
        && PublishedProjectionRevision.load(std::memory_order_acquire) != 0U;
}

auto GetNavigationEngineStatus() noexcept -> NavigationEngineStatus {
    StateLockGuard lock(true);
    return {
        .sessionGeneration = SessionGeneration,
        .destinationRevision = DestinationRevision,
        .observedLevelChanges = ObservedLevelChanges,
        .levelId = LevelId,
        .destinationCount = DestinationCount,
        .projectedLineCount = ProjectedLineCount,
    };
}

} // namespace RuffnecKk::MapSense
