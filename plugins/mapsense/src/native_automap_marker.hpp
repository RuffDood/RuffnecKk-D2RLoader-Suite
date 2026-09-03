#pragma once

#include "overlay_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

inline constexpr std::int32_t ImmunityResistanceThreshold = 100;

enum class MonsterImmunity : std::uint8_t {
    Physical = 1U << 0U,
    Fire = 1U << 1U,
    Cold = 1U << 2U,
    Lightning = 1U << 3U,
    Poison = 1U << 4U,
    Magic = 1U << 5U,
};

[[nodiscard]] constexpr auto ImmunityBit(
        MonsterImmunity immunity) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(immunity);
}

namespace Detail {

inline constexpr std::uint8_t NativeDataContextCount = 4U;
inline constexpr std::uint64_t MaximumMonStatsRecordCount = 65'536U;
inline constexpr std::uint32_t LocalPlayerModeDeath = 0U;
inline constexpr std::uint32_t LocalPlayerModeDead = 17U;

// D2R player modes differ from monster modes. Only the two proven terminal
// player modes suppress MapSense; every other mode remains a living gameplay
// state, including attack, cast, hit recovery and town movement.
[[nodiscard]] constexpr auto IsLocalPlayerAliveMode(
        std::uint32_t mode) noexcept -> bool {
    return mode != LocalPlayerModeDeath && mode != LocalPlayerModeDead;
}

// D2R's MonStats getter asserts before it can return null for an invalid
// context or class id. Keep the complete preflight pure so every native call
// is demonstrably inside the compiled table selected by the local player.
[[nodiscard]] constexpr auto IsMonStatsLookupSafe(
        std::uint8_t localDataContext,
        std::uint8_t unitDataContext,
        std::int32_t classId,
        std::uint64_t recordCount) noexcept -> bool {
    return localDataContext < NativeDataContextCount
        && unitDataContext == localDataContext
        && classId >= 0
        && recordCount > 0U
        && recordCount <= MaximumMonStatsRecordCount
        && static_cast<std::uint64_t>(classId) < recordCount;
}

inline constexpr std::uint8_t SuperUniqueRankFlag = 0x02;
inline constexpr std::uint8_t ChampionRankFlag = 0x04;
inline constexpr std::uint8_t UniqueRankFlag = 0x08;
inline constexpr std::uint8_t MinionRankFlag = 0x10;
inline constexpr std::uint32_t UnitIsMercenaryFlag = 1U << 9U;
inline constexpr std::uint32_t UnitIsAsyncFlag = 1U << 21U;
inline constexpr std::uint32_t MonStatsNpcFlag = 1U << 8U;
inline constexpr std::uint32_t MonStatsInteractFlag = 1U << 9U;
inline constexpr std::uint32_t MonStatsInTownFlag = 1U << 10U;
inline constexpr std::uint32_t MonStatsKillableFlag = 1U << 15U;
inline constexpr std::int32_t EvilAlignment = 0;

// D2 DynamicPath coordinates are unsigned world subtiles. Keep diagnostic
// distance bands independent from the isometric client projection.
[[nodiscard]] constexpr auto SquaredWorldSubtileDistance(
        std::uint16_t leftX,
        std::uint16_t leftY,
        std::uint16_t rightX,
        std::uint16_t rightY) noexcept -> std::uint64_t {
    const auto deltaX = static_cast<std::int64_t>(leftX)
        - static_cast<std::int64_t>(rightX);
    const auto deltaY = static_cast<std::int64_t>(leftY)
        - static_cast<std::int64_t>(rightY);
    return static_cast<std::uint64_t>(
        (deltaX * deltaX) + (deltaY * deltaY));
}

enum class WorldSubtileDistanceBand : std::uint8_t {
    Through80,
    From81Through140,
    From141Through220,
    Beyond220,
};

[[nodiscard]] constexpr auto ClassifyWorldSubtileDistanceSquared(
        std::uint64_t distanceSquared) noexcept
        -> WorldSubtileDistanceBand {
    if (distanceSquared <= UINT64_C(80) * UINT64_C(80)) {
        return WorldSubtileDistanceBand::Through80;
    }
    if (distanceSquared <= UINT64_C(140) * UINT64_C(140)) {
        return WorldSubtileDistanceBand::From81Through140;
    }
    if (distanceSquared <= UINT64_C(220) * UINT64_C(220)) {
        return WorldSubtileDistanceBand::From141Through220;
    }
    return WorldSubtileDistanceBand::Beyond220;
}

[[nodiscard]] constexpr auto IsEnemyMarkerUnitEligible(
        std::uint32_t unitFlags) noexcept -> bool {
    return (unitFlags & (UnitIsMercenaryFlag | UnitIsAsyncFlag)) == 0U;
}

[[nodiscard]] constexpr auto IsEnemyMarkerClassEligible(
        std::uint32_t monStatsFlags) noexcept -> bool {
    return (monStatsFlags & MonStatsKillableFlag) != 0U
        && (monStatsFlags & (MonStatsNpcFlag
            | MonStatsInteractFlag
            | MonStatsInTownFlag)) == 0U;
}

[[nodiscard]] constexpr auto IsEnemyMarkerAlignmentEligible(
        std::int32_t alignment) noexcept -> bool {
    return alignment == EvilAlignment;
}

[[nodiscard]] constexpr auto BuildMonsterImmunityMask(
        const std::array<std::int32_t, 6>& resistances) noexcept
        -> std::uint8_t {
    constexpr std::array Immunities{
        MonsterImmunity::Physical,
        MonsterImmunity::Fire,
        MonsterImmunity::Cold,
        MonsterImmunity::Lightning,
        MonsterImmunity::Poison,
        MonsterImmunity::Magic,
    };
    std::uint8_t mask{};
    for (std::size_t index = 0U; index < Immunities.size(); ++index) {
        if (resistances[index] >= ImmunityResistanceThreshold) {
            mask = static_cast<std::uint8_t>(
                mask | ImmunityBit(Immunities[index]));
        }
    }
    return mask;
}

[[nodiscard]] constexpr auto ClassifyMonsterRankFlags(
        std::uint8_t flags) noexcept -> MonsterRank {
    if ((flags & SuperUniqueRankFlag) != 0U) {
        return MonsterRank::SuperUnique;
    }
    if ((flags & ChampionRankFlag) != 0U) return MonsterRank::Champion;
    if ((flags & UniqueRankFlag) != 0U) return MonsterRank::Unique;
    if ((flags & MinionRankFlag) != 0U) return MonsterRank::Minion;
    return MonsterRank::Normal;
}

inline constexpr std::size_t MaximumNavigationProjectionDiagnosticEntries =
    256U;

class NavigationProjectionDiagnosticCache final {
public:
    [[nodiscard]] auto ShouldLog(
            std::int32_t levelId,
            std::uint8_t lineKind,
            std::uint64_t destinationId,
            std::uint64_t fingerprint) noexcept -> bool {
        if (!hasLevel_ || levelId_ != levelId) {
            levelId_ = levelId;
            entryCount_ = 0U;
            hasLevel_ = true;
        }

        for (std::size_t index = 0U; index < entryCount_; ++index) {
            auto& entry = entries_[index];
            if (entry.destinationId != destinationId
                || entry.lineKind != lineKind) {
                continue;
            }
            if (entry.fingerprint == fingerprint) return false;
            entry.fingerprint = fingerprint;
            return true;
        }

        if (entryCount_ >= entries_.size()) return false;
        entries_[entryCount_++] = {
            .destinationId = destinationId,
            .fingerprint = fingerprint,
            .lineKind = lineKind,
        };
        return true;
    }

    void Reset() noexcept {
        entryCount_ = 0U;
        levelId_ = 0;
        hasLevel_ = false;
    }

private:
    struct Entry final {
        std::uint64_t destinationId{};
        std::uint64_t fingerprint{};
        std::uint8_t lineKind{};
    };

    std::array<Entry, MaximumNavigationProjectionDiagnosticEntries> entries_{};
    std::size_t entryCount_{};
    std::int32_t levelId_{};
    bool hasLevel_{};
};

} // namespace Detail

// Latest renderer-facing copy of D2R's own automap viewport. The native
// context uses an exclusive right/bottom rectangle; MapSense copies only
// values and never retains the borrowed AutomapContext pointer.
struct NativeAutomapViewportSnapshot final {
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    std::int32_t clipLeft{};
    std::int32_t clipTop{};
    std::int32_t clipWidth{};
    std::int32_t clipHeight{};
    std::uint64_t observedTick{};
    std::uint64_t epoch{};
};

struct NativeAutomapClipBounds final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

// Normalizes the native clip to the native render surface. This is the same
// rectangle D2R supplies while laying the automap around Inventory, Character,
// Quest and the other native panels. Invalid or empty snapshots fail closed.
[[nodiscard]] constexpr auto TryResolveNativeAutomapClipBounds(
        const NativeAutomapViewportSnapshot& viewport,
        NativeAutomapClipBounds& output) noexcept -> bool {
    if (viewport.nativeWidth <= 0 || viewport.nativeWidth > 32768
        || viewport.nativeHeight <= 0 || viewport.nativeHeight > 32768
        || viewport.clipWidth <= 0 || viewport.clipHeight <= 0) {
        return false;
    }

    const auto rawRight = static_cast<std::int64_t>(viewport.clipLeft)
        + static_cast<std::int64_t>(viewport.clipWidth);
    const auto rawBottom = static_cast<std::int64_t>(viewport.clipTop)
        + static_cast<std::int64_t>(viewport.clipHeight);
    const auto left = viewport.clipLeft < 0 ? 0 : viewport.clipLeft;
    const auto top = viewport.clipTop < 0 ? 0 : viewport.clipTop;
    const auto right = rawRight > viewport.nativeWidth
        ? viewport.nativeWidth
        : static_cast<std::int32_t>(rawRight);
    const auto bottom = rawBottom > viewport.nativeHeight
        ? viewport.nativeHeight
        : static_cast<std::int32_t>(rawBottom);
    if (right <= left || bottom <= top) return false;

    output = {
        .left = left,
        .top = top,
        .right = right,
        .bottom = bottom,
    };
    return true;
}

struct NativeAutomapMarkerSnapshot final {
    std::uint32_t unitId{};
    std::int32_t classId{-1};
    std::int32_t superUniqueIndex{-1};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    MonsterRank rank{MonsterRank::Normal};
    std::uint8_t immunityMask{};
    std::uint64_t epoch{};
    std::uint64_t sequence{};
};

// MonStats.boss is an engine-behavior flag shared by unnamed families such as
// Putrid Defilers. Player-facing boss identity instead requires the live
// superunique rank/index or the dedicated Prime Evil data contract.
[[nodiscard]] constexpr auto HasNamedBossDisplayIdentity(
        MonsterRank rank,
        std::int32_t superUniqueIndex,
        bool primeEvil) noexcept -> bool {
    return rank == MonsterRank::SuperUnique
        || superUniqueIndex >= 0
        || primeEvil;
}

// ImGui preserves submission order. Draw lower-value layers first so a dense
// pack cannot bury the marker that carries the most useful combat identity.
// Callers may explicitly promote a separately verified named-boss identity.
[[nodiscard]] constexpr auto MonsterMarkerRenderLayer(
        MonsterRank rank,
        bool bossIdentity = false) noexcept -> std::uint8_t {
    if (bossIdentity || rank == MonsterRank::SuperUnique) return 4U;
    switch (rank) {
        case MonsterRank::Normal: return 0U;
        case MonsterRank::Minion: return 1U;
        case MonsterRank::Champion: return 2U;
        case MonsterRank::Unique: return 3U;
        case MonsterRank::SuperUnique: return 4U;
    }
    return 0U;
}

inline constexpr std::uint8_t MaximumMonsterMarkerRenderLayer = 4U;

inline constexpr std::size_t MaximumNativeAutomapMarkers = 32'768U;
inline constexpr std::size_t MaximumRecentNativeAutomapMarkers = 65'536U;

struct NativeAutomapMarkerCounters final {
    std::uint64_t automapPulses{};
    std::uint64_t monsterTableScans{};
    std::uint64_t monsterPositionRefreshes{};
    std::uint64_t trackedCurrent{};
    std::uint64_t trackedIdsResolved{};
    std::uint64_t trackedIdsMissing{};
    std::uint64_t monsterBucketsVisited{};
    std::uint64_t monsterTraversalLimits{};
    std::uint64_t unitsObserved{};
    std::uint64_t monstersObserved{};
    std::uint64_t modeRejected{};
    std::uint64_t unitFlagRejected{};
    std::uint64_t classRejected{};
    std::uint64_t alignmentRejected{};
    std::uint64_t metadataFaults{};
    std::uint64_t hostilesObserved{};
    std::uint64_t hostilesThrough80{};
    std::uint64_t hostilesFrom81Through140{};
    std::uint64_t hostilesFrom141Through220{};
    std::uint64_t hostilesBeyond220{};
    std::uint64_t projectionRejected{};
    std::uint64_t nativeClipRejected{};
    std::uint64_t acceptedThrough80{};
    std::uint64_t acceptedFrom81Through140{};
    std::uint64_t acceptedFrom141Through220{};
    std::uint64_t acceptedBeyond220{};
    std::uint64_t clipRejectedThrough80{};
    std::uint64_t clipRejectedFrom81Through140{};
    std::uint64_t clipRejectedFrom141Through220{};
    std::uint64_t clipRejectedBeyond220{};
    std::uint64_t candidatesAccepted{};
    std::uint64_t markersInserted{};
    std::uint64_t markersRefreshed{};
    std::uint64_t markersExpired{};
    std::uint64_t freshMarkers{};
    std::uint64_t contentionWaits{};
    std::uint64_t storageFailures{};
    std::uint64_t accessFaults{};
    std::uint64_t maximumDiscoveryMicroseconds{};
    std::uint64_t totalDiscoveryMicroseconds{};
    std::uint64_t discoveryTimingSamples{};
    std::uint64_t maximumRefreshMicroseconds{};
    std::uint64_t totalRefreshMicroseconds{};
    std::uint64_t refreshTimingSamples{};
    std::uint32_t maximumHostileDistance{};
    std::uint32_t maximumAcceptedDistance{};
    std::uint32_t maximumPublishedDistance{};
};

using NativeAutomapLevelObservedCallback = void(*)(
    std::int32_t currentLevelId,
    bool levelChanged,
    void* userData) noexcept;

auto InitializeNativeAutomapMarker(
    const D2RL::PluginContext* context,
    bool navigationProjectionDiagnosticsEnabled) noexcept -> bool;
void ShutdownNativeAutomapMarker() noexcept;
void ResetNativeAutomapMarker() noexcept;
// Hides the current renderer-facing marker epoch without resetting diagnostic
// counters. The next native automap pass starts a fresh visible epoch.
void InvalidateNativeAutomapMarkerFrame() noexcept;
// Revokes only the renderer's current local-player witness. Atlas, navigation,
// POI and marker caches stay intact and a later living player pass re-arms the
// frame without rebuilding them.
void InvalidateNativeAutomapLocalPlayerFrame() noexcept;
[[nodiscard]] auto IsNativeAutomapLocalPlayerFrameAlive() noexcept -> bool;
void SetNativeAutomapMarkerEnabled(bool enabled) noexcept;
void SetNativeAutomapImmunityCollectionEnabled(bool enabled) noexcept;
void SetNativeAutomapLevelObservedCallback(
    NativeAutomapLevelObservedCallback callback,
    void* userData) noexcept;

// Copies every recent marker projected from the complete client monster table.
// The cache is keyed by native unit id and does not retain any D2R pointer.
auto AcquireNativeAutomapMarkers(
    std::vector<NativeAutomapMarkerSnapshot>& snapshots,
    bool retainCurrentProjection = false) noexcept
    -> std::size_t;

// Copies the freshest native automap viewport published by the local-player
// pass. A false result means MapSense cannot prove a safe map drawing region
// for this frame and must emit no map pixels; the settings menu is independent.
[[nodiscard]] auto AcquireNativeAutomapViewport(
    NativeAutomapViewportSnapshot& snapshot,
    bool retainCurrentProjection = false) noexcept -> bool;

// Cheap frame predicate for a renderer that otherwise sleeps with its menu
// closed. A true result still requires AcquireNativeAutomapMarkers to return
// at least one fresh snapshot.
auto WantsNativeAutomapMarkerFrame(
    bool retainCurrentProjection = false) noexcept -> bool;

auto GetNativeAutomapMarkerCounters() noexcept
    -> NativeAutomapMarkerCounters;

} // namespace RuffnecKk::MapSense
