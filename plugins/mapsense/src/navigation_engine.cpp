#include "navigation_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <thread>
#include <type_traits>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uint64_t NavigationSnapshotLifetimeMilliseconds = 250U;

std::atomic_bool Active{};
std::atomic_flag StateLock = ATOMIC_FLAG_INIT;
std::array<NavigationSubtileDestination, MaximumNavigationDestinations>
    Destinations{};
std::size_t DestinationCount{};
std::array<NavigationLineSnapshot, MaximumNavigationDestinations>
    ProjectedLines{};
std::size_t ProjectedLineCount{};
std::uint64_t SessionGeneration{};
std::uint64_t DestinationRevision{1U};
std::uint64_t ProjectedRevision{};
std::uint64_t LastProjectionTick{};
std::int32_t LevelId{UnknownNavigationLevelId};
std::uint64_t ObservedLevelChanges{};

std::atomic<std::uint64_t> PublishedProjectionTick{};
std::atomic<std::uint64_t> PublishedProjectionRevision{};
std::atomic<std::size_t> PublishedLineCount{};

static_assert(std::is_standard_layout_v<NavigationSubtileDestination>);
static_assert(std::is_trivially_copyable_v<NavigationSubtileDestination>);
static_assert(std::is_standard_layout_v<NavigationLineSnapshot>);
static_assert(std::is_trivially_copyable_v<NavigationLineSnapshot>);

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
        std::int32_t playerClientY) noexcept -> std::optional<std::size_t> {
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
        if (destination.selection
                != NavigationDestinationSelection::NearestToPlayer
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
    LastProjectionTick = 0U;
    PublishedLineCount.store(0U, std::memory_order_release);
    PublishedProjectionRevision.store(0U, std::memory_order_release);
    PublishedProjectionTick.store(0U, std::memory_order_release);
}

void ClearDestinationsLocked() noexcept {
    DestinationCount = 0U;
    ++DestinationRevision;
    ClearProjectedLinesLocked();
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

void InitializeNavigationEngine() noexcept {
    StateLockGuard lock(true);
    Active.store(false, std::memory_order_release);
    SessionGeneration = 0U;
    LevelId = UnknownNavigationLevelId;
    DestinationCount = 0U;
    DestinationRevision = 1U;
    ObservedLevelChanges = 0U;
    ClearProjectedLinesLocked();
    Active.store(true, std::memory_order_release);
}

void ShutdownNavigationEngine() noexcept {
    Active.store(false, std::memory_order_release);
    StateLockGuard lock(true);
    SessionGeneration = 0U;
    LevelId = UnknownNavigationLevelId;
    ClearDestinationsLocked();
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
        std::size_t destinationCount) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || levelId == UnknownNavigationLevelId
        || destinationCount > MaximumNavigationDestinations
        || (destinationCount != 0U && destinations == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < destinationCount; ++index) {
        if (!IsValidKind(destinations[index].kind)
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
    if (destinationCount != 0U) {
        std::copy_n(destinations, destinationCount, Destinations.begin());
    }
    DestinationCount = destinationCount;
    ++DestinationRevision;
    ClearProjectedLinesLocked();
    return true;
}

auto ObserveNavigationAutomapPass(
        const NavigationAutomapPass& pass) noexcept
        -> NavigationAutomapObservationResult {
    if (!Active.load(std::memory_order_acquire)
        || pass.projectClient == nullptr
        || pass.borrowedAutomapContext == nullptr
        || pass.nativeWidth <= 0 || pass.nativeHeight <= 0
        || pass.nativeWidth > 32768 || pass.nativeHeight > 32768
        || pass.clipWidth <= 0 || pass.clipHeight <= 0) {
        return NavigationAutomapObservationResult::Ignored;
    }

    StateLockGuard lock(false);
    if (!lock || !Active.load(std::memory_order_acquire)) {
        return NavigationAutomapObservationResult::Ignored;
    }
    if (pass.currentLevelId == UnknownNavigationLevelId) {
        ClearProjectedLinesLocked();
        return NavigationAutomapObservationResult::Ignored;
    }
    if (pass.currentLevelId != LevelId) {
        LevelId = pass.currentLevelId;
        ++ObservedLevelChanges;
        ClearDestinationsLocked();
        return NavigationAutomapObservationResult::LevelChanged;
    }
    if (pass.inTown) {
        if (DestinationCount != 0U) {
            ClearDestinationsLocked();
        } else {
            ClearProjectedLinesLocked();
        }
        return NavigationAutomapObservationResult::Ignored;
    }
    if (DestinationCount == 0U) {
        ClearProjectedLinesLocked();
        return NavigationAutomapObservationResult::Ignored;
    }

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
        return NavigationAutomapObservationResult::Ignored;
    }

    const auto nearestDestination = SelectNearestDestinationIndex(
        pass.playerClientX,
        pass.playerClientY);
    std::size_t lineCount{};
    for (std::size_t index = 0U;
            index < DestinationCount;
            ++index) {
        const auto& destination = Destinations[index];
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

    ProjectedLineCount = lineCount;
    ProjectedRevision = DestinationRevision;
    LastProjectionTick = CurrentTickMilliseconds();
    PublishedProjectionRevision.store(
        ProjectedRevision,
        std::memory_order_release);
    PublishedProjectionTick.store(
        LastProjectionTick,
        std::memory_order_release);
    PublishedLineCount.store(lineCount, std::memory_order_release);
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
        if (!lock || ProjectedLineCount == 0U
            || ProjectedRevision != DestinationRevision
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

void InvalidateNavigationProjection() noexcept {
    StateLockGuard lock(true);
    ClearProjectedLinesLocked();
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
