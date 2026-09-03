#include "native_automap_poi.hpp"

#include "mapsense_data_catalog.hpp"
#include "reveal_engine.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t GetUnitByIdAndTypeRva = 0x09A5D0;
constexpr std::uintptr_t GetUnitClassIdRva = 0x349860;
constexpr std::uintptr_t GetUnitIdRva = 0x34A330;
constexpr std::uintptr_t GetUnitModeRva = 0x34AB60;
constexpr std::uintptr_t GetObjectInteractTypeRva = 0x34AD40;
constexpr std::uintptr_t ObjectInteractTypeLayoutWitnessRva = 0x34AD61;
constexpr std::uintptr_t GetObjectRuntimeFlagsC8Rva = 0x4903D0;
constexpr std::uintptr_t GetUnitClientXRva = 0x34AF60;
constexpr std::uintptr_t GetUnitClientYRva = 0x34AFB0;
constexpr std::uintptr_t GetUnitRoomRva = 0x34B440;
constexpr std::uintptr_t UnitRoomLayoutWitnessRva = 0x34B461;
constexpr std::uintptr_t ActiveRoomGetDrlgRoomRva = 0x192B20;
constexpr std::uintptr_t GetDrlgRoomLevelIdRva = 0x360FC0;
constexpr std::uintptr_t ClientUnitHashTableRva = 0x2A23910;
constexpr std::uintptr_t ClientUnitHashLookupWitnessRva = 0x09F270;

constexpr std::uint32_t UnitObject = 2U;
constexpr std::uint32_t ObjectModeNeutral = 0U;
constexpr std::size_t UnitTypeOffset = 0x00U;
constexpr std::size_t UnitHashNextOffset = 0x158U;
constexpr std::size_t ActiveRoomDrlgRoomOffset = 0x18U;
constexpr std::size_t UnitHashBucketCount = 128U;
constexpr std::size_t UnitHashTypeStride =
    UnitHashBucketCount * sizeof(void*);
constexpr std::size_t MaximumUnitsPerObjectBucket = 8'192U;
constexpr std::uint64_t ObjectTableScanIntervalMilliseconds = 100U;
constexpr std::uint64_t PoiSnapshotLifetimeMilliseconds = 250U;
constexpr std::uint64_t NativeShrineVisibilityLifetimeMilliseconds = 350U;
constexpr std::size_t MaximumVisibleNativeShrines = 512U;

using GetUnitByIdAndTypeFn = void*(__fastcall*)(
    std::uint32_t unitId,
    std::uint32_t unitType) noexcept;
using GetUnitValueFn = std::uint32_t(__fastcall*)(void*) noexcept;
using GetUnitSignedValueFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitCoordinateFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetNativePointerFn = void*(__fastcall*)(void*) noexcept;
using GetLevelIdFn = std::int32_t(__fastcall*)(void*) noexcept;

struct TrackedObject final {
    std::uint32_t unitId{};
    std::int32_t classId{-1};
    AutomapPoiKind kind{AutomapPoiKind::Chest};
};

struct TrackedSpecialChestPreset final {
    AutomapSpecialChestDefinition definition{};
    bool consumed{};
    std::uint8_t stateFlags{};
};

struct VisibleNativeShrine final {
    std::uint32_t unitId{UINT32_MAX};
    std::uint64_t observedTick{};
};

std::uint8_t* Base{};
GetUnitByIdAndTypeFn GetUnitByIdAndType{};
GetUnitValueFn GetUnitId{};
GetUnitSignedValueFn GetUnitClassId{};
GetUnitValueFn GetUnitMode{};
GetUnitValueFn GetObjectInteractType{};
GetUnitValueFn GetObjectRuntimeFlagsC8{};
GetUnitCoordinateFn GetUnitClientX{};
GetUnitCoordinateFn GetUnitClientY{};
GetNativePointerFn GetUnitRoom{};
GetLevelIdFn GetDrlgRoomLevelId{};
std::shared_ptr<const MapSenseDataCatalog> Catalog{};

std::atomic_bool Active{};
std::atomic<std::uint32_t> CollectionMask{};
std::atomic_flag StateLock = ATOMIC_FLAG_INIT;
std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
    ExitDefinitions{};
std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
    ExitDefinitionScratch{};
std::size_t ExitDefinitionCount{};
std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
    ExternalExitDefinitions{};
std::size_t ExternalExitDefinitionCount{};
std::array<std::int32_t, MaximumAutomapExitLabels>
    CompleteNativeExitOwners{};
std::size_t CompleteNativeExitOwnerCount{};
Detail::LayeredAutomapWaypointDefinitionCatalog<
    MaximumAutomapWaypointLabels>
    WaypointDefinitions{};
Detail::LayeredAutomapLevelDefinitionCatalog<MaximumAutomapLevelLabels>
    LevelDefinitions{};
std::array<TrackedSpecialChestPreset, MaximumAutomapSpecialChestPresets>
    SpecialChestPresets{};
std::array<TrackedSpecialChestPreset, MaximumAutomapSpecialChestPresets>
    SpecialChestPresetScratch{};
std::size_t SpecialChestPresetCount{};
std::array<TrackedObject, MaximumTrackedAutomapObjects> TrackedObjects{};
std::array<TrackedObject, MaximumTrackedAutomapObjects> ObjectScanScratch{};
std::size_t TrackedObjectCount{};
std::array<VisibleNativeShrine, MaximumVisibleNativeShrines>
    VisibleNativeShrines{};
std::size_t VisibleNativeShrineCount{};
std::array<NativeAutomapPoiSnapshot, MaximumAutomapPoiSnapshots>
    ProjectedSnapshots{};
std::size_t ProjectedSnapshotCount{};
std::uint64_t SessionGeneration{};
std::uint64_t Revision{1U};
std::int32_t LevelId{UnknownNavigationLevelId};
std::uint64_t LastObjectTableScanTick{};
std::uint64_t LastProjectionTick{};

std::atomic<std::size_t> PublishedSnapshotCount{};
std::atomic<std::uint64_t> PublishedProjectionTick{};
std::atomic<std::uint64_t> AutomapPulses{};
std::atomic<std::uint64_t> ObjectTableScans{};
std::atomic<std::uint64_t> ObjectBucketsVisited{};
std::atomic<std::uint64_t> ObjectUnitsObserved{};
std::atomic<std::uint64_t> ObjectUnitsClassified{};
std::atomic<std::uint64_t> ObjectTraversalLimits{};
std::atomic<std::uint64_t> ExitDefinitionsPublished{};
std::atomic<std::uint64_t> SpecialChestDefinitionsPublished{};
std::atomic<std::uint64_t> Projected{};
std::atomic<std::uint64_t> ProjectionRejected{};
std::atomic<std::uint64_t> ContentionWaits{};
std::atomic<std::uint64_t> AccessFaults{};
const D2RL::PluginContext* DiagnosticContext{};
bool DiagnosticsEnabled{};
std::size_t LastDiagnosticExitCount{(std::numeric_limits<std::size_t>::max)()};
std::size_t LastDiagnosticWaypointCount{(std::numeric_limits<std::size_t>::max)()};
std::size_t LastDiagnosticLevelCount{(std::numeric_limits<std::size_t>::max)()};
std::uint64_t LastProjectionDiagnosticTick{};

static_assert(UnitHashTypeStride == 0x400U);
static_assert(std::is_standard_layout_v<AutomapExitLabelDefinition>);
static_assert(std::is_trivially_copyable_v<AutomapExitLabelDefinition>);
static_assert(std::is_standard_layout_v<AutomapWaypointLabelDefinition>);
static_assert(std::is_trivially_copyable_v<AutomapWaypointLabelDefinition>);
static_assert(std::is_standard_layout_v<AutomapLevelLabelDefinition>);
static_assert(std::is_trivially_copyable_v<AutomapLevelLabelDefinition>);
static_assert(std::is_standard_layout_v<AutomapSpecialChestDefinition>);
static_assert(std::is_trivially_copyable_v<AutomapSpecialChestDefinition>);
static_assert(std::is_standard_layout_v<TrackedSpecialChestPreset>);
static_assert(std::is_trivially_copyable_v<TrackedSpecialChestPreset>);
static_assert(std::is_standard_layout_v<NativeAutomapPoiSnapshot>);
static_assert(std::is_trivially_copyable_v<NativeAutomapPoiSnapshot>);
static_assert(std::is_standard_layout_v<TrackedObject>);
static_assert(std::is_trivially_copyable_v<TrackedObject>);
static_assert(std::is_standard_layout_v<VisibleNativeShrine>);
static_assert(std::is_trivially_copyable_v<VisibleNativeShrine>);

class StateLockGuard final {
public:
    explicit StateLockGuard(bool wait) noexcept {
        if (!wait) {
            acquired_ = !StateLock.test_and_set(std::memory_order_acquire);
            return;
        }
        while (StateLock.test_and_set(std::memory_order_acquire)) {
            YieldProcessor();
        }
        acquired_ = true;
    }

    StateLockGuard(const StateLockGuard&) = delete;
    auto operator=(const StateLockGuard&) -> StateLockGuard& = delete;

    ~StateLockGuard() {
        if (acquired_) StateLock.clear(std::memory_order_release);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return acquired_;
    }

private:
    bool acquired_{};
};

template <typename Function>
[[nodiscard]] auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

[[nodiscard]] auto IsAlignedPointer(const void* pointer) noexcept -> bool {
    return pointer != nullptr
        && (reinterpret_cast<std::uintptr_t>(pointer)
            & (alignof(void*) - 1U)) == 0U;
}

[[nodiscard]] auto HasCollection(
        std::uint32_t mask,
        AutomapPoiCollection collection) noexcept -> bool {
    return (mask & AutomapPoiCollectionBit(collection)) != 0U;
}

[[nodiscard]] auto IsRecent(
        std::uint64_t observed,
        std::uint64_t now) noexcept -> bool {
    return observed != 0U && now >= observed
        && now - observed <= PoiSnapshotLifetimeMilliseconds;
}

[[nodiscard]] auto HasCompleteNativeExitOwner(
        std::int32_t levelId,
        std::int32_t additionalOwner = UnknownNavigationLevelId) noexcept
        -> bool {
    if (levelId == additionalOwner) return true;
    for (std::size_t index = 0U;
            index < CompleteNativeExitOwnerCount;
            ++index) {
        if (CompleteNativeExitOwners[index] == levelId) return true;
    }
    return false;
}

[[nodiscard]] auto HasExternalExitOwner(
        std::span<const AutomapExitLabelDefinition> external,
        std::int32_t levelId) noexcept -> bool {
    for (const auto& definition : external) {
        if (definition.sourceLevelId == levelId) return true;
    }
    return false;
}

[[nodiscard]] auto EffectiveExitDefinitionCount(
        std::span<const AutomapExitLabelDefinition> external,
        std::span<const AutomapExitLabelDefinition> native,
        std::int32_t additionalCompleteOwner =
            UnknownNavigationLevelId) noexcept -> std::size_t {
    std::size_t count{};
    for (const auto& definition : external) {
        if (ShouldRetainExternalAutomapExit(
                definition,
                native,
                HasCompleteNativeExitOwner(
                    definition.sourceLevelId,
                    additionalCompleteOwner))) {
            ++count;
        }
    }
    for (const auto& definition : native) {
        if (HasCompleteNativeExitOwner(
                definition.sourceLevelId,
                additionalCompleteOwner)
            || !HasExternalExitOwner(external, definition.sourceLevelId)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] auto BuildEffectiveExitDefinitionsLocked(
        std::array<AutomapExitLabelDefinition,
            MaximumAutomapExitLabels>& output,
        std::size_t& outputCount) noexcept -> bool {
    outputCount = 0U;
    const auto external = std::span(
        ExternalExitDefinitions.data(),
        ExternalExitDefinitionCount);
    const auto native = std::span(
        ExitDefinitions.data(),
        ExitDefinitionCount);
    if (EffectiveExitDefinitionCount(external, native)
            > output.size()) {
        return false;
    }
    for (const auto& definition : external) {
        if (!ShouldRetainExternalAutomapExit(
                definition,
                native,
                HasCompleteNativeExitOwner(definition.sourceLevelId))) {
            continue;
        }
        output[outputCount++] = definition;
    }
    for (const auto& definition : native) {
        if (!HasCompleteNativeExitOwner(definition.sourceLevelId)
            && HasExternalExitOwner(external, definition.sourceLevelId)) {
            continue;
        }
        output[outputCount++] = definition;
    }
    return true;
}

void MarkCompleteNativeExitOwner(std::int32_t levelId) noexcept {
    if (HasCompleteNativeExitOwner(levelId)
        || CompleteNativeExitOwnerCount
            >= CompleteNativeExitOwners.size()) {
        return;
    }
    CompleteNativeExitOwners[CompleteNativeExitOwnerCount++] = levelId;
}

void ClearProjectionLocked() noexcept {
    ProjectedSnapshotCount = 0U;
    LastProjectionTick = 0U;
    PublishedSnapshotCount.store(0U, std::memory_order_release);
    PublishedProjectionTick.store(0U, std::memory_order_release);
}

void ClearCurrentLevelLocked() noexcept {
    TrackedObjectCount = 0U;
    VisibleNativeShrineCount = 0U;
    LastObjectTableScanTick = 0U;
    ++Revision;
    ClearProjectionLocked();
}

void ClearAllPoiLocked() noexcept {
    ExitDefinitionCount = 0U;
    ExternalExitDefinitionCount = 0U;
    CompleteNativeExitOwnerCount = 0U;
    WaypointDefinitions.Clear();
    LevelDefinitions.Clear();
    SpecialChestPresetCount = 0U;
    ClearCurrentLevelLocked();
}

[[nodiscard]] auto CollectionForKind(AutomapPoiKind kind) noexcept
        -> AutomapPoiCollection {
    switch (kind) {
        case AutomapPoiKind::ExitLabel:
            return AutomapPoiCollection::ExitLabels;
        case AutomapPoiKind::WaypointLabel:
            return AutomapPoiCollection::WaypointLabels;
        case AutomapPoiKind::LevelLabel:
            return AutomapPoiCollection::ExitLabels;
        case AutomapPoiKind::ShrineIcon:
        case AutomapPoiKind::ShrineLabel:
            return AutomapPoiCollection::ShrineLabels;
        case AutomapPoiKind::Chest:
            return AutomapPoiCollection::Chests;
        case AutomapPoiKind::SuperChest:
            return AutomapPoiCollection::SuperChests;
        case AutomapPoiKind::ArmorRack:
            return AutomapPoiCollection::ArmorRacks;
        case AutomapPoiKind::WeaponRack:
            return AutomapPoiCollection::WeaponRacks;
    }
    return AutomapPoiCollection::None;
}

[[nodiscard]] auto ClassifyObject(
        std::int32_t classId,
        AutomapPoiKind& kind) noexcept -> bool {
    if (classId < 0 || Catalog == nullptr) return false;
    const auto* const record = Catalog->FindObjectById(
        static_cast<std::uint32_t>(classId));
    if (record == nullptr) return false;

    // These are semantic Objects.txt engine contracts, not object ids or
    // names tied to one mod. A contradictory custom row is ambiguous and
    // therefore hidden instead of being assigned by an arbitrary priority.
    const bool armorRack = record->operateFn == 19U;
    const bool weaponRack = record->operateFn == 20U;
    const bool shrine = IsShrineObjectDefinition(*record);
    const bool chest = IsChestObjectDefinition(*record);
    const auto matches = static_cast<unsigned>(armorRack)
        + static_cast<unsigned>(weaponRack)
        + static_cast<unsigned>(shrine)
        + static_cast<unsigned>(chest);
    if (matches != 1U) return false;
    if (armorRack) {
        kind = AutomapPoiKind::ArmorRack;
        return true;
    }
    if (weaponRack) {
        kind = AutomapPoiKind::WeaponRack;
        return true;
    }
    if (shrine) {
        kind = AutomapPoiKind::ShrineIcon;
        return true;
    }
    // InitFn 57 is the data-defined special-chest constructor. The live flag
    // remains an additional promotion path for generated chests whose special
    // state is assigned only at runtime.
    if (chest) {
        kind = IsSpecialChestObjectDefinition(*record)
            ? AutomapPoiKind::SuperChest
            : AutomapPoiKind::Chest;
        return true;
    }
    return false;
}

[[nodiscard]] auto WantsTrackedObject(
        std::uint32_t mask,
        AutomapPoiKind kind) noexcept -> bool {
    if (kind == AutomapPoiKind::Chest
            || kind == AutomapPoiKind::SuperChest) {
        return HasCollection(mask, AutomapPoiCollection::Chests)
            || HasCollection(mask, AutomapPoiCollection::SuperChests);
    }
    return HasCollection(mask, CollectionForKind(kind));
}

void ScanClientObjectTableLocked(std::uint32_t mask) noexcept {
    __try {
        if (Base == nullptr || GetUnitId == nullptr
                || GetUnitClassId == nullptr) {
            return;
        }
        std::size_t count{};
        auto** const buckets = reinterpret_cast<void**>(
            Base + ClientUnitHashTableRva
            + static_cast<std::size_t>(UnitObject) * UnitHashTypeStride);
        for (std::size_t bucket = 0U;
                bucket < UnitHashBucketCount;
                ++bucket) {
            ObjectBucketsVisited.fetch_add(1U, std::memory_order_relaxed);
            void* unit = buckets[bucket];
            std::size_t traversed{};
            while (unit != nullptr && traversed < MaximumUnitsPerObjectBucket) {
                if (!IsAlignedPointer(unit)) {
                    AccessFaults.fetch_add(1U, std::memory_order_relaxed);
                    break;
                }
                auto* const bytes = static_cast<std::uint8_t*>(unit);
                void* const next = *reinterpret_cast<void* const*>(
                    bytes + UnitHashNextOffset);
                ObjectUnitsObserved.fetch_add(1U, std::memory_order_relaxed);
                if (*reinterpret_cast<const std::uint32_t*>(
                        bytes + UnitTypeOffset) == UnitObject) {
                    const auto classId = GetUnitClassId(unit);
                    AutomapPoiKind kind{};
                    if (ClassifyObject(classId, kind)
                            && WantsTrackedObject(mask, kind)) {
                        const auto unitId = GetUnitId(unit);
                        if (unitId != UINT32_MAX
                                && count < ObjectScanScratch.size()) {
                            ObjectScanScratch[count++] = {
                                .unitId = unitId,
                                .classId = classId,
                                .kind = kind,
                            };
                            ObjectUnitsClassified.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                        } else if (count >= ObjectScanScratch.size()) {
                            ObjectTraversalLimits.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                            unit = nullptr;
                            break;
                        }
                    }
                }
                unit = next;
                ++traversed;
            }
            if (unit != nullptr) {
                ObjectTraversalLimits.fetch_add(1U, std::memory_order_relaxed);
            }
        }
        if (count != 0U) {
            std::copy_n(ObjectScanScratch.begin(), count, TrackedObjects.begin());
        }
        TrackedObjectCount = count;
        ObjectTableScans.fetch_add(1U, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        TrackedObjectCount = 0U;
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
    }
}

[[nodiscard]] auto ProjectPoint(
        const NativeAutomapPoiPass& pass,
        std::int32_t clientX,
        std::int32_t clientY,
        NavigationNativePoint& output) noexcept -> bool {
    if (pass.projectClient == nullptr || pass.borrowedAutomapContext == nullptr
            || !pass.projectClient(
                pass.borrowedAutomapContext,
                clientX,
                clientY,
                output)) {
        return false;
    }
    return output.x >= pass.clipLeft && output.y >= pass.clipTop
        && static_cast<std::int64_t>(output.x)
            < static_cast<std::int64_t>(pass.clipLeft) + pass.clipWidth
        && static_cast<std::int64_t>(output.y)
            < static_cast<std::int64_t>(pass.clipTop) + pass.clipHeight;
}

[[nodiscard]] auto ExitDefinitionClientPoint(
        const AutomapExitLabelDefinition& definition,
        NavigationNativePoint& output) noexcept -> bool {
    if (definition.useExactClientCoordinates) {
        output = {
            .x = definition.exactClientX,
            .y = definition.exactClientY,
        };
        return true;
    }
    return ConvertNavigationSubtileToClientCoordinates(
        definition.subtileX,
        definition.subtileY,
        output);
}

inline constexpr std::size_t MaximumExitGraphNodes =
    MaximumAutomapExitLabels * 2U + 1U;
inline constexpr auto UnknownExitGraphDistance =
    (std::numeric_limits<std::uint16_t>::max)();

void BuildExitGraphDistances(
        std::span<const AutomapExitLabelDefinition> definitions,
        std::int32_t currentLevelId,
        std::array<std::int32_t, MaximumExitGraphNodes>& nodes,
        std::array<std::uint16_t, MaximumExitGraphNodes>& distances,
        std::size_t& nodeCount) noexcept {
    distances.fill(UnknownExitGraphDistance);
    nodeCount = 0U;
    if (currentLevelId <= 0) return;
    const auto addNode = [&nodes, &nodeCount](std::int32_t levelId) noexcept {
        for (std::size_t index = 0U; index < nodeCount; ++index) {
            if (nodes[index] == levelId) return index;
        }
        if (nodeCount >= nodes.size()) return nodes.size();
        nodes[nodeCount] = levelId;
        return nodeCount++;
    };
    const auto currentIndex = addNode(currentLevelId);
    for (const auto& definition : definitions) {
        (void)addNode(definition.sourceLevelId);
        (void)addNode(definition.targetLevelId);
    }
    if (currentIndex >= nodeCount) return;
    distances[currentIndex] = 0U;
    std::array<std::size_t, MaximumExitGraphNodes> queue{};
    std::size_t head{};
    std::size_t tail{};
    queue[tail++] = currentIndex;
    while (head < tail) {
        const auto nodeIndex = queue[head++];
        const auto nodeId = nodes[nodeIndex];
        const auto nextDistance = distances[nodeIndex]
                == UnknownExitGraphDistance - 1U
            ? UnknownExitGraphDistance
            : static_cast<std::uint16_t>(distances[nodeIndex] + 1U);
        for (const auto& definition : definitions) {
            std::int32_t adjacent{};
            if (definition.sourceLevelId == nodeId) {
                adjacent = definition.targetLevelId;
            } else if (definition.targetLevelId == nodeId) {
                adjacent = definition.sourceLevelId;
            } else {
                continue;
            }
            const auto adjacentIndex = addNode(adjacent);
            if (adjacentIndex >= nodeCount
                    || distances[adjacentIndex]
                        != UnknownExitGraphDistance) {
                continue;
            }
            distances[adjacentIndex] = nextDistance;
            queue[tail++] = adjacentIndex;
        }
    }
}

[[nodiscard]] auto ExitGraphDistance(
        const std::array<std::int32_t, MaximumExitGraphNodes>& nodes,
        const std::array<std::uint16_t, MaximumExitGraphNodes>& distances,
        std::size_t nodeCount,
        std::int32_t requestedLevelId) noexcept -> std::uint16_t {
    for (std::size_t index = 0U; index < nodeCount; ++index) {
        if (nodes[index] == requestedLevelId) return distances[index];
    }
    return UnknownExitGraphDistance;
}

[[nodiscard]] auto ReciprocalExitDefinition(
        std::span<const AutomapExitLabelDefinition> definitions,
        std::span<const bool> consumed,
        std::size_t sourceIndex) noexcept -> std::size_t {
    const auto& source = definitions[sourceIndex];
    NavigationNativePoint sourcePoint{};
    const auto sourcePointValid = ExitDefinitionClientPoint(
        source,
        sourcePoint);
    auto winner = definitions.size();
    auto winnerDistance = (std::numeric_limits<std::uint64_t>::max)();
    for (std::size_t index = sourceIndex + 1U;
            index < definitions.size();
            ++index) {
        const auto& candidate = definitions[index];
        if (consumed[index]
                || candidate.sourceLevelId != source.targetLevelId
                || candidate.targetLevelId != source.sourceLevelId) {
            continue;
        }
        NavigationNativePoint candidatePoint{};
        if (!sourcePointValid
                || !ExitDefinitionClientPoint(candidate, candidatePoint)) {
            if (winner == definitions.size()) winner = index;
            continue;
        }
        const auto delta = [](std::int32_t left, std::int32_t right) noexcept {
            return left >= right
                ? static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(left) - right)
                : static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(right) - left);
        };
        const auto distance = delta(sourcePoint.x, candidatePoint.x)
            + delta(sourcePoint.y, candidatePoint.y);
        if (distance < winnerDistance) {
            winner = index;
            winnerDistance = distance;
        }
    }
    return winner;
}

[[nodiscard]] auto PreferProjectedExitDefinition(
        const AutomapExitLabelDefinition& incoming,
        const AutomapExitLabelDefinition& existing,
        std::int32_t labelLevelId) noexcept -> bool {
    if (incoming.canonicalLevelPairAnchor
            != existing.canonicalLevelPairAnchor) {
        return incoming.canonicalLevelPairAnchor;
    }
    const auto incomingNamesLabel = incoming.targetLevelId == labelLevelId;
    const auto existingNamesLabel = existing.targetLevelId == labelLevelId;
    if (incomingNamesLabel != existingNamesLabel) return incomingNamesLabel;
    const auto incomingIdentity = incoming.boundaryIdentity.Valid();
    const auto existingIdentity = existing.boundaryIdentity.Valid();
    if (incomingIdentity != existingIdentity) return incomingIdentity;
    if (incoming.useExactClientCoordinates
            != existing.useExactClientCoordinates) {
        return incoming.useExactClientCoordinates;
    }
    if (incoming.subtileX != existing.subtileX) {
        return incoming.subtileX < existing.subtileX;
    }
    if (incoming.subtileY != existing.subtileY) {
        return incoming.subtileY < existing.subtileY;
    }
    if (incoming.exactClientX != existing.exactClientX) {
        return incoming.exactClientX < existing.exactClientX;
    }
    if (incoming.exactClientY != existing.exactClientY) {
        return incoming.exactClientY < existing.exactClientY;
    }
    if (incoming.sourceLevelId != existing.sourceLevelId) {
        return incoming.sourceLevelId < existing.sourceLevelId;
    }
    if (incoming.targetLevelId != existing.targetLevelId) {
        return incoming.targetLevelId < existing.targetLevelId;
    }
    return incoming.stableId < existing.stableId;
}

void ProjectExitLabelsLocked(
        const NativeAutomapPoiPass& pass,
        std::uint32_t mask,
        std::size_t& count,
        const NativeAutomapLayerCatalog* layers) noexcept {
    if (!HasCollection(mask, AutomapPoiCollection::ExitLabels)
        || layers == nullptr) {
        return;
    }
    std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
        effectiveDefinitions{};
    std::size_t effectiveDefinitionCount{};
    if (!BuildEffectiveExitDefinitionsLocked(
            effectiveDefinitions,
            effectiveDefinitionCount)) {
        ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
        layerDefinitions{};
    std::size_t layerDefinitionCount{};
    for (std::size_t index = 0U;
            index < effectiveDefinitionCount;
            ++index) {
        const auto& definition = effectiveDefinitions[index];
        // An exit's coordinates live in its source Level's map space. The
        // target may deliberately be a dungeon on another native layer.
        if (!NativeAutomapLevelsShareLayer(
                *layers,
                pass.currentLevelId,
                definition.sourceLevelId)) {
            continue;
        }
        layerDefinitions[layerDefinitionCount++] = definition;
    }
    const auto definitions = std::span(
        layerDefinitions.data(),
        layerDefinitionCount);
    std::array<std::int32_t, MaximumExitGraphNodes> graphNodes{};
    std::array<std::uint16_t, MaximumExitGraphNodes> graphDistances{};
    std::size_t graphNodeCount{};
    BuildExitGraphDistances(
        definitions,
        pass.currentLevelId,
        graphNodes,
        graphDistances,
        graphNodeCount);
    std::array<bool, MaximumAutomapExitLabels> consumed{};
    for (std::size_t index = 0U;
            index < definitions.size()
            && count < ProjectedSnapshots.size();
            ++index) {
        if (consumed[index]) continue;
        consumed[index] = true;
        const auto& first = definitions[index];
        const auto pairHasCanonicalAnchor =
            HasCanonicalAutomapExitLevelPair(
            definitions,
            first);
        const auto sourceDistance = ExitGraphDistance(
            graphNodes,
            graphDistances,
            graphNodeCount,
            first.sourceLevelId);
        const auto targetDistance = ExitGraphDistance(
            graphNodes,
            graphDistances,
            graphNodeCount,
            first.targetLevelId);
        const auto labelLevelId = OrientedAutomapExitLevel(
            pass.currentLevelId,
            first.sourceLevelId,
            first.targetLevelId,
            sourceDistance,
            targetDistance);
        auto definitionIndex = index;
        const auto resolvedBoundaryIdentity =
            ResolvedAutomapExitBoundaryIdentity(definitions, first);
        if (pairHasCanonicalAnchor || resolvedBoundaryIdentity.Valid()) {
            for (std::size_t candidateIndex = 0U;
                    candidateIndex < definitions.size();
                    ++candidateIndex) {
                if (candidateIndex != index && consumed[candidateIndex]) {
                    continue;
                }
                const auto& candidate = definitions[candidateIndex];
                if (!IsResolvedAutomapExitPhysicalGroupMember(
                        definitions,
                        first,
                        candidate,
                        pairHasCanonicalAnchor)) {
                    continue;
                }
                consumed[candidateIndex] = true;
                if (PreferProjectedExitDefinition(
                        candidate,
                        definitions[definitionIndex],
                        labelLevelId)) {
                    definitionIndex = candidateIndex;
                }
            }
        } else {
            const auto reciprocalIndex = ReciprocalExitDefinition(
                definitions,
                std::span<const bool>(consumed.data(), definitions.size()),
                index);
            if (reciprocalIndex < definitions.size()) {
                consumed[reciprocalIndex] = true;
                if (definitions[reciprocalIndex].targetLevelId
                        == labelLevelId) {
                    definitionIndex = reciprocalIndex;
                }
            }
        }
        const auto& definition = definitions[definitionIndex];
        NavigationNativePoint client{};
        if (!ExitDefinitionClientPoint(definition, client)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        NavigationNativePoint projected{};
        if (!ProjectPoint(pass, client.x, client.y, projected)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        ProjectedSnapshots[count++] = {
            .stableId = definition.stableId
                ^ (static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(labelLevelId)) << 32U),
            .sessionGeneration = SessionGeneration,
            .revision = Revision,
            .levelId = LevelId,
            .x = projected.x,
            .y = projected.y,
            .nativeWidth = pass.nativeWidth,
            .nativeHeight = pass.nativeHeight,
            .sourceId = labelLevelId,
            .kind = AutomapPoiKind::ExitLabel,
        };
    }
}

void ProjectWaypointLabelsLocked(
        const NativeAutomapPoiPass& pass,
        std::uint32_t mask,
        std::size_t& count,
        const NativeAutomapLayerCatalog* layers) noexcept {
    if (!HasCollection(mask, AutomapPoiCollection::WaypointLabels)
        || layers == nullptr) {
        return;
    }
    for (const auto& definition : WaypointDefinitions.Definitions()) {
        if (count >= ProjectedSnapshots.size()) return;
        if (!NativeAutomapLevelsShareLayer(
                *layers,
                pass.currentLevelId,
                definition.levelId)) {
            continue;
        }
        NavigationNativePoint client{};
        if (!ConvertNavigationSubtileToClientCoordinates(
                definition.subtileX,
                definition.subtileY,
                client)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        NavigationNativePoint projected{};
        if (!ProjectPoint(pass, client.x, client.y, projected)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        ProjectedSnapshots[count++] = {
            .stableId = definition.stableId,
            .sessionGeneration = SessionGeneration,
            .revision = Revision,
            .levelId = definition.levelId,
            .x = projected.x,
            .y = projected.y,
            .nativeWidth = pass.nativeWidth,
            .nativeHeight = pass.nativeHeight,
            .sourceId = definition.levelId,
            .kind = AutomapPoiKind::WaypointLabel,
        };
    }
}

[[nodiscard]] auto HasExactWaypointLabelLocked(
        std::int32_t levelId) noexcept -> bool {
    for (const auto& definition : WaypointDefinitions.Definitions()) {
        if (definition.levelId == levelId) return true;
    }
    return false;
}

[[nodiscard]] auto HasExactExitLabelLocked(
        std::int32_t levelId,
        std::int32_t currentLevelId,
        const NativeAutomapLayerCatalog& layers) noexcept -> bool {
    std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
        effectiveDefinitions{};
    std::size_t effectiveDefinitionCount{};
    if (!BuildEffectiveExitDefinitionsLocked(
            effectiveDefinitions,
            effectiveDefinitionCount)) {
        return false;
    }
    for (std::size_t index = 0U;
            index < effectiveDefinitionCount;
            ++index) {
        const auto& definition = effectiveDefinitions[index];
        if (NativeAutomapLevelsShareLayer(
                layers,
                currentLevelId,
                definition.sourceLevelId)
            && (definition.sourceLevelId == levelId
                || definition.targetLevelId == levelId)) {
            return true;
        }
    }
    return false;
}

void ProjectLevelLabelsLocked(
        const NativeAutomapPoiPass& pass,
        std::uint32_t mask,
        std::size_t& count,
        const NativeAutomapLayerCatalog* layers) noexcept {
    if (!HasCollection(mask, AutomapPoiCollection::ExitLabels)
        || layers == nullptr) {
        return;
    }
    for (const auto& definition : LevelDefinitions.Definitions()) {
        if (count >= ProjectedSnapshots.size()) return;
        if (!NativeAutomapLevelsShareLayer(
                *layers,
                pass.currentLevelId,
                definition.levelId)) {
            continue;
        }
        if (!ShouldProjectAutomapLevelLabel(
                definition.levelId,
                pass.currentLevelId,
                HasExactWaypointLabelLocked(definition.levelId),
                HasExactExitLabelLocked(
                    definition.levelId,
                    pass.currentLevelId,
                    *layers))) {
            continue;
        }
        NavigationNativePoint client{};
        if (!ConvertNavigationSubtileToClientCoordinates(
                definition.subtileX,
                definition.subtileY,
                client)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        NavigationNativePoint projected{};
        if (!ProjectPoint(pass, client.x, client.y, projected)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        ProjectedSnapshots[count++] = {
            .stableId = definition.stableId,
            .sessionGeneration = SessionGeneration,
            .revision = Revision,
            .levelId = definition.levelId,
            .x = projected.x,
            .y = projected.y,
            .nativeWidth = pass.nativeWidth,
            .nativeHeight = pass.nativeHeight,
            .sourceId = definition.levelId,
            .kind = AutomapPoiKind::LevelLabel,
        };
    }
}

[[nodiscard]] auto SpecialChestPresetRecord(
        const TrackedSpecialChestPreset& preset) noexcept
        -> const DataCatalogObject* {
    if (Catalog == nullptr || preset.definition.classId < 0) return nullptr;
    const auto* const record = Catalog->FindObjectById(
        static_cast<std::uint32_t>(preset.definition.classId));
    return record != nullptr
            && IsSpecialChestPresetObjectDefinition(*record)
        ? record
        : nullptr;
}

[[nodiscard]] auto MatchSpecialChestPresetLocked(
        std::int32_t levelId,
        std::int32_t clientX,
        std::int32_t clientY) noexcept -> TrackedSpecialChestPreset* {
    TrackedSpecialChestPreset* winner{};
    auto winnerDistance = (std::numeric_limits<std::uint64_t>::max)();
    for (std::size_t index = 0U;
            index < SpecialChestPresetCount;
            ++index) {
        auto& preset = SpecialChestPresets[index];
        if (preset.consumed || preset.definition.levelId != levelId
                || SpecialChestPresetRecord(preset) == nullptr) {
            continue;
        }
        NavigationNativePoint presetClient{};
        if (!ConvertNavigationSubtileToClientCoordinates(
                preset.definition.subtileX,
                preset.definition.subtileY,
                presetClient)
            || !AutomapPoiWithinSubtileDistance(
                clientX,
                clientY,
                presetClient.x,
                presetClient.y,
                12)) {
            continue;
        }
        const auto delta = [](std::int32_t left, std::int32_t right) noexcept {
            return left >= right
                ? static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(left) - right)
                : static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(right) - left);
        };
        const auto distance = delta(clientX, presetClient.x)
            + delta(clientY, presetClient.y);
        if (distance < winnerDistance) {
            winner = &preset;
            winnerDistance = distance;
        }
    }
    return winner;
}

[[nodiscard]] auto WasNativeShrineRenderedRecentlyLocked(
        std::uint32_t unitId) noexcept -> bool {
    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    for (std::size_t index = 0U;
            index < VisibleNativeShrineCount;
            ++index) {
        const auto& visible = VisibleNativeShrines[index];
        if (visible.unitId != unitId || now < visible.observedTick) continue;
        return now - visible.observedTick
            <= NativeShrineVisibilityLifetimeMilliseconds;
    }
    return false;
}

void ProjectTrackedObjectsLocked(
        const NativeAutomapPoiPass& pass,
        std::uint32_t mask,
        std::size_t& count) noexcept {
    __try {
        if (GetUnitByIdAndType == nullptr || GetUnitMode == nullptr
                || GetObjectInteractType == nullptr
                || GetObjectRuntimeFlagsC8 == nullptr
                || GetUnitClientX == nullptr || GetUnitClientY == nullptr
                || GetUnitRoom == nullptr
                || GetDrlgRoomLevelId == nullptr) {
            return;
        }
        for (std::size_t index = 0U;
                index < TrackedObjectCount
                && count < ProjectedSnapshots.size();
                ++index) {
            const auto& tracked = TrackedObjects[index];
            if (!WantsTrackedObject(mask, tracked.kind)) continue;
            void* const unit = GetUnitByIdAndType(tracked.unitId, UnitObject);
            if (!IsAlignedPointer(unit)) continue;
            const auto unitMode = GetUnitMode(unit);
            auto* const activeRoom = static_cast<std::uint8_t*>(
                GetUnitRoom(unit));
            if (!IsAlignedPointer(activeRoom)) continue;
            auto* const drlgRoom = *reinterpret_cast<std::uint8_t**>(
                activeRoom + ActiveRoomDrlgRoomOffset);
            if (!IsAlignedPointer(drlgRoom)
                    || GetDrlgRoomLevelId(drlgRoom)
                        != pass.currentLevelId) {
                continue;
            }
            const auto interactType = GetObjectInteractType(unit);
            const bool isShrine = tracked.kind == AutomapPoiKind::ShrineIcon;
            auto runtimeKind = tracked.kind;
            if (tracked.kind == AutomapPoiKind::Chest) {
                runtimeKind = IsSparklyChestRuntimeFlags(
                        GetObjectRuntimeFlagsC8(unit))
                    ? AutomapPoiKind::SuperChest
                    : AutomapPoiKind::Chest;
            }
            const auto unitClientX = GetUnitClientX(unit);
            const auto unitClientY = GetUnitClientY(unit);
            std::uint8_t stateFlags{};
            if (runtimeKind == AutomapPoiKind::Chest
                    || runtimeKind == AutomapPoiKind::SuperChest) {
                if (IsLockedChestInteractType(interactType)) {
                    stateFlags = static_cast<std::uint8_t>(
                        stateFlags | AutomapPoiStateLocked);
                }
                if (IsTrappedChestInteractType(interactType)) {
                    stateFlags = static_cast<std::uint8_t>(
                        stateFlags | AutomapPoiStateTrapped);
                }
                if (auto* const preset = MatchSpecialChestPresetLocked(
                        pass.currentLevelId,
                        unitClientX,
                        unitClientY)) {
                    preset->stateFlags = stateFlags;
                    if (unitMode != ObjectModeNeutral) {
                        preset->consumed = true;
                    }
                    // The retained preset owns this physical marker. Never
                    // draw a second ordinary live chest over its star marker.
                    continue;
                }
            }
            if (unitMode != ObjectModeNeutral) continue;
            if (isShrine
                    && (Catalog == nullptr
                        || Catalog->FindShrineByInteractType(interactType)
                            == nullptr)) {
                continue;
            }
            if ((runtimeKind == AutomapPoiKind::Chest
                    || runtimeKind == AutomapPoiKind::SuperChest)
                    && !HasCollection(mask, CollectionForKind(runtimeKind))) {
                continue;
            }
            NavigationNativePoint projected{};
            if (!ProjectPoint(
                    pass,
                    unitClientX,
                    unitClientY,
                    projected)) {
                ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
                continue;
            }
            const auto sourceId = isShrine
                ? static_cast<std::int32_t>(interactType)
                : tracked.classId;
            const auto publish = [&](AutomapPoiKind kind) noexcept {
                if (count >= ProjectedSnapshots.size()) return false;
                ProjectedSnapshots[count++] = {
                    .stableId =
                        (static_cast<std::uint64_t>(tracked.unitId) << 8U)
                            | static_cast<std::uint8_t>(kind),
                    .sessionGeneration = SessionGeneration,
                    .revision = Revision,
                    .levelId = LevelId,
                    .x = projected.x,
                    .y = projected.y,
                    .nativeWidth = pass.nativeWidth,
                    .nativeHeight = pass.nativeHeight,
                    .sourceId = sourceId,
                    .kind = kind,
                    .stateFlags = stateFlags,
                };
                return true;
            };
            if (!isShrine && !publish(runtimeKind)) break;
            if (isShrine
                    && WasNativeShrineRenderedRecentlyLocked(tracked.unitId)
                    && AutomapPoiWithinSubtileDistance(
                        pass.playerClientX,
                        pass.playerClientY,
                        unitClientX,
                        unitClientY,
                        NativeShrineLabelProximitySubtiles)) {
                (void)publish(AutomapPoiKind::ShrineLabel);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
    }
}

void ProjectSpecialChestPresetsLocked(
        const NativeAutomapPoiPass& pass,
        std::uint32_t mask,
        std::size_t& count) noexcept {
    if (!HasCollection(mask, AutomapPoiCollection::SuperChests)) return;
    for (std::size_t index = 0U;
            index < SpecialChestPresetCount
            && count < ProjectedSnapshots.size();
            ++index) {
        const auto& preset = SpecialChestPresets[index];
        if (preset.consumed
                || preset.definition.levelId != pass.currentLevelId
                || SpecialChestPresetRecord(preset) == nullptr) {
            continue;
        }
        NavigationNativePoint client{};
        if (!ConvertNavigationSubtileToClientCoordinates(
                preset.definition.subtileX,
                preset.definition.subtileY,
                client)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        NavigationNativePoint projected{};
        if (!ProjectPoint(pass, client.x, client.y, projected)) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        ProjectedSnapshots[count++] = {
            .stableId = preset.definition.stableId,
            .sessionGeneration = SessionGeneration,
            .revision = Revision,
            .levelId = LevelId,
            .x = projected.x,
            .y = projected.y,
            .nativeWidth = pass.nativeWidth,
            .nativeHeight = pass.nativeHeight,
            .sourceId = preset.definition.classId,
            .kind = AutomapPoiKind::SuperChest,
            .stateFlags = preset.stateFlags,
        };
    }
}

[[nodiscard]] auto ValidateRuntime(
        const D2RL::PluginContext* context) noexcept -> bool {
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
    constexpr std::array<std::uint8_t, 43> getUnitModeExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0xF9, 0xA3, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83,
        0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x0C, 0x48, 0x83,
        0xC4, 0x28, 0xC3};
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
    constexpr std::array<std::uint8_t, 32> objectInteractTypeExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x84, 0xA8, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01};
    constexpr std::array<std::uint8_t, 39> objectLayoutExpected{
        0x83, 0x3B, 0x02, 0x74, 0x14, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8,
        0x3B, 0x9E, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
        0xCC, 0x48, 0x8B, 0x43, 0x10, 0x0F, 0xB6, 0x40,
        0x08, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 59> objectRuntimeFlagsC8Expected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x04, 0xD5, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
        0xCC, 0x0F, 0xB6, 0x83, 0xC8, 0x00, 0x00, 0x00,
        0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x0F, 0xB6,
        0x81, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4,
        0x20, 0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 32> getUnitRoomExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01};
    constexpr std::array<std::uint8_t, 36> unitRoomLayoutWitnessExpected{
        0x8B, 0x0B, 0x83, 0xE9, 0x02, 0x74, 0x25, 0x83,
        0xE9, 0x02, 0x74, 0x20, 0x83, 0xF9, 0x01, 0x74,
        0x1B, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85, 0xC9,
        0x74, 0x0A, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9,
        0xAB, 0x67, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 16>
        activeRoomGetDrlgRoomExpected{
            0x48, 0x8B, 0x41, 0x18, 0xC3, 0xCC, 0xCC, 0xCC,
            0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 14> drlgRoomLevelIdExpected{
        0x48, 0x8B, 0x81, 0x90, 0x00, 0x00, 0x00, 0x8B,
        0x80, 0xF8, 0x01, 0x00, 0x00, 0xC3};
    constexpr std::array<std::uint8_t, 28> clientUnitHashTableExpected{
        0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x36, 0x93,
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
    const auto check = [context](
            std::uintptr_t rva,
            const auto& expected) noexcept {
        return context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()));
    };
    return check(GetUnitIdRva, getUnitIdExpected)
        && check(GetUnitClassIdRva, getUnitClassIdExpected)
        && check(GetUnitModeRva, getUnitModeExpected)
        && check(GetUnitClientXRva, getXExpected)
        && check(GetUnitClientYRva, getYExpected)
        && check(GetObjectInteractTypeRva, objectInteractTypeExpected)
        && check(ObjectInteractTypeLayoutWitnessRva, objectLayoutExpected)
        && check(
            GetObjectRuntimeFlagsC8Rva,
            objectRuntimeFlagsC8Expected)
        && check(GetUnitRoomRva, getUnitRoomExpected)
        && check(UnitRoomLayoutWitnessRva, unitRoomLayoutWitnessExpected)
        && check(
            ActiveRoomGetDrlgRoomRva,
            activeRoomGetDrlgRoomExpected)
        && check(GetDrlgRoomLevelIdRva, drlgRoomLevelIdExpected)
        && check(GetUnitByIdAndTypeRva, clientUnitHashTableExpected)
        && check(
            ClientUnitHashLookupWitnessRva,
            clientUnitHashLookupExpected);
}

void ResetCounters() noexcept {
    AutomapPulses.store(0U, std::memory_order_relaxed);
    ObjectTableScans.store(0U, std::memory_order_relaxed);
    ObjectBucketsVisited.store(0U, std::memory_order_relaxed);
    ObjectUnitsObserved.store(0U, std::memory_order_relaxed);
    ObjectUnitsClassified.store(0U, std::memory_order_relaxed);
    ObjectTraversalLimits.store(0U, std::memory_order_relaxed);
    ExitDefinitionsPublished.store(0U, std::memory_order_relaxed);
    SpecialChestDefinitionsPublished.store(0U, std::memory_order_relaxed);
    Projected.store(0U, std::memory_order_relaxed);
    ProjectionRejected.store(0U, std::memory_order_relaxed);
    ContentionWaits.store(0U, std::memory_order_relaxed);
    AccessFaults.store(0U, std::memory_order_relaxed);
}

} // namespace

auto InitializeNativeAutomapPoi(
        const D2RL::PluginContext* context,
        bool diagnostics) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION
            || context->exeBase == 0U) {
        return false;
    }
    if (Active.load(std::memory_order_acquire)) return true;
    if (!ValidateRuntime(context)) {
        context->LogWarn(
            "MapSense: native object/shrine signature or ABI mismatch; automap POIs refused.");
        return false;
    }

    StateLockGuard lock(true);
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    GetUnitByIdAndType = At<GetUnitByIdAndTypeFn>(GetUnitByIdAndTypeRva);
    GetUnitId = At<GetUnitValueFn>(GetUnitIdRva);
    GetUnitClassId = At<GetUnitSignedValueFn>(GetUnitClassIdRva);
    GetUnitMode = At<GetUnitValueFn>(GetUnitModeRva);
    GetObjectInteractType = At<GetUnitValueFn>(GetObjectInteractTypeRva);
    GetObjectRuntimeFlagsC8 = At<GetUnitValueFn>(
        GetObjectRuntimeFlagsC8Rva);
    GetUnitClientX = At<GetUnitCoordinateFn>(GetUnitClientXRva);
    GetUnitClientY = At<GetUnitCoordinateFn>(GetUnitClientYRva);
    GetUnitRoom = At<GetNativePointerFn>(GetUnitRoomRva);
    GetDrlgRoomLevelId = At<GetLevelIdFn>(GetDrlgRoomLevelIdRva);
    Catalog.reset();
    DiagnosticContext = context;
    DiagnosticsEnabled = diagnostics;
    LastDiagnosticExitCount = (std::numeric_limits<std::size_t>::max)();
    LastDiagnosticWaypointCount = (std::numeric_limits<std::size_t>::max)();
    LastDiagnosticLevelCount = (std::numeric_limits<std::size_t>::max)();
    LastProjectionDiagnosticTick = 0U;
    CollectionMask.store(0U, std::memory_order_release);
    SessionGeneration = 0U;
    LevelId = UnknownNavigationLevelId;
    Revision = 1U;
    ClearAllPoiLocked();
    ResetCounters();
    Active.store(true, std::memory_order_release);
    return true;
}

auto BindNativeAutomapPoiCatalog(
        std::shared_ptr<const MapSenseDataCatalog> catalog) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire) || catalog == nullptr) {
        return false;
    }
    StateLockGuard lock(true);
    if (!Active.load(std::memory_order_acquire)) return false;
    Catalog = std::move(catalog);
    ClearAllPoiLocked();
    return true;
}

void ShutdownNativeAutomapPoi() noexcept {
    Active.store(false, std::memory_order_release);
    CollectionMask.store(0U, std::memory_order_release);
    StateLockGuard lock(true);
    SessionGeneration = 0U;
    LevelId = UnknownNavigationLevelId;
    ClearAllPoiLocked();
    Catalog.reset();
    Base = nullptr;
    GetUnitByIdAndType = nullptr;
    GetUnitId = nullptr;
    GetUnitClassId = nullptr;
    GetUnitMode = nullptr;
    GetObjectInteractType = nullptr;
    GetObjectRuntimeFlagsC8 = nullptr;
    GetUnitClientX = nullptr;
    GetUnitClientY = nullptr;
    GetUnitRoom = nullptr;
    GetDrlgRoomLevelId = nullptr;
    DiagnosticContext = nullptr;
    DiagnosticsEnabled = false;
}

void ResetNativeAutomapPoiSession(
        std::uint64_t sessionGeneration) noexcept {
    StateLockGuard lock(true);
    SessionGeneration = sessionGeneration;
    LevelId = UnknownNavigationLevelId;
    ClearAllPoiLocked();
    ResetCounters();
}

void ResetNativeAutomapPoiLevel(
        std::uint64_t sessionGeneration,
        std::int32_t levelId) noexcept {
    StateLockGuard lock(true);
    SessionGeneration = sessionGeneration;
    LevelId = levelId;
    ClearCurrentLevelLocked();
}

void InvalidateNativeAutomapPoiFrame() noexcept {
    StateLockGuard lock(true);
    ClearProjectionLocked();
}

void SetNativeAutomapPoiCollectionMask(std::uint32_t mask) noexcept {
    constexpr auto SupportedMask =
        AutomapPoiCollectionBit(AutomapPoiCollection::ExitLabels)
        | AutomapPoiCollectionBit(AutomapPoiCollection::ShrineLabels)
        | AutomapPoiCollectionBit(AutomapPoiCollection::Chests)
        | AutomapPoiCollectionBit(AutomapPoiCollection::SuperChests)
        | AutomapPoiCollectionBit(AutomapPoiCollection::ArmorRacks)
        | AutomapPoiCollectionBit(AutomapPoiCollection::WeaponRacks)
        | AutomapPoiCollectionBit(AutomapPoiCollection::WaypointLabels);
    mask &= SupportedMask;
    const auto previous = CollectionMask.exchange(
        mask,
        std::memory_order_acq_rel);
    if (previous == mask) return;
    StateLockGuard lock(true);
    TrackedObjectCount = 0U;
    LastObjectTableScanTick = 0U;
    ClearProjectionLocked();
}

auto PublishNativeAutomapExitLabels(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        const AutomapExitLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (levelId <= 0 || definitionCount > ExitDefinitions.size()
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].sourceLevelId != levelId
                || definitions[index].targetLevelId <= 0) {
            return false;
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) {
        return false;
    }
    // Empty native captures mean "not proved yet", not "the generated atlas
    // owner does not exist". Preserve both layers byte-for-byte.
    if (definitionCount == 0U) return true;
    // Build a complete replacement before publishing it. This is deliberately
    // owner-based, not stable-id based: native evidence can move a doorway by
    // a few subtiles between readiness passes, and retaining the older sample
    // is what produced stacked labels such as three copies of "Stony Field".
    std::size_t replacementCount{};
    for (std::size_t existing = 0U;
            existing < ExitDefinitionCount;
            ++existing) {
        if (ExitDefinitions[existing].sourceLevelId == levelId) continue;
        if (replacementCount >= ExitDefinitionScratch.size()) return false;
        ExitDefinitionScratch[replacementCount++] = ExitDefinitions[existing];
    }
    if (replacementCount + definitionCount > ExitDefinitionScratch.size()) {
        return false;
    }
    if (definitionCount != 0U) {
        std::copy_n(
            definitions,
            definitionCount,
            ExitDefinitionScratch.begin() + replacementCount);
        replacementCount += definitionCount;
    }
    const auto external = std::span(
        ExternalExitDefinitions.data(),
        ExternalExitDefinitionCount);
    const auto replacement = std::span(
        ExitDefinitionScratch.data(),
        replacementCount);
    if (EffectiveExitDefinitionCount(external, replacement, levelId)
            > MaximumAutomapExitLabels) {
        return false;
    }
    if (replacementCount != 0U) {
        std::copy_n(
            ExitDefinitionScratch.begin(),
            replacementCount,
            ExitDefinitions.begin());
    }
    ExitDefinitionCount = replacementCount;
    MarkCompleteNativeExitOwner(levelId);
    ++Revision;
    ClearProjectionLocked();
    ExitDefinitionsPublished.fetch_add(
        definitionCount,
        std::memory_order_relaxed);
    return true;
}

auto MergeNativeAutomapExitLabelFragments(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        const AutomapExitLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (levelId <= 0 || definitionCount > ExitDefinitions.size()
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].sourceLevelId != levelId
                || definitions[index].targetLevelId <= 0) {
            return false;
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    if (definitionCount == 0U) return true;

    std::size_t replacementCount{};
    for (std::size_t existing = 0U;
            existing < ExitDefinitionCount;
            ++existing) {
        if (ExitDefinitions[existing].sourceLevelId == levelId) continue;
        if (replacementCount >= ExitDefinitionScratch.size()) return false;
        ExitDefinitionScratch[replacementCount++] = ExitDefinitions[existing];
    }

    const auto mergeOwnerFragment = [&](
            const AutomapExitLabelDefinition& incoming) noexcept -> bool {
        for (std::size_t existing = 0U;
                existing < replacementCount;
                ++existing) {
            auto& retained = ExitDefinitionScratch[existing];
            if (retained.sourceLevelId != levelId
                || !SameAutomapExitOwnerFragment(retained, incoming)) {
                continue;
            }
            if (PreferAutomapExitOwnerFragment(incoming, retained)) {
                retained = incoming;
            }
            return true;
        }
        if (replacementCount >= ExitDefinitionScratch.size()) return false;
        ExitDefinitionScratch[replacementCount++] = incoming;
        return true;
    };

    for (std::size_t existing = 0U;
            existing < ExitDefinitionCount;
            ++existing) {
        if (ExitDefinitions[existing].sourceLevelId == levelId
            && !mergeOwnerFragment(ExitDefinitions[existing])) {
            return false;
        }
    }
    for (std::size_t incoming = 0U;
            incoming < definitionCount;
            ++incoming) {
        if (!mergeOwnerFragment(definitions[incoming])) return false;
    }

    // A canonical facade can arrive after several passive collision fragments.
    // Once present, collapse every definition for that directed level pair to
    // the canonical sample without affecting other targets or physical seams.
    for (std::size_t canonical = 0U;
            canonical < replacementCount;
            ++canonical) {
        const auto anchor = ExitDefinitionScratch[canonical];
        if (anchor.sourceLevelId != levelId
            || !anchor.canonicalLevelPairAnchor) {
            continue;
        }
        std::size_t candidate{};
        while (candidate < replacementCount) {
            if (candidate == canonical
                || ExitDefinitionScratch[candidate].sourceLevelId
                    != anchor.sourceLevelId
                || ExitDefinitionScratch[candidate].targetLevelId
                    != anchor.targetLevelId) {
                ++candidate;
                continue;
            }
            for (std::size_t move = candidate + 1U;
                    move < replacementCount;
                    ++move) {
                ExitDefinitionScratch[move - 1U] =
                    ExitDefinitionScratch[move];
            }
            --replacementCount;
            if (candidate < canonical) --canonical;
        }
    }

    const auto external = std::span(
        ExternalExitDefinitions.data(),
        ExternalExitDefinitionCount);
    const auto replacement = std::span(
        ExitDefinitionScratch.data(),
        replacementCount);
    if (EffectiveExitDefinitionCount(external, replacement)
            > MaximumAutomapExitLabels) {
        return false;
    }
    if (replacementCount != 0U) {
        std::copy_n(
            ExitDefinitionScratch.begin(),
            replacementCount,
            ExitDefinitions.begin());
    }
    ExitDefinitionCount = replacementCount;
    ++Revision;
    ClearProjectionLocked();
    ExitDefinitionsPublished.fetch_add(
        definitionCount,
        std::memory_order_relaxed);
    return true;
}

auto ReplaceExternalAutomapExitLabels(
        std::uint64_t sessionGeneration,
        const AutomapExitLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (definitionCount > ExternalExitDefinitions.size()
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].sourceLevelId <= 0
                || definitions[index].targetLevelId <= 0) {
            return false;
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    const auto external = definitionCount == 0U
        ? std::span<const AutomapExitLabelDefinition>{}
        : std::span<const AutomapExitLabelDefinition>{
            definitions,
            definitionCount};
    const auto native = std::span(
        ExitDefinitions.data(),
        ExitDefinitionCount);
    if (EffectiveExitDefinitionCount(external, native)
            > MaximumAutomapExitLabels) {
        return false;
    }
    if (definitionCount != 0U) {
        std::copy_n(
            definitions,
            definitionCount,
            ExternalExitDefinitions.begin());
    }
    ExternalExitDefinitionCount = definitionCount;
    ++Revision;
    ClearProjectionLocked();
    ExitDefinitionsPublished.fetch_add(
        definitionCount,
        std::memory_order_relaxed);
    return true;
}

auto PublishNativeAutomapWaypointLabels(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        const AutomapWaypointLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (levelId <= 0 || definitionCount > 1U
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].levelId != levelId
                || definitions[index].subtileX < 0
                || definitions[index].subtileY < 0) {
            return false;
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    if (definitionCount == 0U) return true;
    const auto incoming = definitionCount == 0U
        ? std::span<const AutomapWaypointLabelDefinition>{}
        : std::span<const AutomapWaypointLabelDefinition>{
            definitions,
            definitionCount};
    if (!WaypointDefinitions.PublishNativeOwner(levelId, incoming)) {
        return false;
    }
    ++Revision;
    ClearProjectionLocked();
    return true;
}

auto PublishNativeAutomapLevelLabels(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        const AutomapLevelLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (levelId <= 0 || definitionCount > 1U
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].levelId != levelId
                || definitions[index].subtileX < 0
                || definitions[index].subtileY < 0) {
            return false;
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    if (definitionCount == 0U) return true;
    const auto incoming = definitionCount == 0U
        ? std::span<const AutomapLevelLabelDefinition>{}
        : std::span<const AutomapLevelLabelDefinition>{
            definitions,
            definitionCount};
    if (!LevelDefinitions.PublishNativeOwner(levelId, incoming)) return false;
    ++Revision;
    ClearProjectionLocked();
    return true;
}

auto ReplaceExternalAutomapWaypointLabels(
        std::uint64_t sessionGeneration,
        const AutomapWaypointLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (definitionCount > MaximumAutomapWaypointLabels
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].levelId <= 0
                || definitions[index].subtileX < 0
                || definitions[index].subtileY < 0) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (definitions[earlier].levelId == definitions[index].levelId) {
                return false;
            }
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    const auto incoming = definitionCount == 0U
        ? std::span<const AutomapWaypointLabelDefinition>{}
        : std::span<const AutomapWaypointLabelDefinition>{
            definitions,
            definitionCount};
    if (!WaypointDefinitions.ReplaceExternal(incoming)) return false;
    ++Revision;
    ClearProjectionLocked();
    return true;
}

auto ReplaceExternalAutomapLevelLabels(
        std::uint64_t sessionGeneration,
        const AutomapLevelLabelDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (definitionCount > MaximumAutomapLevelLabels
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].levelId <= 0
                || definitions[index].subtileX < 0
                || definitions[index].subtileY < 0) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (definitions[earlier].levelId == definitions[index].levelId) {
                return false;
            }
        }
    }
    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    const auto incoming = definitionCount == 0U
        ? std::span<const AutomapLevelLabelDefinition>{}
        : std::span<const AutomapLevelLabelDefinition>{
            definitions,
            definitionCount};
    if (!LevelDefinitions.ReplaceExternal(incoming)) return false;
    ++Revision;
    ClearProjectionLocked();
    return true;
}

auto PublishNativeAutomapSpecialChests(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        const AutomapSpecialChestDefinition* definitions,
        std::size_t definitionCount) noexcept -> bool {
    if (levelId <= 0 || definitionCount > SpecialChestPresets.size()
            || (definitionCount != 0U && definitions == nullptr)) {
        return false;
    }
    for (std::size_t index = 0U; index < definitionCount; ++index) {
        if (definitions[index].levelId != levelId
                || definitions[index].classId < 0
                || definitions[index].subtileX < 0
                || definitions[index].subtileY < 0) {
            return false;
        }
    }

    StateLockGuard lock(true);
    if (SessionGeneration != sessionGeneration) return false;
    std::size_t replacementCount{};
    for (std::size_t existing = 0U;
            existing < SpecialChestPresetCount;
            ++existing) {
        if (SpecialChestPresets[existing].definition.levelId == levelId) {
            continue;
        }
        if (replacementCount >= SpecialChestPresetScratch.size()) return false;
        SpecialChestPresetScratch[replacementCount++] =
            SpecialChestPresets[existing];
    }
    for (std::size_t incoming = 0U;
            incoming < definitionCount;
            ++incoming) {
        if (replacementCount >= SpecialChestPresetScratch.size()) return false;
        TrackedSpecialChestPreset retained{
            .definition = definitions[incoming],
        };
        for (std::size_t existing = 0U;
                existing < SpecialChestPresetCount;
                ++existing) {
            const auto& candidate = SpecialChestPresets[existing];
            if (candidate.definition.stableId
                    == definitions[incoming].stableId) {
                retained.consumed = candidate.consumed;
                retained.stateFlags = candidate.stateFlags;
                break;
            }
        }
        SpecialChestPresetScratch[replacementCount++] = retained;
    }
    if (replacementCount != 0U) {
        std::copy_n(
            SpecialChestPresetScratch.begin(),
            replacementCount,
            SpecialChestPresets.begin());
    }
    SpecialChestPresetCount = replacementCount;
    ++Revision;
    ClearProjectionLocked();
    SpecialChestDefinitionsPublished.fetch_add(
        definitionCount,
        std::memory_order_relaxed);
    return true;
}

[[nodiscard]] auto IsRenderedObjectUnitUnchecked(void* unit) noexcept -> bool {
    __try {
        return IsAlignedPointer(unit)
            && *reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(unit) + UnitTypeOffset)
                == UnitObject;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
}

void RecordRenderedNativeShrineUnchecked(void* unit) noexcept {
    __try {
        if (Catalog == nullptr
                || GetUnitId == nullptr || GetUnitClassId == nullptr
                || GetUnitMode == nullptr
                || GetUnitMode(unit) != ObjectModeNeutral) {
            return;
        }
        AutomapPoiKind kind{};
        if (!ClassifyObject(GetUnitClassId(unit), kind)
                || kind != AutomapPoiKind::ShrineIcon) {
            return;
        }
        const auto unitId = GetUnitId(unit);
        if (unitId == UINT32_MAX) return;
        const auto now = static_cast<std::uint64_t>(GetTickCount64());
        std::size_t oldestIndex{};
        auto oldestTick = (std::numeric_limits<std::uint64_t>::max)();
        for (std::size_t index = 0U;
                index < VisibleNativeShrineCount;
                ++index) {
            auto& visible = VisibleNativeShrines[index];
            if (visible.unitId == unitId) {
                visible.observedTick = now;
                return;
            }
            if (visible.observedTick < oldestTick) {
                oldestTick = visible.observedTick;
                oldestIndex = index;
            }
        }
        if (VisibleNativeShrineCount < VisibleNativeShrines.size()) {
            VisibleNativeShrines[VisibleNativeShrineCount++] = {
                .unitId = unitId,
                .observedTick = now,
            };
        } else {
            VisibleNativeShrines[oldestIndex] = {
                .unitId = unitId,
                .observedTick = now,
            };
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
    }
}

void ObserveNativeAutomapRenderedUnit(void* unit) noexcept {
    if (!Active.load(std::memory_order_acquire)
            || unit == nullptr
            || !HasCollection(
                CollectionMask.load(std::memory_order_acquire),
                AutomapPoiCollection::ShrineLabels)
            || !IsRenderedObjectUnitUnchecked(unit)) {
        return;
    }
    StateLockGuard lock(false);
    if (!lock) return;
    RecordRenderedNativeShrineUnchecked(unit);
}

void ObserveNativeAutomapPoiPass(
        const NativeAutomapPoiPass& pass) noexcept {
    if (!Active.load(std::memory_order_acquire)) return;
    const auto mask = CollectionMask.load(std::memory_order_acquire);
    if (mask == 0U || pass.currentLevelId <= 0
            || pass.nativeWidth <= 0 || pass.nativeWidth > 32768
            || pass.nativeHeight <= 0 || pass.nativeHeight > 32768
            || pass.clipWidth <= 0 || pass.clipHeight <= 0
            || pass.projectClient == nullptr
            || pass.borrowedAutomapContext == nullptr) {
        return;
    }
    StateLockGuard lock(false);
    if (!lock) {
        ContentionWaits.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    if (LevelId != pass.currentLevelId) {
        LevelId = pass.currentLevelId;
        ClearCurrentLevelLocked();
    }
    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    AutomapPulses.fetch_add(1U, std::memory_order_relaxed);
    constexpr auto ObjectCollections =
        AutomapPoiCollectionBit(AutomapPoiCollection::ShrineLabels)
        | AutomapPoiCollectionBit(AutomapPoiCollection::Chests)
        | AutomapPoiCollectionBit(AutomapPoiCollection::SuperChests)
        | AutomapPoiCollectionBit(AutomapPoiCollection::ArmorRacks)
        | AutomapPoiCollectionBit(AutomapPoiCollection::WeaponRacks);
    if ((mask & ObjectCollections) != 0U
            && (LastObjectTableScanTick == 0U
                || now < LastObjectTableScanTick
                || now - LastObjectTableScanTick
                    >= ObjectTableScanIntervalMilliseconds)) {
        ScanClientObjectTableLocked(mask);
        LastObjectTableScanTick = now;
    }

    std::size_t count{};
    const auto nativeLayers = AcquireNativeAutomapLayerCatalog();
    ProjectExitLabelsLocked(pass, mask, count, nativeLayers.get());
    ProjectWaypointLabelsLocked(pass, mask, count, nativeLayers.get());
    ProjectLevelLabelsLocked(pass, mask, count, nativeLayers.get());
    ProjectTrackedObjectsLocked(pass, mask, count);
    ProjectSpecialChestPresetsLocked(pass, mask, count);
    ProjectedSnapshotCount = count;
    LastProjectionTick = now;
    PublishedProjectionTick.store(now, std::memory_order_release);
    PublishedSnapshotCount.store(count, std::memory_order_release);
    Projected.fetch_add(count, std::memory_order_relaxed);
    if (DiagnosticsEnabled && DiagnosticContext != nullptr) {
        std::size_t visibleExits{};
        std::size_t visibleWaypoints{};
        std::size_t visibleLevels{};
        for (std::size_t index = 0U; index < count; ++index) {
            switch (ProjectedSnapshots[index].kind) {
                case AutomapPoiKind::ExitLabel:
                    ++visibleExits;
                    break;
                case AutomapPoiKind::WaypointLabel:
                    ++visibleWaypoints;
                    break;
                case AutomapPoiKind::LevelLabel:
                    ++visibleLevels;
                    break;
                default:
                    break;
            }
        }
        const auto changed = visibleExits != LastDiagnosticExitCount
            || visibleWaypoints != LastDiagnosticWaypointCount
            || visibleLevels != LastDiagnosticLevelCount;
        if (changed && (LastProjectionDiagnosticTick == 0U
                || now < LastProjectionDiagnosticTick
                || now - LastProjectionDiagnosticTick >= 100U)) {
            const auto effectiveExitCount = EffectiveExitDefinitionCount(
                std::span(
                    ExternalExitDefinitions.data(),
                    ExternalExitDefinitionCount),
                std::span(
                    ExitDefinitions.data(),
                    ExitDefinitionCount));
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense label projection: session=%llu level=%d definitions=exits/%zu-waypoints/%zu-levels/%zu visible=exits/%zu-waypoints/%zu-levels/%zu clip=%d,%d,%d,%d.",
                static_cast<unsigned long long>(SessionGeneration),
                pass.currentLevelId,
                effectiveExitCount,
                WaypointDefinitions.Definitions().size(),
                LevelDefinitions.Definitions().size(),
                visibleExits,
                visibleWaypoints,
                visibleLevels,
                pass.clipLeft,
                pass.clipTop,
                pass.clipWidth,
                pass.clipHeight);
            DiagnosticContext->LogInfo(message);
            LastDiagnosticExitCount = visibleExits;
            LastDiagnosticWaypointCount = visibleWaypoints;
            LastDiagnosticLevelCount = visibleLevels;
            LastProjectionDiagnosticTick = now;
        }
    }
}

auto AcquireNativeAutomapPoiSnapshots(
        std::vector<NativeAutomapPoiSnapshot>& snapshots,
        bool retainCurrentProjection) noexcept
        -> std::size_t {
    snapshots.clear();
    if (!Active.load(std::memory_order_acquire)
            || PublishedSnapshotCount.load(std::memory_order_acquire) == 0U) {
        return 0U;
    }
    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    if (!retainCurrentProjection && !IsRecent(
            PublishedProjectionTick.load(std::memory_order_acquire),
            now)) {
        return 0U;
    }
    try {
        StateLockGuard lock(false);
        if (!lock || ProjectedSnapshotCount == 0U
                || (!retainCurrentProjection
                    && !IsRecent(LastProjectionTick, now))) {
            return 0U;
        }
        snapshots.assign(
            ProjectedSnapshots.begin(),
            ProjectedSnapshots.begin()
                + static_cast<std::ptrdiff_t>(ProjectedSnapshotCount));
        return snapshots.size();
    } catch (...) {
        snapshots.clear();
        return 0U;
    }
}

auto WantsNativeAutomapPoiFrame(bool retainCurrentProjection) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
            || CollectionMask.load(std::memory_order_acquire) == 0U
            || PublishedSnapshotCount.load(std::memory_order_acquire) == 0U) {
        return false;
    }
    return retainCurrentProjection || IsRecent(
        PublishedProjectionTick.load(std::memory_order_acquire),
        static_cast<std::uint64_t>(GetTickCount64()));
}

auto GetNativeAutomapPoiCounters() noexcept -> NativeAutomapPoiCounters {
    return {
        .automapPulses = AutomapPulses.load(std::memory_order_relaxed),
        .objectTableScans = ObjectTableScans.load(std::memory_order_relaxed),
        .objectBucketsVisited = ObjectBucketsVisited.load(
            std::memory_order_relaxed),
        .objectUnitsObserved = ObjectUnitsObserved.load(
            std::memory_order_relaxed),
        .objectUnitsClassified = ObjectUnitsClassified.load(
            std::memory_order_relaxed),
        .objectTraversalLimits = ObjectTraversalLimits.load(
            std::memory_order_relaxed),
        .exitDefinitionsPublished = ExitDefinitionsPublished.load(
            std::memory_order_relaxed),
        .specialChestDefinitionsPublished =
            SpecialChestDefinitionsPublished.load(
                std::memory_order_relaxed),
        .projected = Projected.load(std::memory_order_relaxed),
        .projectionRejected = ProjectionRejected.load(
            std::memory_order_relaxed),
        .contentionWaits = ContentionWaits.load(std::memory_order_relaxed),
        .accessFaults = AccessFaults.load(std::memory_order_relaxed),
    };
}

} // namespace RuffnecKk::MapSense
