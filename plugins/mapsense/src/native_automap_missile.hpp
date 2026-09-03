#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

inline constexpr std::uint32_t NativeMissileUnitType = 3U;
inline constexpr std::size_t NativeClientUnitHashBucketCount = 128U;
inline constexpr std::size_t NativeClientUnitHashTypeStride =
    NativeClientUnitHashBucketCount * sizeof(void*);
inline constexpr std::size_t NativeClientUnitHashTypeCount = 6U;
inline constexpr std::size_t NativeServerUnitHashTableOffsetFromClient =
    NativeClientUnitHashTypeCount * NativeClientUnitHashTypeStride;
inline constexpr std::size_t MaximumNativeAutomapMissiles = 32'768U;
inline constexpr std::size_t MaximumNativeMissilesPerBucket = 8'192U;
inline constexpr std::size_t NativeMissileIdentityTableCapacity =
    MaximumNativeAutomapMissiles * 2U;
inline constexpr std::uint64_t NativeAutomapMissileLifetimeMilliseconds = 250U;

namespace Detail {

[[nodiscard]] constexpr auto NativeClientUnitHashTableOffsetForType(
        std::uint32_t unitType) noexcept -> std::size_t {
    return static_cast<std::size_t>(unitType)
        * NativeClientUnitHashTypeStride;
}

[[nodiscard]] constexpr auto MayVisitNativeMissileUnit(
        std::size_t totalUnits,
        std::size_t bucketUnits) noexcept -> bool {
    return totalUnits < MaximumNativeAutomapMissiles
        && bucketUnits < MaximumNativeMissilesPerBucket;
}

[[nodiscard]] constexpr auto IsNativeAutomapMissileSnapshotFresh(
        std::uint64_t observedTick,
        std::uint64_t currentTick,
        std::uint64_t snapshotEpoch,
        std::uint64_t currentEpoch) noexcept -> bool {
    return snapshotEpoch == currentEpoch
        && observedTick != 0U
        && currentTick >= observedTick
        && currentTick - observedTick
            <= NativeAutomapMissileLifetimeMilliseconds;
}

enum class NativeMissileIdentityResult : std::uint8_t {
    Missing,
    Inserted,
    DuplicateClient,
    DuplicateServer,
    ClassConflict,
    CapacityExceeded,
    Invalid,
};

// A fixed open-addressed set keeps dual client/server collection bounded and
// allocation-free. Identity is the native unit id; class id is retained as a
// consistency witness so a conflicting server copy can never produce a
// second marker. Reset touches only slots used by the previous frame.
template <std::size_t Capacity, std::size_t MaximumEntries>
class NativeMissileIdentitySet final {
    static_assert(Capacity != 0U && (Capacity & (Capacity - 1U)) == 0U);
    static_assert(MaximumEntries != 0U);
    static_assert(MaximumEntries * 2U <= Capacity);
    static_assert(Capacity <= static_cast<std::size_t>(UINT32_MAX));

public:
    constexpr void Reset() noexcept {
        for (std::size_t index = 0U; index < touchedCount_; ++index) {
            entries_[touchedSlots_[index]] = 0U;
        }
        touchedCount_ = 0U;
    }

    [[nodiscard]] constexpr auto Find(
            std::uint32_t unitId,
            std::int32_t classId) const noexcept
            -> NativeMissileIdentityResult {
        return Probe(unitId, classId).result;
    }

    [[nodiscard]] constexpr auto Insert(
            std::uint32_t unitId,
            std::int32_t classId,
            bool serverSource) noexcept
            -> NativeMissileIdentityResult {
        const auto probe = Probe(unitId, classId);
        if (probe.result != NativeMissileIdentityResult::Missing) {
            return probe.result;
        }
        if (touchedCount_ >= MaximumEntries) {
            return NativeMissileIdentityResult::CapacityExceeded;
        }

        entries_[probe.slot] = Encode(unitId, classId, serverSource) + 1U;
        touchedSlots_[touchedCount_++] = static_cast<std::uint32_t>(probe.slot);
        return NativeMissileIdentityResult::Inserted;
    }

    [[nodiscard]] constexpr auto Size() const noexcept -> std::size_t {
        return touchedCount_;
    }

private:
    struct ProbeResult final {
        NativeMissileIdentityResult result{};
        std::size_t slot{};
    };

    [[nodiscard]] static constexpr auto Encode(
            std::uint32_t unitId,
            std::int32_t classId,
            bool serverSource) noexcept -> std::uint64_t {
        return (static_cast<std::uint64_t>(unitId) << 32U)
            | static_cast<std::uint32_t>(classId)
            | (serverSource ? UINT32_C(0x80000000) : 0U);
    }

    [[nodiscard]] static constexpr auto Hash(
            std::uint32_t unitId) noexcept -> std::size_t {
        return static_cast<std::size_t>(
            static_cast<std::uint64_t>(unitId)
                * UINT64_C(0x9E3779B185EBCA87))
            & (Capacity - 1U);
    }

    [[nodiscard]] constexpr auto Probe(
            std::uint32_t unitId,
            std::int32_t classId) const noexcept -> ProbeResult {
        if (unitId == UINT32_MAX || classId < 0) {
            return {NativeMissileIdentityResult::Invalid, 0U};
        }

        auto slot = Hash(unitId);
        for (std::size_t attempt = 0U; attempt < Capacity; ++attempt) {
            const auto storedPlusOne = entries_[slot];
            if (storedPlusOne == 0U) {
                return {NativeMissileIdentityResult::Missing, slot};
            }

            const auto stored = storedPlusOne - 1U;
            const auto storedUnitId = static_cast<std::uint32_t>(
                stored >> 32U);
            if (storedUnitId == unitId) {
                const auto storedDetails = static_cast<std::uint32_t>(stored);
                const auto storedClassId = static_cast<std::int32_t>(
                    storedDetails & UINT32_C(0x7FFFFFFF));
                const auto storedOnServer =
                    (storedDetails & UINT32_C(0x80000000)) != 0U;
                return {
                    storedClassId == classId
                        ? (storedOnServer
                            ? NativeMissileIdentityResult::DuplicateServer
                            : NativeMissileIdentityResult::DuplicateClient)
                        : NativeMissileIdentityResult::ClassConflict,
                    slot,
                };
            }
            slot = (slot + 1U) & (Capacity - 1U);
        }
        return {NativeMissileIdentityResult::CapacityExceeded, 0U};
    }

    std::array<std::uint64_t, Capacity> entries_{};
    std::array<std::uint32_t, MaximumEntries> touchedSlots_{};
    std::size_t touchedCount_{};
};

} // namespace Detail

// Transient values copied by the existing local-player automap hook. The
// borrowed AutomapContext is valid only for the synchronous Observe call and
// is never retained by the missile collector.
struct NativeAutomapMissilePlayerPass final {
    void* automapContext{};
    std::uint16_t playerSubtileX{};
    std::uint16_t playerSubtileY{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    std::int32_t clipLeft{};
    std::int32_t clipTop{};
    std::int32_t clipWidth{};
    std::int32_t clipHeight{};
    std::uint64_t observedTick{};
};

// Renderer-facing value copy. Class ids intentionally remain raw active-table
// ordinals; the later data-policy layer will map them through the current mod
// instead of compiling a vanilla or BKVince-specific enum into this collector.
struct NativeAutomapMissileSnapshot final {
    std::uint32_t unitId{};
    std::int32_t classId{-1};
    std::int32_t x{};
    std::int32_t y{};
    std::uint16_t worldSubtileX{};
    std::uint16_t worldSubtileY{};
    std::uint16_t playerSubtileX{};
    std::uint16_t playerSubtileY{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    std::uint64_t observedTick{};
    std::uint64_t epoch{};
    std::uint64_t sequence{};
};

struct NativeAutomapMissileCounters final {
    std::uint64_t automapPulses{};
    std::uint64_t clientTableScans{};
    std::uint64_t serverTableScans{};
    std::uint64_t bucketsVisited{};
    std::uint64_t clientBucketsVisited{};
    std::uint64_t serverBucketsVisited{};
    std::uint64_t traversalLimits{};
    std::uint64_t cyclesRejected{};
    std::uint64_t unitsObserved{};
    std::uint64_t clientUnitsObserved{};
    std::uint64_t serverUnitsObserved{};
    std::uint64_t unitTypeRejected{};
    std::uint64_t invalidUnitIds{};
    std::uint64_t invalidClassIds{};
    std::uint64_t pathRejected{};
    std::uint64_t projectionRejected{};
    std::uint64_t nativeClipRejected{};
    std::uint64_t duplicatesSuppressed{};
    std::uint64_t clientPreferredDuplicates{};
    std::uint64_t classConflictsRejected{};
    std::uint64_t framesPublished{};
    std::uint64_t missilesPublished{};
    std::uint64_t clientMissilesPublished{};
    std::uint64_t serverMissilesPublished{};
    std::uint64_t writerContentionDrops{};
    std::uint64_t readerContentionDrops{};
    std::uint64_t accessFaults{};
    std::uint64_t maximumScanMicroseconds{};
    std::uint64_t totalScanMicroseconds{};
    std::uint64_t scanTimingSamples{};
    std::uint64_t currentPublished{};
    std::uint64_t currentClientPublished{};
    std::uint64_t currentServerPublished{};
};

auto InitializeNativeAutomapMissile(
    const D2RL::PluginContext* context) noexcept -> bool;
void ShutdownNativeAutomapMissile() noexcept;
void ResetNativeAutomapMissile() noexcept;
void InvalidateNativeAutomapMissileFrame() noexcept;
void SetNativeAutomapMissileEnabled(bool enabled) noexcept;

// Called only by the existing AUTOMAP_RenderUnit owner during the local-player
// pass. It performs bounded client and server type-3 table scans, suppresses
// duplicate identities with client priority, and publishes value copies only.
void ObserveNativeAutomapMissilePlayerPass(
    const NativeAutomapMissilePlayerPass& pass) noexcept;

auto AcquireNativeAutomapMissiles(
    std::vector<NativeAutomapMissileSnapshot>& snapshots) noexcept
    -> std::size_t;

auto WantsNativeAutomapMissileFrame() noexcept -> bool;

auto GetNativeAutomapMissileCounters() noexcept
    -> NativeAutomapMissileCounters;

} // namespace RuffnecKk::MapSense
