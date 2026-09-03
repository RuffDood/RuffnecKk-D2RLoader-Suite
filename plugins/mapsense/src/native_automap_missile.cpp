#include "native_automap_missile.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t ProjectClientToAutomapRva = 0x0D4910;
constexpr std::uintptr_t GetUnitIdRva = 0x34A330;
constexpr std::uintptr_t GetUnitClassIdRva = 0x349860;
constexpr std::uintptr_t GetDynamicPathRva = 0x34AE80;
constexpr std::uintptr_t GetUnitClientXRva = 0x34AF60;
constexpr std::uintptr_t GetUnitClientYRva = 0x34AFB0;
constexpr std::uintptr_t PathGetXRva = 0x341A20;
constexpr std::uintptr_t PathGetYRva = 0x341A30;
constexpr std::uintptr_t ClientUnitHashTableRva = 0x2A23910;
constexpr std::uintptr_t ServerUnitHashTableRva =
    ClientUnitHashTableRva + NativeServerUnitHashTableOffsetFromClient;
constexpr std::uintptr_t ClientUnitHashTableWitnessRva = 0x09A5D0;
constexpr std::uintptr_t ServerUnitHashTableWitnessRva = 0x09A5A0;
constexpr std::uintptr_t ClientUnitHashLookupWitnessRva = 0x09F270;
constexpr std::uintptr_t MissileUnitTypeWitnessRva = 0x3F21E0;
constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitHashNextOffset = 0x158;
constexpr std::uint32_t FrameSlotCount = 3U;

enum class FrameSlotState : std::uint8_t {
    Free,
    Writing,
    Published,
    Reading,
};

struct NativePoint final {
    std::int32_t x{};
    std::int32_t y{};
};

struct MissileFrameSlot final {
    std::array<NativeAutomapMissileSnapshot, MaximumNativeAutomapMissiles>
        snapshots{};
    std::atomic<FrameSlotState> state{FrameSlotState::Free};
    Detail::NativeMissileIdentitySet<
        NativeMissileIdentityTableCapacity,
        MaximumNativeAutomapMissiles> identities{};
    std::size_t count{};
    std::size_t clientCount{};
    std::size_t serverCount{};
    std::uint64_t observedTick{};
    std::uint64_t epoch{};
    std::uint64_t sequence{};
};

using GetUnitIdFn = std::uint32_t(__fastcall*)(void* unit) noexcept;
using GetUnitClassIdFn = std::int32_t(__fastcall*)(void* unit) noexcept;
using GetNativePointerFn = void*(__fastcall*)(void* value) noexcept;
using GetCoordinateFn = std::int32_t(__fastcall*)(void* value) noexcept;
using ProjectClientToAutomapFn = NativePoint*(__fastcall*)(
    void* automapContext,
    NativePoint* output,
    std::uint64_t packedClientCoordinates) noexcept;

std::uint8_t* Base{};
GetUnitIdFn GetUnitId{};
GetUnitClassIdFn GetUnitClassId{};
GetNativePointerFn GetDynamicPath{};
GetCoordinateFn GetUnitClientX{};
GetCoordinateFn GetUnitClientY{};
GetCoordinateFn PathGetX{};
GetCoordinateFn PathGetY{};
ProjectClientToAutomapFn ProjectClientToAutomap{};

std::atomic_bool Active{};
std::atomic_bool CollectionEnabled{};
std::atomic<std::uint64_t> Epoch{1U};
std::atomic<std::uint64_t> PublishedSequence{};
std::atomic<std::int32_t> PublishedFrame{-1};
std::array<MissileFrameSlot, FrameSlotCount> FrameSlots{};
std::int64_t PerformanceCounterFrequency{};

std::atomic<std::uint64_t> AutomapPulses{};
std::atomic<std::uint64_t> ClientTableScans{};
std::atomic<std::uint64_t> ServerTableScans{};
std::atomic<std::uint64_t> BucketsVisited{};
std::atomic<std::uint64_t> ClientBucketsVisited{};
std::atomic<std::uint64_t> ServerBucketsVisited{};
std::atomic<std::uint64_t> TraversalLimits{};
std::atomic<std::uint64_t> CyclesRejected{};
std::atomic<std::uint64_t> UnitsObserved{};
std::atomic<std::uint64_t> ClientUnitsObserved{};
std::atomic<std::uint64_t> ServerUnitsObserved{};
std::atomic<std::uint64_t> UnitTypeRejected{};
std::atomic<std::uint64_t> InvalidUnitIds{};
std::atomic<std::uint64_t> InvalidClassIds{};
std::atomic<std::uint64_t> PathRejected{};
std::atomic<std::uint64_t> ProjectionRejected{};
std::atomic<std::uint64_t> NativeClipRejected{};
std::atomic<std::uint64_t> DuplicatesSuppressed{};
std::atomic<std::uint64_t> ClientPreferredDuplicates{};
std::atomic<std::uint64_t> ClassConflictsRejected{};
std::atomic<std::uint64_t> FramesPublished{};
std::atomic<std::uint64_t> MissilesPublished{};
std::atomic<std::uint64_t> ClientMissilesPublished{};
std::atomic<std::uint64_t> ServerMissilesPublished{};
std::atomic<std::uint64_t> WriterContentionDrops{};
std::atomic<std::uint64_t> ReaderContentionDrops{};
std::atomic<std::uint64_t> AccessFaults{};
std::atomic<std::uint64_t> MaximumScanMicroseconds{};
std::atomic<std::uint64_t> TotalScanMicroseconds{};
std::atomic<std::uint64_t> ScanTimingSamples{};
std::atomic<std::uint64_t> CurrentPublished{};
std::atomic<std::uint64_t> CurrentClientPublished{};
std::atomic<std::uint64_t> CurrentServerPublished{};

static_assert(FrameSlotCount >= 3U);
static_assert(NativeClientUnitHashTypeStride == 0x400U);
static_assert(NativeServerUnitHashTableOffsetFromClient == 0x1800U);
static_assert(ServerUnitHashTableRva == 0x2A25110U);
static_assert(
    Detail::NativeClientUnitHashTableOffsetForType(NativeMissileUnitType)
    == 0xC00U);
static_assert(std::is_trivially_copyable_v<NativeAutomapMissileSnapshot>);
static_assert(std::is_standard_layout_v<NativeAutomapMissileSnapshot>);
static_assert(sizeof(NativeAutomapMissileSnapshot) == 56U);

template <typename Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

[[nodiscard]] auto IsAlignedPointer(const void* value) noexcept -> bool {
    const auto address = reinterpret_cast<std::uintptr_t>(value);
    return address >= 0x10000U
        && (address % alignof(void*)) == 0U;
}

[[nodiscard]] auto PerformanceCounterMicroseconds() noexcept
        -> std::uint64_t {
    LARGE_INTEGER counter{};
    if (PerformanceCounterFrequency <= 0
        || QueryPerformanceCounter(&counter) == FALSE
        || counter.QuadPart < 0) {
        return 0U;
    }
    const auto wholeSeconds = counter.QuadPart / PerformanceCounterFrequency;
    const auto remainder = counter.QuadPart % PerformanceCounterFrequency;
    return static_cast<std::uint64_t>(wholeSeconds) * UINT64_C(1000000)
        + static_cast<std::uint64_t>(
            (remainder * INT64_C(1000000))
                / PerformanceCounterFrequency);
}

void UpdateMaximum(
        std::atomic<std::uint64_t>& destination,
        std::uint64_t candidate) noexcept {
    auto current = destination.load(std::memory_order_relaxed);
    while (current < candidate
        && !destination.compare_exchange_weak(
            current,
            candidate,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}

void ResetCounters() noexcept {
    AutomapPulses.store(0U, std::memory_order_relaxed);
    ClientTableScans.store(0U, std::memory_order_relaxed);
    ServerTableScans.store(0U, std::memory_order_relaxed);
    BucketsVisited.store(0U, std::memory_order_relaxed);
    ClientBucketsVisited.store(0U, std::memory_order_relaxed);
    ServerBucketsVisited.store(0U, std::memory_order_relaxed);
    TraversalLimits.store(0U, std::memory_order_relaxed);
    CyclesRejected.store(0U, std::memory_order_relaxed);
    UnitsObserved.store(0U, std::memory_order_relaxed);
    ClientUnitsObserved.store(0U, std::memory_order_relaxed);
    ServerUnitsObserved.store(0U, std::memory_order_relaxed);
    UnitTypeRejected.store(0U, std::memory_order_relaxed);
    InvalidUnitIds.store(0U, std::memory_order_relaxed);
    InvalidClassIds.store(0U, std::memory_order_relaxed);
    PathRejected.store(0U, std::memory_order_relaxed);
    ProjectionRejected.store(0U, std::memory_order_relaxed);
    NativeClipRejected.store(0U, std::memory_order_relaxed);
    DuplicatesSuppressed.store(0U, std::memory_order_relaxed);
    ClientPreferredDuplicates.store(0U, std::memory_order_relaxed);
    ClassConflictsRejected.store(0U, std::memory_order_relaxed);
    FramesPublished.store(0U, std::memory_order_relaxed);
    MissilesPublished.store(0U, std::memory_order_relaxed);
    ClientMissilesPublished.store(0U, std::memory_order_relaxed);
    ServerMissilesPublished.store(0U, std::memory_order_relaxed);
    WriterContentionDrops.store(0U, std::memory_order_relaxed);
    ReaderContentionDrops.store(0U, std::memory_order_relaxed);
    AccessFaults.store(0U, std::memory_order_relaxed);
    MaximumScanMicroseconds.store(0U, std::memory_order_relaxed);
    TotalScanMicroseconds.store(0U, std::memory_order_relaxed);
    ScanTimingSamples.store(0U, std::memory_order_relaxed);
    CurrentPublished.store(0U, std::memory_order_relaxed);
    CurrentClientPublished.store(0U, std::memory_order_relaxed);
    CurrentServerPublished.store(0U, std::memory_order_relaxed);
}

void ReleasePublishedSlot(std::int32_t index) noexcept {
    if (index < 0
        || static_cast<std::uint32_t>(index) >= FrameSlotCount) {
        return;
    }
    auto expected = FrameSlotState::Published;
    (void)FrameSlots[static_cast<std::size_t>(index)]
        .state.compare_exchange_strong(
            expected,
            FrameSlotState::Free,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
}

void InvalidatePublishedFrame() noexcept {
    Epoch.fetch_add(1U, std::memory_order_acq_rel);
    const auto previous = PublishedFrame.exchange(
        -1,
        std::memory_order_acq_rel);
    ReleasePublishedSlot(previous);
    CurrentPublished.store(0U, std::memory_order_release);
    CurrentClientPublished.store(0U, std::memory_order_release);
    CurrentServerPublished.store(0U, std::memory_order_release);
}

[[nodiscard]] auto ClaimWriteSlot() noexcept -> std::int32_t {
    for (std::uint32_t index = 0U; index < FrameSlotCount; ++index) {
        auto expected = FrameSlotState::Free;
        if (FrameSlots[index].state.compare_exchange_strong(
                expected,
                FrameSlotState::Writing,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return static_cast<std::int32_t>(index);
        }
    }
    WriterContentionDrops.fetch_add(1U, std::memory_order_relaxed);
    return -1;
}

void ReleaseReadSlot(std::int32_t index) noexcept {
    if (index < 0
        || static_cast<std::uint32_t>(index) >= FrameSlotCount) {
        return;
    }
    auto& slot = FrameSlots[static_cast<std::size_t>(index)];
    if (PublishedFrame.load(std::memory_order_acquire) == index) {
        slot.state.store(FrameSlotState::Published, std::memory_order_release);
        if (PublishedFrame.load(std::memory_order_acquire) != index) {
            auto expected = FrameSlotState::Published;
            (void)slot.state.compare_exchange_strong(
                expected,
                FrameSlotState::Free,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }
        return;
    }
    slot.state.store(FrameSlotState::Free, std::memory_order_release);
}

[[nodiscard]] auto ClaimPublishedSlot() noexcept -> std::int32_t {
    for (std::uint32_t attempt = 0U; attempt < FrameSlotCount; ++attempt) {
        const auto index = PublishedFrame.load(std::memory_order_acquire);
        if (index < 0
            || static_cast<std::uint32_t>(index) >= FrameSlotCount) {
            return -1;
        }
        auto expected = FrameSlotState::Published;
        if (FrameSlots[static_cast<std::size_t>(index)]
                .state.compare_exchange_strong(
                    expected,
                    FrameSlotState::Reading,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
            return index;
        }
    }
    ReaderContentionDrops.fetch_add(1U, std::memory_order_relaxed);
    return -1;
}

enum class NativeMissileTableSource : std::uint8_t {
    Client,
    Server,
};

void CountDuplicateIdentity(
        NativeMissileTableSource source,
        Detail::NativeMissileIdentityResult result) noexcept {
    if (result != Detail::NativeMissileIdentityResult::DuplicateClient
        && result != Detail::NativeMissileIdentityResult::DuplicateServer) {
        return;
    }
    DuplicatesSuppressed.fetch_add(1U, std::memory_order_relaxed);
    if (source == NativeMissileTableSource::Server
        && result == Detail::NativeMissileIdentityResult::DuplicateClient) {
        ClientPreferredDuplicates.fetch_add(1U, std::memory_order_relaxed);
    }
}

[[nodiscard]] auto ScanNativeMissileTable(
        std::uintptr_t tableRva,
        NativeMissileTableSource source,
        const NativeAutomapMissilePlayerPass& pass,
        MissileFrameSlot& frame,
        std::size_t& published,
        std::size_t& sourcePublished,
        std::uint64_t scanEpoch,
        std::uint64_t sequence) noexcept -> bool {
    const auto serverSource = source == NativeMissileTableSource::Server;
    auto& tableScans = serverSource ? ServerTableScans : ClientTableScans;
    auto& sourceBuckets = serverSource
        ? ServerBucketsVisited
        : ClientBucketsVisited;
    auto& sourceObserved = serverSource
        ? ServerUnitsObserved
        : ClientUnitsObserved;
    tableScans.fetch_add(1U, std::memory_order_relaxed);
    std::size_t sourceUnits{};

    auto** const buckets = reinterpret_cast<void**>(
        Base + tableRva
        + Detail::NativeClientUnitHashTableOffsetForType(
            NativeMissileUnitType));
    for (std::size_t bucketIndex = 0U;
            bucketIndex < NativeClientUnitHashBucketCount;
            ++bucketIndex) {
        BucketsVisited.fetch_add(1U, std::memory_order_relaxed);
        sourceBuckets.fetch_add(1U, std::memory_order_relaxed);
        void* unit = buckets[bucketIndex];
        void* fast = unit;
        std::size_t bucketUnits{};

        while (unit != nullptr) {
            if (!IsAlignedPointer(unit)) {
                AccessFaults.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }
            if (!Detail::MayVisitNativeMissileUnit(
                    sourceUnits,
                    bucketUnits)) {
                TraversalLimits.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }

            auto* const unitBytes = static_cast<std::uint8_t*>(unit);
            void* const next = *reinterpret_cast<void**>(
                unitBytes + UnitHashNextOffset);
            if (next != nullptr && !IsAlignedPointer(next)) {
                AccessFaults.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }
            ++sourceUnits;
            ++bucketUnits;
            UnitsObserved.fetch_add(1U, std::memory_order_relaxed);
            sourceObserved.fetch_add(1U, std::memory_order_relaxed);

            // Floyd's algorithm detects every finite hash-chain cycle
            // without allocating or retaining native pointers.
            void* const slow = next;
            if (fast != nullptr) {
                if (!IsAlignedPointer(fast)) {
                    AccessFaults.fetch_add(1U, std::memory_order_relaxed);
                    return false;
                }
                fast = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(fast) + UnitHashNextOffset);
                if (fast != nullptr) {
                    if (!IsAlignedPointer(fast)) {
                        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
                        return false;
                    }
                    fast = *reinterpret_cast<void**>(
                        static_cast<std::uint8_t*>(fast)
                            + UnitHashNextOffset);
                }
            }
            if (slow != nullptr && slow == fast) {
                CyclesRejected.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }

            const auto unitType = *reinterpret_cast<const std::uint32_t*>(
                unitBytes + UnitTypeOffset);
            if (unitType != NativeMissileUnitType) {
                UnitTypeRejected.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }

            const auto unitId = GetUnitId(unit);
            const auto classId = GetUnitClassId(unit);
            if (unitId == UINT32_MAX) {
                InvalidUnitIds.fetch_add(1U, std::memory_order_relaxed);
            } else if (classId < 0) {
                InvalidClassIds.fetch_add(1U, std::memory_order_relaxed);
            } else {
                const auto existing = frame.identities.Find(unitId, classId);
                if (existing == Detail::NativeMissileIdentityResult::DuplicateClient
                    || existing
                        == Detail::NativeMissileIdentityResult::DuplicateServer) {
                    CountDuplicateIdentity(source, existing);
                    unit = next;
                    continue;
                }
                if (existing
                        == Detail::NativeMissileIdentityResult::ClassConflict) {
                    ClassConflictsRejected.fetch_add(
                        1U,
                        std::memory_order_relaxed);
                    unit = next;
                    continue;
                }
                if (existing != Detail::NativeMissileIdentityResult::Missing) {
                    TraversalLimits.fetch_add(1U, std::memory_order_relaxed);
                    return false;
                }

                void* const path = GetDynamicPath(unit);
                if (!IsAlignedPointer(path)) {
                    PathRejected.fetch_add(1U, std::memory_order_relaxed);
                } else {
                    const auto worldX = PathGetX(path);
                    const auto worldY = PathGetY(path);
                    constexpr auto MaximumPathCoordinate =
                        static_cast<std::int32_t>(
                            (std::numeric_limits<std::uint16_t>::max)());
                    if (worldX < 0 || worldX > MaximumPathCoordinate
                        || worldY < 0 || worldY > MaximumPathCoordinate) {
                        PathRejected.fetch_add(1U, std::memory_order_relaxed);
                    } else {
                        const auto clientX = GetUnitClientX(unit);
                        const auto clientY = GetUnitClientY(unit);
                        const auto packedClientCoordinates =
                            static_cast<std::uint64_t>(
                                static_cast<std::uint32_t>(clientX))
                            | (static_cast<std::uint64_t>(
                                static_cast<std::uint32_t>(clientY))
                                << 32U);
                        NativePoint projected{};
                        if (ProjectClientToAutomap(
                                pass.automapContext,
                                &projected,
                                packedClientCoordinates) != &projected) {
                            ProjectionRejected.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                        } else if (projected.x < pass.clipLeft
                            || projected.y < pass.clipTop
                            || static_cast<std::int64_t>(projected.x)
                                >= static_cast<std::int64_t>(pass.clipLeft)
                                    + pass.clipWidth
                            || static_cast<std::int64_t>(projected.y)
                                >= static_cast<std::int64_t>(pass.clipTop)
                                    + pass.clipHeight) {
                            NativeClipRejected.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                        } else if (published >= frame.snapshots.size()) {
                            TraversalLimits.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                            return false;
                        } else {
                            const auto inserted = frame.identities.Insert(
                                unitId,
                                classId,
                                serverSource);
                            if (inserted
                                    != Detail::NativeMissileIdentityResult::Inserted) {
                                if (inserted
                                        == Detail::NativeMissileIdentityResult::DuplicateClient
                                    || inserted
                                        == Detail::NativeMissileIdentityResult::DuplicateServer) {
                                    CountDuplicateIdentity(source, inserted);
                                    unit = next;
                                    continue;
                                }
                                if (inserted
                                        == Detail::NativeMissileIdentityResult::ClassConflict) {
                                    ClassConflictsRejected.fetch_add(
                                        1U,
                                        std::memory_order_relaxed);
                                    unit = next;
                                    continue;
                                }
                                TraversalLimits.fetch_add(
                                    1U,
                                    std::memory_order_relaxed);
                                return false;
                            }
                            frame.snapshots[published++] = {
                                .unitId = unitId,
                                .classId = classId,
                                .x = projected.x,
                                .y = projected.y,
                                .worldSubtileX =
                                    static_cast<std::uint16_t>(worldX),
                                .worldSubtileY =
                                    static_cast<std::uint16_t>(worldY),
                                .playerSubtileX = pass.playerSubtileX,
                                .playerSubtileY = pass.playerSubtileY,
                                .nativeWidth = pass.nativeWidth,
                                .nativeHeight = pass.nativeHeight,
                                .observedTick = pass.observedTick,
                                .epoch = scanEpoch,
                                .sequence = sequence,
                            };
                            ++sourcePublished;
                        }
                    }
                }
            }
            unit = next;
        }
    }
    return true;
}

[[nodiscard]] auto ValidateRuntime(
        const D2RL::PluginContext* context) noexcept -> bool {
    constexpr std::array<std::uint8_t, 32> projectExpected{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x66, 0x0F, 0x6E, 0x41, 0x10, 0x4C, 0x8B, 0xFA};
    constexpr std::array<std::uint8_t, 46> getUnitIdExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
        0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x41,
        0x08, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 32> getUnitClassIdExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0x49, 0xCB, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 68> getDynamicPathExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0xD4, 0xB9, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
        0xCC, 0x83, 0x3B, 0x05, 0x75, 0x14, 0x48, 0x8D,
        0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
        0xE8, 0xEB, 0xA4, 0xFF, 0xFF, 0x84, 0xC0, 0x74,
        0x01, 0xCC, 0x48, 0x8B, 0x43, 0x38, 0x48, 0x83,
        0xC4, 0x20, 0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 32> getXExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x94, 0xA3, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01};
    constexpr std::array<std::uint8_t, 32> getYExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x94, 0x9D, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01};
    constexpr std::array<std::uint8_t, 16> pathGetXExpected{
        0x0F, 0xB7, 0x41, 0x02, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 16> pathGetYExpected{
        0x0F, 0xB7, 0x41, 0x06, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 28> clientUnitHashTableExpected{
        0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x36, 0x93,
        0x98, 0x02, 0x8B, 0xD1, 0x44, 0x8B, 0xC1, 0x49,
        0x8B, 0xC9, 0x83, 0xE2, 0x7F, 0x48, 0xC1, 0xE1,
        0x0A, 0x48, 0x03, 0xC8};
    constexpr std::array<std::uint8_t, 28> serverUnitHashTableExpected{
        0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x66, 0xAB,
        0x98, 0x02, 0x8B, 0xD1, 0x44, 0x8B, 0xC1, 0x49,
        0x8B, 0xC9, 0x83, 0xE2, 0x7F, 0x48, 0xC1, 0xE1,
        0x0A, 0x48, 0x03, 0xC8};
    constexpr std::array<std::uint8_t, 41> clientUnitHashLookupExpected{
        0x48, 0x63, 0xC2, 0x48, 0x8B, 0x04, 0xC1, 0x48,
        0x85, 0xC0, 0x74, 0x1A, 0x44, 0x39, 0x40, 0x08,
        0x75, 0x05, 0x44, 0x39, 0x08, 0x74, 0x11, 0x48,
        0x8B, 0x88, 0x58, 0x01, 0x00, 0x00, 0x48, 0x8B,
        0xC1, 0x48, 0x85, 0xC9, 0xEB, 0xE4, 0x33, 0xC0,
        0xC3};
    constexpr std::array<std::uint8_t, 32> missileUnitTypeExpected{
        0x40, 0x53, 0x83, 0x39, 0x03, 0x4D, 0x8B, 0xD0,
        0x48, 0x8B, 0xD9, 0x0F, 0x85, 0x59, 0x01, 0x00,
        0x00, 0x4C, 0x8B, 0x49, 0x18, 0x48, 0x8D, 0x05,
        0x30, 0xEC, 0x91, 0x01, 0x48, 0x89, 0x7C, 0x24};
    const auto check = [context](
            std::uintptr_t rva,
            const auto& expected) noexcept {
        return context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()));
    };
    return check(ProjectClientToAutomapRva, projectExpected)
        && check(GetUnitIdRva, getUnitIdExpected)
        && check(GetUnitClassIdRva, getUnitClassIdExpected)
        && check(GetDynamicPathRva, getDynamicPathExpected)
        && check(GetUnitClientXRva, getXExpected)
        && check(GetUnitClientYRva, getYExpected)
        && check(PathGetXRva, pathGetXExpected)
        && check(PathGetYRva, pathGetYExpected)
        && check(
            ClientUnitHashTableWitnessRva,
            clientUnitHashTableExpected)
        && check(
            ServerUnitHashTableWitnessRva,
            serverUnitHashTableExpected)
        && check(
            ClientUnitHashLookupWitnessRva,
            clientUnitHashLookupExpected)
        && check(MissileUnitTypeWitnessRva, missileUnitTypeExpected);
}

} // namespace

auto InitializeNativeAutomapMissile(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
        || context->apiVersion != D2RL_PLUGIN_API_VERSION
        || context->exeBase == 0U) {
        return false;
    }
    if (Active.load(std::memory_order_acquire)) return true;
    if (!ValidateRuntime(context)) {
        context->LogWarn(
            "MapSense: native client/server missile-table signature or ABI mismatch; missile collection refused.");
        return false;
    }

    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    GetUnitId = At<GetUnitIdFn>(GetUnitIdRva);
    GetUnitClassId = At<GetUnitClassIdFn>(GetUnitClassIdRva);
    GetDynamicPath = At<GetNativePointerFn>(GetDynamicPathRva);
    GetUnitClientX = At<GetCoordinateFn>(GetUnitClientXRva);
    GetUnitClientY = At<GetCoordinateFn>(GetUnitClientYRva);
    PathGetX = At<GetCoordinateFn>(PathGetXRva);
    PathGetY = At<GetCoordinateFn>(PathGetYRva);
    ProjectClientToAutomap = At<ProjectClientToAutomapFn>(
        ProjectClientToAutomapRva);

    for (auto& slot : FrameSlots) {
        slot.identities.Reset();
        slot.count = 0U;
        slot.clientCount = 0U;
        slot.serverCount = 0U;
        slot.observedTick = 0U;
        slot.epoch = 0U;
        slot.sequence = 0U;
        slot.state.store(FrameSlotState::Free, std::memory_order_relaxed);
    }
    PublishedFrame.store(-1, std::memory_order_relaxed);
    PublishedSequence.store(0U, std::memory_order_relaxed);
    Epoch.store(1U, std::memory_order_relaxed);
    CollectionEnabled.store(false, std::memory_order_relaxed);
    ResetCounters();

    LARGE_INTEGER performanceFrequency{};
    PerformanceCounterFrequency = QueryPerformanceFrequency(
        &performanceFrequency) != FALSE
        ? performanceFrequency.QuadPart
        : 0;
    Active.store(true, std::memory_order_release);
    return true;
}

void ShutdownNativeAutomapMissile() noexcept {
    CollectionEnabled.store(false, std::memory_order_release);
    Active.store(false, std::memory_order_release);
    InvalidatePublishedFrame();
}

void ResetNativeAutomapMissile() noexcept {
    InvalidatePublishedFrame();
}

void InvalidateNativeAutomapMissileFrame() noexcept {
    InvalidatePublishedFrame();
}

void SetNativeAutomapMissileEnabled(bool enabled) noexcept {
    const auto previous = CollectionEnabled.exchange(
        enabled,
        std::memory_order_acq_rel);
    if (previous != enabled) InvalidatePublishedFrame();
}

void ObserveNativeAutomapMissilePlayerPass(
        const NativeAutomapMissilePlayerPass& pass) noexcept {
    if (!Active.load(std::memory_order_acquire)
        || !CollectionEnabled.load(std::memory_order_acquire)
        || Base == nullptr || pass.automapContext == nullptr
        || pass.nativeWidth <= 0 || pass.nativeHeight <= 0
        || pass.clipWidth <= 0 || pass.clipHeight <= 0
        || pass.observedTick == 0U) {
        return;
    }

    AutomapPulses.fetch_add(1U, std::memory_order_relaxed);
    const auto writeIndex = ClaimWriteSlot();
    if (writeIndex < 0) return;
    auto& frame = FrameSlots[static_cast<std::size_t>(writeIndex)];
    const auto scanEpoch = Epoch.load(std::memory_order_acquire);
    const auto sequence = PublishedSequence.fetch_add(
        1U,
        std::memory_order_acq_rel) + 1U;
    const auto started = PerformanceCounterMicroseconds();
    std::size_t published{};
    std::size_t clientPublished{};
    std::size_t serverPublished{};
    bool complete = true;
    frame.identities.Reset();

    __try {
        complete = ScanNativeMissileTable(
            ClientUnitHashTableRva,
            NativeMissileTableSource::Client,
            pass,
            frame,
            published,
            clientPublished,
            scanEpoch,
            sequence);
        if (complete) {
            complete = ScanNativeMissileTable(
                ServerUnitHashTableRva,
                NativeMissileTableSource::Server,
                pass,
                frame,
                published,
                serverPublished,
                scanEpoch,
                sequence);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
        complete = false;
    }

    const auto finished = PerformanceCounterMicroseconds();
    if (started != 0U && finished >= started) {
        const auto elapsed = finished - started;
        UpdateMaximum(MaximumScanMicroseconds, elapsed);
        TotalScanMicroseconds.fetch_add(elapsed, std::memory_order_relaxed);
        ScanTimingSamples.fetch_add(1U, std::memory_order_relaxed);
    }

    if (!complete
        || scanEpoch != Epoch.load(std::memory_order_acquire)
        || !Active.load(std::memory_order_acquire)
        || !CollectionEnabled.load(std::memory_order_acquire)) {
        frame.count = 0U;
        frame.clientCount = 0U;
        frame.serverCount = 0U;
        frame.state.store(FrameSlotState::Free, std::memory_order_release);
        return;
    }

    frame.count = published;
    frame.clientCount = clientPublished;
    frame.serverCount = serverPublished;
    frame.observedTick = pass.observedTick;
    frame.epoch = scanEpoch;
    frame.sequence = sequence;
    frame.state.store(FrameSlotState::Published, std::memory_order_release);
    const auto previous = PublishedFrame.exchange(
        writeIndex,
        std::memory_order_acq_rel);
    ReleasePublishedSlot(previous);
    FramesPublished.fetch_add(1U, std::memory_order_relaxed);
    MissilesPublished.fetch_add(
        static_cast<std::uint64_t>(published),
        std::memory_order_relaxed);
    ClientMissilesPublished.fetch_add(
        static_cast<std::uint64_t>(clientPublished),
        std::memory_order_relaxed);
    ServerMissilesPublished.fetch_add(
        static_cast<std::uint64_t>(serverPublished),
        std::memory_order_relaxed);
    CurrentPublished.store(
        static_cast<std::uint64_t>(published),
        std::memory_order_release);
    CurrentClientPublished.store(
        static_cast<std::uint64_t>(clientPublished),
        std::memory_order_release);
    CurrentServerPublished.store(
        static_cast<std::uint64_t>(serverPublished),
        std::memory_order_release);
}

auto AcquireNativeAutomapMissiles(
        std::vector<NativeAutomapMissileSnapshot>& snapshots) noexcept
        -> std::size_t {
    snapshots.clear();
    if (!Active.load(std::memory_order_acquire)
        || !CollectionEnabled.load(std::memory_order_acquire)) {
        return 0U;
    }
    const auto readIndex = ClaimPublishedSlot();
    if (readIndex < 0) return 0U;
    auto& frame = FrameSlots[static_cast<std::size_t>(readIndex)];
    const auto currentTick = static_cast<std::uint64_t>(GetTickCount64());
    const auto currentEpoch = Epoch.load(std::memory_order_acquire);
    if (!Detail::IsNativeAutomapMissileSnapshotFresh(
            frame.observedTick,
            currentTick,
            frame.epoch,
            currentEpoch)
        || frame.count > frame.snapshots.size()) {
        ReleaseReadSlot(readIndex);
        return 0U;
    }

    try {
        snapshots.assign(
            frame.snapshots.begin(),
            frame.snapshots.begin()
                + static_cast<std::ptrdiff_t>(frame.count));
    } catch (...) {
        snapshots.clear();
        ReaderContentionDrops.fetch_add(1U, std::memory_order_relaxed);
    }
    ReleaseReadSlot(readIndex);
    return snapshots.size();
}

auto WantsNativeAutomapMissileFrame() noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || !CollectionEnabled.load(std::memory_order_acquire)
        || PublishedFrame.load(std::memory_order_acquire) < 0) {
        return false;
    }
    const auto index = ClaimPublishedSlot();
    if (index < 0) return false;
    const auto& frame = FrameSlots[static_cast<std::size_t>(index)];
    const auto wants = frame.count != 0U
        && Detail::IsNativeAutomapMissileSnapshotFresh(
            frame.observedTick,
            static_cast<std::uint64_t>(GetTickCount64()),
            frame.epoch,
            Epoch.load(std::memory_order_acquire));
    ReleaseReadSlot(index);
    return wants;
}

auto GetNativeAutomapMissileCounters() noexcept
        -> NativeAutomapMissileCounters {
    return {
        .automapPulses = AutomapPulses.load(std::memory_order_relaxed),
        .clientTableScans = ClientTableScans.load(std::memory_order_relaxed),
        .serverTableScans = ServerTableScans.load(std::memory_order_relaxed),
        .bucketsVisited = BucketsVisited.load(std::memory_order_relaxed),
        .clientBucketsVisited = ClientBucketsVisited.load(
            std::memory_order_relaxed),
        .serverBucketsVisited = ServerBucketsVisited.load(
            std::memory_order_relaxed),
        .traversalLimits = TraversalLimits.load(std::memory_order_relaxed),
        .cyclesRejected = CyclesRejected.load(std::memory_order_relaxed),
        .unitsObserved = UnitsObserved.load(std::memory_order_relaxed),
        .clientUnitsObserved = ClientUnitsObserved.load(
            std::memory_order_relaxed),
        .serverUnitsObserved = ServerUnitsObserved.load(
            std::memory_order_relaxed),
        .unitTypeRejected = UnitTypeRejected.load(std::memory_order_relaxed),
        .invalidUnitIds = InvalidUnitIds.load(std::memory_order_relaxed),
        .invalidClassIds = InvalidClassIds.load(std::memory_order_relaxed),
        .pathRejected = PathRejected.load(std::memory_order_relaxed),
        .projectionRejected = ProjectionRejected.load(
            std::memory_order_relaxed),
        .nativeClipRejected = NativeClipRejected.load(
            std::memory_order_relaxed),
        .duplicatesSuppressed = DuplicatesSuppressed.load(
            std::memory_order_relaxed),
        .clientPreferredDuplicates = ClientPreferredDuplicates.load(
            std::memory_order_relaxed),
        .classConflictsRejected = ClassConflictsRejected.load(
            std::memory_order_relaxed),
        .framesPublished = FramesPublished.load(std::memory_order_relaxed),
        .missilesPublished = MissilesPublished.load(std::memory_order_relaxed),
        .clientMissilesPublished = ClientMissilesPublished.load(
            std::memory_order_relaxed),
        .serverMissilesPublished = ServerMissilesPublished.load(
            std::memory_order_relaxed),
        .writerContentionDrops = WriterContentionDrops.load(
            std::memory_order_relaxed),
        .readerContentionDrops = ReaderContentionDrops.load(
            std::memory_order_relaxed),
        .accessFaults = AccessFaults.load(std::memory_order_relaxed),
        .maximumScanMicroseconds = MaximumScanMicroseconds.load(
            std::memory_order_relaxed),
        .totalScanMicroseconds = TotalScanMicroseconds.load(
            std::memory_order_relaxed),
        .scanTimingSamples = ScanTimingSamples.load(
            std::memory_order_relaxed),
        .currentPublished = CurrentPublished.load(std::memory_order_relaxed),
        .currentClientPublished = CurrentClientPublished.load(
            std::memory_order_relaxed),
        .currentServerPublished = CurrentServerPublished.load(
            std::memory_order_relaxed),
    };
}

} // namespace RuffnecKk::MapSense
