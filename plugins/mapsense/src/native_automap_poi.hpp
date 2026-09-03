#pragma once

#include "navigation_engine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

class MapSenseDataCatalog;

inline constexpr std::size_t MaximumAutomapExitLabels = 256U;
inline constexpr std::size_t MaximumAutomapWaypointLabels = 512U;
inline constexpr std::size_t MaximumAutomapLevelLabels = 512U;
inline constexpr std::size_t MaximumAutomapSpecialChestPresets = 1'024U;
inline constexpr std::size_t MaximumTrackedAutomapObjects = 4'096U;
inline constexpr std::size_t MaximumAutomapPoiSnapshots =
    MaximumAutomapExitLabels
    + MaximumAutomapWaypointLabels
    + MaximumAutomapLevelLabels
    + MaximumAutomapSpecialChestPresets
    + MaximumTrackedAutomapObjects * 2U;

enum class AutomapPoiKind : std::uint8_t {
    ExitLabel,
    WaypointLabel,
    LevelLabel,
    ShrineIcon,
    ShrineLabel,
    Chest,
    SuperChest,
    ArmorRack,
    WeaponRack,
};

enum class AutomapPoiCollection : std::uint32_t {
    None = 0U,
    ExitLabels = 1U << 0U,
    ShrineLabels = 1U << 1U,
    Chests = 1U << 2U,
    SuperChests = 1U << 3U,
    ArmorRacks = 1U << 4U,
    WeaponRacks = 1U << 5U,
    WaypointLabels = 1U << 6U,
};

[[nodiscard]] constexpr auto AutomapPoiCollectionBit(
        AutomapPoiCollection collection) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(collection);
}

inline constexpr std::uint8_t AutomapPoiStateLocked = 1U << 0U;
inline constexpr std::uint8_t AutomapPoiStateTrapped = 1U << 1U;

// Native POI projections identify the sprite origin, not the upper edge of
// D2R's automap cell. Reserve the complete visible cell before placing text;
// this keeps the label's bottom edge above the icon at every overlay scale.
inline constexpr float NativeExitIconTopExtent = 26.0F;
inline constexpr float NativeWaypointIconTopExtent = 24.0F;
inline constexpr float NativeShrineIconTopExtent = 44.0F;
inline constexpr float NativeAutomapLabelGap = 12.0F;
inline constexpr float NativeWaypointLabelGap = 2.0F;
inline constexpr float NativeShrineLabelGap = 18.0F;
inline constexpr std::int32_t NativeShrineLabelProximitySubtiles = 56;

struct AutomapLabelRectangle final {
    float left{};
    float top{};
    float right{};
    float bottom{};
};

// The same destination can be discovered from more than one generated Level.
// Keep every physical definition in the retained catalog, then suppress only
// same-name rectangles that actually collide in the current automap view.
// This preserves distinct exits when zoom/pan separates them and prevents the
// stacked duplicate glyphs seen on remote outdoor transitions.
[[nodiscard]] constexpr auto AutomapLabelRectanglesOverlap(
        const AutomapLabelRectangle& left,
        const AutomapLabelRectangle& right,
        float padding = 0.0F) noexcept -> bool {
    return left.left < right.right + padding
        && left.right + padding > right.left
        && left.top < right.bottom + padding
        && left.bottom + padding > right.top;
}

[[nodiscard]] constexpr auto AutomapLabelTopAboveIcon(
        float iconCenterY,
        float textHeight,
        float iconTopExtent,
        float gap) noexcept -> float {
    return iconCenterY - iconTopExtent - gap - textHeight;
}

[[nodiscard]] constexpr auto AutomapPoiWithinSubtileDistance(
        std::int32_t playerClientX,
        std::int32_t playerClientY,
        std::int32_t objectClientX,
        std::int32_t objectClientY,
        std::int32_t maximumSubtiles) noexcept -> bool {
    if (maximumSubtiles < 0) return false;
    // D2R client coordinates are the isometric transform of subtile X/Y.
    // Compare the inverse-transform numerators directly to avoid rounding at
    // the proximity boundary and to keep every intermediate 64-bit safe.
    const auto playerX = static_cast<std::int64_t>(playerClientX)
        + std::int64_t{2} * playerClientY;
    const auto playerY = std::int64_t{2} * playerClientY - playerClientX;
    const auto objectX = static_cast<std::int64_t>(objectClientX)
        + std::int64_t{2} * objectClientY;
    const auto objectY = std::int64_t{2} * objectClientY - objectClientX;
    const auto distance = [](std::int64_t left, std::int64_t right) noexcept {
        return left >= right
            ? static_cast<std::uint64_t>(left - right)
            : static_cast<std::uint64_t>(right - left);
    };
    const auto limit = static_cast<std::uint64_t>(maximumSubtiles) * 32U;
    return distance(playerX, objectX) <= limit
        && distance(playerY, objectY) <= limit;
}

[[nodiscard]] constexpr auto IsLockedChestInteractType(
        std::uint32_t interactType) noexcept -> bool {
    return (interactType & 0x80U) != 0U;
}

[[nodiscard]] constexpr auto IsTrappedChestInteractType(
        std::uint32_t interactType) noexcept -> bool {
    const auto trapType = interactType & 0x7FU;
    return trapType >= 1U && trapType <= 9U;
}

[[nodiscard]] constexpr auto IsSparklyChestRuntimeFlags(
        std::uint32_t runtimeFlagsC8) noexcept -> bool {
    return (runtimeFlagsC8 & 0x01U) != 0U;
}

// Immutable exit location produced by the gameplay-thread resolver. The
// destination level id remains available after navigation policy selection so
// every physical exit can receive its own localized label.
struct AutomapExitLabelDefinition final {
    std::uint64_t stableId{};
    // The generated level whose doorway produced this label. Per-level
    // refreshes replace this owner's complete batch atomically; they must
    // never append a second, slightly shifted copy over a reveal-wide catalog.
    std::int32_t sourceLevelId{UnknownNavigationLevelId};
    std::int32_t targetLevelId{UnknownNavigationLevelId};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t exactClientX{};
    std::int32_t exactClientY{};
    bool useExactClientCoordinates{};
    NavigationExitBoundaryIdentity boundaryIdentity{};
    bool canonicalLevelPairAnchor{};
};

// Native evidence can refine one directed transition without owning every
// other transition emitted for the same source Level. This pair-level rule is
// especially important for permanent portals: a complete native room pass
// may prove the ordinary exits while the external DRLG atlas remains the only
// seed-static proof of the portal destination.
[[nodiscard]] constexpr auto NativeAutomapExitOverridesExternal(
        const AutomapExitLabelDefinition& external,
        const AutomapExitLabelDefinition& native) noexcept -> bool {
    return external.sourceLevelId == native.sourceLevelId
        && external.targetLevelId == native.targetLevelId;
}

[[nodiscard]] constexpr auto ShouldRetainExternalAutomapExit(
        const AutomapExitLabelDefinition& external,
        std::span<const AutomapExitLabelDefinition> native,
        bool nativeOwnerComplete) noexcept -> bool {
    if (!nativeOwnerComplete) return true;
    for (const auto& definition : native) {
        if (NativeAutomapExitOverridesExternal(external, definition)) {
            return false;
        }
    }
    return true;
}

// One exact generated waypoint preset owned by its materialized level. Unlike
// live object units, this immutable definition remains available while the
// player pans anywhere across the revealed native automap.
struct AutomapWaypointLabelDefinition final {
    std::uint64_t stableId{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
};

// One allocation-free fallback label per generated Level. Its anchor comes
// only from the already-linked DrlgRoom rectangles; it never requires an
// ActiveRoom, collision grid, preset chain, unit table, or monster data.
struct AutomapLevelLabelDefinition final {
    std::uint64_t stableId{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
};

[[nodiscard]] constexpr auto ShouldProjectAutomapLevelLabel(
        std::int32_t levelId,
        std::int32_t currentLevelId,
        bool hasExactWaypoint,
        bool hasExactCurrentExit) noexcept -> bool {
    return levelId > 0 && levelId != currentLevelId
        && !hasExactWaypoint && !hasExactCurrentExit;
}

namespace Detail {

// Fixed-capacity owner-replacement store used behind the native POI lock.
// It builds into scratch first, so overflow leaves the published catalog
// byte-for-byte unchanged.
template <std::size_t Capacity>
class AutomapWaypointDefinitionCatalog final {
public:
    [[nodiscard]] constexpr auto ReplaceOwner(
            std::int32_t levelId,
            std::span<const AutomapWaypointLabelDefinition> incoming) noexcept
            -> bool {
        std::size_t replacementCount{};
        for (std::size_t index = 0U; index < count_; ++index) {
            if (definitions_[index].levelId == levelId) continue;
            if (replacementCount >= scratch_.size()) return false;
            scratch_[replacementCount++] = definitions_[index];
        }
        for (const auto& definition : incoming) {
            if (replacementCount >= scratch_.size()) return false;
            scratch_[replacementCount++] = definition;
        }
        for (std::size_t index = 0U; index < replacementCount; ++index) {
            definitions_[index] = scratch_[index];
        }
        count_ = replacementCount;
        return true;
    }

    [[nodiscard]] constexpr auto ReplaceAll(
            std::span<const AutomapWaypointLabelDefinition> incoming) noexcept
            -> bool {
        if (incoming.size() > definitions_.size()) return false;
        for (std::size_t index = 0U; index < incoming.size(); ++index) {
            scratch_[index] = incoming[index];
        }
        for (std::size_t index = 0U; index < incoming.size(); ++index) {
            definitions_[index] = scratch_[index];
        }
        count_ = incoming.size();
        return true;
    }

    constexpr void Clear() noexcept {
        count_ = 0U;
    }

    [[nodiscard]] constexpr auto Definitions() const noexcept
            -> std::span<const AutomapWaypointLabelDefinition> {
        return {definitions_.data(), count_};
    }

private:
    std::array<AutomapWaypointLabelDefinition, Capacity> definitions_{};
    std::array<AutomapWaypointLabelDefinition, Capacity> scratch_{};
    std::size_t count_{};
};

template <std::size_t Capacity>
class AutomapLevelDefinitionCatalog final {
public:
    [[nodiscard]] constexpr auto ReplaceOwner(
            std::int32_t levelId,
            std::span<const AutomapLevelLabelDefinition> incoming) noexcept
            -> bool {
        std::size_t replacementCount{};
        for (std::size_t index = 0U; index < count_; ++index) {
            if (definitions_[index].levelId == levelId) continue;
            if (replacementCount >= scratch_.size()) return false;
            scratch_[replacementCount++] = definitions_[index];
        }
        for (const auto& definition : incoming) {
            if (replacementCount >= scratch_.size()) return false;
            scratch_[replacementCount++] = definition;
        }
        for (std::size_t index = 0U; index < replacementCount; ++index) {
            definitions_[index] = scratch_[index];
        }
        count_ = replacementCount;
        return true;
    }

    [[nodiscard]] constexpr auto ReplaceAll(
            std::span<const AutomapLevelLabelDefinition> incoming) noexcept
            -> bool {
        if (incoming.size() > definitions_.size()) return false;
        for (std::size_t index = 0U; index < incoming.size(); ++index) {
            scratch_[index] = incoming[index];
        }
        for (std::size_t index = 0U; index < incoming.size(); ++index) {
            definitions_[index] = scratch_[index];
        }
        count_ = incoming.size();
        return true;
    }

    constexpr void Clear() noexcept {
        count_ = 0U;
    }

    [[nodiscard]] constexpr auto Definitions() const noexcept
            -> std::span<const AutomapLevelLabelDefinition> {
        return {definitions_.data(), count_};
    }

private:
    std::array<AutomapLevelLabelDefinition, Capacity> definitions_{};
    std::array<AutomapLevelLabelDefinition, Capacity> scratch_{};
    std::size_t count_{};
};

// The externally generated atlas is the immutable baseline for one
// session/seed/difficulty/act. Native DRLG observations are a second layer:
// a positive definition overrides the same owner, while an empty observation
// is deliberately a no-op and can never erase the baseline. Keeping this
// ownership rule in a small allocation-free catalog makes the regression
// contract independently testable from the renderer and gameplay hooks.
template <typename Definition, typename Catalog, std::size_t Capacity>
class LayeredAutomapOwnerDefinitionCatalog final {
public:
    [[nodiscard]] constexpr auto ReplaceExternal(
            std::span<const Definition> incoming) noexcept -> bool {
        if (!UniqueOwners(incoming)) return false;
        std::size_t effectiveCount = incoming.size();
        for (const auto& definition : native_.Definitions()) {
            if (!HasOwner(incoming, definition.levelId)) ++effectiveCount;
        }
        if (effectiveCount > Capacity) return false;
        if (!external_.ReplaceAll(incoming)) return false;
        RebuildEffective();
        return true;
    }

    [[nodiscard]] constexpr auto PublishNativeOwner(
            std::int32_t levelId,
            std::span<const Definition> incoming) noexcept -> bool {
        // A negative or not-yet-ready native observation is not evidence that
        // an externally generated owner does not exist.
        if (incoming.empty()) return true;
        if (incoming.size() != 1U || incoming.front().levelId != levelId) {
            return false;
        }
        const auto nativeHasOwner = HasOwner(native_.Definitions(), levelId);
        const auto externalHasOwner = HasOwner(
            external_.Definitions(),
            levelId);
        auto effectiveCount = effectiveCount_;
        if (!nativeHasOwner && !externalHasOwner) ++effectiveCount;
        if (effectiveCount > Capacity) return false;
        if (!native_.ReplaceOwner(levelId, incoming)) return false;
        RebuildEffective();
        return true;
    }

    constexpr void Clear() noexcept {
        external_.Clear();
        native_.Clear();
        effectiveCount_ = 0U;
    }

    [[nodiscard]] constexpr auto Definitions() const noexcept
            -> std::span<const Definition> {
        return {effective_.data(), effectiveCount_};
    }

    [[nodiscard]] constexpr auto ExternalDefinitions() const noexcept
            -> std::span<const Definition> {
        return external_.Definitions();
    }

    [[nodiscard]] constexpr auto NativeDefinitions() const noexcept
            -> std::span<const Definition> {
        return native_.Definitions();
    }

private:
    [[nodiscard]] static constexpr auto HasOwner(
            std::span<const Definition> definitions,
            std::int32_t levelId) noexcept -> bool {
        for (const auto& definition : definitions) {
            if (definition.levelId == levelId) return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr auto UniqueOwners(
            std::span<const Definition> definitions) noexcept -> bool {
        for (std::size_t index = 0U; index < definitions.size(); ++index) {
            for (std::size_t earlier = 0U; earlier < index; ++earlier) {
                if (definitions[earlier].levelId
                        == definitions[index].levelId) {
                    return false;
                }
            }
        }
        return true;
    }

    constexpr void RebuildEffective() noexcept {
        effectiveCount_ = 0U;
        for (const auto& definition : external_.Definitions()) {
            if (HasOwner(native_.Definitions(), definition.levelId)) continue;
            effective_[effectiveCount_++] = definition;
        }
        for (const auto& definition : native_.Definitions()) {
            effective_[effectiveCount_++] = definition;
        }
    }

    Catalog external_{};
    Catalog native_{};
    std::array<Definition, Capacity> effective_{};
    std::size_t effectiveCount_{};
};

template <std::size_t Capacity>
using LayeredAutomapWaypointDefinitionCatalog =
    LayeredAutomapOwnerDefinitionCatalog<
        AutomapWaypointLabelDefinition,
        AutomapWaypointDefinitionCatalog<Capacity>,
        Capacity>;

template <std::size_t Capacity>
using LayeredAutomapLevelDefinitionCatalog =
    LayeredAutomapOwnerDefinitionCatalog<
        AutomapLevelLabelDefinition,
        AutomapLevelDefinitionCatalog<Capacity>,
        Capacity>;

} // namespace Detail

[[nodiscard]] constexpr auto SameAutomapExitLevelPair(
        const AutomapExitLabelDefinition& left,
        const AutomapExitLabelDefinition& right) noexcept -> bool {
    return (left.sourceLevelId == right.sourceLevelId
            && left.targetLevelId == right.targetLevelId)
        || (left.sourceLevelId == right.targetLevelId
            && left.targetLevelId == right.sourceLevelId);
}

[[nodiscard]] constexpr auto SameAutomapExitPhysicalBoundary(
        const AutomapExitLabelDefinition& left,
        const AutomapExitLabelDefinition& right) noexcept -> bool {
    return SameAutomapExitLevelPair(left, right)
        && left.boundaryIdentity.Valid()
        && right.boundaryIdentity.Valid()
        && left.boundaryIdentity == right.boundaryIdentity;
}

// Partial InitLevel captures may revisit one source owner before all of its
// rooms exist. Merge only definitions with a stable physical identity. A
// canonical level-pair anchor is authoritative for generated facades such as
// Tamoe Highland <-> Monastery Gate; ordinary outdoor pairs retain separate
// proven seams even when their projected labels happen to be close together.
[[nodiscard]] constexpr auto SameAutomapExitOwnerFragment(
        const AutomapExitLabelDefinition& left,
        const AutomapExitLabelDefinition& right) noexcept -> bool {
    if (left.sourceLevelId != right.sourceLevelId
        || left.targetLevelId != right.targetLevelId) {
        return false;
    }
    if (left.canonicalLevelPairAnchor
        || right.canonicalLevelPairAnchor) {
        return true;
    }
    if (left.stableId != 0U && left.stableId == right.stableId) return true;
    if (left.boundaryIdentity.Valid() && right.boundaryIdentity.Valid()) {
        return left.boundaryIdentity == right.boundaryIdentity;
    }
    if (left.subtileX == right.subtileX
        && left.subtileY == right.subtileY) {
        return true;
    }
    return left.useExactClientCoordinates
        && right.useExactClientCoordinates
        && left.exactClientX == right.exactClientX
        && left.exactClientY == right.exactClientY;
}

[[nodiscard]] constexpr auto PreferAutomapExitOwnerFragment(
        const AutomapExitLabelDefinition& incoming,
        const AutomapExitLabelDefinition& existing) noexcept -> bool {
    if (incoming.canonicalLevelPairAnchor
            != existing.canonicalLevelPairAnchor) {
        return incoming.canonicalLevelPairAnchor;
    }
    const auto incomingIdentity = incoming.boundaryIdentity.Valid();
    const auto existingIdentity = existing.boundaryIdentity.Valid();
    if (incomingIdentity != existingIdentity) return incomingIdentity;
    if (incoming.useExactClientCoordinates
            != existing.useExactClientCoordinates) {
        return incoming.useExactClientCoordinates;
    }
    return false;
}

[[nodiscard]] constexpr auto IsAutomapExitPhysicalGroupMember(
        const AutomapExitLabelDefinition& anchor,
        const AutomapExitLabelDefinition& candidate,
        bool pairHasCanonicalAnchor) noexcept -> bool {
    if (!SameAutomapExitLevelPair(anchor, candidate)) return false;
    if (pairHasCanonicalAnchor) return true;
    return SameAutomapExitPhysicalBoundary(anchor, candidate);
}

[[nodiscard]] constexpr auto HasCanonicalAutomapExitLevelPair(
        std::span<const AutomapExitLabelDefinition> definitions,
        const AutomapExitLabelDefinition& anchor) noexcept -> bool {
    for (const auto& candidate : definitions) {
        if (candidate.canonicalLevelPairAnchor
                && SameAutomapExitLevelPair(anchor, candidate)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr auto AutomapExitBoundaryIdentityLess(
        const NavigationExitBoundaryIdentity& left,
        const NavigationExitBoundaryIdentity& right) noexcept -> bool {
    if (left.axis != right.axis) {
        return static_cast<std::uint8_t>(left.axis)
            < static_cast<std::uint8_t>(right.axis);
    }
    if (left.fixedSubtile != right.fixedSubtile) {
        return left.fixedSubtile < right.fixedSubtile;
    }
    if (left.startSubtile != right.startSubtile) {
        return left.startSubtile < right.startSubtile;
    }
    return left.endSubtile < right.endSubtile;
}

// A definition without a structural seam can still be the reciprocal sample
// of a definition that has one. Resolve that mixed pair before grouping so the
// result does not depend on which source Level was captured first. When more
// than one proven seam is available, proximity selects exactly one; the stable
// seam ordering breaks ties and prevents an unproven sample from bridging two
// distinct valid identities.
[[nodiscard]] constexpr auto ResolvedAutomapExitBoundaryIdentity(
        std::span<const AutomapExitLabelDefinition> definitions,
        const AutomapExitLabelDefinition& definition) noexcept
        -> NavigationExitBoundaryIdentity {
    if (definition.boundaryIdentity.Valid()) {
        return definition.boundaryIdentity;
    }
    const auto delta = [](std::int32_t left, std::int32_t right) noexcept {
        return left >= right
            ? static_cast<std::uint64_t>(
                static_cast<std::int64_t>(left) - right)
            : static_cast<std::uint64_t>(
                static_cast<std::int64_t>(right) - left);
    };
    NavigationExitBoundaryIdentity winner{};
    auto winnerDistance = (std::numeric_limits<std::uint64_t>::max)();
    for (const auto& candidate : definitions) {
        if (!candidate.boundaryIdentity.Valid()
            || candidate.sourceLevelId != definition.targetLevelId
            || candidate.targetLevelId != definition.sourceLevelId) {
            continue;
        }
        const auto distance = delta(
            definition.subtileX,
            candidate.subtileX) + delta(
                definition.subtileY,
                candidate.subtileY);
        if (distance < winnerDistance
            || (distance == winnerDistance
                && (!winner.Valid()
                    || AutomapExitBoundaryIdentityLess(
                        candidate.boundaryIdentity,
                        winner)))) {
            winner = candidate.boundaryIdentity;
            winnerDistance = distance;
        }
    }
    return winner;
}

[[nodiscard]] constexpr auto IsResolvedAutomapExitPhysicalGroupMember(
        std::span<const AutomapExitLabelDefinition> definitions,
        const AutomapExitLabelDefinition& anchor,
        const AutomapExitLabelDefinition& candidate,
        bool pairHasCanonicalAnchor) noexcept -> bool {
    if (!SameAutomapExitLevelPair(anchor, candidate)) return false;
    if (pairHasCanonicalAnchor) return true;
    const auto anchorIdentity = ResolvedAutomapExitBoundaryIdentity(
        definitions,
        anchor);
    const auto candidateIdentity = ResolvedAutomapExitBoundaryIdentity(
        definitions,
        candidate);
    return anchorIdentity.Valid()
        && candidateIdentity.Valid()
        && anchorIdentity == candidateIdentity;
}

struct AutomapSpecialChestDefinition final {
    std::uint64_t stableId{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t classId{-1};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
};

// Select the name on the far side of one boundary relative to the player's
// current graph position. Equal/unreachable distances use the canonical
// Levels.txt ordinal as a deterministic progression tie-break.
[[nodiscard]] constexpr auto OrientedAutomapExitLevel(
        std::int32_t currentLevelId,
        std::int32_t sourceLevelId,
        std::int32_t targetLevelId,
        std::uint16_t sourceDistance,
        std::uint16_t targetDistance) noexcept -> std::int32_t {
    if (sourceDistance < targetDistance) return targetLevelId;
    if (targetDistance < sourceDistance) return sourceLevelId;
    const auto earlier = sourceLevelId < targetLevelId
        ? sourceLevelId : targetLevelId;
    const auto later = sourceLevelId < targetLevelId
        ? targetLevelId : sourceLevelId;
    return currentLevelId >= later ? earlier : later;
}

// Borrowed only for the duration of the native automap local-player pass.
// No pointer or callback is retained after ObserveNativeAutomapPoiPass returns.
struct NativeAutomapPoiPass final {
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
};

// Renderer-facing value snapshot. sourceId is a destination level id for an
// exit, the owning level id for a waypoint or level label, a shrines.txt row for a
// shrine, or an objects.txt class id otherwise.
struct NativeAutomapPoiSnapshot final {
    std::uint64_t stableId{};
    std::uint64_t sessionGeneration{};
    std::uint64_t revision{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    std::int32_t sourceId{};
    AutomapPoiKind kind{AutomapPoiKind::ExitLabel};
    std::uint8_t stateFlags{};
};

struct NativeAutomapPoiCounters final {
    std::uint64_t automapPulses{};
    std::uint64_t objectTableScans{};
    std::uint64_t objectBucketsVisited{};
    std::uint64_t objectUnitsObserved{};
    std::uint64_t objectUnitsClassified{};
    std::uint64_t objectTraversalLimits{};
    std::uint64_t exitDefinitionsPublished{};
    std::uint64_t specialChestDefinitionsPublished{};
    std::uint64_t projected{};
    std::uint64_t projectionRejected{};
    std::uint64_t contentionWaits{};
    std::uint64_t accessFaults{};
};

[[nodiscard]] auto InitializeNativeAutomapPoi(
    const D2RL::PluginContext* context,
    bool diagnostics) noexcept -> bool;
// Native fingerprints must be validated while D2R's original entry bytes are
// still present. The immutable localized catalog is intentionally attached
// later, after D2R has initialized its language tables.
[[nodiscard]] auto BindNativeAutomapPoiCatalog(
    std::shared_ptr<const MapSenseDataCatalog> catalog) noexcept -> bool;
void ShutdownNativeAutomapPoi() noexcept;

void ResetNativeAutomapPoiSession(
    std::uint64_t sessionGeneration) noexcept;
void ResetNativeAutomapPoiLevel(
    std::uint64_t sessionGeneration,
    std::int32_t levelId) noexcept;
void InvalidateNativeAutomapPoiFrame() noexcept;

void SetNativeAutomapPoiCollectionMask(std::uint32_t mask) noexcept;

[[nodiscard]] auto PublishNativeAutomapExitLabels(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    const AutomapExitLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto MergeNativeAutomapExitLabelFragments(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    const AutomapExitLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto ReplaceExternalAutomapExitLabels(
    std::uint64_t sessionGeneration,
    const AutomapExitLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto PublishNativeAutomapWaypointLabels(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    const AutomapWaypointLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto PublishNativeAutomapLevelLabels(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    const AutomapLevelLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto ReplaceExternalAutomapWaypointLabels(
    std::uint64_t sessionGeneration,
    const AutomapWaypointLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto ReplaceExternalAutomapLevelLabels(
    std::uint64_t sessionGeneration,
    const AutomapLevelLabelDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;
[[nodiscard]] auto PublishNativeAutomapSpecialChests(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    const AutomapSpecialChestDefinition* definitions,
    std::size_t definitionCount) noexcept -> bool;

// Called only from the governed AUTOMAP_RenderUnit hook, after D2R has handled
// the unit. Shrine text is admitted only while the corresponding native
// shrine unit is observed in that same renderer stream, so MapSense can never
// leave an orphan label after D2R stops drawing its icon.
void ObserveNativeAutomapRenderedUnit(void* unit) noexcept;

void ObserveNativeAutomapPoiPass(
    const NativeAutomapPoiPass& pass) noexcept;

[[nodiscard]] auto AcquireNativeAutomapPoiSnapshots(
    std::vector<NativeAutomapPoiSnapshot>& snapshots,
    bool retainCurrentProjection = false) noexcept
    -> std::size_t;
[[nodiscard]] auto WantsNativeAutomapPoiFrame(
    bool retainCurrentProjection = false) noexcept -> bool;
[[nodiscard]] auto GetNativeAutomapPoiCounters() noexcept
    -> NativeAutomapPoiCounters;

} // namespace RuffnecKk::MapSense
