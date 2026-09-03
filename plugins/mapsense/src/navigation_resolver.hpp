#pragma once

#include "mapsense_config.hpp"
#include "navigation_policy.hpp"
#include "reveal_engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

class MapSenseDataCatalog;

namespace Detail {

inline constexpr std::array<std::int32_t, 5U> TownLevelIds{
    1,   // Rogue Encampment
    40,  // Lut Gholein
    75,  // Kurast Docks
    103, // The Pandemonium Fortress
    109, // Harrogath
};

// Waypoint labels add no navigation value inside D2R's five canonical towns.
// Keep this owner-level policy independent of the player's current room so a
// Reveal All capture cannot reintroduce a town label while viewed from outside.
[[nodiscard]] constexpr auto AllowsWaypointLabelForLevel(
        std::int32_t levelId) noexcept -> bool {
    return std::ranges::find(TownLevelIds, levelId) == TownLevelIds.end();
}

struct NavigationWaypointPresetCandidate final {
    std::int32_t nativeTileX{};
    std::int32_t nativeTileY{};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t classId{};
};

struct NavigationRoomTileLinkCandidate final {
    std::int32_t sourcePresetId{};
    std::int32_t targetLevelId{UnknownNavigationLevelId};
};

struct NavigationRoomRectangle final {
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t tileX{};
    std::int32_t tileY{};
    std::int32_t width{};
    std::int32_t height{};
};

struct NavigationCollisionGridView final {
    std::int32_t originX{};
    std::int32_t originY{};
    std::int32_t width{};
    std::int32_t height{};
    std::span<const std::uint16_t> cells{};
};

struct NavigationOutdoorOpening final {
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t spanSubtiles{};
    NavigationExitBoundaryIdentity boundaryIdentity{};
};

inline constexpr std::int32_t NavigationSubtilesPerGameTile = 5;
inline constexpr std::uint16_t NavigationPlayerPathCollisionMask = 0x1C09U;

[[nodiscard]] constexpr auto IsNavigationPlayerPathOpen(
        std::uint16_t collisionFlags) noexcept -> bool {
    return (collisionFlags & NavigationPlayerPathCollisionMask) == 0U;
}

[[nodiscard]] constexpr auto TryMakeNavigationLevelTileAnchor(
        std::int32_t levelTileX,
        std::int32_t levelTileY,
        std::int32_t offsetTileX,
        std::int32_t offsetTileY,
        NavigationOutdoorOpening& output) noexcept -> bool {
    if (levelTileX < 0 || levelTileY < 0
        || offsetTileX < 0 || offsetTileY < 0) {
        return false;
    }
    const auto subtileX =
        (static_cast<std::int64_t>(levelTileX) + offsetTileX)
        * NavigationSubtilesPerGameTile;
    const auto subtileY =
        (static_cast<std::int64_t>(levelTileY) + offsetTileY)
        * NavigationSubtilesPerGameTile;
    if (subtileX > (std::numeric_limits<std::int32_t>::max)()
        || subtileY > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    output = {
        .subtileX = static_cast<std::int32_t>(subtileX),
        .subtileY = static_cast<std::int32_t>(subtileY),
        .spanSubtiles = 0,
    };
    return true;
}

enum class NavigationBoundarySide : std::uint8_t {
    Left,
    Right,
    Top,
    Bottom,
};

struct NavigationBoundarySpan final {
    std::int32_t targetLevelId{UnknownNavigationLevelId};
    NavigationBoundarySide side{NavigationBoundarySide::Left};
    std::int32_t startSubtile{};
    std::int32_t endSubtile{};
    // The axis coordinate of the shared Room boundary: X for left/right and
    // Y for top/bottom. Keeping it prevents parallel seams from collapsing
    // merely because their projected intervals overlap.
    std::int32_t fixedSubtile{-1};
    // Room identities are transient keys used only during one UI-thread
    // refresh. Together they bind both collision spans to one exact RoomsNear
    // pair without retaining either pointer after resolution.
    std::uintptr_t sourceRoomIdentity{};
    std::uintptr_t targetRoomIdentity{};
};

struct NavigationLevelSubtileBounds final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

// Native collision coordinates are signed 32-bit values. Always widen before
// combining an origin with a dimension or local position, then fail closed if
// the absolute subtile coordinate cannot be represented by the public model.
[[nodiscard]] constexpr auto TryAddNavigationSubtileOffset(
        std::int32_t origin,
        std::int32_t offset,
        std::int32_t& output) noexcept -> bool {
    if (origin < 0 || offset < 0) return false;
    const auto sum = static_cast<std::int64_t>(origin)
        + static_cast<std::int64_t>(offset);
    if (sum > (std::numeric_limits<std::int32_t>::max)()) return false;
    output = static_cast<std::int32_t>(sum);
    return true;
}

enum class NavigationOutdoorBoundaryMatchResult : std::uint8_t {
    NotFound,
    Found,
    Ambiguous,
    Invalid,
};

enum class NavigationOutdoorOpeningSelectionPolicy : std::uint8_t {
    RequireUnique,
    AcceptStablePlayerPath,
};

enum class NavigationExitEvidence : std::uint8_t {
    OutdoorLevelBoundary = 2U,
    OutdoorCollision = OutdoorLevelBoundary,
    RoomTile = 3U,
    QuestPreset = 4U,
    BossPreset = 5U,
    RuntimeObject = 6U,
};

struct NavigationExitSelection final {
    NavigationExitCandidate candidate{};
    NavigationExitEvidence evidence{NavigationExitEvidence::OutdoorCollision};
    std::int32_t spanSubtiles{};
    NavigationExitBoundaryIdentity boundaryIdentity{};
    bool canonicalLevelPairAnchor{};
};

// Native waypoint output is quantized to game tiles. The exact endpoint must
// come from the waypoint PresetUnit that maps back to those native tile
// coordinates, never from the quantized output itself.
[[nodiscard]] inline auto SelectExactWaypointPreset(
        std::int32_t nativeTileX,
        std::int32_t nativeTileY,
        std::span<const NavigationWaypointPresetCandidate> candidates)
        noexcept -> std::optional<NavigationWaypointPresetCandidate> {
    for (const auto& candidate : candidates) {
        if (candidate.nativeTileX != nativeTileX
            || candidate.nativeTileY != nativeTileY
            || candidate.subtileX < 0
            || candidate.subtileY < 0) {
            continue;
        }
        // 0x3DAD90 stops on the first matching waypoint preset in the same
        // chain. Preserve that order if several subtiles quantize to one tile.
        return candidate;
    }
    return std::nullopt;
}

struct PassiveWaypointPresetSelection final {
    std::optional<NavigationWaypointPresetCandidate> exact{};
    bool pending{true};
};

// Passive reveal-wide collection has no native tile result to disambiguate a
// room. A complete traversal proves absence when it finds zero waypoint
// presets; it may publish one valid preset, while multiple or an incomplete
// room chain keeps the existing owner definition.
[[nodiscard]] inline auto SelectPassiveWaypointPreset(
        std::span<const NavigationWaypointPresetCandidate> candidates,
        bool hasIncompleteRoom) noexcept -> PassiveWaypointPresetSelection {
    if (hasIncompleteRoom || candidates.size() > 1U
        || (candidates.size() == 1U
            && (candidates.front().subtileX < 0
                || candidates.front().subtileY < 0))) {
        return {};
    }
    if (candidates.empty()) {
        return {.exact = std::nullopt, .pending = false};
    }
    return {.exact = candidates.front(), .pending = false};
}

struct PassivePoiPublicationPolicy final {
    bool publishExitLabels{};
    bool mergeProvenExitFragments{};
    bool publishWaypoint{};
};

[[nodiscard]] constexpr auto MakePassivePoiPublicationPolicy(
        bool pendingExits,
        bool pendingWaypoint) noexcept -> PassivePoiPublicationPolicy {
    return {
        .publishExitLabels = !pendingExits,
        .mergeProvenExitFragments = pendingExits,
        .publishWaypoint = !pendingWaypoint,
    };
}

// A type-5 PresetUnit stores the source-side LvlWarp id. RoomTile+0 points to
// the destination DrlgRoom even when D2R has not created the reciprocal tile
// yet, so exact exit selection must not depend on that reciprocal state.
[[nodiscard]] inline auto SelectRoomTileTargetLevel(
        std::int32_t sourcePresetId,
        std::span<const NavigationRoomTileLinkCandidate> links) noexcept
        -> std::optional<std::int32_t> {
    if (sourcePresetId < 0) return std::nullopt;
    for (const auto& link : links) {
        if (link.sourcePresetId == sourcePresetId
            && link.targetLevelId > 0) {
            return link.targetLevelId;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline auto IsDirectVisibleTarget(
        std::int32_t currentLevelId,
        std::int32_t targetLevelId,
        std::span<const std::int32_t> visibleTargetLevelIds) noexcept -> bool {
    if (currentLevelId <= 0 || targetLevelId <= 0
        || targetLevelId == currentLevelId) {
        return false;
    }
    for (const auto visibleTargetLevelId : visibleTargetLevelIds) {
        if (visibleTargetLevelId == targetLevelId) return true;
    }
    return false;
}

// Finds the same walkable boundary opening that both collision maps expose to
// gameplay. The edge and adjacent inward cells must be free on each side, the
// shared opening must span at least three subtiles, and the widest exact
// intersection wins with a stable low-coordinate tie-breaker.
[[nodiscard]] inline auto FindOutdoorCollisionOpening(
        const NavigationRoomRectangle& source,
        const NavigationRoomRectangle& neighbour,
        const NavigationCollisionGridView& sourceGrid,
        const NavigationCollisionGridView& neighbourGrid,
        NavigationOutdoorOpening& output) noexcept -> bool {
    if (source.levelId <= 0 || neighbour.levelId <= 0
        || source.levelId == neighbour.levelId
        || source.tileX < 0 || source.tileY < 0
        || neighbour.tileX < 0 || neighbour.tileY < 0
        || source.width <= 0 || source.height <= 0
        || neighbour.width <= 0 || neighbour.height <= 0) {
        return false;
    }

    const auto sourceLeft = static_cast<std::int64_t>(source.tileX);
    const auto sourceTop = static_cast<std::int64_t>(source.tileY);
    const auto sourceRight = sourceLeft + source.width;
    const auto sourceBottom = sourceTop + source.height;
    const auto neighbourLeft = static_cast<std::int64_t>(neighbour.tileX);
    const auto neighbourTop = static_cast<std::int64_t>(neighbour.tileY);
    const auto neighbourRight = neighbourLeft + neighbour.width;
    const auto neighbourBottom = neighbourTop + neighbour.height;
    constexpr auto scale = std::int64_t{5};
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (sourceRight > maximum || sourceBottom > maximum
        || neighbourRight > maximum || neighbourBottom > maximum
        || sourceRight * scale > maximum
        || sourceBottom * scale > maximum
        || neighbourRight * scale > maximum
        || neighbourBottom * scale > maximum) {
        return false;
    }
    const auto validGrid = [scale](
            const NavigationRoomRectangle& rectangle,
            const NavigationCollisionGridView& grid) noexcept {
        if (grid.originX < 0 || grid.originY < 0
            || grid.width < 2 || grid.height < 2) {
            return false;
        }
        const auto expectedOriginX =
            static_cast<std::int64_t>(rectangle.tileX) * scale;
        const auto expectedOriginY =
            static_cast<std::int64_t>(rectangle.tileY) * scale;
        const auto expectedWidth =
            static_cast<std::int64_t>(rectangle.width) * scale;
        const auto expectedHeight =
            static_cast<std::int64_t>(rectangle.height) * scale;
        const auto gridRight = static_cast<std::int64_t>(grid.originX)
            + static_cast<std::int64_t>(grid.width);
        const auto gridBottom = static_cast<std::int64_t>(grid.originY)
            + static_cast<std::int64_t>(grid.height);
        if (grid.originX != expectedOriginX
            || grid.originY != expectedOriginY
            || grid.width != expectedWidth
            || grid.height != expectedHeight
            || gridRight > (std::numeric_limits<std::int32_t>::max)()
            || gridBottom > (std::numeric_limits<std::int32_t>::max)()) {
            return false;
        }
        const auto requiredCellCount =
            static_cast<std::uint64_t>(grid.width)
            * static_cast<std::uint64_t>(grid.height);
        return requiredCellCount <= grid.cells.size();
    };
    if (!validGrid(source, sourceGrid)
        || !validGrid(neighbour, neighbourGrid)) {
        return false;
    }

    enum class Side : std::uint8_t { Left, Right, Top, Bottom };
    Side side{};
    std::int64_t overlapStart{};
    std::int64_t overlapEnd{};
    const auto verticalStart = sourceTop > neighbourTop
        ? sourceTop : neighbourTop;
    const auto verticalEnd = sourceBottom < neighbourBottom
        ? sourceBottom : neighbourBottom;
    const auto horizontalStart = sourceLeft > neighbourLeft
        ? sourceLeft : neighbourLeft;
    const auto horizontalEnd = sourceRight < neighbourRight
        ? sourceRight : neighbourRight;
    if (neighbourRight == sourceLeft && verticalEnd > verticalStart) {
        side = Side::Left;
        overlapStart = verticalStart * scale;
        overlapEnd = verticalEnd * scale;
    } else if (neighbourLeft == sourceRight
            && verticalEnd > verticalStart) {
        side = Side::Right;
        overlapStart = verticalStart * scale;
        overlapEnd = verticalEnd * scale;
    } else if (neighbourBottom == sourceTop
            && horizontalEnd > horizontalStart) {
        side = Side::Top;
        overlapStart = horizontalStart * scale;
        overlapEnd = horizontalEnd * scale;
    } else if (neighbourTop == sourceBottom
            && horizontalEnd > horizontalStart) {
        side = Side::Bottom;
        overlapStart = horizontalStart * scale;
        overlapEnd = horizontalEnd * scale;
    } else {
        return false;
    }

    if (overlapStart < 0 || overlapEnd <= overlapStart
        || overlapEnd > maximum) {
        return false;
    }

    const auto cell = [](const NavigationCollisionGridView& grid,
            std::int32_t x,
            std::int32_t y) noexcept {
        return grid.cells[
            static_cast<std::size_t>(y)
                * static_cast<std::size_t>(grid.width)
            + static_cast<std::size_t>(x)];
    };
    const auto isOpen = [&cell, &sourceGrid, &neighbourGrid, side](
            std::int32_t position) noexcept {
        const auto sourceVertical = position - sourceGrid.originY;
        const auto neighbourVertical = position - neighbourGrid.originY;
        const auto sourceHorizontal = position - sourceGrid.originX;
        const auto neighbourHorizontal = position - neighbourGrid.originX;
        switch (side) {
            case Side::Left:
                return (cell(sourceGrid, 0, sourceVertical) & 1U) == 0U
                    && (cell(sourceGrid, 1, sourceVertical) & 1U) == 0U
                    && (cell(neighbourGrid,
                            neighbourGrid.width - 1,
                            neighbourVertical) & 1U) == 0U
                    && (cell(neighbourGrid,
                            neighbourGrid.width - 2,
                            neighbourVertical) & 1U) == 0U;
            case Side::Right:
                return (cell(sourceGrid,
                            sourceGrid.width - 1,
                            sourceVertical) & 1U) == 0U
                    && (cell(sourceGrid,
                            sourceGrid.width - 2,
                            sourceVertical) & 1U) == 0U
                    && (cell(neighbourGrid, 0, neighbourVertical) & 1U) == 0U
                    && (cell(neighbourGrid, 1, neighbourVertical) & 1U) == 0U;
            case Side::Top:
                return (cell(sourceGrid, sourceHorizontal, 0) & 1U) == 0U
                    && (cell(sourceGrid, sourceHorizontal, 1) & 1U) == 0U
                    && (cell(neighbourGrid,
                            neighbourHorizontal,
                            neighbourGrid.height - 1) & 1U) == 0U
                    && (cell(neighbourGrid,
                            neighbourHorizontal,
                            neighbourGrid.height - 2) & 1U) == 0U;
            case Side::Bottom:
                return (cell(sourceGrid,
                            sourceHorizontal,
                            sourceGrid.height - 1) & 1U) == 0U
                    && (cell(sourceGrid,
                            sourceHorizontal,
                            sourceGrid.height - 2) & 1U) == 0U
                    && (cell(neighbourGrid, neighbourHorizontal, 0) & 1U)
                        == 0U
                    && (cell(neighbourGrid, neighbourHorizontal, 1) & 1U)
                        == 0U;
        }
        return false;
    };

    std::int32_t bestStart{-1};
    std::int32_t bestLength{};
    std::int32_t currentStart{-1};
    const auto considerRun = [&bestStart, &bestLength](
            std::int32_t start,
            std::int32_t end) noexcept {
        if (start < 0) return;
        const auto length = end - start;
        if (length >= 3 && length > bestLength) {
            bestStart = start;
            bestLength = length;
        }
    };
    for (auto position = static_cast<std::int32_t>(overlapStart);
            position < static_cast<std::int32_t>(overlapEnd);
            ++position) {
        if (isOpen(position)) {
            if (currentStart < 0) currentStart = position;
        } else {
            considerRun(currentStart, position);
            currentStart = -1;
        }
    }
    considerRun(currentStart, static_cast<std::int32_t>(overlapEnd));
    if (bestStart < 0) return false;

    const auto midpoint = bestStart + bestLength / 2;
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    switch (side) {
        case Side::Left:
            if (!TryAddNavigationSubtileOffset(
                    neighbourGrid.originX,
                    neighbourGrid.width - 1,
                    subtileX)) {
                return false;
            }
            subtileY = midpoint;
            break;
        case Side::Right:
            subtileX = neighbourGrid.originX;
            subtileY = midpoint;
            break;
        case Side::Top:
            subtileX = midpoint;
            if (!TryAddNavigationSubtileOffset(
                    neighbourGrid.originY,
                    neighbourGrid.height - 1,
                    subtileY)) {
                return false;
            }
            break;
        case Side::Bottom:
            subtileX = midpoint;
            subtileY = neighbourGrid.originY;
            break;
    }
    output = {
        .subtileX = subtileX,
        .subtileY = subtileY,
        .spanSubtiles = bestLength,
    };
    return true;
}

// Sorts and coalesces touching fragments only when they belong to the same
// exact RoomsNear pair and fixed boundary coordinate. Anonymous spans with no
// fixed coordinate are retained solely for source-side policy fixtures.
[[nodiscard]] inline auto MergeOutdoorBoundarySpans(
        std::span<NavigationBoundarySpan> spans,
        std::size_t count,
        std::size_t& mergedCount) noexcept -> bool {
    mergedCount = 0U;
    if (count > spans.size()) return false;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& span = spans[index];
        if (span.targetLevelId <= 0
            || span.startSubtile < 0
            || span.endSubtile <= span.startSubtile) {
            return false;
        }
        const auto hasExplicitIdentity = span.fixedSubtile >= 0
            && span.sourceRoomIdentity != 0U
            && span.targetRoomIdentity != 0U;
        const auto isAnonymousFixture = span.fixedSubtile < 0
            && span.sourceRoomIdentity == 0U
            && span.targetRoomIdentity == 0U;
        if (!hasExplicitIdentity && !isAnonymousFixture) return false;
    }
    std::sort(
        spans.begin(),
        spans.begin() + static_cast<std::ptrdiff_t>(count),
        [](const NavigationBoundarySpan& left,
                const NavigationBoundarySpan& right) noexcept {
            if (left.targetLevelId != right.targetLevelId) {
                return left.targetLevelId < right.targetLevelId;
            }
            if (left.side != right.side) {
                return static_cast<std::uint8_t>(left.side)
                    < static_cast<std::uint8_t>(right.side);
            }
            if (left.fixedSubtile != right.fixedSubtile) {
                return left.fixedSubtile < right.fixedSubtile;
            }
            if (left.sourceRoomIdentity != right.sourceRoomIdentity) {
                return left.sourceRoomIdentity < right.sourceRoomIdentity;
            }
            if (left.targetRoomIdentity != right.targetRoomIdentity) {
                return left.targetRoomIdentity < right.targetRoomIdentity;
            }
            if (left.startSubtile != right.startSubtile) {
                return left.startSubtile < right.startSubtile;
            }
            return left.endSubtile < right.endSubtile;
        });
    for (std::size_t index = 0U; index < count; ++index) {
        const auto current = spans[index];
        if (mergedCount != 0U) {
            auto& previous = spans[mergedCount - 1U];
            if (previous.targetLevelId == current.targetLevelId
                && previous.side == current.side
                && previous.fixedSubtile == current.fixedSubtile
                && previous.sourceRoomIdentity
                    == current.sourceRoomIdentity
                && previous.targetRoomIdentity
                    == current.targetRoomIdentity
                && current.startSubtile <= previous.endSubtile) {
                if (current.endSubtile > previous.endSubtile) {
                    previous.endSubtile = current.endSubtile;
                }
                continue;
            }
        }
        spans[mergedCount++] = current;
    }
    return true;
}

[[nodiscard]] constexpr auto HasNavigationOutdoorVisibilitySlot(
        std::uint32_t roomFlags,
        std::uint8_t slot) noexcept -> bool {
    if (slot >= 8U) return false;
    const auto mask = std::uint32_t{1U}
        << (static_cast<std::uint32_t>(slot) + 4U);
    return (roomFlags & mask) != 0U;
}

// Intersects source- and destination-side openings only when both came from
// the same exact RoomsNear pair at the same fixed boundary coordinate. The
// native visibility-slot and player-path filters are applied before runtime
// spans reach this helper. Strict callers can reject multiple openings. The
// outdoor runtime accepts the first geographically sorted proven player path:
// jungle generation legitimately permits several exits to the same level,
// and their width has no native selection meaning.
[[nodiscard]] inline auto FindUniqueOutdoorLevelBoundaryOpening(
        const NavigationLevelSubtileBounds& sourceBounds,
        std::int32_t targetLevelId,
        std::span<const NavigationBoundarySpan> sourceSpans,
        std::span<const NavigationBoundarySpan> targetSpans,
        NavigationOutdoorOpening& output,
        NavigationOutdoorOpeningSelectionPolicy selectionPolicy =
            NavigationOutdoorOpeningSelectionPolicy::RequireUnique,
        std::span<NavigationOutdoorOpening> collectedOpenings = {},
        std::size_t* collectedOpeningCount = nullptr) noexcept
        -> NavigationOutdoorBoundaryMatchResult {
    if (collectedOpeningCount != nullptr) *collectedOpeningCount = 0U;
    if (targetLevelId <= 0
        || sourceBounds.left < 0 || sourceBounds.top < 0
        || sourceBounds.right <= sourceBounds.left
        || sourceBounds.bottom <= sourceBounds.top) {
        return NavigationOutdoorBoundaryMatchResult::Invalid;
    }

    // Source spans use the current level id as a positive tag while target
    // spans use the destination id. All other identity fields must match.
    const auto sourceLevelTag = sourceSpans.empty()
        ? UnknownNavigationLevelId : sourceSpans.front().targetLevelId;
    for (const auto& source : sourceSpans) {
        if (sourceLevelTag <= 0
            || source.targetLevelId != sourceLevelTag
            || source.startSubtile < 0
            || source.endSubtile <= source.startSubtile) {
            return NavigationOutdoorBoundaryMatchResult::Invalid;
        }
        const auto hasExplicitIdentity = source.fixedSubtile >= 0
            && source.sourceRoomIdentity != 0U
            && source.targetRoomIdentity != 0U;
        const auto isAnonymousFixture = source.fixedSubtile < 0
            && source.sourceRoomIdentity == 0U
            && source.targetRoomIdentity == 0U;
        if (!hasExplicitIdentity && !isAnonymousFixture) {
            return NavigationOutdoorBoundaryMatchResult::Invalid;
        }
    }

    bool hasCandidate{};
    bool candidateIsAmbiguous{};
    NavigationBoundarySide candidateSide{NavigationBoundarySide::Left};
    std::int32_t candidateFixedSubtile{-1};
    std::int32_t candidateStart{};
    std::int32_t candidateEnd{};
    NavigationOutdoorOpening candidateOpening{};
    bool invalidCandidate{};
    bool collectionOverflow{};
    const auto makeOpening = [&sourceBounds](
            NavigationBoundarySide side,
            std::int32_t fixedCoordinate,
            std::int32_t start,
            std::int32_t end,
            NavigationOutdoorOpening& opening) noexcept -> bool {
        const auto midpoint = start + (end - start) / 2;
        const auto fixedSubtile = fixedCoordinate >= 0
            ? fixedCoordinate
            : side == NavigationBoundarySide::Left
                ? sourceBounds.left
                : side == NavigationBoundarySide::Right
                    ? sourceBounds.right
                    : side == NavigationBoundarySide::Top
                        ? sourceBounds.top
                        : sourceBounds.bottom;
        switch (side) {
            case NavigationBoundarySide::Left:
                if (fixedSubtile < sourceBounds.left
                    || fixedSubtile >= sourceBounds.right
                    || start < sourceBounds.top
                    || end > sourceBounds.bottom) {
                    return false;
                }
                opening = {
                    .subtileX = fixedSubtile,
                    .subtileY = midpoint,
                    .spanSubtiles = end - start,
                };
                return true;
            case NavigationBoundarySide::Right:
                if (fixedSubtile <= sourceBounds.left
                    || fixedSubtile > sourceBounds.right
                    || start < sourceBounds.top
                    || end > sourceBounds.bottom) {
                    return false;
                }
                opening = {
                    .subtileX = fixedSubtile - 1,
                    .subtileY = midpoint,
                    .spanSubtiles = end - start,
                };
                return true;
            case NavigationBoundarySide::Top:
                if (fixedSubtile < sourceBounds.top
                    || fixedSubtile >= sourceBounds.bottom
                    || start < sourceBounds.left
                    || end > sourceBounds.right) {
                    return false;
                }
                opening = {
                    .subtileX = midpoint,
                    .subtileY = fixedSubtile,
                    .spanSubtiles = end - start,
                };
                return true;
            case NavigationBoundarySide::Bottom:
                if (fixedSubtile <= sourceBounds.top
                    || fixedSubtile > sourceBounds.bottom
                    || start < sourceBounds.left
                    || end > sourceBounds.right) {
                    return false;
                }
                opening = {
                    .subtileX = midpoint,
                    .subtileY = fixedSubtile - 1,
                    .spanSubtiles = end - start,
                };
                return true;
        }
        return false;
    };
    const auto accept = [&hasCandidate,
            &candidateSide,
            &candidateFixedSubtile,
            &candidateStart,
            &candidateEnd,
            &candidateOpening,
            &candidateIsAmbiguous,
            &invalidCandidate,
            &collectionOverflow,
            &makeOpening,
            collectedOpenings,
            collectedOpeningCount,
            selectionPolicy](const NavigationBoundarySpan& span,
            std::int32_t start,
            std::int32_t end) noexcept {
        const auto spanLength = end - start;
        if (spanLength < 3) return;
        NavigationOutdoorOpening opening{};
        if (!makeOpening(
                span.side,
                span.fixedSubtile,
                start,
                end,
                opening)) {
            invalidCandidate = true;
            return;
        }
        if (span.fixedSubtile >= 0) {
            opening.boundaryIdentity = {
                .axis = span.side == NavigationBoundarySide::Left
                        || span.side == NavigationBoundarySide::Right
                    ? NavigationBoundaryAxis::Vertical
                    : NavigationBoundaryAxis::Horizontal,
                .fixedSubtile = span.fixedSubtile,
                .startSubtile = start,
                .endSubtile = end,
            };
        }
        if (collectedOpeningCount != nullptr) {
            auto duplicate = false;
            for (std::size_t index = 0U;
                    index < *collectedOpeningCount;
                    ++index) {
                const auto& existing = collectedOpenings[index];
                if (existing.subtileX == opening.subtileX
                    && existing.subtileY == opening.subtileY
                    && existing.spanSubtiles == opening.spanSubtiles) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                if (*collectedOpeningCount >= collectedOpenings.size()) {
                    collectionOverflow = true;
                    return;
                }
                collectedOpenings[(*collectedOpeningCount)++] = opening;
            }
        }
        if (!hasCandidate) {
            hasCandidate = true;
            candidateSide = span.side;
            candidateFixedSubtile = span.fixedSubtile;
            candidateStart = start;
            candidateEnd = end;
            candidateOpening = opening;
            candidateIsAmbiguous = false;
            return;
        }

        // Exact RoomsNear identities already constrained the intersection.
        // Multiple identities may still describe the same physical run, which
        // must not create a false ambiguity.
        if (candidateSide == span.side
            && candidateFixedSubtile == span.fixedSubtile
            && candidateStart == start
            && candidateEnd == end) {
            return;
        }

        if (selectionPolicy
                == NavigationOutdoorOpeningSelectionPolicy::RequireUnique) {
            candidateIsAmbiguous = true;
            return;
        }

        candidateIsAmbiguous = false;
    };

    const auto identityLess = [](const NavigationBoundarySpan& left,
            const NavigationBoundarySpan& right) noexcept {
        if (left.side != right.side) {
            return static_cast<std::uint8_t>(left.side)
                < static_cast<std::uint8_t>(right.side);
        }
        if (left.fixedSubtile != right.fixedSubtile) {
            return left.fixedSubtile < right.fixedSubtile;
        }
        if (left.sourceRoomIdentity != right.sourceRoomIdentity) {
            return left.sourceRoomIdentity < right.sourceRoomIdentity;
        }
        return left.targetRoomIdentity < right.targetRoomIdentity;
    };
    const auto sameIdentity = [&identityLess](
            const NavigationBoundarySpan& left,
            const NavigationBoundarySpan& right) noexcept {
        return !identityLess(left, right) && !identityLess(right, left);
    };

    // Both inputs are produced by MergeOutdoorBoundarySpans. Their composite
    // identity order permits a linear intersection at the governed ceiling.
    std::size_t sourceIndex{};
    std::size_t targetIndex{};
    while (sourceIndex < sourceSpans.size()
        && targetIndex < targetSpans.size()) {
        const auto& source = sourceSpans[sourceIndex];
        const auto& target = targetSpans[targetIndex];
        const auto targetHasExplicitIdentity = target.fixedSubtile >= 0
            && target.sourceRoomIdentity != 0U
            && target.targetRoomIdentity != 0U;
        const auto targetIsAnonymousFixture = target.fixedSubtile < 0
            && target.sourceRoomIdentity == 0U
            && target.targetRoomIdentity == 0U;
        if (source.targetLevelId != sourceLevelTag
            || target.targetLevelId != targetLevelId
            || source.startSubtile < 0
            || source.endSubtile <= source.startSubtile
            || target.startSubtile < 0
            || target.endSubtile <= target.startSubtile
            || (!targetHasExplicitIdentity && !targetIsAnonymousFixture)) {
            return NavigationOutdoorBoundaryMatchResult::Invalid;
        }
        if (identityLess(source, target)) {
            ++sourceIndex;
            continue;
        }
        if (identityLess(target, source)) {
            ++targetIndex;
            continue;
        }
        if (!sameIdentity(source, target)) {
            return NavigationOutdoorBoundaryMatchResult::Invalid;
        }
        const auto start = source.startSubtile > target.startSubtile
            ? source.startSubtile : target.startSubtile;
        const auto end = source.endSubtile < target.endSubtile
            ? source.endSubtile : target.endSubtile;
        if (end > start) accept(source, start, end);
        if (source.endSubtile <= target.endSubtile) ++sourceIndex;
        if (target.endSubtile <= source.endSubtile) ++targetIndex;
    }
    if (invalidCandidate || collectionOverflow) {
        return NavigationOutdoorBoundaryMatchResult::Invalid;
    }
    if (!hasCandidate) {
        return NavigationOutdoorBoundaryMatchResult::NotFound;
    }
    if (candidateIsAmbiguous) {
        return NavigationOutdoorBoundaryMatchResult::Ambiguous;
    }

    output = candidateOpening;
    return NavigationOutdoorBoundaryMatchResult::Found;
}

[[nodiscard]] constexpr auto StrongerExitSelection(
        const NavigationExitSelection& incoming,
        const NavigationExitSelection& existing) noexcept -> bool {
    if (incoming.canonicalLevelPairAnchor
            != existing.canonicalLevelPairAnchor) {
        return incoming.canonicalLevelPairAnchor;
    }
    const auto incomingEvidence = static_cast<std::uint8_t>(
        incoming.evidence);
    const auto existingEvidence = static_cast<std::uint8_t>(
        existing.evidence);
    const auto& candidate = incoming.candidate;
    if (incomingEvidence != existingEvidence) {
        return incomingEvidence > existingEvidence;
    }
    if (incoming.spanSubtiles != existing.spanSubtiles) {
        return incoming.spanSubtiles > existing.spanSubtiles;
    }
    const auto& previous = existing.candidate;
    if (candidate.subtileX != previous.subtileX) {
        return candidate.subtileX < previous.subtileX;
    }
    if (candidate.subtileY != previous.subtileY) {
        return candidate.subtileY < previous.subtileY;
    }
    if (candidate.useExactClientCoordinates
            != previous.useExactClientCoordinates) {
        return candidate.useExactClientCoordinates;
    }
    if (candidate.exactClientX != previous.exactClientX) {
        return candidate.exactClientX < previous.exactClientX;
    }
    if (candidate.exactClientY != previous.exactClientY) {
        return candidate.exactClientY < previous.exactClientY;
    }
    return candidate.destinationId < previous.destinationId;
}

// One navigation destination is published per target level. An exact active
// runtime object wins over generated boss/quest presets, which win over
// RoomTile and outdoor collision evidence; equal evidence uses a stable
// tie-breaker.
[[nodiscard]] inline auto UpsertExitSelection(
        std::int32_t currentLevelId,
        std::span<NavigationExitSelection> selections,
        std::size_t& count,
        NavigationExitSelection incoming) noexcept -> bool {
    const auto& candidate = incoming.candidate;
    if (currentLevelId <= 0
        || candidate.targetLevelId <= 0
        || candidate.targetLevelId == currentLevelId
        || candidate.subtileX < 0 || candidate.subtileY < 0) {
        return false;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        auto& existing = selections[index];
        if (existing.candidate.targetLevelId != candidate.targetLevelId) {
            continue;
        }
        if (StrongerExitSelection(incoming, existing)) existing = incoming;
        return true;
    }
    if (count >= selections.size()) return false;
    selections[count++] = incoming;
    return true;
}

inline constexpr std::int32_t ExitLabelEvidenceClusterSubtiles = 10;

struct NavigationPhysicalExitSelection final {
    NavigationExitSelection winner{};
    std::int32_t anchorSubtileX{};
    std::int32_t anchorSubtileY{};
    NavigationExitBoundaryIdentity boundaryIdentity{};
};

// Preserve raw physical evidence until enumeration is complete. Exact duplicate
// coordinates are reduced immediately, but spatial clustering is deliberately
// deferred so native traversal order cannot affect the result.
[[nodiscard]] inline auto AppendPhysicalExitEvidence(
        std::int32_t currentLevelId,
        std::span<NavigationExitSelection> evidence,
        std::size_t& count,
        NavigationExitSelection incoming) noexcept -> bool {
    const auto& candidate = incoming.candidate;
    if (currentLevelId <= 0
        || candidate.targetLevelId <= 0
        || candidate.targetLevelId == currentLevelId
        || candidate.subtileX < 0 || candidate.subtileY < 0) {
        return false;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        auto& existing = evidence[index];
        if (existing.candidate.targetLevelId != candidate.targetLevelId
            || existing.candidate.subtileX != candidate.subtileX
            || existing.candidate.subtileY != candidate.subtileY) {
            continue;
        }
        if (existing.boundaryIdentity.Valid()
                && incoming.boundaryIdentity.Valid()
                && existing.boundaryIdentity != incoming.boundaryIdentity) {
            continue;
        }
        // Coordinate de-duplication can combine a strong RoomTile/runtime
        // witness with the outdoor collision witness that owns the stable seam
        // identity. Select the stronger display point without throwing away
        // that structural key; otherwise the later reciprocal reduction falls
        // back to a fragile distance cluster.
        const auto retainedBoundaryIdentity =
            existing.boundaryIdentity.Valid()
                ? existing.boundaryIdentity
                : incoming.boundaryIdentity;
        if (StrongerExitSelection(incoming, existing)) existing = incoming;
        if (!existing.boundaryIdentity.Valid()
                && retainedBoundaryIdentity.Valid()) {
            existing.boundaryIdentity = retainedBoundaryIdentity;
        }
        return true;
    }
    if (count >= evidence.size()) return false;
    evidence[count++] = incoming;
    return true;
}

// Labels retain distinct physical exits even when several lead to the same
// target. A proven boundary identity is authoritative regardless of projected
// distance; the legacy fixed spatial anchor is used only when either piece of
// evidence lacks that identity. A stronger winner never moves the anchor or
// erases a proven seam, keeping the reduction deterministic and preventing
// transitive A-near-B-near-C chains from absorbing A and C.
[[nodiscard]] inline auto ReducePhysicalExitEvidence(
        std::span<NavigationExitSelection> evidence,
        std::size_t evidenceCount,
        std::span<NavigationPhysicalExitSelection> selections,
        std::size_t& selectionCount) noexcept -> bool {
    selectionCount = 0U;
    if (evidenceCount > evidence.size()) return false;
    std::sort(
        evidence.begin(),
        evidence.begin() + static_cast<std::ptrdiff_t>(evidenceCount),
        [](const NavigationExitSelection& left,
                const NavigationExitSelection& right) noexcept {
            const auto& a = left.candidate;
            const auto& b = right.candidate;
            if (a.targetLevelId != b.targetLevelId) {
                return a.targetLevelId < b.targetLevelId;
            }
            if (left.canonicalLevelPairAnchor
                    != right.canonicalLevelPairAnchor) {
                return left.canonicalLevelPairAnchor;
            }
            const auto leftIdentity = left.boundaryIdentity.Valid();
            const auto rightIdentity = right.boundaryIdentity.Valid();
            if (leftIdentity != rightIdentity) return leftIdentity;
            if (leftIdentity) {
                if (left.boundaryIdentity.axis
                        != right.boundaryIdentity.axis) {
                    return static_cast<std::uint8_t>(
                        left.boundaryIdentity.axis)
                        < static_cast<std::uint8_t>(
                            right.boundaryIdentity.axis);
                }
                if (left.boundaryIdentity.fixedSubtile
                        != right.boundaryIdentity.fixedSubtile) {
                    return left.boundaryIdentity.fixedSubtile
                        < right.boundaryIdentity.fixedSubtile;
                }
                if (left.boundaryIdentity.startSubtile
                        != right.boundaryIdentity.startSubtile) {
                    return left.boundaryIdentity.startSubtile
                        < right.boundaryIdentity.startSubtile;
                }
                if (left.boundaryIdentity.endSubtile
                        != right.boundaryIdentity.endSubtile) {
                    return left.boundaryIdentity.endSubtile
                        < right.boundaryIdentity.endSubtile;
                }
            }
            if (a.subtileX != b.subtileX) return a.subtileX < b.subtileX;
            if (a.subtileY != b.subtileY) return a.subtileY < b.subtileY;
            if (left.evidence != right.evidence) {
                return static_cast<std::uint8_t>(left.evidence)
                    > static_cast<std::uint8_t>(right.evidence);
            }
            if (left.spanSubtiles != right.spanSubtiles) {
                return left.spanSubtiles > right.spanSubtiles;
            }
            return a.destinationId < b.destinationId;
        });
    for (std::size_t evidenceIndex = 0U;
            evidenceIndex < evidenceCount;
            ++evidenceIndex) {
        const auto& incoming = evidence[evidenceIndex];
        const auto& candidate = incoming.candidate;
        NavigationPhysicalExitSelection* cluster{};
        for (std::size_t selectionIndex = 0U;
                selectionIndex < selectionCount;
                ++selectionIndex) {
            auto& existing = selections[selectionIndex];
            if (existing.winner.candidate.targetLevelId
                    != candidate.targetLevelId) {
                continue;
            }
            const auto incomingIdentity = incoming.boundaryIdentity.Valid();
            const auto existingIdentity = existing.boundaryIdentity.Valid();
            if (incomingIdentity && existingIdentity) {
                if (incoming.boundaryIdentity
                        != existing.boundaryIdentity) {
                    continue;
                }
                cluster = &existing;
                break;
            }
            const auto deltaX = static_cast<std::int64_t>(candidate.subtileX)
                - static_cast<std::int64_t>(existing.anchorSubtileX);
            const auto deltaY = static_cast<std::int64_t>(candidate.subtileY)
                - static_cast<std::int64_t>(existing.anchorSubtileY);
            if (deltaX < -ExitLabelEvidenceClusterSubtiles
                || deltaX > ExitLabelEvidenceClusterSubtiles
                || deltaY < -ExitLabelEvidenceClusterSubtiles
                || deltaY > ExitLabelEvidenceClusterSubtiles) {
                continue;
            }
            cluster = &existing;
            break;
        }
        if (cluster != nullptr) {
            if (!cluster->boundaryIdentity.Valid()
                    && incoming.boundaryIdentity.Valid()) {
                cluster->boundaryIdentity = incoming.boundaryIdentity;
            }
            if (StrongerExitSelection(incoming, cluster->winner)) {
                cluster->winner = incoming;
            }
            continue;
        }
        if (selectionCount >= selections.size()) return false;
        selections[selectionCount++] = {
            .winner = incoming,
            .anchorSubtileX = candidate.subtileX,
            .anchorSubtileY = candidate.subtileY,
            .boundaryIdentity = incoming.boundaryIdentity,
        };
    }
    return true;
}

} // namespace Detail

enum class NavigationRefreshResult : std::uint8_t {
    Complete,
    PartialRetryable,
    Failed,
};

struct NavigationResolverCounters final {
    std::uint64_t refreshes{};
    std::uint64_t rooms{};
    std::uint64_t presets{};
    std::uint64_t exits{};
    std::uint64_t waypoints{};
    std::uint64_t published{};
    std::uint64_t unresolvedNames{};
    std::uint64_t failures{};
    std::uint64_t traversalLimits{};
    std::uint64_t partialRefreshes{};
    std::uint64_t visibilitySlots{};
    std::uint64_t visibilityPairs{};
    std::uint64_t pendingVisibilityTargets{};
    std::int32_t lastLevelId{UnknownNavigationLevelId};
    std::uint32_t lastDestinationCount{};
    std::int32_t lastWaypointX{};
    std::int32_t lastWaypointY{};
    std::int32_t lastProgressionX{};
    std::int32_t lastProgressionY{};
};

[[nodiscard]] auto InitializeNavigationResolver(
    const D2RL::PluginContext* context,
    bool diagnostics) noexcept -> bool;
[[nodiscard]] auto BindNavigationResolverCatalog(
    std::shared_ptr<const MapSenseDataCatalog> catalog) noexcept -> bool;
void ShutdownNavigationResolver() noexcept;

// Runs only from D2RLoader's gameplay/UI thread. UnknownNavigationLevelId lets
// the resolver trust the local player's proven current Level; a concrete id is
// cross-checked against it before any destination is published.
[[nodiscard]] auto RefreshNavigationDestinations(
    std::uint64_t sessionGeneration,
    std::int32_t expectedLevelId,
    std::span<const CustomLevelTarget> customTargets) noexcept
    -> NavigationRefreshResult;

// Called synchronously from MapSense's already-owned DRLG_InitLevel hook,
// immediately after D2R has materialized a generated level. The callback keeps
// every native pointer inside that hook window and publishes copied POI data.
[[nodiscard]] auto ObserveInitializedClientLevelPoiDefinitions(
    std::uint64_t sessionGeneration,
    std::uint8_t dataContext,
    void* level) noexcept -> bool;

// Initializes one seed-valid LevelId and copies its dynamic Vis[8] targets.
// The output contains stable ids only and is used by passive distant-name
// discovery; no borrowed Level pointer escapes the call.
[[nodiscard]] auto DiscoverProgressiveRevealLevelTargets(
    const ClientLevelView& current,
    std::int32_t levelId,
    std::array<std::int32_t, ProgressiveRevealVisCapacity>& targets,
    std::size_t& targetCount) noexcept -> bool;

// Captures lightweight generated exit and waypoint definitions for one level.
// It never creates an ActiveRoom or traverses gameplay units.
[[nodiscard]] auto CaptureProgressiveRevealLevelPoiDefinitions(
    std::uint64_t sessionGeneration,
    const ClientLevelView& current,
    std::int32_t levelId) noexcept -> bool;

// Enumerates every generated Level currently linked into the active client
// DRLG. It publishes only static POI definitions; navigation lines remain
// scoped to the player's current level.
[[nodiscard]] auto RefreshRevealedActPoiDefinitions(
    std::uint64_t sessionGeneration) noexcept -> NavigationRefreshResult;

[[nodiscard]] auto IsNavigationResolverActive() noexcept -> bool;
[[nodiscard]] auto GetNavigationResolverCounters() noexcept
    -> NavigationResolverCounters;

} // namespace RuffnecKk::MapSense
