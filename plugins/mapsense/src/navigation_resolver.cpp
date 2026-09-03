#include "navigation_resolver.hpp"

#include "navigation_engine.hpp"
#include "navigation_level_catalog.hpp"
#include "navigation_policy.hpp"
#include "mapsense_data_catalog.hpp"
#include "native_automap_poi.hpp"
#include "reveal_engine.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t GetDrlgRoomFromActiveRoomRva = 0x192B20;
constexpr std::uintptr_t PlayerPathMaskWitnessRva = 0x2EF8F9;
constexpr std::uintptr_t GetCollisionGridRva = 0x2EFB30;
constexpr std::uintptr_t GetFirstUnitInRoomRva = 0x2EFD90;
constexpr std::uintptr_t IsRoomInTownRva = 0x2F0750;
constexpr std::uintptr_t DrlgRoomActiveRoomWitnessRva = 0x3289EE;
constexpr std::uintptr_t GetUnitClassIdRva = 0x349860;
constexpr std::uintptr_t GetUnitDataContextRva = 0x34A0E0;
constexpr std::uintptr_t GetUnitClientCoordXRva = 0x34AF60;
constexpr std::uintptr_t GetUnitClientCoordYRva = 0x34AFB0;
constexpr std::uintptr_t GetUnitRoomRva = 0x34B440;
constexpr std::uintptr_t GetNextUnitInRoomRva = 0x34B4A0;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t LevelListLayoutWitnessRva = 0x3267C0;
constexpr std::uintptr_t GetHoradricStaffTombLevelIdRva = 0x326A70;
constexpr std::uintptr_t GetQuestStateRva = 0x325C50;
constexpr std::uintptr_t ClientQuestRecordPointerRva = 0x2A48778;
constexpr std::uintptr_t ClientQuestRecordWitnessRva = 0x114C20;
constexpr std::uintptr_t GetDrlgRoomLevelIdRva = 0x360FC0;
constexpr std::uintptr_t GetLevelVisCoreRva = 0x360800;
constexpr std::uintptr_t GetLevelVisNodeWitnessRva = 0x36083A;
constexpr std::uintptr_t GetLevelVisDynamicWitnessRva = 0x360866;
constexpr std::uintptr_t GetLevelVisRva = 0x360880;
constexpr std::uintptr_t NearRoomLayoutWitnessRva = 0x3614C0;
constexpr std::uintptr_t NearRoomGeometryWitnessRva = 0x361530;
constexpr std::uintptr_t NearRoomSortWitnessRva = 0x3616A9;
constexpr std::uintptr_t GetObjectsTxtRecordCountRva = 0x38FC70;
constexpr std::uintptr_t GetObjectsTxtRecordRva = 0x38FD00;
constexpr std::uintptr_t GetLevelWarpRva = 0x3DAAD0;
constexpr std::uintptr_t GetLevelWarpNodeWitnessRva = 0x3DAB1D;
constexpr std::uintptr_t GetLevelWarpDynamicWitnessRva = 0x3DAB4E;
constexpr std::uintptr_t FindWaypointRoomAndCoordinatesRva = 0x3DAD90;
constexpr std::uintptr_t RoomTileLayoutWitnessRva = 0x3DA910;
constexpr std::uintptr_t RoomTileDestinationLayoutWitnessRva = 0x3DA9FB;
constexpr std::uintptr_t WaypointLayoutWitnessRva = 0x3DADEC;
constexpr std::uintptr_t OutdoorVisibilityFlagWitnessRva = 0x36177B;
constexpr std::uintptr_t OutdoorVisibilityGeometryWitnessRva = 0x3617B3;
constexpr std::uintptr_t CollisionGridLayoutWitnessRva = 0x36697B;
constexpr std::uintptr_t CollisionGridHeightWitnessRva = 0x3669E6;
constexpr std::uintptr_t MonasteryAnchorWitnessRva = 0x3FFB60;

constexpr std::size_t DrlgRoomRoomsNearOffset = 0x10;
constexpr std::size_t DrlgRoomRoomsNearCountOffset = 0x18;
constexpr std::size_t DrlgRoomNextOffset = 0x48;
constexpr std::size_t DrlgRoomVisibilityFlagsOffset = 0x50;
constexpr std::size_t DrlgRoomActiveRoomOffset = 0x58;
constexpr std::size_t DrlgRoomTileXOffset = 0x60;
constexpr std::size_t DrlgRoomTileYOffset = 0x64;
constexpr std::size_t DrlgRoomWidthOffset = 0x68;
constexpr std::size_t DrlgRoomHeightOffset = 0x6C;
constexpr std::size_t DrlgRoomRoomTileOffset = 0x78;
constexpr std::size_t DrlgRoomLevelOffset = 0x90;
constexpr std::size_t DrlgRoomPresetUnitOffset = 0x98;
constexpr std::size_t LevelFirstRoomOffset = 0x10;
constexpr std::size_t LevelPositionXOffset = 0x24;
constexpr std::size_t LevelPositionYOffset = 0x28;
constexpr std::size_t LevelNextOffset = 0x1B8;
constexpr std::size_t LevelDrlgOffset = 0x1C8;
constexpr std::size_t LevelIdOffset = 0x1F8;
constexpr std::size_t DrlgFirstLevelOffset = 0x868;
constexpr std::size_t PresetClassIdOffset = 0x04;
constexpr std::size_t PresetRelativeXOffset = 0x08;
constexpr std::size_t PresetNextOffset = 0x10;
constexpr std::size_t PresetUnitTypeOffset = 0x20;
constexpr std::size_t PresetRelativeYOffset = 0x24;
constexpr std::size_t RoomTileDestinationRoomOffset = 0x00;
constexpr std::size_t RoomTileNextOffset = 0x08;
constexpr std::size_t RoomTileLvlWarpOffset = 0x20;
constexpr std::size_t LvlWarpSourceIdOffset = 0x2C;
constexpr std::size_t ObjectsTxtSubClassOffset = 0x127;
constexpr std::size_t CollisionGridOriginXOffset = 0x00;
constexpr std::size_t CollisionGridOriginYOffset = 0x04;
constexpr std::size_t CollisionGridWidthOffset = 0x08;
constexpr std::size_t CollisionGridHeightOffset = 0x0C;
constexpr std::size_t CollisionGridCellsOffset = 0x20;

constexpr std::uint32_t PresetObject = 2U;
constexpr std::uint32_t PresetMonster = 1U;
constexpr std::uint32_t PresetLevelExit = 5U;
constexpr std::int32_t UnitObject = 2;
constexpr std::uint8_t ObjectsTxtWaypointSubClass = 0x40U;
constexpr std::int32_t SubtilesPerGameTile =
    Detail::NavigationSubtilesPerGameTile;
constexpr std::int32_t MaximumSupportedLevelId = 65'535;
constexpr std::int32_t TamoeHighlandLevelId = 7;
constexpr std::int32_t MonasteryGateLevelId = 26;
constexpr std::int32_t MonasteryAnchorOffsetTileX = 27;
constexpr std::int32_t MonasteryAnchorOffsetTileY = 13;
constexpr std::int32_t CanyonOfTheMagiLevelId = 46;
constexpr std::int32_t FirstTalRashaTombLevelId = 66;
constexpr std::int32_t LastTalRashaTombLevelId = 72;
constexpr std::int32_t ActTwoQuestSixStateFlag = 14;
constexpr std::int32_t QuestRewardGrantedFlag = 0;
constexpr std::size_t MaximumRoomsPerLevel = 4'096U;
constexpr std::size_t MaximumPresetsPerRoom = 8'192U;
constexpr std::size_t MaximumRoomTilesPerRoom = 1'024U;
constexpr std::size_t MaximumNearRoomsPerRoom = 64U;
constexpr std::size_t MaximumWaypointPresetsPerRoom = 64U;
constexpr std::size_t MaximumUnitsPerRoom = 16'384U;
constexpr std::size_t MaximumLevelsPerDrlg = 512U;
static_assert(MaximumAutomapWaypointLabels == MaximumLevelsPerDrlg);
constexpr std::size_t MaximumOutdoorBoundarySpans = 16'384U;
constexpr std::uint64_t MaximumCollisionCellsPerRoom = 16'777'216U;
constexpr std::size_t VisibilitySlotCount = 8U;
static_assert(
    sizeof(Detail::NavigationBoundarySpan)
        * MaximumOutdoorBoundarySpans * 2U
    <= 2U * 1'024U * 1'024U);

using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using GetDrlgRoomFromActiveRoomFn = void*(__fastcall*)(void*) noexcept;
using GetCollisionGridFn = void*(__fastcall*)(void*) noexcept;
using GetFirstUnitInRoomFn = void*(__fastcall*)(void*) noexcept;
using IsRoomInTownFn = std::int32_t(__fastcall*)(void* activeRoom) noexcept;
using GetUnitClassIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitDataContextFn = std::uint8_t(__fastcall*)(
    const void* unit) noexcept;
using GetUnitClientCoordFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitRoomFn = void*(__fastcall*)(void*) noexcept;
using GetNextUnitInRoomFn = void*(__fastcall*)(void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetDrlgRoomLevelIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetHoradricStaffTombLevelIdFn = std::int32_t(__fastcall*)(
    void*) noexcept;
using GetQuestStateFn = std::int32_t(__fastcall*)(
    void* questRecord,
    std::int32_t quest,
    std::int32_t state) noexcept;
using GetLevelVisFn = std::int32_t*(__fastcall*)(
    std::uint8_t dataContext,
    void* level) noexcept;
using GetLevelWarpFn = std::int32_t(__fastcall*)(
    std::uint8_t dataContext,
    void* level,
    std::uint8_t slot) noexcept;
using GetObjectsTxtRecordCountFn = std::int32_t(__fastcall*)(
    std::uint8_t dataContext) noexcept;
using GetObjectsTxtRecordFn = std::uint8_t*(__fastcall*)(
    std::uint8_t dataContext,
    std::int32_t recordId) noexcept;
using FindWaypointRoomAndCoordinatesFn = void*(__fastcall*)(
    std::uint8_t dataContext,
    void* level,
    std::int32_t* tileX,
    std::int32_t* tileY) noexcept;

struct CandidateBatch final {
    std::int32_t levelId{UnknownNavigationLevelId};
    bool inTown{};
    std::array<NavigationExitCandidate, MaximumNavigationDestinations> exits{};
    std::size_t exitCount{};
    std::array<
        Detail::NavigationExitSelection,
        MaximumNavigationDestinations> exitSelections{};
    std::size_t exitSelectionCount{};
    std::array<
        Detail::NavigationExitSelection,
        MaximumNavigationDestinations> exitLabelEvidence{};
    std::size_t exitLabelEvidenceCount{};
    std::array<
        AutomapSpecialChestDefinition,
        MaximumAutomapSpecialChestPresets> specialChestPresets{};
    std::size_t specialChestPresetCount{};
    NavigationPointCandidate waypoint{};
    bool hasWaypoint{};
    std::int32_t waypointNativeTileX{-1};
    std::int32_t waypointNativeTileY{-1};
    std::int32_t waypointExactSubtileX{-1};
    std::int32_t waypointExactSubtileY{-1};
    std::int32_t waypointExactClientX{};
    std::int32_t waypointExactClientY{};
    bool pendingWaypoint{};
    std::array<
        NavigationPointCandidate,
        MaximumNavigationDestinations> questTargets{};
    std::size_t questTargetCount{};
    NavigationPointCandidate correctTomb{};
    std::int32_t correctTombLevelId{UnknownNavigationLevelId};
    bool hasCorrectTomb{};
    bool hasCorrectTombState{};
    bool durielRewardGranted{};
    bool pendingCorrectTomb{};
    std::uint64_t roomCount{};
    std::uint64_t presetCount{};
    std::uint64_t rawRoomTileCount{};
    std::uint64_t nativeRoomTileCount{};
    std::uint64_t exactRoomTileCount{};
    std::uint64_t pendingRoomTileRoomCount{};
    std::uint64_t requestedTargetLevelCount{};
    std::uint64_t initializedTargetLevelCount{};
    std::uint64_t pendingTargetLevelCount{};
    std::uint64_t nearRoomLinkCount{};
    std::uint64_t outdoorOpeningCount{};
    std::uint64_t outdoorSourceSpanCount{};
    std::uint64_t outdoorTargetSpanCount{};
    std::uint64_t outdoorMergedSpanCount{};
    std::uint64_t ambiguousOutdoorTargetCount{};
    std::uint64_t pendingCollisionRoomCount{};
    std::uint64_t visibilitySlotCount{};
    std::uint64_t visibilityPairCount{};
    std::uint64_t pendingVisibilityTargetCount{};
    bool traversalLimited{};
};

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
GetDrlgRoomFromActiveRoomFn GetDrlgRoomFromActiveRoom{};
GetCollisionGridFn GetCollisionGrid{};
GetFirstUnitInRoomFn GetFirstUnitInRoom{};
IsRoomInTownFn IsRoomInTown{};
GetUnitClassIdFn GetUnitClassId{};
GetUnitDataContextFn GetUnitDataContext{};
GetUnitClientCoordFn GetUnitClientCoordX{};
GetUnitClientCoordFn GetUnitClientCoordY{};
GetUnitRoomFn GetUnitRoom{};
GetNextUnitInRoomFn GetNextUnitInRoom{};
GetUnitTypeFn GetUnitType{};
GetDrlgRoomLevelIdFn GetDrlgRoomLevelId{};
GetHoradricStaffTombLevelIdFn GetHoradricStaffTombLevelId{};
GetQuestStateFn GetQuestState{};
GetLevelVisFn GetLevelVis{};
GetLevelWarpFn GetLevelWarp{};
GetObjectsTxtRecordCountFn GetObjectsTxtRecordCount{};
GetObjectsTxtRecordFn GetObjectsTxtRecord{};
FindWaypointRoomAndCoordinatesFn FindWaypointRoomAndCoordinates{};
std::atomic_bool Active{};
std::atomic_bool Diagnostics{};
std::atomic<std::shared_ptr<const MapSenseDataCatalog>> ResolverCatalog{};
std::atomic_uint64_t Refreshes{};
std::atomic_uint64_t Rooms{};
std::atomic_uint64_t Presets{};
std::atomic_uint64_t Exits{};
std::atomic_uint64_t Waypoints{};
std::atomic_uint64_t Published{};
std::atomic_uint64_t UnresolvedNames{};
std::atomic_uint64_t Failures{};
std::atomic_uint64_t TraversalLimits{};
std::atomic_uint64_t PartialRefreshes{};
std::atomic_uint64_t VisibilitySlots{};
std::atomic_uint64_t VisibilityPairs{};
std::atomic_uint64_t PendingVisibilityTargets{};
std::atomic_uint64_t RevealedLevelsCaptured{};
std::atomic_uint64_t RevealedLabelsCaptured{};
std::atomic_uint64_t RevealedWaypointsCaptured{};
std::atomic_int32_t LastLevelId{UnknownNavigationLevelId};
std::atomic_uint32_t LastDestinationCount{};
std::atomic_int32_t LastWaypointX{};
std::atomic_int32_t LastWaypointY{};
std::atomic_int32_t LastProgressionX{};
std::atomic_int32_t LastProgressionY{};
std::atomic_uint64_t LastDiagnosticFingerprint{UINT64_MAX};
std::array<
    Detail::NavigationBoundarySpan,
    MaximumOutdoorBoundarySpans> OutdoorSourceSpanScratch{};
std::array<
    Detail::NavigationBoundarySpan,
    MaximumOutdoorBoundarySpans> OutdoorTargetSpanScratch{};
std::atomic_flag OutdoorSpanScratchInUse = ATOMIC_FLAG_INIT;

struct InitializedLevelCaptureRequest final {
    std::uint8_t dataContext{};
    void* level{};
};

struct RevealedWaypointResolution final {
    std::int32_t levelId{UnknownNavigationLevelId};
    AutomapWaypointLabelDefinition definition{};
    bool hasWaypoint{};
};

// InitLevel is reentrant: resolving an outdoor boundary can initialize a
// neighbouring Level before the outer observer has finished. Keep those
// pointers only for the duration of the outer hook invocation, then drain the
// complete bounded queue before returning control to D2R. The progressive
// scheduler stores LevelIds only between callbacks.
thread_local std::array<
    InitializedLevelCaptureRequest,
    MaximumLevelsPerDrlg> InitializedLevelCaptureQueue{};
thread_local std::size_t InitializedLevelCaptureQueueCount{};
thread_local bool InitializedLevelCaptureActive{};
thread_local bool InitializedLevelCaptureQueueOverflow{};

[[nodiscard]] auto QueueInitializedLevelCapture(
        std::uint8_t dataContext,
        void* level) noexcept -> bool {
    if (dataContext >= 8U || level == nullptr) return false;
    for (std::size_t index = 0U;
            index < InitializedLevelCaptureQueueCount;
            ++index) {
        const auto& queued = InitializedLevelCaptureQueue[index];
        if (queued.dataContext == dataContext && queued.level == level) {
            return true;
        }
    }
    if (InitializedLevelCaptureQueueCount
            >= InitializedLevelCaptureQueue.size()) {
        InitializedLevelCaptureQueueOverflow = true;
        return false;
    }
    InitializedLevelCaptureQueue[InitializedLevelCaptureQueueCount++] = {
        .dataContext = dataContext,
        .level = level,
    };
    return true;
}

class InitializedLevelCaptureScope final {
public:
    InitializedLevelCaptureScope() noexcept {
        InitializedLevelCaptureQueueCount = 0U;
        InitializedLevelCaptureQueueOverflow = false;
        InitializedLevelCaptureActive = true;
    }

    ~InitializedLevelCaptureScope() {
        InitializedLevelCaptureActive = false;
        InitializedLevelCaptureQueueCount = 0U;
        InitializedLevelCaptureQueueOverflow = false;
    }

    InitializedLevelCaptureScope(const InitializedLevelCaptureScope&) = delete;
    auto operator=(const InitializedLevelCaptureScope&)
        -> InitializedLevelCaptureScope& = delete;
};

class OutdoorSpanScratchLease final {
public:
    OutdoorSpanScratchLease() noexcept
        : acquired_(!OutdoorSpanScratchInUse.test_and_set(
            std::memory_order_acquire)) {}

    ~OutdoorSpanScratchLease() {
        if (acquired_) {
            OutdoorSpanScratchInUse.clear(std::memory_order_release);
        }
    }

    OutdoorSpanScratchLease(const OutdoorSpanScratchLease&) = delete;
    auto operator=(const OutdoorSpanScratchLease&)
        -> OutdoorSpanScratchLease& = delete;

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

[[nodiscard]] auto MakeDestinationId(
        std::uint32_t type,
        std::int32_t id,
        std::int32_t subtileX,
        std::int32_t subtileY) noexcept -> std::uint64_t {
    auto hash = UINT64_C(1469598103934665603);
    const std::array values{
        type,
        static_cast<std::uint32_t>(id),
        static_cast<std::uint32_t>(subtileX),
        static_cast<std::uint32_t>(subtileY),
    };
    for (const auto value : values) {
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            hash ^= static_cast<std::uint8_t>(value >> shift);
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

[[nodiscard]] auto IsPresetWithinRoom(
        std::int32_t roomWidth,
        std::int32_t roomHeight,
        std::int32_t relativeX,
        std::int32_t relativeY) noexcept -> bool {
    if (roomWidth <= 0 || roomHeight <= 0
        || relativeX < 0 || relativeY < 0) {
        return false;
    }
    const auto maximumX = static_cast<std::int64_t>(roomWidth)
        * SubtilesPerGameTile;
    const auto maximumY = static_cast<std::int64_t>(roomHeight)
        * SubtilesPerGameTile;
    return static_cast<std::int64_t>(relativeX) <= maximumX
        && static_cast<std::int64_t>(relativeY) <= maximumY;
}

[[nodiscard]] auto IsAlignedPointer(const void* pointer) noexcept -> bool;

[[nodiscard]] auto CheckedGameTileCoordinate(
        std::int32_t roomTile,
        std::int32_t relativeSubtile,
        std::int32_t& output) noexcept -> bool {
    if (relativeSubtile < 0) return false;
    const auto value = static_cast<std::int64_t>(roomTile)
        + relativeSubtile / SubtilesPerGameTile;
    if (value < 0
        || value > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    output = static_cast<std::int32_t>(value);
    return true;
}

[[nodiscard]] auto ReadRoomRectangleUnchecked(
        const std::uint8_t* room,
        Detail::NavigationRoomRectangle& rectangle) noexcept -> bool {
    if (!IsAlignedPointer(room)) return false;
    auto* const level = *reinterpret_cast<std::uint8_t* const*>(
        room + DrlgRoomLevelOffset);
    if (!IsAlignedPointer(level)) return false;
    if (GetDrlgRoomLevelId == nullptr) return false;
    const auto levelId = GetDrlgRoomLevelId(
        const_cast<std::uint8_t*>(room));
    const auto tileX = *reinterpret_cast<const std::int32_t*>(
        room + DrlgRoomTileXOffset);
    const auto tileY = *reinterpret_cast<const std::int32_t*>(
        room + DrlgRoomTileYOffset);
    const auto width = *reinterpret_cast<const std::int32_t*>(
        room + DrlgRoomWidthOffset);
    const auto height = *reinterpret_cast<const std::int32_t*>(
        room + DrlgRoomHeightOffset);
    if (levelId <= 0 || levelId > MaximumSupportedLevelId
        || tileX < 0 || tileY < 0 || width <= 0 || height <= 0) {
        return false;
    }
    const auto right = static_cast<std::int64_t>(tileX) + width;
    const auto bottom = static_cast<std::int64_t>(tileY) + height;
    if (right > (std::numeric_limits<std::int32_t>::max)()
        || bottom > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    rectangle = {
        .levelId = levelId,
        .tileX = tileX,
        .tileY = tileY,
        .width = width,
        .height = height,
    };
    return true;
}

enum class CollisionGridReadResult : std::uint8_t {
    Ready,
    Pending,
    Invalid,
};

// Full materialization is reserved for the local-player navigation pass. The
// reveal-wide POI passes may inspect only rooms D2R already made active; asking
// them to create rooms makes a large act scan retain far more map state.
enum class RoomMaterializationPolicy : std::uint8_t {
    Passive,
    CurrentLevel,
};

[[nodiscard]] auto ReadCollisionGridUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* room,
        const Detail::NavigationRoomRectangle& rectangle,
        RoomMaterializationPolicy materialization,
        Detail::NavigationCollisionGridView& output) noexcept
        -> CollisionGridReadResult {
    auto* activeRoom = *reinterpret_cast<std::uint8_t**>(
        room + DrlgRoomActiveRoomOffset);
    if (activeRoom == nullptr
        && materialization == RoomMaterializationPolicy::CurrentLevel) {
        activeRoom = static_cast<std::uint8_t*>(
            MaterializeClientRoom(dataContext, room));
    }
    if (activeRoom == nullptr) return CollisionGridReadResult::Pending;
    if (!IsAlignedPointer(activeRoom)
        || GetDrlgRoomFromActiveRoom(activeRoom) != room) {
        return CollisionGridReadResult::Invalid;
    }
    auto* const collision = static_cast<std::uint8_t*>(
        GetCollisionGrid(activeRoom));
    if (collision == nullptr) return CollisionGridReadResult::Pending;
    if (!IsAlignedPointer(collision)) {
        return CollisionGridReadResult::Invalid;
    }
    const auto originX = *reinterpret_cast<const std::int32_t*>(
        collision + CollisionGridOriginXOffset);
    const auto originY = *reinterpret_cast<const std::int32_t*>(
        collision + CollisionGridOriginYOffset);
    const auto width = *reinterpret_cast<const std::int32_t*>(
        collision + CollisionGridWidthOffset);
    const auto height = *reinterpret_cast<const std::int32_t*>(
        collision + CollisionGridHeightOffset);
    auto* const cells = *reinterpret_cast<const std::uint16_t* const*>(
        collision + CollisionGridCellsOffset);
    if (originX < 0 || originY < 0 || width < 2 || height < 2) {
        return CollisionGridReadResult::Invalid;
    }
    const auto cellCount = static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height);
    if (cellCount == 0U || cellCount > MaximumCollisionCellsPerRoom
        || cells == nullptr
        || (reinterpret_cast<std::uintptr_t>(cells)
            & (alignof(std::uint16_t) - 1U)) != 0U) {
        return CollisionGridReadResult::Invalid;
    }
    const auto expectedOriginX = static_cast<std::int64_t>(rectangle.tileX)
        * SubtilesPerGameTile;
    const auto expectedOriginY = static_cast<std::int64_t>(rectangle.tileY)
        * SubtilesPerGameTile;
    const auto expectedWidth = static_cast<std::int64_t>(rectangle.width)
        * SubtilesPerGameTile;
    const auto expectedHeight = static_cast<std::int64_t>(rectangle.height)
        * SubtilesPerGameTile;
    const auto right = static_cast<std::int64_t>(originX)
        + static_cast<std::int64_t>(width);
    const auto bottom = static_cast<std::int64_t>(originY)
        + static_cast<std::int64_t>(height);
    if (originX != expectedOriginX || originY != expectedOriginY
        || width != expectedWidth || height != expectedHeight
        || right > (std::numeric_limits<std::int32_t>::max)()
        || bottom > (std::numeric_limits<std::int32_t>::max)()) {
        return CollisionGridReadResult::Invalid;
    }
    output = {
        .originX = originX,
        .originY = originY,
        .width = width,
        .height = height,
        .cells = std::span(
            cells,
            static_cast<std::size_t>(cellCount)),
    };
    return CollisionGridReadResult::Ready;
}

[[nodiscard]] auto DetermineOutdoorBoundarySide(
        const Detail::NavigationRoomRectangle& source,
        const Detail::NavigationRoomRectangle& target,
        Detail::NavigationBoundarySide& side) noexcept -> bool {
    const auto sourceLeft = static_cast<std::int64_t>(source.tileX);
    const auto sourceTop = static_cast<std::int64_t>(source.tileY);
    const auto sourceRight = sourceLeft + source.width;
    const auto sourceBottom = sourceTop + source.height;
    const auto targetLeft = static_cast<std::int64_t>(target.tileX);
    const auto targetTop = static_cast<std::int64_t>(target.tileY);
    const auto targetRight = targetLeft + target.width;
    const auto targetBottom = targetTop + target.height;
    const auto verticalStart = sourceTop > targetTop ? sourceTop : targetTop;
    const auto verticalEnd = sourceBottom < targetBottom
        ? sourceBottom : targetBottom;
    const auto horizontalStart = sourceLeft > targetLeft
        ? sourceLeft : targetLeft;
    const auto horizontalEnd = sourceRight < targetRight
        ? sourceRight : targetRight;
    if (targetRight == sourceLeft && verticalEnd > verticalStart) {
        side = Detail::NavigationBoundarySide::Left;
        return true;
    }
    if (targetLeft == sourceRight && verticalEnd > verticalStart) {
        side = Detail::NavigationBoundarySide::Right;
        return true;
    }
    if (targetBottom == sourceTop && horizontalEnd > horizontalStart) {
        side = Detail::NavigationBoundarySide::Top;
        return true;
    }
    if (targetTop == sourceBottom && horizontalEnd > horizontalStart) {
        side = Detail::NavigationBoundarySide::Bottom;
        return true;
    }
    return false;
}

[[nodiscard]] auto DetermineOutdoorBoundaryFixedSubtile(
        const Detail::NavigationCollisionGridView& source,
        const Detail::NavigationCollisionGridView& target,
        Detail::NavigationBoundarySide side,
        std::int32_t& fixedSubtile) noexcept -> bool {
    const auto sourceRight = static_cast<std::int64_t>(source.originX)
        + source.width;
    const auto sourceBottom = static_cast<std::int64_t>(source.originY)
        + source.height;
    const auto targetRight = static_cast<std::int64_t>(target.originX)
        + target.width;
    const auto targetBottom = static_cast<std::int64_t>(target.originY)
        + target.height;
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (sourceRight > maximum || sourceBottom > maximum
        || targetRight > maximum || targetBottom > maximum) {
        return false;
    }

    std::int64_t fixed{};
    switch (side) {
        case Detail::NavigationBoundarySide::Left:
            if (targetRight != source.originX) return false;
            fixed = source.originX;
            break;
        case Detail::NavigationBoundarySide::Right:
            if (sourceRight != target.originX) return false;
            fixed = sourceRight;
            break;
        case Detail::NavigationBoundarySide::Top:
            if (targetBottom != source.originY) return false;
            fixed = source.originY;
            break;
        case Detail::NavigationBoundarySide::Bottom:
            if (sourceBottom != target.originY) return false;
            fixed = sourceBottom;
            break;
    }
    if (fixed < 0 || fixed > maximum) return false;
    fixedSubtile = static_cast<std::int32_t>(fixed);
    return true;
}

[[nodiscard]] auto UpdateLevelSubtileBounds(
        const Detail::NavigationCollisionGridView& grid,
        Detail::NavigationLevelSubtileBounds& bounds,
        bool& initialized) noexcept -> bool {
    const auto right = static_cast<std::int64_t>(grid.originX) + grid.width;
    const auto bottom = static_cast<std::int64_t>(grid.originY) + grid.height;
    if (grid.originX < 0 || grid.originY < 0
        || grid.width < 2 || grid.height < 2
        || right > (std::numeric_limits<std::int32_t>::max)()
        || bottom > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    if (!initialized) {
        bounds = {
            .left = grid.originX,
            .top = grid.originY,
            .right = static_cast<std::int32_t>(right),
            .bottom = static_cast<std::int32_t>(bottom),
        };
        initialized = true;
        return true;
    }
    if (grid.originX < bounds.left) bounds.left = grid.originX;
    if (grid.originY < bounds.top) bounds.top = grid.originY;
    if (right > bounds.right) bounds.right = static_cast<std::int32_t>(right);
    if (bottom > bounds.bottom) {
        bounds.bottom = static_cast<std::int32_t>(bottom);
    }
    return true;
}

[[nodiscard]] auto AppendCollisionBoundaryRuns(
        const Detail::NavigationCollisionGridView& grid,
        Detail::NavigationBoundarySide side,
        bool sourceSide,
        std::int32_t targetLevelId,
        std::int32_t fixedSubtile,
        std::uintptr_t sourceRoomIdentity,
        std::uintptr_t targetRoomIdentity,
        std::span<Detail::NavigationBoundarySpan> spans,
        std::size_t& count) noexcept -> bool {
    if (targetLevelId <= 0 || fixedSubtile < 0
        || sourceRoomIdentity == 0U || targetRoomIdentity == 0U
        || grid.width < 2 || grid.height < 2) {
        return false;
    }
    const auto cell = [&grid](std::int32_t x, std::int32_t y) noexcept {
        return grid.cells[
            static_cast<std::size_t>(y)
                * static_cast<std::size_t>(grid.width)
            + static_cast<std::size_t>(x)];
    };
    const auto runLength = side == Detail::NavigationBoundarySide::Left
            || side == Detail::NavigationBoundarySide::Right
        ? grid.height : grid.width;
    const auto coordinateOrigin = side == Detail::NavigationBoundarySide::Left
            || side == Detail::NavigationBoundarySide::Right
        ? grid.originY : grid.originX;
    std::int32_t coordinateEnd{};
    if (!Detail::TryAddNavigationSubtileOffset(
            coordinateOrigin,
            runLength,
            coordinateEnd)) {
        return false;
    }
    const auto openAt = [&cell, &grid, side, sourceSide](
            std::int32_t position) noexcept {
        switch (side) {
            case Detail::NavigationBoundarySide::Left:
                if (sourceSide) {
                    return Detail::IsNavigationPlayerPathOpen(
                               cell(0, position))
                        && Detail::IsNavigationPlayerPathOpen(
                            cell(1, position));
                }
                return Detail::IsNavigationPlayerPathOpen(
                           cell(grid.width - 1, position))
                    && Detail::IsNavigationPlayerPathOpen(
                        cell(grid.width - 2, position));
            case Detail::NavigationBoundarySide::Right:
                if (sourceSide) {
                    return Detail::IsNavigationPlayerPathOpen(
                               cell(grid.width - 1, position))
                        && Detail::IsNavigationPlayerPathOpen(
                            cell(grid.width - 2, position));
                }
                return Detail::IsNavigationPlayerPathOpen(
                           cell(0, position))
                    && Detail::IsNavigationPlayerPathOpen(
                        cell(1, position));
            case Detail::NavigationBoundarySide::Top:
                if (sourceSide) {
                    return Detail::IsNavigationPlayerPathOpen(
                               cell(position, 0))
                        && Detail::IsNavigationPlayerPathOpen(
                            cell(position, 1));
                }
                return Detail::IsNavigationPlayerPathOpen(
                           cell(position, grid.height - 1))
                    && Detail::IsNavigationPlayerPathOpen(
                        cell(position, grid.height - 2));
            case Detail::NavigationBoundarySide::Bottom:
                if (sourceSide) {
                    return Detail::IsNavigationPlayerPathOpen(
                               cell(position, grid.height - 1))
                        && Detail::IsNavigationPlayerPathOpen(
                            cell(position, grid.height - 2));
                }
                return Detail::IsNavigationPlayerPathOpen(
                           cell(position, 0))
                    && Detail::IsNavigationPlayerPathOpen(
                        cell(position, 1));
        }
        return false;
    };
    const auto append = [targetLevelId,
            side,
            fixedSubtile,
            sourceRoomIdentity,
            targetRoomIdentity,
            &spans,
            &count](
            std::int32_t start,
            std::int32_t end) noexcept {
        if (start < 0 || end <= start || count >= spans.size()) return false;
        spans[count++] = {
            .targetLevelId = targetLevelId,
            .side = side,
            .startSubtile = start,
            .endSubtile = end,
            .fixedSubtile = fixedSubtile,
            .sourceRoomIdentity = sourceRoomIdentity,
            .targetRoomIdentity = targetRoomIdentity,
        };
        return true;
    };

    std::int32_t runStartPosition{-1};
    for (std::int32_t position = 0; position < runLength; ++position) {
        if (openAt(position)) {
            if (runStartPosition < 0) runStartPosition = position;
            continue;
        }
        if (runStartPosition >= 0) {
            std::int32_t absoluteStart{};
            std::int32_t absoluteEnd{};
            if (!Detail::TryAddNavigationSubtileOffset(
                    coordinateOrigin,
                    runStartPosition,
                    absoluteStart)
                || !Detail::TryAddNavigationSubtileOffset(
                    coordinateOrigin,
                    position,
                    absoluteEnd)
                || !append(absoluteStart, absoluteEnd)) {
                return false;
            }
        }
        runStartPosition = -1;
    }
    if (runStartPosition < 0) return true;
    std::int32_t absoluteStart{};
    return Detail::TryAddNavigationSubtileOffset(
            coordinateOrigin,
            runStartPosition,
            absoluteStart)
        && append(absoluteStart, coordinateEnd);
}

void UpsertExitCandidate(
        CandidateBatch& batch,
        Detail::NavigationExitSelection selection) noexcept {
    if (!Detail::UpsertExitSelection(
            batch.levelId,
            batch.exitSelections,
            batch.exitSelectionCount,
            selection)) {
        batch.traversalLimited = batch.exitSelectionCount
            >= batch.exitSelections.size();
    }
    if (!Detail::AppendPhysicalExitEvidence(
            batch.levelId,
            batch.exitLabelEvidence,
            batch.exitLabelEvidenceCount,
            selection)) {
        batch.traversalLimited = batch.traversalLimited
            || batch.exitLabelEvidenceCount
                >= batch.exitLabelEvidence.size();
    }
}

void AppendExitLabelEvidence(
        CandidateBatch& batch,
        Detail::NavigationExitSelection selection) noexcept {
    if (!Detail::AppendPhysicalExitEvidence(
            batch.levelId,
            batch.exitLabelEvidence,
            batch.exitLabelEvidenceCount,
            selection)) {
        batch.traversalLimited = batch.traversalLimited
            || batch.exitLabelEvidenceCount
                >= batch.exitLabelEvidence.size();
    }
}

void AppendQuestTarget(
        CandidateBatch& batch,
        NavigationPointCandidate candidate) noexcept {
    if (candidate.subtileX < 0 || candidate.subtileY < 0) {
        return;
    }
    for (std::size_t index = 0U;
            index < batch.questTargetCount;
            ++index) {
        const auto& existing = batch.questTargets[index];
        if (existing.destinationId == candidate.destinationId
            && existing.subtileX == candidate.subtileX
            && existing.subtileY == candidate.subtileY) {
            return;
        }
    }
    if (batch.questTargetCount >= batch.questTargets.size()) {
        batch.traversalLimited = true;
        return;
    }
    batch.questTargets[batch.questTargetCount++] = candidate;
}

void AppendSpecialChestPreset(
        CandidateBatch& batch,
        std::int32_t classId,
        std::int32_t subtileX,
        std::int32_t subtileY) noexcept {
    if (classId < 0 || subtileX < 0 || subtileY < 0) return;
    const auto stableId = MakeDestinationId(
        PresetObject,
        classId,
        subtileX,
        subtileY);
    for (std::size_t index = 0U;
            index < batch.specialChestPresetCount;
            ++index) {
        const auto& existing = batch.specialChestPresets[index];
        if (existing.stableId == stableId
                || (existing.classId == classId
                    && existing.subtileX == subtileX
                    && existing.subtileY == subtileY)) {
            return;
        }
    }
    if (batch.specialChestPresetCount
            >= batch.specialChestPresets.size()) {
        batch.traversalLimited = true;
        return;
    }
    batch.specialChestPresets[batch.specialChestPresetCount++] = {
        .stableId = stableId,
        .levelId = batch.levelId,
        .classId = classId,
        .subtileX = subtileX,
        .subtileY = subtileY,
    };
}

[[nodiscard]] auto ResolveNativeMonasteryAnchorUnchecked(
        std::uint8_t* sourceLevel,
        std::uint8_t* targetLevel,
        std::int32_t currentLevelId,
        std::int32_t targetLevelId,
        CandidateBatch& batch) noexcept -> bool {
    const auto forward = currentLevelId == TamoeHighlandLevelId
        && targetLevelId == MonasteryGateLevelId;
    const auto reverse = currentLevelId == MonasteryGateLevelId
        && targetLevelId == TamoeHighlandLevelId;
    auto* const monasteryLevel = forward ? targetLevel : sourceLevel;
    if ((!forward && !reverse) || !IsAlignedPointer(monasteryLevel)
        || *reinterpret_cast<const std::int32_t*>(
            monasteryLevel + LevelIdOffset) != MonasteryGateLevelId) {
        return false;
    }
    const auto levelTileX = *reinterpret_cast<const std::int32_t*>(
        monasteryLevel + LevelPositionXOffset);
    const auto levelTileY = *reinterpret_cast<const std::int32_t*>(
        monasteryLevel + LevelPositionYOffset);
    Detail::NavigationOutdoorOpening anchor{};
    if (!Detail::TryMakeNavigationLevelTileAnchor(
            levelTileX,
            levelTileY,
            MonasteryAnchorOffsetTileX,
            MonasteryAnchorOffsetTileY,
            anchor)) {
        return false;
    }
    ++batch.outdoorOpeningCount;
    UpsertExitCandidate(
        batch,
        Detail::NavigationExitSelection{
            .candidate = NavigationExitCandidate{
                .destinationId = MakeDestinationId(
                    PresetLevelExit,
                    targetLevelId,
                    anchor.subtileX,
                    anchor.subtileY),
                .targetLevelId = targetLevelId,
                .subtileX = anchor.subtileX,
                .subtileY = anchor.subtileY,
            },
            .evidence = Detail::NavigationExitEvidence::OutdoorLevelBoundary,
            .spanSubtiles = 0,
            .canonicalLevelPairAnchor = true,
        });
    return !batch.traversalLimited;
}

[[nodiscard]] auto ReadSourceLevelBoundsUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* sourceLevel,
        std::int32_t currentLevelId,
        RoomMaterializationPolicy materialization,
        CandidateBatch& batch,
        Detail::NavigationLevelSubtileBounds& bounds,
        bool& ready) noexcept -> bool {
    ready = false;
    bool initialized{};
    bool pending{};
    auto* room = *reinterpret_cast<std::uint8_t**>(
        sourceLevel + LevelFirstRoomOffset);
    std::size_t roomCount{};
    while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
        if (!IsAlignedPointer(room)
            || *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomLevelOffset) != sourceLevel) {
            return false;
        }
        Detail::NavigationRoomRectangle rectangle{};
        if (!ReadRoomRectangleUnchecked(room, rectangle)
            || rectangle.levelId != currentLevelId) {
            return false;
        }
        Detail::NavigationCollisionGridView grid{};
        const auto readResult = ReadCollisionGridUnchecked(
            dataContext,
            room,
            rectangle,
            materialization,
            grid);
        if (readResult == CollisionGridReadResult::Invalid) return false;
        if (readResult == CollisionGridReadResult::Pending) {
            ++batch.pendingCollisionRoomCount;
            pending = true;
        } else if (!UpdateLevelSubtileBounds(grid, bounds, initialized)) {
            return false;
        }
        room = *reinterpret_cast<std::uint8_t**>(
            room + DrlgRoomNextOffset);
        ++roomCount;
    }
    if (room != nullptr) {
        batch.traversalLimited = true;
        return false;
    }
    if (pending) return true;
    ready = initialized;
    return initialized;
}

[[nodiscard]] auto CollectOutdoorBoundaryPairSpansUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* sourceLevel,
        std::uint8_t* targetLevel,
        std::int32_t currentLevelId,
        std::int32_t targetLevelId,
        std::uint8_t sourceSlot,
        std::uint8_t reciprocalSlot,
        RoomMaterializationPolicy materialization,
        std::span<Detail::NavigationBoundarySpan> sourceSpans,
        std::size_t& sourceSpanCount,
        std::span<Detail::NavigationBoundarySpan> targetSpans,
        std::size_t& targetSpanCount,
        CandidateBatch& batch,
        bool& ready) noexcept -> bool {
    ready = false;
    sourceSpanCount = 0U;
    targetSpanCount = 0U;
    if (sourceSlot >= VisibilitySlotCount
        || reciprocalSlot >= VisibilitySlotCount) {
        return false;
    }
    bool pending{};
    auto* room = *reinterpret_cast<std::uint8_t**>(
        sourceLevel + LevelFirstRoomOffset);
    std::size_t roomCount{};
    while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
        Detail::NavigationRoomRectangle sourceRectangle{};
        if (!IsAlignedPointer(room)
            || *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomLevelOffset) != sourceLevel
            || !ReadRoomRectangleUnchecked(room, sourceRectangle)
            || sourceRectangle.levelId != currentLevelId) {
            return false;
        }
        const auto sourceVisibilityFlags =
            *reinterpret_cast<const std::uint32_t*>(
                room + DrlgRoomVisibilityFlagsOffset);
        if (!Detail::HasNavigationOutdoorVisibilitySlot(
                sourceVisibilityFlags,
                sourceSlot)) {
            room = *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomNextOffset);
            ++roomCount;
            continue;
        }
        const auto nearCount = *reinterpret_cast<const std::size_t*>(
            room + DrlgRoomRoomsNearCountOffset);
        if (nearCount > MaximumNearRoomsPerRoom) return false;
        batch.nearRoomLinkCount += nearCount;
        auto** const nearRooms = *reinterpret_cast<std::uint8_t***>(
            room + DrlgRoomRoomsNearOffset);
        if (nearCount != 0U && !IsAlignedPointer(nearRooms)) return false;
        for (std::size_t index = 0U; index < nearCount; ++index) {
            auto* const targetRoom = nearRooms[index];
            if (!IsAlignedPointer(targetRoom)) return false;
            Detail::NavigationRoomRectangle targetRectangle{};
            if (!ReadRoomRectangleUnchecked(targetRoom, targetRectangle)) {
                return false;
            }
            if (targetRectangle.levelId != targetLevelId) continue;
            if (*reinterpret_cast<std::uint8_t**>(
                    targetRoom + DrlgRoomLevelOffset) != targetLevel) {
                return false;
            }
            const auto targetVisibilityFlags =
                *reinterpret_cast<const std::uint32_t*>(
                    targetRoom + DrlgRoomVisibilityFlagsOffset);
            if (!Detail::HasNavigationOutdoorVisibilitySlot(
                    targetVisibilityFlags,
                    reciprocalSlot)) {
                continue;
            }
            Detail::NavigationBoundarySide side{};
            if (!DetermineOutdoorBoundarySide(
                    sourceRectangle,
                    targetRectangle,
                    side)) {
                continue;
            }
            Detail::NavigationCollisionGridView sourceGrid{};
            Detail::NavigationCollisionGridView targetGrid{};
            const auto sourceReadResult = ReadCollisionGridUnchecked(
                dataContext,
                room,
                sourceRectangle,
                materialization,
                sourceGrid);
            if (sourceReadResult == CollisionGridReadResult::Invalid) {
                return false;
            }
            const auto targetReadResult = ReadCollisionGridUnchecked(
                dataContext,
                targetRoom,
                targetRectangle,
                materialization,
                targetGrid);
            if (targetReadResult == CollisionGridReadResult::Invalid) {
                return false;
            }
            if (sourceReadResult == CollisionGridReadResult::Pending
                || targetReadResult == CollisionGridReadResult::Pending) {
                if (sourceReadResult == CollisionGridReadResult::Pending) {
                    ++batch.pendingCollisionRoomCount;
                }
                if (targetReadResult == CollisionGridReadResult::Pending) {
                    ++batch.pendingCollisionRoomCount;
                }
                pending = true;
                continue;
            }
            std::int32_t fixedSubtile{};
            if (!DetermineOutdoorBoundaryFixedSubtile(
                    sourceGrid,
                    targetGrid,
                    side,
                    fixedSubtile)) {
                return false;
            }
            const auto sourceRoomIdentity = reinterpret_cast<std::uintptr_t>(
                room);
            const auto targetRoomIdentity = reinterpret_cast<std::uintptr_t>(
                targetRoom);
            if (!AppendCollisionBoundaryRuns(
                    sourceGrid,
                    side,
                    true,
                    currentLevelId,
                    fixedSubtile,
                    sourceRoomIdentity,
                    targetRoomIdentity,
                    sourceSpans,
                    sourceSpanCount)
                || !AppendCollisionBoundaryRuns(
                    targetGrid,
                    side,
                    false,
                    targetLevelId,
                    fixedSubtile,
                    sourceRoomIdentity,
                    targetRoomIdentity,
                    targetSpans,
                    targetSpanCount)) {
                batch.traversalLimited = true;
                return false;
            }
        }
        room = *reinterpret_cast<std::uint8_t**>(
            room + DrlgRoomNextOffset);
        ++roomCount;
    }
    if (room != nullptr) {
        batch.traversalLimited = true;
        return false;
    }
    if (pending) return true;
    ready = true;
    return true;
}

[[nodiscard]] auto ResolveOutdoorLevelBoundaryUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* sourceLevel,
        std::uint8_t* targetLevel,
        std::int32_t currentLevelId,
        std::int32_t targetLevelId,
        std::uint8_t sourceSlot,
        std::uint8_t reciprocalSlot,
        RoomMaterializationPolicy materialization,
        CandidateBatch& batch) noexcept -> bool {
    if ((currentLevelId == TamoeHighlandLevelId
            && targetLevelId == MonasteryGateLevelId)
        || (currentLevelId == MonasteryGateLevelId
            && targetLevelId == TamoeHighlandLevelId)) {
        return ResolveNativeMonasteryAnchorUnchecked(
            sourceLevel,
            targetLevel,
            currentLevelId,
            targetLevelId,
            batch);
    }
    auto sourceSpans = std::span(OutdoorSourceSpanScratch);
    auto targetSpans = std::span(OutdoorTargetSpanScratch);

    Detail::NavigationLevelSubtileBounds sourceBounds{};
    bool ready{};
    if (!ReadSourceLevelBoundsUnchecked(
            dataContext,
            sourceLevel,
            currentLevelId,
            materialization,
            batch,
            sourceBounds,
            ready)) {
        return false;
    }
    if (!ready) return true;

    std::size_t sourceSpanCount{};
    std::size_t targetSpanCount{};
    if (!CollectOutdoorBoundaryPairSpansUnchecked(
            dataContext,
            sourceLevel,
            targetLevel,
            currentLevelId,
            targetLevelId,
            sourceSlot,
            reciprocalSlot,
            materialization,
            sourceSpans,
            sourceSpanCount,
            targetSpans,
            targetSpanCount,
            batch,
            ready)) {
        return false;
    }
    if (!ready) return true;

    batch.outdoorSourceSpanCount += sourceSpanCount;
    batch.outdoorTargetSpanCount += targetSpanCount;
    std::size_t mergedSourceSpanCount{};
    std::size_t mergedTargetSpanCount{};
    if (!Detail::MergeOutdoorBoundarySpans(
            sourceSpans,
            sourceSpanCount,
            mergedSourceSpanCount)
        || !Detail::MergeOutdoorBoundarySpans(
            targetSpans,
            targetSpanCount,
            mergedTargetSpanCount)) {
        return false;
    }
    batch.outdoorMergedSpanCount += mergedSourceSpanCount
        + mergedTargetSpanCount;

    Detail::NavigationOutdoorOpening opening{};
    std::array<
        Detail::NavigationOutdoorOpening,
        MaximumNavigationDestinations> physicalOpenings{};
    std::size_t physicalOpeningCount{};
    const auto matchResult = Detail::FindUniqueOutdoorLevelBoundaryOpening(
        sourceBounds,
        targetLevelId,
        sourceSpans.first(mergedSourceSpanCount),
        targetSpans.first(mergedTargetSpanCount),
        opening,
        Detail::NavigationOutdoorOpeningSelectionPolicy::
            AcceptStablePlayerPath,
        physicalOpenings,
        &physicalOpeningCount);
    if (matchResult
            == Detail::NavigationOutdoorBoundaryMatchResult::Invalid) {
        return false;
    }
    if (matchResult
            == Detail::NavigationOutdoorBoundaryMatchResult::Ambiguous) {
        ++batch.ambiguousOutdoorTargetCount;
        return true;
    }
    if (matchResult
            == Detail::NavigationOutdoorBoundaryMatchResult::NotFound) {
        return true;
    }

    ++batch.outdoorOpeningCount;
    UpsertExitCandidate(
        batch,
        Detail::NavigationExitSelection{
            .candidate = NavigationExitCandidate{
                .destinationId = MakeDestinationId(
                    PresetLevelExit,
                    targetLevelId,
                    opening.subtileX,
                    opening.subtileY),
                .targetLevelId = targetLevelId,
                .subtileX = opening.subtileX,
                .subtileY = opening.subtileY,
            },
            .evidence = Detail::NavigationExitEvidence::
                OutdoorLevelBoundary,
            .spanSubtiles = opening.spanSubtiles,
            .boundaryIdentity = opening.boundaryIdentity,
        });
    for (std::size_t index = 0U;
            index < physicalOpeningCount;
            ++index) {
        const auto& physical = physicalOpenings[index];
        AppendExitLabelEvidence(
            batch,
            Detail::NavigationExitSelection{
                .candidate = NavigationExitCandidate{
                    .destinationId = MakeDestinationId(
                        PresetLevelExit,
                        targetLevelId,
                        physical.subtileX,
                        physical.subtileY),
                    .targetLevelId = targetLevelId,
                    .subtileX = physical.subtileX,
                    .subtileY = physical.subtileY,
                },
                .evidence = Detail::NavigationExitEvidence::
                    OutdoorLevelBoundary,
                .spanSubtiles = physical.spanSubtiles,
                .boundaryIdentity = physical.boundaryIdentity,
            });
    }
    return !batch.traversalLimited;
}

[[nodiscard]] auto ResolveDynamicProgressionObjectsUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* activeRoom,
        std::int32_t currentLevelId,
        CandidateBatch& batch) noexcept -> bool {
    if (!HasDynamicMainProgressionTargetFor(currentLevelId)) return true;
    if (!IsAlignedPointer(activeRoom)) return false;

    auto* unit = static_cast<std::uint8_t*>(
        GetFirstUnitInRoom(activeRoom));
    std::size_t unitCount{};
    while (unit != nullptr && unitCount < MaximumUnitsPerRoom) {
        if (!IsAlignedPointer(unit)) return false;
        auto* const nextUnit = static_cast<std::uint8_t*>(
            GetNextUnitInRoom(unit));
        if (nextUnit == unit) {
            batch.traversalLimited = true;
            return false;
        }

        if (GetUnitType(unit) == UnitObject) {
            const auto classId = GetUnitClassId(unit);
            const auto targetLevelId = DynamicMainProgressionTargetFor(
                currentLevelId,
                classId);
            if (targetLevelId.has_value()
                && GetUnitDataContext(unit) == dataContext
                && GetUnitRoom(unit) == activeRoom) {
                const auto exactClientX = GetUnitClientCoordX(unit);
                const auto exactClientY = GetUnitClientCoordY(unit);
                NavigationNativePoint subtile{};
                if (!ConvertNavigationClientToSubtileCoordinates(
                        exactClientX,
                        exactClientY,
                        subtile)) {
                    return false;
                }
                UpsertExitCandidate(
                    batch,
                    Detail::NavigationExitSelection{
                        .candidate = NavigationExitCandidate{
                            .destinationId = MakeDestinationId(
                                static_cast<std::uint32_t>(UnitObject),
                                classId,
                                subtile.x,
                                subtile.y),
                            .targetLevelId = *targetLevelId,
                            .subtileX = subtile.x,
                            .subtileY = subtile.y,
                            .exactClientX = exactClientX,
                            .exactClientY = exactClientY,
                            .useExactClientCoordinates = true,
                        },
                        .evidence =
                            Detail::NavigationExitEvidence::RuntimeObject,
                    });
                if (batch.traversalLimited) return false;
            }
        }

        unit = nextUnit;
        ++unitCount;
    }
    if (unit != nullptr) {
        batch.traversalLimited = true;
        return false;
    }
    return true;
}

void FinalizeExitCandidates(CandidateBatch& batch) noexcept {
    batch.exitCount = 0U;
    for (std::size_t index = 0U;
         index < batch.exitSelectionCount
         && batch.exitCount < batch.exits.size();
         ++index) {
        const auto& selection = batch.exitSelections[index];
        if (selection.candidate.targetLevelId <= 0
            || selection.candidate.targetLevelId == batch.levelId
            || selection.candidate.subtileX < 0
            || selection.candidate.subtileY < 0) {
            continue;
        }
        batch.exits[batch.exitCount++] = selection.candidate;
    }
}

[[nodiscard]] auto IsAlignedPointer(const void* pointer) noexcept -> bool {
    return pointer != nullptr
        && (reinterpret_cast<std::uintptr_t>(pointer)
            & (alignof(void*) - 1U)) == 0U;
}

// Floyd's algorithm detects malformed cycles without allocations. Returning
// true also covers a chain that exceeds its safety ceiling; both conditions
// invalidate the complete publication instead of producing partial lines.
[[nodiscard]] auto HasCycleOrExceedsLimitUnchecked(
        std::uint8_t* head,
        std::size_t nextOffset,
        std::size_t maximumNodes) noexcept -> bool {
    auto* slow = head;
    auto* fast = head;
    std::size_t steps{};
    while (fast != nullptr && steps < maximumNodes) {
        if (!IsAlignedPointer(fast)) return true;
        fast = *reinterpret_cast<std::uint8_t**>(fast + nextOffset);
        if (fast != nullptr) {
            if (!IsAlignedPointer(fast)) return true;
            fast = *reinterpret_cast<std::uint8_t**>(fast + nextOffset);
        }
        if (slow != nullptr) {
            if (!IsAlignedPointer(slow)) return true;
            slow = *reinterpret_cast<std::uint8_t**>(slow + nextOffset);
        }
        ++steps;
        if (slow != nullptr && slow == fast) return true;
    }
    return fast != nullptr;
}

struct StaticPoiRoomLeaseSet final {
    std::array<StaticClientRoomLease, MaximumRoomsPerLevel> leases{};
    std::size_t count{};
};

[[nodiscard]] auto PrepareStaticPoiRoomsUnchecked(
        std::uint8_t dataContext,
        void* level,
        StaticPoiRoomLeaseSet& leases) noexcept -> bool {
    leases = {};
    __try {
        if (dataContext >= 8U || !IsAlignedPointer(level)) return false;
        auto* const sourceLevel = static_cast<std::uint8_t*>(level);
        auto* room = *reinterpret_cast<std::uint8_t**>(
            sourceLevel + LevelFirstRoomOffset);
        if (room == nullptr
            || HasCycleOrExceedsLimitUnchecked(
                room,
                DrlgRoomNextOffset,
                MaximumRoomsPerLevel)) {
            return false;
        }

        bool complete = true;
        std::size_t roomCount{};
        while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
            if (!IsAlignedPointer(room)
                || *reinterpret_cast<std::uint8_t**>(
                    room + DrlgRoomLevelOffset) != sourceLevel) {
                return false;
            }
            const auto flags = *reinterpret_cast<const std::uint32_t*>(
                room + DrlgRoomVisibilityFlagsOffset);
            auto selectionFlags = flags;
            const auto levelId = *reinterpret_cast<const std::int32_t*>(
                sourceLevel + LevelIdOffset);
            if (!Detail::AllowsWaypointLabelForLevel(levelId)) {
                selectionFlags &= ~StaticPoiRoomWaypointMask;
            }
            const auto action = SelectStaticPoiRoomAction(
                selectionFlags,
                *reinterpret_cast<void**>(
                    room + DrlgRoomRoomTileOffset) != nullptr,
                (flags & StaticPoiRoomPresetUnitsAddedMask) != 0U
                    && *reinterpret_cast<void**>(
                        room + DrlgRoomPresetUnitOffset) != nullptr,
                *reinterpret_cast<void**>(
                    room + DrlgRoomActiveRoomOffset) != nullptr);
            if (action == StaticPoiRoomAction::Materialize) {
                StaticClientRoomLease lease{};
                if (!PrepareStaticClientRoom(dataContext, room, lease)) {
                    complete = false;
                } else if (lease.owned) {
                    if (leases.count >= leases.leases.size()) return false;
                    leases.leases[leases.count++] = lease;
                }
            } else if (action == StaticPoiRoomAction::Wait) {
                complete = false;
            }
            room = *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomNextOffset);
            ++roomCount;
        }
        return room == nullptr && complete;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] auto ReleaseStaticPoiRooms(
        StaticPoiRoomLeaseSet& leases) noexcept -> bool {
    bool complete = true;
    while (leases.count != 0U) {
        --leases.count;
        if (!ReleaseStaticClientRoom(leases.leases[leases.count])) {
            complete = false;
        }
    }
    return complete;
}

enum class VisibilityTargetResolution : std::uint8_t {
    Initialize,
    LinkedOnly,
};

[[nodiscard]] auto FindLinkedClientLevelByIdUnchecked(
        const ClientLevelView& current,
        std::int32_t levelId,
        void*& outputLevel) noexcept -> bool {
    outputLevel = nullptr;
    if (!IsAlignedPointer(current.drlg) || levelId <= 0) return false;
    auto* const drlg = static_cast<std::uint8_t*>(current.drlg);
    auto* level = *reinterpret_cast<std::uint8_t**>(
        drlg + DrlgFirstLevelOffset);
    if (HasCycleOrExceedsLimitUnchecked(
            level,
            LevelNextOffset,
            MaximumLevelsPerDrlg)) {
        return false;
    }
    std::size_t count{};
    while (level != nullptr && count < MaximumLevelsPerDrlg) {
        if (!IsAlignedPointer(level)
            || *reinterpret_cast<std::uint8_t**>(
                level + LevelDrlgOffset) != drlg) {
            return false;
        }
        if (*reinterpret_cast<const std::int32_t*>(
                level + LevelIdOffset) == levelId) {
            outputLevel = level;
            return true;
        }
        level = *reinterpret_cast<std::uint8_t**>(
            level + LevelNextOffset);
        ++count;
    }
    return false;
}

[[nodiscard]] auto ResolveExactWaypointUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* level,
        std::int32_t levelId,
        RoomMaterializationPolicy materialization,
        CandidateBatch& batch) noexcept -> bool {
    if (!Detail::AllowsWaypointLabelForLevel(levelId)) {
        // Publish a complete empty owner for towns. This both avoids needless
        // preset traversal and clears any town label retained by an earlier
        // build when the catalog is reconstructed after Reveal All.
        batch.hasWaypoint = false;
        batch.pendingWaypoint = false;
        return true;
    }
    if (materialization == RoomMaterializationPolicy::Passive) {
        // The governed native resolver initializes the selected DrlgRoom before
        // it searches presets. Reveal-wide collection instead reads only the
        // already-linked PresetUnit chains. A missing chain is deliberately
        // pending rather than a proved no-waypoint result, so the owner catalog
        // cannot be replaced by an empty entry.
        const auto objectRecordCount = GetObjectsTxtRecordCount(dataContext);
        if (objectRecordCount <= 0 || objectRecordCount > 1'000'000) {
            return false;
        }
        auto* room = *reinterpret_cast<std::uint8_t**>(
            level + LevelFirstRoomOffset);
        if (HasCycleOrExceedsLimitUnchecked(
                room,
                DrlgRoomNextOffset,
                MaximumRoomsPerLevel)) {
            batch.traversalLimited = true;
            return false;
        }
        std::array<Detail::NavigationWaypointPresetCandidate, 2U>
            candidates{};
        std::size_t candidateCount{};
        bool hasIncompleteRoom{};
        std::size_t roomCount{};
        while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
            if (!IsAlignedPointer(room)
                || *reinterpret_cast<std::uint8_t**>(
                    room + DrlgRoomLevelOffset) != level) {
                return false;
            }
            Detail::NavigationRoomRectangle roomRectangle{};
            if (!ReadRoomRectangleUnchecked(room, roomRectangle)
                || roomRectangle.levelId != levelId) {
                return false;
            }
            auto* preset = *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomPresetUnitOffset);
            if (preset == nullptr) {
                // HookInitLevel calls us after the native initialization, so a
                // null PresetUnit head is a proved no-preset room, not a
                // request to materialize it again.
            } else if (HasCycleOrExceedsLimitUnchecked(
                    preset,
                    PresetNextOffset,
                    MaximumPresetsPerRoom)) {
                batch.traversalLimited = true;
                return false;
            } else {
                std::size_t presetCount{};
                while (preset != nullptr && presetCount < MaximumPresetsPerRoom) {
                    const auto presetType = *reinterpret_cast<const std::uint32_t*>(
                        preset + PresetUnitTypeOffset);
                    const auto classId = *reinterpret_cast<const std::int32_t*>(
                        preset + PresetClassIdOffset);
                    const auto relativeX = *reinterpret_cast<const std::int32_t*>(
                        preset + PresetRelativeXOffset);
                    const auto relativeY = *reinterpret_cast<const std::int32_t*>(
                        preset + PresetRelativeYOffset);
                    if (presetType == PresetObject
                        && classId >= 0 && classId < objectRecordCount
                        && IsPresetWithinRoom(
                            roomRectangle.width,
                            roomRectangle.height,
                            relativeX,
                            relativeY)) {
                        auto* const record = GetObjectsTxtRecord(dataContext, classId);
                        if (record == nullptr) return false;
                        if ((record[ObjectsTxtSubClassOffset]
                                & ObjectsTxtWaypointSubClass) != 0U) {
                            Detail::NavigationWaypointPresetCandidate candidate{};
                            if (!CheckedGameTileCoordinate(
                                    roomRectangle.tileX,
                                    relativeX,
                                    candidate.nativeTileX)
                                || !CheckedGameTileCoordinate(
                                    roomRectangle.tileY,
                                    relativeY,
                                    candidate.nativeTileY)
                                || !CheckedNavigationSubtileCoordinate(
                                    roomRectangle.tileX,
                                    relativeX,
                                    candidate.subtileX)
                                || !CheckedNavigationSubtileCoordinate(
                                    roomRectangle.tileY,
                                    relativeY,
                                    candidate.subtileY)) {
                                return false;
                            }
                            candidate.classId = classId;
                            if (candidateCount < candidates.size()) {
                                candidates[candidateCount++] = candidate;
                            }
                        }
                    }
                    preset = *reinterpret_cast<std::uint8_t**>(
                        preset + PresetNextOffset);
                    ++presetCount;
                }
                if (preset != nullptr) {
                    batch.traversalLimited = true;
                    return false;
                }
            }
            room = *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomNextOffset);
            ++roomCount;
        }
        if (room != nullptr) {
            batch.traversalLimited = true;
            return false;
        }
        const auto selection = Detail::SelectPassiveWaypointPreset(
            std::span(candidates.data(), candidateCount),
            hasIncompleteRoom || candidateCount >= candidates.size());
        if (selection.pending) {
            batch.pendingWaypoint = true;
            return true;
        }
        if (!selection.exact.has_value()) {
            // A complete passive traversal with no waypoint preset proves
            // absence for this source level. Leave hasWaypoint false so the
            // owner can be cleared without dereferencing an empty selection.
            return true;
        }
        const auto& exact = *selection.exact;
        batch.waypointNativeTileX = exact.nativeTileX;
        batch.waypointNativeTileY = exact.nativeTileY;
        batch.waypointExactSubtileX = exact.subtileX;
        batch.waypointExactSubtileY = exact.subtileY;
        NavigationNativePoint derivedClient{};
        if (!ConvertNavigationSubtileToClientCoordinates(
                exact.subtileX,
                exact.subtileY,
                derivedClient)) {
            return false;
        }
        batch.waypointExactClientX = derivedClient.x;
        batch.waypointExactClientY = derivedClient.y;
        batch.waypoint = {
            .destinationId = MakeDestinationId(
                PresetObject,
                levelId,
                exact.subtileX,
                exact.subtileY),
            .subtileX = exact.subtileX,
            .subtileY = exact.subtileY,
            .useExactClientCoordinates = false,
        };
        batch.hasWaypoint = true;
        return true;
    }
    std::int32_t nativeTileX{-1};
    std::int32_t nativeTileY{-1};
    auto* const waypointRoom = static_cast<std::uint8_t*>(
        FindWaypointRoomAndCoordinates(
            dataContext,
            level,
            &nativeTileX,
            &nativeTileY));
    batch.waypointNativeTileX = nativeTileX;
    batch.waypointNativeTileY = nativeTileY;
    if (waypointRoom == nullptr) return true;
    if (!IsAlignedPointer(waypointRoom)
        || *reinterpret_cast<std::uint8_t**>(
            waypointRoom + DrlgRoomLevelOffset) != level) {
        return false;
    }

    Detail::NavigationRoomRectangle roomRectangle{};
    if (!ReadRoomRectangleUnchecked(waypointRoom, roomRectangle)
        || roomRectangle.levelId != levelId) {
        return false;
    }
    const auto objectRecordCount = GetObjectsTxtRecordCount(dataContext);
    if (objectRecordCount <= 0 || objectRecordCount > 1'000'000) {
        return false;
    }

    auto* preset = *reinterpret_cast<std::uint8_t**>(
        waypointRoom + DrlgRoomPresetUnitOffset);
    if (HasCycleOrExceedsLimitUnchecked(
            preset,
            PresetNextOffset,
            MaximumPresetsPerRoom)) {
        batch.traversalLimited = true;
        return false;
    }
    std::array<
        Detail::NavigationWaypointPresetCandidate,
        MaximumWaypointPresetsPerRoom> candidates{};
    std::size_t candidateCount{};
    std::size_t presetCount{};
    while (preset != nullptr && presetCount < MaximumPresetsPerRoom) {
        const auto presetType = *reinterpret_cast<const std::uint32_t*>(
            preset + PresetUnitTypeOffset);
        const auto classId = *reinterpret_cast<const std::int32_t*>(
            preset + PresetClassIdOffset);
        const auto relativeX = *reinterpret_cast<const std::int32_t*>(
            preset + PresetRelativeXOffset);
        const auto relativeY = *reinterpret_cast<const std::int32_t*>(
            preset + PresetRelativeYOffset);
        if (presetType == PresetObject
            && classId >= 0 && classId < objectRecordCount
            && IsPresetWithinRoom(
                roomRectangle.width,
                roomRectangle.height,
                relativeX,
                relativeY)) {
            auto* const record = GetObjectsTxtRecord(dataContext, classId);
            // ObjectsTxt is a packed 0x168-byte table. A valid record is not
            // required to keep pointer-size alignment.
            if (record == nullptr) return false;
            if ((record[ObjectsTxtSubClassOffset]
                    & ObjectsTxtWaypointSubClass) != 0U) {
                std::int32_t candidateNativeTileX{};
                std::int32_t candidateNativeTileY{};
                std::int32_t subtileX{};
                std::int32_t subtileY{};
                if (!CheckedGameTileCoordinate(
                        roomRectangle.tileX,
                        relativeX,
                        candidateNativeTileX)
                    || !CheckedGameTileCoordinate(
                        roomRectangle.tileY,
                        relativeY,
                        candidateNativeTileY)
                    || !CheckedNavigationSubtileCoordinate(
                        roomRectangle.tileX,
                        relativeX,
                        subtileX)
                    || !CheckedNavigationSubtileCoordinate(
                        roomRectangle.tileY,
                        relativeY,
                        subtileY)) {
                    return false;
                }
                if (candidateCount >= candidates.size()) {
                    batch.traversalLimited = true;
                    return false;
                }
                candidates[candidateCount++] = {
                    .nativeTileX = candidateNativeTileX,
                    .nativeTileY = candidateNativeTileY,
                    .subtileX = subtileX,
                    .subtileY = subtileY,
                    .classId = classId,
                };
            }
        }
        preset = *reinterpret_cast<std::uint8_t**>(
            preset + PresetNextOffset);
        ++presetCount;
    }
    if (preset != nullptr) {
        batch.traversalLimited = true;
        return false;
    }
    const auto exact = Detail::SelectExactWaypointPreset(
        nativeTileX,
        nativeTileY,
        std::span(candidates.data(), candidateCount));
    if (!exact.has_value()) {
        batch.pendingWaypoint = true;
        return true;
    }

    // PrimeMH's generated POI contract uses the exact preset world position,
    // not a nearby runtime Unit sharing the same object class. Keep the raw
    // subtile pair authoritative all the way to the projection boundary.
    NavigationNativePoint derivedClient{};
    if (!ConvertNavigationSubtileToClientCoordinates(
            exact->subtileX,
            exact->subtileY,
            derivedClient)) {
        return false;
    }
    batch.waypointExactSubtileX = exact->subtileX;
    batch.waypointExactSubtileY = exact->subtileY;
    batch.waypointExactClientX = derivedClient.x;
    batch.waypointExactClientY = derivedClient.y;
    batch.waypoint = {
        .destinationId = MakeDestinationId(
            PresetObject,
            levelId,
            exact->subtileX,
            exact->subtileY),
        .subtileX = exact->subtileX,
        .subtileY = exact->subtileY,
        .useExactClientCoordinates = false,
    };
    batch.hasWaypoint = true;
    return true;
}

[[nodiscard]] auto CopyWaypointLabelDefinition(
        const CandidateBatch& batch,
        AutomapWaypointLabelDefinition& output) noexcept -> bool {
    if (!batch.hasWaypoint || batch.levelId <= 0
            || batch.waypoint.subtileX < 0
            || batch.waypoint.subtileY < 0) {
        output = {};
        return false;
    }
    output = {
        .stableId = batch.waypoint.destinationId,
        .levelId = batch.levelId,
        .subtileX = batch.waypoint.subtileX,
        .subtileY = batch.waypoint.subtileY,
    };
    return true;
}

[[nodiscard]] auto ResolveRoomTileExitsUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* room,
        std::int32_t currentLevelId,
        RoomMaterializationPolicy materialization,
        CandidateBatch& batch) noexcept -> bool {
    // D2R creates a room's RoomTile chain lazily. The local-player refresh owns
    // this materialization; reveal-wide passes inspect only an already-active
    // room and defer the source when no such room exists.
    auto* activeRoom = *reinterpret_cast<std::uint8_t**>(
        room + DrlgRoomActiveRoomOffset);
    if (activeRoom == nullptr
        && materialization == RoomMaterializationPolicy::CurrentLevel) {
        activeRoom = static_cast<std::uint8_t*>(
            MaterializeClientRoom(dataContext, room));
    }
    if (activeRoom == nullptr) {
        if (materialization == RoomMaterializationPolicy::CurrentLevel) {
            ++batch.pendingRoomTileRoomCount;
            return true;
        }
        // Ordinary preset and RoomTile chains belong to the initialized
        // DrlgRoom and can be read without creating an ActiveRoom. Dynamic
        // progression objects are the sole active-room-only evidence here.
        if (HasDynamicMainProgressionTargetFor(currentLevelId)) {
            ++batch.pendingRoomTileRoomCount;
        }
    } else {
        if (!IsAlignedPointer(activeRoom)
            || GetDrlgRoomFromActiveRoom(activeRoom) != room) {
            return false;
        }
        if (!ResolveDynamicProgressionObjectsUnchecked(
                dataContext,
                activeRoom,
                currentLevelId,
                batch)) {
            return false;
        }
    }

    Detail::NavigationRoomRectangle sourceRectangle{};
    if (!ReadRoomRectangleUnchecked(room, sourceRectangle)
        || sourceRectangle.levelId != currentLevelId) {
        return false;
    }

    auto* roomTile = *reinterpret_cast<std::uint8_t**>(
        room + DrlgRoomRoomTileOffset);
    if (HasCycleOrExceedsLimitUnchecked(
            roomTile,
            RoomTileNextOffset,
            MaximumRoomTilesPerRoom)) {
        batch.traversalLimited = true;
        return false;
    }

    std::array<
        Detail::NavigationRoomTileLinkCandidate,
        MaximumNavigationDestinations> resolvedSources{};
    std::size_t resolvedSourceCount{};
    std::size_t roomTileCount{};
    while (roomTile != nullptr
        && roomTileCount < MaximumRoomTilesPerRoom) {
        auto* const lvlWarp = *reinterpret_cast<std::uint8_t**>(
            roomTile + RoomTileLvlWarpOffset);
        if (!IsAlignedPointer(lvlWarp)) return false;
        const auto sourceId = *reinterpret_cast<const std::int32_t*>(
            lvlWarp + LvlWarpSourceIdOffset);
        if (sourceId >= 0 && sourceId <= MaximumSupportedLevelId) {
            ++batch.rawRoomTileCount;
            auto* const destinationRoom =
                *reinterpret_cast<std::uint8_t**>(
                    roomTile + RoomTileDestinationRoomOffset);
            if (destinationRoom == nullptr) {
                ++batch.pendingRoomTileRoomCount;
                roomTile = *reinterpret_cast<std::uint8_t**>(
                    roomTile + RoomTileNextOffset);
                ++roomTileCount;
                continue;
            }
            Detail::NavigationRoomRectangle destinationRectangle{};
            if (!ReadRoomRectangleUnchecked(
                    destinationRoom,
                    destinationRectangle)) {
                return false;
            }
            ++batch.nativeRoomTileCount;
            bool duplicate{};
            for (std::size_t index = 0U;
                    index < resolvedSourceCount;
                    ++index) {
                if (resolvedSources[index].sourcePresetId == sourceId) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                if (resolvedSourceCount >= resolvedSources.size()) {
                    batch.traversalLimited = true;
                    return false;
                }
                resolvedSources[resolvedSourceCount++] = {
                    .sourcePresetId = sourceId,
                    .targetLevelId = destinationRectangle.levelId,
                };
            }
        }
        roomTile = *reinterpret_cast<std::uint8_t**>(
            roomTile + RoomTileNextOffset);
        ++roomTileCount;
    }
    if (roomTile != nullptr) {
        batch.traversalLimited = true;
        return false;
    }

    // 0x3DA9FB proves RoomTile+0 is the destination DrlgRoom. Do not call the
    // native helper at 0x3DA9A0 here: that helper additionally requires the
    // destination's reciprocal RoomTile and returns null while that unrelated
    // side is still lazy, even though this source-side exact exit is complete.
    auto* preset = *reinterpret_cast<std::uint8_t**>(
        room + DrlgRoomPresetUnitOffset);
    if (HasCycleOrExceedsLimitUnchecked(
            preset,
            PresetNextOffset,
            MaximumPresetsPerRoom)) {
        batch.traversalLimited = true;
        return false;
    }
    const auto catalog = ResolverCatalog.load(std::memory_order_acquire);
    std::size_t presetCount{};
    while (preset != nullptr && presetCount < MaximumPresetsPerRoom) {
        const auto presetType = *reinterpret_cast<const std::uint32_t*>(
            preset + PresetUnitTypeOffset);
        const auto presetId = *reinterpret_cast<const std::int32_t*>(
            preset + PresetClassIdOffset);
        const auto relativeX = *reinterpret_cast<const std::int32_t*>(
            preset + PresetRelativeXOffset);
        const auto relativeY = *reinterpret_cast<const std::int32_t*>(
            preset + PresetRelativeYOffset);
        const auto isWithinRoom = IsPresetWithinRoom(
                sourceRectangle.width,
                sourceRectangle.height,
                relativeX,
                relativeY);
        if (isWithinRoom) {
            if (presetType == PresetObject && presetId >= 0
                    && catalog != nullptr) {
                const auto* const record = catalog->FindObjectById(
                    static_cast<std::uint32_t>(presetId));
                if (record != nullptr
                        && IsSpecialChestPresetObjectDefinition(*record)) {
                    std::int32_t subtileX{};
                    std::int32_t subtileY{};
                    if (!CheckedNavigationSubtileCoordinate(
                            sourceRectangle.tileX,
                            relativeX,
                            subtileX)
                        || !CheckedNavigationSubtileCoordinate(
                            sourceRectangle.tileY,
                            relativeY,
                            subtileY)) {
                        return false;
                    }
                    AppendSpecialChestPreset(
                        batch,
                        presetId,
                        subtileX,
                        subtileY);
                    if (batch.traversalLimited) return false;
                }
            }
            const auto presetProgression = PresetMainProgressionTargetFor(
                currentLevelId,
                presetType,
                presetId);
            if (presetProgression.has_value()) {
                std::int32_t subtileX{};
                std::int32_t subtileY{};
                if (!CheckedNavigationSubtileCoordinate(
                        sourceRectangle.tileX,
                        relativeX,
                        subtileX)
                    || !CheckedNavigationSubtileCoordinate(
                        sourceRectangle.tileY,
                        relativeY,
                        subtileY)) {
                    return false;
                }
                const auto evidence = presetProgression->kind
                        == NavigationPresetProgressionKind::Boss
                    ? Detail::NavigationExitEvidence::BossPreset
                    : Detail::NavigationExitEvidence::QuestPreset;
                UpsertExitCandidate(
                    batch,
                    Detail::NavigationExitSelection{
                        .candidate = NavigationExitCandidate{
                            .destinationId = MakeDestinationId(
                                presetType,
                                presetId,
                                subtileX,
                                subtileY),
                            .targetLevelId =
                                presetProgression->targetLevelId,
                            .subtileX = subtileX,
                            .subtileY = subtileY,
                        },
                        .evidence = evidence,
                    });
                if (batch.traversalLimited) return false;
            }
            const auto questPreset = StaticQuestPresetTargetFor(
                currentLevelId,
                presetType,
                presetId);
            if (questPreset.has_value()) {
                std::int32_t subtileX{};
                std::int32_t subtileY{};
                if (!CheckedNavigationSubtileCoordinate(
                        sourceRectangle.tileX,
                        relativeX,
                        subtileX)
                    || !CheckedNavigationSubtileCoordinate(
                        sourceRectangle.tileY,
                        relativeY,
                        subtileY)) {
                    return false;
                }
                AppendQuestTarget(
                    batch,
                    NavigationPointCandidate{
                        .destinationId = MakeDestinationId(
                            presetType,
                            presetId,
                            subtileX,
                            subtileY),
                        .subtileX = subtileX,
                        .subtileY = subtileY,
                        .selection = questPreset->selection,
                    });
                if (batch.traversalLimited) return false;
            }
        }
        if (presetType == PresetLevelExit && isWithinRoom) {
            const auto targetLevelId = Detail::SelectRoomTileTargetLevel(
                presetId,
                std::span(
                    resolvedSources.data(),
                    resolvedSourceCount));
            if (targetLevelId && *targetLevelId != currentLevelId) {
                std::int32_t subtileX{};
                std::int32_t subtileY{};
                if (!CheckedNavigationSubtileCoordinate(
                        sourceRectangle.tileX,
                        relativeX,
                        subtileX)
                    || !CheckedNavigationSubtileCoordinate(
                        sourceRectangle.tileY,
                        relativeY,
                        subtileY)) {
                    return false;
                }
                UpsertExitCandidate(
                    batch,
                    Detail::NavigationExitSelection{
                        .candidate = NavigationExitCandidate{
                            .destinationId = MakeDestinationId(
                                PresetLevelExit,
                                *targetLevelId,
                                subtileX,
                                subtileY),
                            .targetLevelId = *targetLevelId,
                            .subtileX = subtileX,
                            .subtileY = subtileY,
                        },
                        .evidence = Detail::NavigationExitEvidence::RoomTile,
                    });
                ++batch.exactRoomTileCount;
            } else {
                // The preset proves an exit, but a lazy/missing RoomTile map
                // cannot prove its destination. Preserve this exit owner.
                ++batch.pendingRoomTileRoomCount;
            }
        }
        preset = *reinterpret_cast<std::uint8_t**>(
            preset + PresetNextOffset);
        ++presetCount;
        ++batch.presetCount;
    }
    if (preset != nullptr) {
        batch.traversalLimited = true;
        return false;
    }
    return !batch.traversalLimited;
}

[[nodiscard]] auto ResolveOutdoorVisibilityRoomPairsUnchecked(
        std::uint8_t dataContext,
        std::uint8_t* sourceLevel,
        std::uint8_t* targetLevel,
        std::int32_t currentLevelId,
        std::int32_t targetLevelId,
        std::uint8_t sourceSlot,
        std::uint8_t reciprocalSlot,
        RoomMaterializationPolicy materialization,
        CandidateBatch& batch) noexcept -> bool {
    auto* const sourceRoom = *reinterpret_cast<std::uint8_t**>(
        sourceLevel + LevelFirstRoomOffset);
    auto* const targetRoomHead = *reinterpret_cast<std::uint8_t**>(
        targetLevel + LevelFirstRoomOffset);
    if (HasCycleOrExceedsLimitUnchecked(
            sourceRoom,
            DrlgRoomNextOffset,
            MaximumRoomsPerLevel)
        || HasCycleOrExceedsLimitUnchecked(
            targetRoomHead,
            DrlgRoomNextOffset,
            MaximumRoomsPerLevel)) {
        batch.traversalLimited = true;
        return false;
    }

    return ResolveOutdoorLevelBoundaryUnchecked(
        dataContext,
        sourceLevel,
        targetLevel,
        currentLevelId,
        targetLevelId,
        sourceSlot,
        reciprocalSlot,
        materialization,
        batch);
}

[[nodiscard]] auto ResolveOutdoorVisibilityLinksUnchecked(
        const ClientLevelView& current,
        std::int32_t currentLevelId,
        RoomMaterializationPolicy materialization,
        VisibilityTargetResolution targetResolution,
        CandidateBatch& batch) noexcept -> bool {
    const auto dataContext = current.dataContext;
    auto* const level = static_cast<std::uint8_t*>(current.level);
    if (GetLevelVis == nullptr || GetLevelWarp == nullptr) return false;
    if (!IsAlignedPointer(level) || !IsAlignedPointer(current.drlg)) {
        return false;
    }
    auto* const sourceVis = GetLevelVis(dataContext, level);
    if (sourceVis == nullptr) return false;

    std::array<std::int32_t, VisibilitySlotCount> sourceTargets{};
    for (std::size_t slot = 0U; slot < VisibilitySlotCount; ++slot) {
        sourceTargets[slot] = sourceVis[slot];
    }
    for (std::size_t slot = 0U; slot < VisibilitySlotCount; ++slot) {
        const auto targetLevelId = sourceTargets[slot];
        if (targetLevelId <= 0
            || targetLevelId > MaximumSupportedLevelId
            || targetLevelId == currentLevelId
            || GetLevelWarp(
                dataContext,
                level,
                static_cast<std::uint8_t>(slot)) != -1) {
            continue;
        }
        ++batch.visibilitySlotCount;

        void* targetLevelPointer{};
        const bool targetResolved = targetResolution
                == VisibilityTargetResolution::Initialize
            ? ResolveClientLevelById(
                current, targetLevelId, targetLevelPointer)
            : FindLinkedClientLevelByIdUnchecked(
                current, targetLevelId, targetLevelPointer);
        if (!targetResolved || targetLevelPointer == nullptr) {
            ++batch.pendingVisibilityTargetCount;
            continue;
        }
        auto* const targetLevel = static_cast<std::uint8_t*>(
            targetLevelPointer);
        if (*reinterpret_cast<void**>(
                targetLevel + LevelFirstRoomOffset) == nullptr) {
            ++batch.pendingVisibilityTargetCount;
            continue;
        }
        auto* const targetVis = GetLevelVis(dataContext, targetLevel);
        if (targetVis == nullptr) return false;
        std::array<std::int32_t, VisibilitySlotCount> targetLinks{};
        for (std::size_t reciprocal = 0U;
                reciprocal < VisibilitySlotCount;
                ++reciprocal) {
            targetLinks[reciprocal] = targetVis[reciprocal];
        }
        for (std::size_t reciprocal = 0U;
                reciprocal < VisibilitySlotCount;
                ++reciprocal) {
            if (targetLinks[reciprocal] != currentLevelId
                || GetLevelWarp(
                    dataContext,
                    targetLevel,
                    static_cast<std::uint8_t>(reciprocal)) != -1) {
                continue;
            }
            ++batch.visibilityPairCount;
            if (!ResolveOutdoorVisibilityRoomPairsUnchecked(
                    dataContext,
                    level,
                    targetLevel,
                    currentLevelId,
                    targetLevelId,
                    static_cast<std::uint8_t>(slot),
                    static_cast<std::uint8_t>(reciprocal),
                    materialization,
                    batch)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto InitializeRequestedTargetLevelsUnchecked(
        const ClientLevelView& current,
        std::span<const std::int32_t> customTargetLevelIds,
        CandidateBatch& batch) noexcept -> bool {
    auto* const sourceVis = GetLevelVis(
        current.dataContext,
        current.level);
    if (sourceVis == nullptr) return false;
    std::array<std::int32_t, VisibilitySlotCount> visibleTargetIds{};
    for (std::size_t slot = 0U; slot < VisibilitySlotCount; ++slot) {
        visibleTargetIds[slot] = sourceVis[slot];
    }

    std::array<
        std::int32_t,
        MaximumCustomLevelTargets + MaximumMainProgressionTargets
            + MaximumStaticQuestRouteTargets> targetIds{};
    const auto targetCount = BuildNavigationPreparationTargets(
        current.levelId,
        customTargetLevelIds,
        targetIds);
    for (std::size_t index = 0U; index < targetCount; ++index) {
        // A global custom list may contain destinations from every act. Never
        // ask the current act's DRLG to create an unrelated Level; only direct
        // Vis neighbours are eligible for initialization in this refresh.
        if (!Detail::IsDirectVisibleTarget(
                current.levelId,
                targetIds[index],
                visibleTargetIds)) {
            continue;
        }
        ++batch.requestedTargetLevelCount;
        void* targetLevel{};
        if (!ResolveClientLevelById(current, targetIds[index], targetLevel)) {
            ++batch.pendingTargetLevelCount;
            continue;
        }
        if (!IsAlignedPointer(targetLevel)) return false;
        ++batch.initializedTargetLevelCount;
    }
    return true;
}

[[nodiscard]] auto PrepareCanyonCorrectTombUnchecked(
        const ClientLevelView& current,
        CandidateBatch& batch) noexcept -> bool {
    if (current.levelId != CanyonOfTheMagiLevelId) return true;
    if (Base == nullptr || GetHoradricStaffTombLevelId == nullptr
        || GetQuestState == nullptr || !IsAlignedPointer(current.drlg)) {
        return false;
    }

    const auto tombLevelId = GetHoradricStaffTombLevelId(current.drlg);
    if (tombLevelId < FirstTalRashaTombLevelId
        || tombLevelId > LastTalRashaTombLevelId) {
        batch.pendingCorrectTomb = true;
        return true;
    }
    batch.correctTombLevelId = tombLevelId;

    void* const questRecord = *reinterpret_cast<void**>(
        Base + ClientQuestRecordPointerRva);
    if (!IsAlignedPointer(questRecord)) {
        batch.pendingCorrectTomb = true;
        return true;
    }
    batch.durielRewardGranted = GetQuestState(
        questRecord,
        ActTwoQuestSixStateFlag,
        QuestRewardGrantedFlag) != 0;
    batch.hasCorrectTombState = true;

    ++batch.requestedTargetLevelCount;
    void* targetLevel{};
    if (!ResolveClientLevelById(current, tombLevelId, targetLevel)) {
        ++batch.pendingTargetLevelCount;
        batch.pendingCorrectTomb = true;
        return true;
    }
    if (!IsAlignedPointer(targetLevel)) return false;
    ++batch.initializedTargetLevelCount;
    return true;
}

void FinalizeCanyonCorrectTomb(CandidateBatch& batch) noexcept {
    if (batch.levelId != CanyonOfTheMagiLevelId) return;
    if (!batch.hasCorrectTombState
        || batch.correctTombLevelId == UnknownNavigationLevelId) {
        batch.pendingCorrectTomb = true;
        return;
    }
    for (std::size_t index = 0U; index < batch.exitCount; ++index) {
        const auto& exit = batch.exits[index];
        if (exit.targetLevelId != batch.correctTombLevelId) continue;
        batch.correctTomb = {
            .destinationId = exit.destinationId,
            .subtileX = exit.subtileX,
            .subtileY = exit.subtileY,
            .exactClientX = exit.exactClientX,
            .exactClientY = exit.exactClientY,
            .useExactClientCoordinates = exit.useExactClientCoordinates,
        };
        batch.hasCorrectTomb = true;
        batch.pendingCorrectTomb = false;
        return;
    }
    batch.pendingCorrectTomb = true;
}

[[nodiscard]] auto EnumerateCurrentLevelUnchecked(
        std::int32_t expectedLevelId,
        std::span<const std::int32_t> customTargetLevelIds,
        CandidateBatch& batch) noexcept -> bool {
    __try {
        if (GetLocalDataContext == nullptr || GetLocalPlayer == nullptr
            || GetDrlgRoomFromActiveRoom == nullptr
            || GetCollisionGrid == nullptr
            || GetFirstUnitInRoom == nullptr
            || IsRoomInTown == nullptr
            || GetUnitClassId == nullptr
            || GetUnitDataContext == nullptr
            || GetUnitClientCoordX == nullptr
            || GetUnitClientCoordY == nullptr
            || GetUnitRoom == nullptr
            || GetNextUnitInRoom == nullptr
            || GetUnitType == nullptr
            || GetDrlgRoomLevelId == nullptr
            || GetHoradricStaffTombLevelId == nullptr
            || GetQuestState == nullptr
            || GetLevelVis == nullptr
            || GetLevelWarp == nullptr
            || GetObjectsTxtRecordCount == nullptr
            || GetObjectsTxtRecord == nullptr
            || FindWaypointRoomAndCoordinates == nullptr) {
            return false;
        }
        ClientLevelView current{};
        if (!ResolveCurrentClientLevelView(current)
            || current.dataContext >= 8U) {
            return false;
        }
        auto* const activeRoom = static_cast<std::uint8_t*>(
            current.activeRoom);
        auto* const level = static_cast<std::uint8_t*>(current.level);
        if (!IsAlignedPointer(activeRoom)
            || !IsAlignedPointer(level)
            || !IsAlignedPointer(current.drlg)) {
            return false;
        }
        const auto levelId = current.levelId;
        if (levelId <= 0 || levelId > MaximumSupportedLevelId
            || (expectedLevelId != UnknownNavigationLevelId
                && expectedLevelId != levelId)) {
            return false;
        }
        batch.levelId = levelId;
        batch.inTown = IsRoomInTown(activeRoom) != 0;

        if (!batch.inTown) {
            // DRLG links are lazy. Initialize only the requested progression
            // and configured targets before source-room materialization so
            // forward links such as Barracks -> Jail 1 and Jail 1 -> Jail 2
            // exist during this same refresh. Towns deliberately skip these
            // navigation-only targets, but still enumerate their physical
            // boundaries below so exit labels remain available.
            if (!PrepareCanyonCorrectTombUnchecked(current, batch)) {
                return false;
            }
            if (!InitializeRequestedTargetLevelsUnchecked(
                    current,
                    customTargetLevelIds,
                    batch)) {
                return false;
            }
        }
        // Waypoint labels are object POIs, not navigation lines. Town owners
        // resolve to a complete empty entry; non-town levels keep the exact
        // generated preset independently of the navigation-line policy.
        if (!ResolveExactWaypointUnchecked(
                current.dataContext,
                level,
                levelId,
                RoomMaterializationPolicy::CurrentLevel,
                batch)) {
            return false;
        }
        if (!ResolveOutdoorVisibilityLinksUnchecked(
                current,
                levelId,
                RoomMaterializationPolicy::CurrentLevel,
                VisibilityTargetResolution::Initialize,
                batch)) {
            return false;
        }

        auto* room = *reinterpret_cast<std::uint8_t**>(
            level + LevelFirstRoomOffset);
        if (HasCycleOrExceedsLimitUnchecked(
                room,
                DrlgRoomNextOffset,
                MaximumRoomsPerLevel)) {
            batch.traversalLimited = true;
            return false;
        }
        std::size_t roomCount{};
        while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
            if (!IsAlignedPointer(room)
                || *reinterpret_cast<std::uint8_t**>(
                    room + DrlgRoomLevelOffset) != level) {
                return false;
            }
            if (!ResolveRoomTileExitsUnchecked(
                    current.dataContext,
                    room,
                    levelId,
                    RoomMaterializationPolicy::CurrentLevel,
                    batch)) {
                return false;
            }
            auto* const nextRoom = *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomNextOffset);
            room = nextRoom;
            ++roomCount;
            ++batch.roomCount;
        }
        if (room != nullptr) batch.traversalLimited = true;
        if (batch.traversalLimited) return false;
        FinalizeExitCandidates(batch);
        FinalizeCanyonCorrectTomb(batch);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] auto AppendPhysicalExitLabels(
        CandidateBatch& batch,
        std::array<
            AutomapExitLabelDefinition,
            MaximumAutomapExitLabels>& output,
        std::size_t& outputCount) noexcept -> bool {
    std::array<
        Detail::NavigationPhysicalExitSelection,
        MaximumAutomapExitLabels> physical{};
    std::size_t physicalCount{};
    if (!Detail::ReducePhysicalExitEvidence(
            batch.exitLabelEvidence,
            batch.exitLabelEvidenceCount,
            physical,
            physicalCount)) {
        return false;
    }
    for (std::size_t index = 0U; index < physicalCount; ++index) {
        const auto& exit = physical[index].winner.candidate;
        const AutomapExitLabelDefinition definition{
            .stableId = exit.destinationId,
            .sourceLevelId = batch.levelId,
            .targetLevelId = exit.targetLevelId,
            .subtileX = exit.subtileX,
            .subtileY = exit.subtileY,
            .exactClientX = exit.exactClientX,
            .exactClientY = exit.exactClientY,
            .useExactClientCoordinates = exit.useExactClientCoordinates,
            .boundaryIdentity = physical[index].boundaryIdentity,
            .canonicalLevelPairAnchor =
                physical[index].winner.canonicalLevelPairAnchor,
        };
        std::size_t existing{};
        while (existing < outputCount) {
            const auto& candidate = output[existing];
            if (candidate.sourceLevelId == definition.sourceLevelId
                && (candidate.stableId == definition.stableId
                || (candidate.targetLevelId == definition.targetLevelId
                    && candidate.subtileX == definition.subtileX
                    && candidate.subtileY == definition.subtileY))) {
                break;
            }
            ++existing;
        }
        if (existing < outputCount) {
            output[existing] = definition;
            continue;
        }
        if (outputCount >= output.size()) return false;
        output[outputCount++] = definition;
    }
    return true;
}

[[nodiscard]] auto EnumerateRevealedActPoiDefinitionsUnchecked(
        std::array<
            AutomapExitLabelDefinition,
            MaximumAutomapExitLabels>& output,
        std::size_t& outputCount,
        std::array<
            RevealedWaypointResolution,
            MaximumAutomapWaypointLabels>& waypointOutput,
        std::size_t& waypointOutputCount,
        std::size_t& levelCount,
        std::uint64_t& pendingCount) noexcept -> bool {
    outputCount = 0U;
    waypointOutputCount = 0U;
    levelCount = 0U;
    pendingCount = 0U;
    __try {
        ClientLevelView current{};
        if (!ResolveCurrentClientLevelView(current)
                || current.dataContext >= 8U
                || !IsAlignedPointer(current.drlg)) {
            return false;
        }
        auto* const drlg = static_cast<std::uint8_t*>(current.drlg);
        auto* level = *reinterpret_cast<std::uint8_t**>(
            drlg + DrlgFirstLevelOffset);
        if (HasCycleOrExceedsLimitUnchecked(
                level,
                LevelNextOffset,
                MaximumLevelsPerDrlg)) {
            return false;
        }
        std::array<std::uint8_t*, MaximumLevelsPerDrlg> levels{};
        std::size_t linkedLevelCount{};
        while (level != nullptr
                && linkedLevelCount < levels.size()) {
            if (!IsAlignedPointer(level)
                    || *reinterpret_cast<std::uint8_t**>(
                        level + LevelDrlgOffset) != drlg) {
                return false;
            }
            levels[linkedLevelCount++] = level;
            level = *reinterpret_cast<std::uint8_t**>(
                level + LevelNextOffset);
        }
        if (level != nullptr) return false;

        for (std::size_t levelIndex = 0U;
                levelIndex < linkedLevelCount;
                ++levelIndex) {
            auto* const sourceLevel = levels[levelIndex];
            const auto sourceLevelId = *reinterpret_cast<const std::int32_t*>(
                sourceLevel + LevelIdOffset);
            if (sourceLevelId <= 0
                    || sourceLevelId > MaximumSupportedLevelId) {
                return false;
            }
            auto* room = *reinterpret_cast<std::uint8_t**>(
                sourceLevel + LevelFirstRoomOffset);
            if (room == nullptr) {
                ++pendingCount;
                continue;
            }

            CandidateBatch batch{};
            batch.levelId = sourceLevelId;
            if (!ResolveExactWaypointUnchecked(
                    current.dataContext,
                    sourceLevel,
                    sourceLevelId,
                    RoomMaterializationPolicy::Passive,
                    batch)) {
                return false;
            }
            ClientLevelView source = current;
            source.level = sourceLevel;
            source.levelId = sourceLevelId;
            if (!ResolveOutdoorVisibilityLinksUnchecked(
                    source,
                    sourceLevelId,
                    RoomMaterializationPolicy::Passive,
                    VisibilityTargetResolution::LinkedOnly,
                    batch)) {
                return false;
            }
            if (HasCycleOrExceedsLimitUnchecked(
                    room,
                    DrlgRoomNextOffset,
                    MaximumRoomsPerLevel)) {
                return false;
            }
            std::size_t roomCount{};
            while (room != nullptr
                    && roomCount < MaximumRoomsPerLevel) {
                if (!IsAlignedPointer(room)
                        || *reinterpret_cast<std::uint8_t**>(
                            room + DrlgRoomLevelOffset) != sourceLevel
                        || !ResolveRoomTileExitsUnchecked(
                            current.dataContext,
                            room,
                            sourceLevelId,
                            RoomMaterializationPolicy::Passive,
                            batch)) {
                    return false;
                }
                room = *reinterpret_cast<std::uint8_t**>(
                    room + DrlgRoomNextOffset);
                ++roomCount;
            }
            if (room != nullptr || batch.traversalLimited) {
                return false;
            }
            const auto pendingExits = batch.pendingCollisionRoomCount != 0U
                || batch.pendingRoomTileRoomCount != 0U
                || batch.pendingVisibilityTargetCount != 0U;
            const auto publication = Detail::MakePassivePoiPublicationPolicy(
                pendingExits,
                batch.pendingWaypoint);
            pendingCount += batch.pendingCollisionRoomCount
                + batch.pendingRoomTileRoomCount
                + batch.pendingVisibilityTargetCount;
            ++levelCount;
            if (batch.pendingWaypoint) ++pendingCount;
            if (publication.publishWaypoint) {
                if (waypointOutputCount >= waypointOutput.size()) return false;
                auto& resolution = waypointOutput[waypointOutputCount++];
                resolution = {
                    .levelId = sourceLevelId,
                    .hasWaypoint = batch.hasWaypoint,
                };
                if (batch.hasWaypoint
                        && !CopyWaypointLabelDefinition(
                            batch,
                            resolution.definition)) {
                    return false;
                }
            }
            if (publication.publishExitLabels
                    && !AppendPhysicalExitLabels(batch, output, outputCount)) {
                return false;
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] auto CollectInitializedLevelPoiDefinitionsUnchecked(
        std::uint8_t dataContext,
        void* level,
        std::array<
            AutomapExitLabelDefinition,
            MaximumAutomapExitLabels>& output,
        std::size_t& outputCount,
        std::int32_t& sourceLevelId,
        AutomapWaypointLabelDefinition& waypointOutput,
        bool& hasWaypoint,
        bool& pendingWaypoint,
        bool& pendingExits) noexcept -> bool {
    outputCount = 0U;
    sourceLevelId = UnknownNavigationLevelId;
    waypointOutput = {};
    hasWaypoint = false;
    pendingWaypoint = false;
    pendingExits = false;
    __try {
        if (dataContext >= 8U || !IsAlignedPointer(level)) return false;
        auto* const sourceLevel = static_cast<std::uint8_t*>(level);
        auto* const drlg = *reinterpret_cast<std::uint8_t**>(
            sourceLevel + LevelDrlgOffset);
        sourceLevelId = *reinterpret_cast<const std::int32_t*>(
            sourceLevel + LevelIdOffset);
        if (!IsAlignedPointer(drlg) || sourceLevelId <= 0
                || sourceLevelId > MaximumSupportedLevelId) {
            return false;
        }
        auto* room = *reinterpret_cast<std::uint8_t**>(
            sourceLevel + LevelFirstRoomOffset);
        if (room == nullptr || HasCycleOrExceedsLimitUnchecked(
                room,
                DrlgRoomNextOffset,
                MaximumRoomsPerLevel)) {
            return false;
        }

        CandidateBatch batch{};
        batch.levelId = sourceLevelId;
        if (!ResolveExactWaypointUnchecked(
                dataContext,
                sourceLevel,
                sourceLevelId,
                RoomMaterializationPolicy::Passive,
                batch)) {
            return false;
        }
        if (batch.hasWaypoint) {
            if (!CopyWaypointLabelDefinition(batch, waypointOutput)) {
                return false;
            }
            hasWaypoint = true;
        }
        const ClientLevelView source{
            .dataContext = dataContext,
            .levelId = sourceLevelId,
            .drlg = drlg,
            .level = sourceLevel,
        };
        if (!ResolveOutdoorVisibilityLinksUnchecked(
                source,
                sourceLevelId,
                RoomMaterializationPolicy::Passive,
                VisibilityTargetResolution::LinkedOnly,
                batch)) {
            return false;
        }
        std::size_t roomCount{};
        while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
            if (!IsAlignedPointer(room)
                    || *reinterpret_cast<std::uint8_t**>(
                        room + DrlgRoomLevelOffset) != sourceLevel
                    || !ResolveRoomTileExitsUnchecked(
                        dataContext,
                        room,
                        sourceLevelId,
                        RoomMaterializationPolicy::Passive,
                        batch)) {
                return false;
            }
            room = *reinterpret_cast<std::uint8_t**>(
                room + DrlgRoomNextOffset);
            ++roomCount;
        }
        if (room != nullptr || batch.traversalLimited) return false;
        pendingWaypoint = batch.pendingWaypoint;
        pendingExits = batch.pendingCollisionRoomCount != 0U
            || batch.pendingRoomTileRoomCount != 0U
            || batch.pendingVisibilityTargetCount != 0U;
        return AppendPhysicalExitLabels(batch, output, outputCount);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        outputCount = 0U;
        sourceLevelId = UnknownNavigationLevelId;
        waypointOutput = {};
        hasWaypoint = false;
        pendingWaypoint = false;
        pendingExits = false;
        return false;
    }
}

[[nodiscard]] auto ValidateRuntime(
        const D2RL::PluginContext* context) noexcept -> bool {
    constexpr std::array<std::uint8_t, 10U> localContextExpected{
        0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
        0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 19U> localPlayerExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
        0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 16U> activeRoomDrlgExpected{
        0x48, 0x8B, 0x41, 0x18, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 32U> collisionGridExpected{
        0x48, 0x8B, 0x41, 0x38, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        0x48, 0x85, 0xC9, 0x75, 0x0B, 0x0F, 0x57, 0xC0,
        0x0F, 0x11, 0x02, 0x0F, 0x11, 0x42, 0x10, 0xC3};
    constexpr std::array<std::uint8_t, 16U> firstUnitInRoomExpected{
        0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00, 0xC3,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 25U> isRoomInTownExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
        0x48, 0x8B, 0x49, 0x18, 0xE8, 0x57, 0x08, 0x07,
        0x00};
    constexpr std::array<std::uint8_t, 24U> drlgRoomActiveRoomExpected{
        0x48, 0x8B, 0x43, 0x58, 0x48, 0x8B, 0x5C, 0x24,
        0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xCC,
        0xCC, 0xCC, 0x40, 0x53, 0x56, 0x57, 0x41, 0x54};
    constexpr std::array<std::uint8_t, 46U> unitClassIdExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0x49, 0xCB, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
        0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x41,
        0x04, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 47U> unitDataContextExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83,
        0xC4, 0x28, 0xC3, 0x0F, 0xB6, 0x81, 0xBD, 0x01,
        0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 32U> unitClientCoordXExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x94, 0xA3, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01};
    constexpr std::array<std::uint8_t, 32U> unitClientCoordYExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x94, 0x9D, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01};
    constexpr std::array<std::uint8_t, 90U> unitRoomExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
        0xCC, 0x8B, 0x0B, 0x83, 0xE9, 0x02, 0x74, 0x25,
        0x83, 0xE9, 0x02, 0x74, 0x20, 0x83, 0xF9, 0x01,
        0x74, 0x1B, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85,
        0xC9, 0x74, 0x0A, 0x48, 0x83, 0xC4, 0x20, 0x5B,
        0xE9, 0xAB, 0x67, 0xFF, 0xFF, 0x33, 0xC0, 0x48,
        0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x43,
        0x38, 0x48, 0x8B, 0x00, 0x48, 0x83, 0xC4, 0x20,
        0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 46U> nextUnitInRoomExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C,
        0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
        0xB4, 0x9E, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
        0xCC, 0x48, 0x8B, 0x83, 0x60, 0x01, 0x00, 0x00,
        0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 32U> unitTypeExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 48U> levelListLayoutExpected{
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x02,
        0x00, 0x00, 0x48, 0x8B, 0x82, 0x68, 0x08, 0x00,
        0x00, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0xF2, 0x0F,
        0xB6, 0xE9, 0x48, 0x85, 0xC0, 0x74, 0x21, 0x66,
        0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 16U> drlgRoomLevelIdExpected{
        0x48, 0x8B, 0x81, 0x90, 0x00, 0x00, 0x00, 0x8B,
        0x80, 0xF8, 0x01, 0x00, 0x00, 0xC3, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 31U> getLevelVisCoreExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0x9A, 0x18, 0x01, 0x00, 0x00, 0x41, 0x8B,
        0xF8, 0x0F, 0xB6, 0xF1, 0x48, 0x85, 0xDB};
    constexpr std::array<std::uint8_t, 44U> getLevelVisNodeExpected{
        0x3B, 0x3B, 0x74, 0x28, 0x48, 0x8B, 0x5B, 0x48,
        0x48, 0x85, 0xDB, 0x75, 0xDA, 0x8B, 0xD7, 0x40,
        0x0F, 0xB6, 0xCE, 0xE8, 0xAE, 0xB9, 0xFC, 0xFF,
        0x48, 0x83, 0xC0, 0x48, 0x48, 0x8B, 0x5C, 0x24,
        0x30, 0x48, 0x8B, 0x74, 0x24, 0x40, 0x48, 0x83,
        0xC4, 0x20, 0x5F, 0xC3};
    constexpr std::array<std::uint8_t, 20U> getLevelVisDynamicExpected{
        0x48, 0x8B, 0x74, 0x24, 0x40, 0x48, 0x8D, 0x43,
        0x04, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83,
        0xC4, 0x20, 0x5F, 0xC3};
    constexpr std::array<std::uint8_t, 19U> getLevelVisExpected{
        0x44, 0x8B, 0x82, 0xF8, 0x01, 0x00, 0x00, 0x48,
        0x8B, 0x92, 0xC8, 0x01, 0x00, 0x00, 0xE9, 0x6D,
        0xFF, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 64U> nearRoomLayoutExpected{
        0x4C, 0x8B, 0xDC, 0x48, 0x83, 0xEC, 0x68, 0x48,
        0x8B, 0x81, 0x90, 0x00, 0x00, 0x00, 0x45, 0x33,
        0xD2, 0x49, 0x89, 0x73, 0xF0, 0x48, 0xC7, 0x41,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x89, 0x73,
        0xD0, 0x4C, 0x8B, 0xF1, 0x48, 0x8B, 0x70, 0x10,
        0x49, 0x89, 0x73, 0x10, 0x48, 0x85, 0xF6, 0x0F,
        0x84, 0xEB, 0x01, 0x00, 0x00, 0x49, 0x89, 0x5B,
        0x18, 0x49, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 68U> nearRoomGeometryExpected{
        0x41, 0x8B, 0x46, 0x60, 0x8B, 0x4E, 0x60, 0x3B,
        0xC1, 0x7D, 0x08, 0x41, 0x2B, 0x4E, 0x68, 0x2B,
        0xC8, 0xEB, 0x07, 0x2B, 0x46, 0x68, 0x2B, 0xC1,
        0x8B, 0xC8, 0x41, 0x8B, 0x56, 0x64, 0x8B, 0x46,
        0x64, 0x3B, 0xD0, 0x7D, 0x08, 0x41, 0x2B, 0x46,
        0x6C, 0x2B, 0xC2, 0xEB, 0x07, 0x2B, 0x56, 0x6C,
        0x2B, 0xD0, 0x8B, 0xC2, 0x83, 0xF9, 0x06, 0x0F,
        0x8D, 0x3C, 0x01, 0x00, 0x00, 0x83, 0xF8, 0x06,
        0x0F, 0x8D, 0x33, 0x01};
    constexpr std::array<std::uint8_t, 100U> nearRoomSortExpected{
        0x48, 0x8B, 0x76, 0x48, 0x48, 0x89, 0x74, 0x24,
        0x78, 0x48, 0x85, 0xF6, 0x0F, 0x85, 0x75, 0xFE,
        0xFF, 0xFF, 0x4D, 0x8B, 0x56, 0x18, 0x4C, 0x8B,
        0x7C, 0x24, 0x30, 0x4C, 0x8B, 0x6C, 0x24, 0x40,
        0x4C, 0x8B, 0x64, 0x24, 0x48, 0x48, 0x8B, 0x7C,
        0x24, 0x50, 0x48, 0x8B, 0x6C, 0x24, 0x60, 0x48,
        0x8B, 0x9C, 0x24, 0x80, 0x00, 0x00, 0x00, 0x4D,
        0x8B, 0x4E, 0x10, 0x4C, 0x8B, 0x74, 0x24, 0x38,
        0x48, 0x8B, 0x74, 0x24, 0x58, 0x49, 0x83, 0xFA,
        0x01, 0x76, 0x54, 0x49, 0x83, 0xEA, 0x01, 0x4D,
        0x8B, 0xDA, 0x74, 0x4B, 0x0F, 0x1F, 0x00, 0x33,
        0xD2, 0x0F, 0x1F, 0x40, 0x00, 0x66, 0x66, 0x0F,
        0x1F, 0x84, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 20U> objectsCountExpected{
        0x48, 0x83, 0xEC, 0x28, 0xE8, 0x17, 0x0E, 0xF7,
        0xFF, 0x8B, 0x80, 0x30, 0x15, 0x00, 0x00, 0x48,
        0x83, 0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 36U> objectsRecordExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B,
        0xDA, 0xE8, 0x7A, 0x0D, 0xF7, 0xFF, 0x48, 0x8B,
        0xF0, 0x8B, 0xFB, 0x48, 0x3B, 0x98, 0x30, 0x15,
        0x00, 0x00, 0x72, 0x14};
    constexpr std::array<std::uint8_t, 15U>
        roomTileDestinationLayoutExpected{
            0x48, 0x8B, 0x38, 0x48, 0x8B, 0x57, 0x78, 0x48,
            0x85, 0xD2, 0x74, 0xDC, 0x48, 0x39, 0x1A};
    constexpr std::array<std::uint8_t, 40U> getLevelWarpExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
        0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x9A, 0xC8,
        0x01, 0x00, 0x00, 0x0F, 0xB6, 0xE9, 0x8B, 0xBA,
        0xF8, 0x01, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0xF0};
    constexpr std::array<std::uint8_t, 28U> getLevelWarpNodeExpected{
        0x3B, 0x3B, 0x74, 0x2D, 0x48, 0x8B, 0x5B, 0x48,
        0x48, 0x85, 0xDB, 0x75, 0xDA, 0x8B, 0xD7, 0x40,
        0x0F, 0xB6, 0xCD, 0xE8, 0xCB, 0x16, 0xF5, 0xFF,
        0x8B, 0x44, 0xB0, 0x68};
    constexpr std::array<std::uint8_t, 6U> getLevelWarpDynamicExpected{
        0x8B, 0x44, 0xB3, 0x24, 0xEB, 0xE5};
    constexpr std::array<std::uint8_t, 50U>
        outdoorVisibilityFlagExpected{
            0x83, 0xC1, 0x04, 0xBD, 0x01, 0x00, 0x00, 0x00,
            0xD3, 0xE5, 0x45, 0x0F, 0xB6, 0xF8, 0x48, 0x8B,
            0xF2, 0x45, 0x8B, 0xCD, 0x48, 0x85, 0xFF, 0x0F,
            0x84, 0xE3, 0x00, 0x00, 0x00, 0x44, 0x8B, 0x74,
            0x24, 0x78, 0x0F, 0x1F, 0x00, 0x85, 0x6F, 0x50,
            0x0F, 0x84, 0xC5, 0x00, 0x00, 0x00, 0x41, 0x83,
            0xFE, 0xFF};
    constexpr std::array<std::uint8_t, 66U>
        outdoorVisibilityGeometryExpected{
            0x8B, 0x46, 0x60, 0x8B, 0x4F, 0x60, 0x3B, 0xC1,
            0x7D, 0x07, 0x2B, 0x4E, 0x68, 0x2B, 0xC8, 0xEB,
            0x07, 0x2B, 0x47, 0x68, 0x2B, 0xC1, 0x8B, 0xC8,
            0x8B, 0x56, 0x64, 0x8B, 0x47, 0x64, 0x3B, 0xD0,
            0x7D, 0x07, 0x2B, 0x46, 0x6C, 0x2B, 0xC2, 0xEB,
            0x07, 0x2B, 0x57, 0x6C, 0x2B, 0xD0, 0x8B, 0xC2,
            0x83, 0xF9, 0x06, 0x0F, 0x8D, 0x82, 0x00, 0x00,
            0x00, 0x83, 0xF8, 0x06, 0x0F, 0x8D, 0x79, 0x00,
            0x00, 0x00};
    constexpr std::array<std::uint8_t, 61U> playerPathMaskExpected{
        0xE8, 0xD2, 0x55, 0x04, 0x00, 0x8B, 0x4C, 0x24,
        0x30, 0x48, 0x8D, 0x54, 0x24, 0x38, 0x44, 0x8B,
        0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x83, 0xC1,
        0x03, 0x89, 0x4C, 0x24, 0x38, 0x41, 0xB9, 0x09,
        0x1C, 0x00, 0x00, 0x8B, 0x4C, 0x24, 0x34, 0x83,
        0xC1, 0x03, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00,
        0x00, 0x00, 0x89, 0x4C, 0x24, 0x3C, 0x48, 0x8B,
        0xCB, 0xE8, 0x59, 0x36, 0x07};
    constexpr std::array<std::uint8_t, 60U> monasteryAnchorExpected{
        0x49, 0x8B, 0x11, 0x8B, 0x8A, 0xF8, 0x01, 0x00,
        0x00, 0x83, 0xE9, 0x01, 0x74, 0x30, 0x83, 0xF9,
        0x19, 0x0F, 0x85, 0xA4, 0x00, 0x00, 0x00, 0x48,
        0x63, 0x87, 0x30, 0x03, 0x00, 0x00, 0x48, 0x83,
        0xC0, 0x0A, 0x48, 0x8D, 0x04, 0x40, 0x48, 0x8D,
        0x0C, 0xC7, 0x8B, 0x42, 0x24, 0x83, 0xC0, 0x1B,
        0x89, 0x01, 0x8B, 0x42, 0x28, 0x83, 0xC0, 0x0D,
        0xC6, 0x41, 0x08, 0x01};
    constexpr std::array<std::uint8_t, 43U> collisionGridLayoutExpected{
        0x4C, 0x8B, 0x53, 0x20, 0x4D, 0x85, 0xD2, 0x0F,
        0x84, 0x6A, 0x03, 0x00, 0x00, 0x44, 0x8B, 0x03,
        0x45, 0x8D, 0x5F, 0x01, 0x44, 0x8B, 0x4B, 0x04,
        0x33, 0xC0, 0x45, 0x3B, 0xC5, 0x48, 0x89, 0x74,
        0x24, 0x50, 0x48, 0x63, 0x73, 0x08, 0x0F, 0x9D,
        0xC0, 0x48, 0x89};
    constexpr std::array<std::uint8_t, 15U> collisionGridHeightExpected{
        0x8B, 0x4B, 0x0C, 0x41, 0x03, 0xC9, 0x41, 0x3B,
        0xCB, 0x0F, 0x4F, 0xC2, 0x83, 0xF8, 0x0C};
    constexpr std::array<std::uint8_t, 24U> resolveWaypointExpected{
        0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0x7A, 0x10, 0x4D, 0x8B, 0xF1, 0x4D, 0x8B, 0xF8};
    constexpr std::array<std::uint8_t, 88U> roomTileLayoutExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0x59, 0x78, 0x48, 0x8B, 0xC2, 0x48, 0x8B, 0xC8,
        0x48, 0x8D, 0x15, 0x99, 0x31, 0x93, 0x01, 0x41,
        0xB8, 0x17, 0x02, 0x00, 0x00, 0xE8, 0x2E, 0xEF,
        0xF6, 0xFF, 0x48, 0x85, 0xDB, 0x74, 0x29, 0x0F,
        0xB6, 0xC8, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00,
        0x48, 0x8B, 0x43, 0x20, 0x39, 0x48, 0x2C, 0x74,
        0x11, 0x48, 0x8B, 0x5B, 0x08, 0x48, 0x85, 0xDB,
        0x75, 0xEE, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20,
        0x5B, 0xC3, 0x83, 0x7B, 0x10, 0x00, 0x75, 0x02,
        0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 112U> waypointLayoutExpected{
        0x48, 0x8B, 0x9F, 0x98, 0x00, 0x00, 0x00, 0x48,
        0x85, 0xDB, 0x74, 0x66, 0x83, 0x7B, 0x20, 0x02,
        0x75, 0x24, 0x8B, 0x73, 0x04, 0x40, 0x0F, 0xB6,
        0xCD, 0xE8, 0x66, 0x4E, 0xFB, 0xFF, 0x3B, 0xF0,
        0x7D, 0x14, 0x8B, 0xD6, 0x40, 0x0F, 0xB6, 0xCD,
        0xE8, 0xE7, 0x4E, 0xFB, 0xFF, 0xF6, 0x80, 0x27,
        0x01, 0x00, 0x00, 0x40, 0x75, 0x0E, 0x48, 0x8B,
        0x5B, 0x10, 0x48, 0x85, 0xDB, 0x75, 0xCD, 0x48,
        0x8B, 0xC7, 0xEB, 0x31, 0xB8, 0x67, 0x66, 0x66,
        0x66, 0xF7, 0x6B, 0x08, 0xB8, 0x67, 0x66, 0x66,
        0x66, 0xD1, 0xFA, 0x8B, 0xCA, 0xC1, 0xE9, 0x1F,
        0x03, 0xD1, 0x03, 0x57, 0x60, 0x41, 0x89, 0x17,
        0xF7, 0x6B, 0x24, 0xD1, 0xFA, 0x8B, 0xCA, 0xC1,
         0xE9, 0x1F, 0x03, 0xD1, 0x03, 0x57, 0x64, 0x41};
    constexpr std::array<std::uint8_t, 15U> staffTombLevelExpected{
        0x48, 0x85, 0xC9, 0x74, 0x07, 0x8B, 0x81, 0x20,
        0x01, 0x00, 0x00, 0xC3, 0x33, 0xC0, 0xC3};
    constexpr std::array<std::uint8_t, 32U> questStateExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
        0x8B, 0xF0, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48,
        0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x38};
    constexpr std::array<std::uint8_t, 21U> clientQuestRecordExpected{
        0x48, 0x8B, 0x0D, 0x51, 0x3B, 0x93, 0x02, 0x45,
        0x33, 0xC0, 0x41, 0x8D, 0x50, 0x29, 0xE8, 0x1D,
        0x10, 0x21, 0x00, 0x85, 0xC0};
    const auto check = [context](
            std::uintptr_t rva,
            const auto& expected) noexcept {
        return context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()));
    };
    return check(GetLocalDataContextRva, localContextExpected)
        && check(GetLocalPlayerRva, localPlayerExpected)
        && check(GetDrlgRoomFromActiveRoomRva, activeRoomDrlgExpected)
        && check(PlayerPathMaskWitnessRva, playerPathMaskExpected)
        && check(GetCollisionGridRva, collisionGridExpected)
        && check(GetFirstUnitInRoomRva, firstUnitInRoomExpected)
        && check(IsRoomInTownRva, isRoomInTownExpected)
        && check(
            DrlgRoomActiveRoomWitnessRva,
            drlgRoomActiveRoomExpected)
        && check(GetUnitClassIdRva, unitClassIdExpected)
        && check(GetUnitDataContextRva, unitDataContextExpected)
        && check(GetUnitClientCoordXRva, unitClientCoordXExpected)
        && check(GetUnitClientCoordYRva, unitClientCoordYExpected)
        && check(GetUnitRoomRva, unitRoomExpected)
        && check(GetNextUnitInRoomRva, nextUnitInRoomExpected)
        && check(GetUnitTypeRva, unitTypeExpected)
        && check(LevelListLayoutWitnessRva, levelListLayoutExpected)
        && check(
            GetHoradricStaffTombLevelIdRva,
            staffTombLevelExpected)
        && check(GetQuestStateRva, questStateExpected)
        && check(
            ClientQuestRecordWitnessRva,
            clientQuestRecordExpected)
        && check(GetDrlgRoomLevelIdRva, drlgRoomLevelIdExpected)
        && check(GetLevelVisCoreRva, getLevelVisCoreExpected)
        && check(GetLevelVisNodeWitnessRva, getLevelVisNodeExpected)
        && check(
            GetLevelVisDynamicWitnessRva,
            getLevelVisDynamicExpected)
        && check(GetLevelVisRva, getLevelVisExpected)
        && check(NearRoomLayoutWitnessRva, nearRoomLayoutExpected)
        && check(NearRoomGeometryWitnessRva, nearRoomGeometryExpected)
        && check(NearRoomSortWitnessRva, nearRoomSortExpected)
        && check(GetObjectsTxtRecordCountRva, objectsCountExpected)
        && check(GetObjectsTxtRecordRva, objectsRecordExpected)
        && check(RoomTileLayoutWitnessRva, roomTileLayoutExpected)
        && check(
            RoomTileDestinationLayoutWitnessRva,
            roomTileDestinationLayoutExpected)
        && check(GetLevelWarpRva, getLevelWarpExpected)
        && check(GetLevelWarpNodeWitnessRva, getLevelWarpNodeExpected)
        && check(
            GetLevelWarpDynamicWitnessRva,
            getLevelWarpDynamicExpected)
        && check(
            OutdoorVisibilityFlagWitnessRva,
            outdoorVisibilityFlagExpected)
        && check(
            OutdoorVisibilityGeometryWitnessRva,
            outdoorVisibilityGeometryExpected)
        && check(MonasteryAnchorWitnessRva, monasteryAnchorExpected)
        && check(
            CollisionGridLayoutWitnessRva,
            collisionGridLayoutExpected)
        && check(
            CollisionGridHeightWitnessRva,
            collisionGridHeightExpected)
        && check(
            FindWaypointRoomAndCoordinatesRva,
            resolveWaypointExpected)
        && check(WaypointLayoutWitnessRva, waypointLayoutExpected);
}

[[nodiscard]] auto ResolveCustomTargetIds(
        std::span<const CustomLevelTarget> targets,
        std::array<std::int32_t, MaximumCustomLevelTargets>& ids) noexcept
        -> std::size_t {
    std::size_t count{};
    for (const auto& target : targets) {
        std::optional<std::int32_t> resolved;
        if (const auto* const id = std::get_if<std::int32_t>(&target)) {
            resolved = *id;
        } else if (const auto* const name = std::get_if<std::string>(&target)) {
            resolved = ResolveCanonicalLevelName(*name);
            if (!resolved) {
                UnresolvedNames.fetch_add(1U, std::memory_order_relaxed);
                if (Diagnostics.load(std::memory_order_acquire)
                    && Context != nullptr) {
                    char message[256]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "MapSense navigation: level_name '%.*s' is unknown or ambiguous; use level_id.",
                        static_cast<int>(name->size()),
                        name->c_str());
                    Context->LogWarn(message);
                }
                continue;
            }
        }
        if (!resolved || *resolved <= 0
            || *resolved > MaximumSupportedLevelId) {
            continue;
        }
        bool duplicate{};
        for (std::size_t index = 0U; index < count; ++index) {
            if (ids[index] == *resolved) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && count < ids.size()) ids[count++] = *resolved;
    }
    return count;
}

void MixDiagnosticValue(
        std::uint64_t& hash,
        std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= UINT64_C(1099511628211);
    }
}

void LogBoundedNavigationDiagnostics(
        const CandidateBatch& batch,
        std::span<const NavigationSubtileDestination> destinations,
        std::int32_t publishedWaypointX,
        std::int32_t publishedWaypointY,
        std::int32_t publishedProgressionX,
        std::int32_t publishedProgressionY) noexcept {
    if (!Diagnostics.load(std::memory_order_acquire) || Context == nullptr) {
        return;
    }
    auto fingerprint = UINT64_C(1469598103934665603);
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.levelId));
    MixDiagnosticValue(fingerprint, batch.inTown ? 1U : 0U);
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.waypointNativeTileX));
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.waypointNativeTileY));
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.waypointExactSubtileX));
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.waypointExactSubtileY));
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.waypointExactClientX));
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.waypointExactClientY));
    MixDiagnosticValue(fingerprint, batch.pendingWaypoint ? 1U : 0U);
    MixDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(batch.correctTombLevelId));
    MixDiagnosticValue(fingerprint, batch.hasCorrectTombState ? 1U : 0U);
    MixDiagnosticValue(fingerprint, batch.durielRewardGranted ? 1U : 0U);
    MixDiagnosticValue(fingerprint, batch.hasCorrectTomb ? 1U : 0U);
    MixDiagnosticValue(fingerprint, batch.pendingCorrectTomb ? 1U : 0U);
    MixDiagnosticValue(fingerprint, batch.rawRoomTileCount);
    MixDiagnosticValue(fingerprint, batch.nativeRoomTileCount);
    MixDiagnosticValue(fingerprint, batch.exactRoomTileCount);
    MixDiagnosticValue(fingerprint, batch.pendingRoomTileRoomCount);
    MixDiagnosticValue(fingerprint, batch.requestedTargetLevelCount);
    MixDiagnosticValue(fingerprint, batch.initializedTargetLevelCount);
    MixDiagnosticValue(fingerprint, batch.pendingTargetLevelCount);
    MixDiagnosticValue(fingerprint, batch.nearRoomLinkCount);
    MixDiagnosticValue(fingerprint, batch.outdoorOpeningCount);
    MixDiagnosticValue(fingerprint, batch.outdoorSourceSpanCount);
    MixDiagnosticValue(fingerprint, batch.outdoorTargetSpanCount);
    MixDiagnosticValue(fingerprint, batch.outdoorMergedSpanCount);
    MixDiagnosticValue(fingerprint, batch.ambiguousOutdoorTargetCount);
    MixDiagnosticValue(fingerprint, batch.pendingCollisionRoomCount);
    MixDiagnosticValue(fingerprint, batch.visibilitySlotCount);
    MixDiagnosticValue(fingerprint, batch.visibilityPairCount);
    MixDiagnosticValue(fingerprint, batch.pendingVisibilityTargetCount);
    MixDiagnosticValue(fingerprint, batch.exitSelectionCount);
    for (std::size_t index = 0U;
            index < batch.exitSelectionCount;
            ++index) {
        const auto& selection = batch.exitSelections[index];
        MixDiagnosticValue(
            fingerprint,
            selection.candidate.destinationId);
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(
                selection.candidate.targetLevelId));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(selection.candidate.subtileX));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(selection.candidate.subtileY));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(
                selection.candidate.exactClientX));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(
                selection.candidate.exactClientY));
        MixDiagnosticValue(
            fingerprint,
            selection.candidate.useExactClientCoordinates ? 1U : 0U);
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint8_t>(selection.evidence));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(selection.spanSubtiles));
    }
    MixDiagnosticValue(fingerprint, destinations.size());
    for (const auto& destination : destinations) {
        MixDiagnosticValue(fingerprint, destination.destinationId);
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(destination.subtileX));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(destination.subtileY));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint8_t>(destination.kind));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(destination.exactClientX));
        MixDiagnosticValue(
            fingerprint,
            static_cast<std::uint32_t>(destination.exactClientY));
        MixDiagnosticValue(
            fingerprint,
            destination.useExactClientCoordinates ? 1U : 0U);
    }
    if (LastDiagnosticFingerprint.exchange(
            fingerprint,
            std::memory_order_acq_rel) == fingerprint) {
        return;
    }

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "MapSense navigation: level=%d town=%d; waypoint native-tile=(%d,%d) exact-subtile=(%d,%d) exact-client=(%d,%d) pending=%d published-subtile=(%d,%d); correct-tomb=%d state=%d reward=%d found=%d pending=%d; exits raw=%llu linked=%llu exact=%llu roomtile-pending=%llu near-links=%llu outdoor-openings=%llu collision-pending=%llu target-levels=%llu/%llu pending=%llu vis-slots=%llu vis-pairs=%llu vis-pending=%llu candidates=%zu published=%zu; progression-subtile=(%d,%d).",
        batch.levelId,
        batch.inTown ? 1 : 0,
        batch.waypointNativeTileX,
        batch.waypointNativeTileY,
        batch.waypointExactSubtileX,
        batch.waypointExactSubtileY,
        batch.waypointExactClientX,
        batch.waypointExactClientY,
        batch.pendingWaypoint ? 1 : 0,
        publishedWaypointX,
        publishedWaypointY,
        batch.correctTombLevelId,
        batch.hasCorrectTombState ? 1 : 0,
        batch.durielRewardGranted ? 1 : 0,
        batch.hasCorrectTomb ? 1 : 0,
        batch.pendingCorrectTomb ? 1 : 0,
        static_cast<unsigned long long>(batch.rawRoomTileCount),
        static_cast<unsigned long long>(batch.nativeRoomTileCount),
        static_cast<unsigned long long>(batch.exactRoomTileCount),
        static_cast<unsigned long long>(batch.pendingRoomTileRoomCount),
        static_cast<unsigned long long>(batch.nearRoomLinkCount),
        static_cast<unsigned long long>(batch.outdoorOpeningCount),
        static_cast<unsigned long long>(batch.pendingCollisionRoomCount),
        static_cast<unsigned long long>(batch.initializedTargetLevelCount),
        static_cast<unsigned long long>(batch.requestedTargetLevelCount),
        static_cast<unsigned long long>(batch.pendingTargetLevelCount),
        static_cast<unsigned long long>(batch.visibilitySlotCount),
        static_cast<unsigned long long>(batch.visibilityPairCount),
        static_cast<unsigned long long>(
            batch.pendingVisibilityTargetCount),
        batch.exitCount,
        destinations.size(),
        publishedProgressionX,
        publishedProgressionY);
    Context->LogInfo(message);

    std::snprintf(
        message,
        sizeof(message),
        "MapSense diagnostic outdoor-boundary: source-spans=%llu target-spans=%llu merged-spans=%llu unique-openings=%llu ambiguous-targets=%llu collision-pending=%llu.",
        static_cast<unsigned long long>(batch.outdoorSourceSpanCount),
        static_cast<unsigned long long>(batch.outdoorTargetSpanCount),
        static_cast<unsigned long long>(batch.outdoorMergedSpanCount),
        static_cast<unsigned long long>(batch.outdoorOpeningCount),
        static_cast<unsigned long long>(batch.ambiguousOutdoorTargetCount),
        static_cast<unsigned long long>(batch.pendingCollisionRoomCount));
    Context->LogInfo(message);

    const auto preferredProgression = MainProgressionTargetFor(batch.levelId);
    const auto selectedProgression = batch.hasCorrectTomb
            && batch.durielRewardGranted
        ? std::optional<std::int32_t>(batch.correctTombLevelId)
        : SelectMainProgressionTargetFor(
            batch.levelId,
            std::span(batch.exits.data(), batch.exitCount));
    std::snprintf(
        message,
        sizeof(message),
        "MapSense diagnostic resolver: level=%d preferred-progression=%d selected-progression=%d candidate-found=%d selection-count=%zu destination-count=%zu.",
        batch.levelId,
        preferredProgression.value_or(UnknownNavigationLevelId),
        selectedProgression.value_or(UnknownNavigationLevelId),
        selectedProgression.has_value() ? 1 : 0,
        batch.exitSelectionCount,
        destinations.size());
    Context->LogInfo(message);

    for (std::size_t index = 0U;
            index < batch.exitSelectionCount;
            ++index) {
        const auto& selection = batch.exitSelections[index];
        const char* evidence = "outdoor-level-boundary";
        if (selection.evidence == Detail::NavigationExitEvidence::RoomTile) {
            evidence = "room-tile";
        } else if (selection.evidence
                == Detail::NavigationExitEvidence::RuntimeObject) {
            evidence = "runtime-object";
        }
        std::snprintf(
            message,
            sizeof(message),
            "MapSense diagnostic exit[%zu]: target-level=%d subtile=(%d,%d) exact-client=(%d,%d/%d) evidence=%s span=%d id=%llu.",
            index,
            selection.candidate.targetLevelId,
            selection.candidate.subtileX,
            selection.candidate.subtileY,
            selection.candidate.exactClientX,
            selection.candidate.exactClientY,
            selection.candidate.useExactClientCoordinates ? 1 : 0,
            evidence,
            selection.spanSubtiles,
            static_cast<unsigned long long>(
                selection.candidate.destinationId));
        Context->LogInfo(message);
    }

    for (std::size_t index = 0U;
            index < destinations.size();
            ++index) {
        const auto& destination = destinations[index];
        std::snprintf(
            message,
            sizeof(message),
            "MapSense diagnostic published[%zu]: kind=%u subtile=(%d,%d) id=%llu.",
            index,
            static_cast<unsigned>(destination.kind),
            destination.subtileX,
            destination.subtileY,
            static_cast<unsigned long long>(destination.destinationId));
        Context->LogInfo(message);
    }
}

void ResetCounters() noexcept {
    Refreshes.store(0U, std::memory_order_relaxed);
    Rooms.store(0U, std::memory_order_relaxed);
    Presets.store(0U, std::memory_order_relaxed);
    Exits.store(0U, std::memory_order_relaxed);
    Waypoints.store(0U, std::memory_order_relaxed);
    Published.store(0U, std::memory_order_relaxed);
    UnresolvedNames.store(0U, std::memory_order_relaxed);
    Failures.store(0U, std::memory_order_relaxed);
    TraversalLimits.store(0U, std::memory_order_relaxed);
    PartialRefreshes.store(0U, std::memory_order_relaxed);
    VisibilitySlots.store(0U, std::memory_order_relaxed);
    VisibilityPairs.store(0U, std::memory_order_relaxed);
    PendingVisibilityTargets.store(0U, std::memory_order_relaxed);
    RevealedLevelsCaptured.store(0U, std::memory_order_relaxed);
    RevealedLabelsCaptured.store(0U, std::memory_order_relaxed);
    RevealedWaypointsCaptured.store(0U, std::memory_order_relaxed);
    LastLevelId.store(UnknownNavigationLevelId, std::memory_order_relaxed);
    LastDestinationCount.store(0U, std::memory_order_relaxed);
    LastWaypointX.store(0, std::memory_order_relaxed);
    LastWaypointY.store(0, std::memory_order_relaxed);
    LastProgressionX.store(0, std::memory_order_relaxed);
    LastProgressionY.store(0, std::memory_order_relaxed);
    LastDiagnosticFingerprint.store(UINT64_MAX, std::memory_order_relaxed);
}

} // namespace

auto InitializeNavigationResolver(
        const D2RL::PluginContext* context,
        bool diagnostics) noexcept -> bool {
    ShutdownNavigationResolver();
    if (!D2RL::HasContext(context) || context->exeBase == 0U) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    Diagnostics.store(diagnostics, std::memory_order_release);
    ResetCounters();
    if (!ValidateRuntime(context)) {
        Context = nullptr;
        Base = nullptr;
        return false;
    }
    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    GetDrlgRoomFromActiveRoom = At<GetDrlgRoomFromActiveRoomFn>(
        GetDrlgRoomFromActiveRoomRva);
    GetCollisionGrid = At<GetCollisionGridFn>(GetCollisionGridRva);
    GetFirstUnitInRoom = At<GetFirstUnitInRoomFn>(
        GetFirstUnitInRoomRva);
    IsRoomInTown = At<IsRoomInTownFn>(IsRoomInTownRva);
    GetUnitClassId = At<GetUnitClassIdFn>(GetUnitClassIdRva);
    GetUnitDataContext = At<GetUnitDataContextFn>(GetUnitDataContextRva);
    GetUnitClientCoordX = At<GetUnitClientCoordFn>(GetUnitClientCoordXRva);
    GetUnitClientCoordY = At<GetUnitClientCoordFn>(GetUnitClientCoordYRva);
    GetUnitRoom = At<GetUnitRoomFn>(GetUnitRoomRva);
    GetNextUnitInRoom = At<GetNextUnitInRoomFn>(GetNextUnitInRoomRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetDrlgRoomLevelId = At<GetDrlgRoomLevelIdFn>(
        GetDrlgRoomLevelIdRva);
    GetHoradricStaffTombLevelId = At<GetHoradricStaffTombLevelIdFn>(
        GetHoradricStaffTombLevelIdRva);
    GetQuestState = At<GetQuestStateFn>(GetQuestStateRva);
    GetLevelVis = At<GetLevelVisFn>(GetLevelVisRva);
    GetLevelWarp = At<GetLevelWarpFn>(GetLevelWarpRva);
    GetObjectsTxtRecordCount = At<GetObjectsTxtRecordCountFn>(
        GetObjectsTxtRecordCountRva);
    GetObjectsTxtRecord = At<GetObjectsTxtRecordFn>(
        GetObjectsTxtRecordRva);
    FindWaypointRoomAndCoordinates =
        At<FindWaypointRoomAndCoordinatesFn>(
            FindWaypointRoomAndCoordinatesRva);
    Active.store(true, std::memory_order_release);
    return true;
}

auto BindNavigationResolverCatalog(
        std::shared_ptr<const MapSenseDataCatalog> catalog) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire) || catalog == nullptr) {
        return false;
    }
    ResolverCatalog.store(std::move(catalog), std::memory_order_release);
    return true;
}

void ShutdownNavigationResolver() noexcept {
    Active.store(false, std::memory_order_release);
    ResolverCatalog.store(nullptr, std::memory_order_release);
    GetLocalDataContext = nullptr;
    GetLocalPlayer = nullptr;
    GetDrlgRoomFromActiveRoom = nullptr;
    GetCollisionGrid = nullptr;
    GetFirstUnitInRoom = nullptr;
    IsRoomInTown = nullptr;
    GetUnitClassId = nullptr;
    GetUnitDataContext = nullptr;
    GetUnitClientCoordX = nullptr;
    GetUnitClientCoordY = nullptr;
    GetUnitRoom = nullptr;
    GetNextUnitInRoom = nullptr;
    GetUnitType = nullptr;
    GetDrlgRoomLevelId = nullptr;
    GetHoradricStaffTombLevelId = nullptr;
    GetQuestState = nullptr;
    GetLevelVis = nullptr;
    GetLevelWarp = nullptr;
    GetObjectsTxtRecordCount = nullptr;
    GetObjectsTxtRecord = nullptr;
    FindWaypointRoomAndCoordinates = nullptr;
    Base = nullptr;
    Context = nullptr;
}

auto RefreshNavigationDestinations(
        std::uint64_t sessionGeneration,
        std::int32_t expectedLevelId,
        std::span<const CustomLevelTarget> customTargets) noexcept
        -> NavigationRefreshResult {
    if (!Active.load(std::memory_order_acquire)) {
        return NavigationRefreshResult::Failed;
    }
    // The refresh contract is UI-thread-only. This bounded lease both reuses
    // the 512 KiB level-boundary scratch across all targets in one refresh and
    // fails closed if an unexpected reentrant caller violates that contract.
    OutdoorSpanScratchLease scratchLease;
    if (!scratchLease) return NavigationRefreshResult::Failed;
    Refreshes.fetch_add(1U, std::memory_order_relaxed);

    std::array<std::int32_t, MaximumCustomLevelTargets> customIds{};
    const auto customIdCount = ResolveCustomTargetIds(
        customTargets,
        customIds);
    CandidateBatch batch{};
    if (!EnumerateCurrentLevelUnchecked(
            expectedLevelId,
            std::span(customIds.data(), customIdCount),
            batch)) {
        Rooms.fetch_add(batch.roomCount, std::memory_order_relaxed);
        Presets.fetch_add(batch.presetCount, std::memory_order_relaxed);
        if (batch.traversalLimited) {
            TraversalLimits.fetch_add(1U, std::memory_order_relaxed);
        }
        Failures.fetch_add(1U, std::memory_order_relaxed);
        LogBoundedNavigationDiagnostics(
            batch,
            std::span<const NavigationSubtileDestination>{},
            0,
            0,
            0,
            0);
        return NavigationRefreshResult::Failed;
    }

    std::array<NavigationSubtileDestination, MaximumNavigationDestinations>
        destinations{};
    std::array<NavigationPointCandidate, MaximumNavigationDestinations>
        questTargets{};
    auto questTargetCount = batch.questTargetCount;
    if (questTargetCount != 0U) {
        std::copy_n(
            batch.questTargets.begin(),
            questTargetCount,
            questTargets.begin());
    }
    if (batch.hasCorrectTomb && !batch.durielRewardGranted
        && questTargetCount < questTargets.size()) {
        questTargets[questTargetCount++] = batch.correctTomb;
    }
    const auto correctTombProgressionOverride = batch.hasCorrectTomb
            && batch.durielRewardGranted
        ? std::optional<std::int32_t>(batch.correctTombLevelId)
        : std::nullopt;
    const auto destinationCount = BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = batch.levelId,
            .inTown = batch.inTown,
            .exits = std::span(batch.exits.data(), batch.exitCount),
            .waypoint = batch.hasWaypoint ? &batch.waypoint : nullptr,
            .questTargets = std::span(
                questTargets.data(),
                questTargetCount),
            .customTargetLevelIds = std::span(
                customIds.data(),
                customIdCount),
            .progressionTargetOverride = correctTombProgressionOverride,
        },
        destinations);

    Rooms.fetch_add(batch.roomCount, std::memory_order_relaxed);
    Presets.fetch_add(batch.presetCount, std::memory_order_relaxed);
    Exits.fetch_add(batch.exitCount, std::memory_order_relaxed);
    Waypoints.fetch_add(batch.hasWaypoint ? 1U : 0U, std::memory_order_relaxed);
    VisibilitySlots.fetch_add(
        batch.visibilitySlotCount,
        std::memory_order_relaxed);
    VisibilityPairs.fetch_add(
        batch.visibilityPairCount,
        std::memory_order_relaxed);
    PendingVisibilityTargets.fetch_add(
        batch.pendingVisibilityTargetCount,
        std::memory_order_relaxed);

    // Publishing an empty town line batch is intentional: it clears any line
    // from the level active before the transition. The independently retained
    // waypoint label may still be published below.
    if (!BindNavigationLevelForPublish(sessionGeneration, batch.levelId)
        || !PublishNavigationDestinations(
            sessionGeneration,
            batch.levelId,
            destinations.data(),
            destinationCount)) {
        Failures.fetch_add(1U, std::memory_order_relaxed);
        return NavigationRefreshResult::Failed;
    }
    AutomapWaypointLabelDefinition waypointLabel{};
    const auto hasWaypointLabel = CopyWaypointLabelDefinition(
        batch,
        waypointLabel);
    // A transient resolver miss must not erase an exact waypoint already
    // proven for this level. Only a complete no-waypoint result owns an empty
    // replacement; pending keeps the existing owner entry untouched.
    if (!batch.pendingWaypoint) {
        if (batch.hasWaypoint != hasWaypointLabel
                || !PublishNativeAutomapWaypointLabels(
                    sessionGeneration,
                    batch.levelId,
                    hasWaypointLabel ? &waypointLabel : nullptr,
                    hasWaypointLabel ? 1U : 0U)) {
            Failures.fetch_add(1U, std::memory_order_relaxed);
            return NavigationRefreshResult::Failed;
        }
    }
    std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
        exitLabels{};
    std::array<
        Detail::NavigationPhysicalExitSelection,
        MaximumAutomapExitLabels> physicalExitLabels{};
    std::size_t physicalExitLabelCount{};
    if (!Detail::ReducePhysicalExitEvidence(
            batch.exitLabelEvidence,
            batch.exitLabelEvidenceCount,
            physicalExitLabels,
            physicalExitLabelCount)) {
        Failures.fetch_add(1U, std::memory_order_relaxed);
        return NavigationRefreshResult::Failed;
    }
    // Navigation retains one target winner, while labels retain every distinct
    // physical doorway and spatially merge competing evidence for that door.
    for (std::size_t index = 0U;
            index < physicalExitLabelCount;
            ++index) {
        const auto& exit = physicalExitLabels[index].winner.candidate;
        exitLabels[index] = {
            .stableId = exit.destinationId,
            .sourceLevelId = batch.levelId,
            .targetLevelId = exit.targetLevelId,
            .subtileX = exit.subtileX,
            .subtileY = exit.subtileY,
            .exactClientX = exit.exactClientX,
            .exactClientY = exit.exactClientY,
            .useExactClientCoordinates = exit.useExactClientCoordinates,
            .boundaryIdentity =
                physicalExitLabels[index].boundaryIdentity,
            .canonicalLevelPairAnchor =
                physicalExitLabels[index].winner.canonicalLevelPairAnchor,
        };
    }
    (void)PublishNativeAutomapExitLabels(
        sessionGeneration,
        batch.levelId,
        exitLabels.data(),
        physicalExitLabelCount);
    if (!PublishNativeAutomapSpecialChests(
            sessionGeneration,
            batch.levelId,
            batch.specialChestPresets.data(),
            batch.specialChestPresetCount)) {
        Failures.fetch_add(1U, std::memory_order_relaxed);
        return NavigationRefreshResult::Failed;
    }
    Published.fetch_add(destinationCount, std::memory_order_relaxed);
    LastLevelId.store(batch.levelId, std::memory_order_relaxed);
    LastDestinationCount.store(
        static_cast<std::uint32_t>(destinationCount),
        std::memory_order_relaxed);
    LastWaypointX.store(0, std::memory_order_relaxed);
    LastWaypointY.store(0, std::memory_order_relaxed);
    LastProgressionX.store(0, std::memory_order_relaxed);
    LastProgressionY.store(0, std::memory_order_relaxed);
    std::int32_t publishedWaypointX{};
    std::int32_t publishedWaypointY{};
    std::int32_t publishedProgressionX{};
    std::int32_t publishedProgressionY{};
    for (std::size_t index = 0U; index < destinationCount; ++index) {
        const auto& destination = destinations[index];
        if (destination.kind == NavigationLineKind::Waypoint) {
            publishedWaypointX = destination.subtileX;
            publishedWaypointY = destination.subtileY;
            LastWaypointX.store(destination.subtileX, std::memory_order_relaxed);
            LastWaypointY.store(destination.subtileY, std::memory_order_relaxed);
        } else if (destination.kind == NavigationLineKind::Progression) {
            publishedProgressionX = destination.subtileX;
            publishedProgressionY = destination.subtileY;
            LastProgressionX.store(destination.subtileX, std::memory_order_relaxed);
            LastProgressionY.store(destination.subtileY, std::memory_order_relaxed);
        }
    }
    LogBoundedNavigationDiagnostics(
        batch,
        std::span(destinations.data(), destinationCount),
        publishedWaypointX,
        publishedWaypointY,
        publishedProgressionX,
        publishedProgressionY);

    if (batch.inTown) return NavigationRefreshResult::Complete;

    const auto completeness = EvaluateNavigationResolutionCompleteness(
        batch.levelId,
        std::span(batch.exits.data(), batch.exitCount));
    if (batch.pendingWaypoint || batch.pendingCorrectTomb
        || batch.pendingCollisionRoomCount != 0U
        || batch.pendingRoomTileRoomCount != 0U
        || completeness
            == NavigationResolutionCompleteness::PartialRetryable) {
        PartialRefreshes.fetch_add(1U, std::memory_order_relaxed);
        return NavigationRefreshResult::PartialRetryable;
    }
    return NavigationRefreshResult::Complete;
}

auto ObserveInitializedClientLevelPoiDefinitions(
        std::uint64_t sessionGeneration,
        std::uint8_t dataContext,
        void* level) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
            || sessionGeneration == 0U || level == nullptr) {
        return false;
    }
    // A nested InitLevel belongs to the same synchronous reveal traversal.
    // Queue it for the outer observer instead of dropping it behind the shared
    // scratch lease. The outer call drains every queued pointer before control
    // returns to D2R.
    if (InitializedLevelCaptureActive) {
        return QueueInitializedLevelCapture(dataContext, level);
    }
    OutdoorSpanScratchLease scratchLease;
    if (!scratchLease) return false;

    InitializedLevelCaptureScope captureScope;
    if (!QueueInitializedLevelCapture(dataContext, level)) return false;

    // First walk is discovery-only. Resolving one source can synchronously
    // initialize and enqueue its neighbouring Levels; publishing that source
    // immediately records an incomplete, mostly reverse-only graph.
    std::size_t cursor{};
    while (cursor < InitializedLevelCaptureQueueCount) {
        const auto request = InitializedLevelCaptureQueue[cursor++];
        std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
            labels{};
        std::size_t labelCount{};
        std::int32_t sourceLevelId{UnknownNavigationLevelId};
        AutomapWaypointLabelDefinition waypoint{};
        bool hasWaypoint{};
        bool pendingWaypoint{};
        bool pendingExits{};
        (void)CollectInitializedLevelPoiDefinitionsUnchecked(
            request.dataContext,
            request.level,
            labels,
            labelCount,
            sourceLevelId,
            waypoint,
            hasWaypoint,
            pendingWaypoint,
            pendingExits);
    }

    // Revisit the complete queue after discovery has settled. A second bounded
    // pass is available only if the first settled walk exposes one last nested
    // Level; the final pass atomically replaces every source-owned definition.
    bool complete{};
    bool settled{};
    std::uint64_t capturedLevels{};
    std::uint64_t capturedLabels{};
    std::uint64_t capturedWaypoints{};
    std::uint64_t completeExitOwners{};
    std::uint64_t partialExitOwners{};
    std::uint64_t mergedExitFragments{};
    constexpr std::size_t MaximumSettlementPasses = 3U;
    for (std::size_t pass = 0U;
            pass < MaximumSettlementPasses && !settled;
            ++pass) {
        const auto passLevelCount = InitializedLevelCaptureQueueCount;
        bool passComplete = true;
        std::uint64_t passCapturedLevels{};
        std::uint64_t passCapturedLabels{};
        std::uint64_t passCapturedWaypoints{};
        std::uint64_t passCompleteExitOwners{};
        std::uint64_t passPartialExitOwners{};
        std::uint64_t passMergedExitFragments{};
        for (std::size_t index = 0U; index < passLevelCount; ++index) {
            const auto request = InitializedLevelCaptureQueue[index];
            std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels>
                labels{};
            std::size_t labelCount{};
            std::int32_t sourceLevelId{UnknownNavigationLevelId};
            AutomapWaypointLabelDefinition waypoint{};
            bool hasWaypoint{};
            bool pendingWaypoint{};
            bool pendingExits{};
            if (!CollectInitializedLevelPoiDefinitionsUnchecked(
                    request.dataContext,
                    request.level,
                    labels,
                    labelCount,
                    sourceLevelId,
                    waypoint,
                    hasWaypoint,
                    pendingWaypoint,
                    pendingExits)) {
                passComplete = false;
                continue;
            }
            const auto publication = Detail::MakePassivePoiPublicationPolicy(
                pendingExits,
                pendingWaypoint);
            if (!publication.publishExitLabels) {
                passComplete = false;
                ++passPartialExitOwners;
                // Every emitted definition already owns physical native
                // evidence. Retain that proven subset while unresolved rooms
                // remain pending; a later complete capture still replaces the
                // source owner atomically.
                if (publication.mergeProvenExitFragments
                    && labelCount != 0U) {
                    if (!MergeNativeAutomapExitLabelFragments(
                            sessionGeneration,
                            sourceLevelId,
                            labels.data(),
                            labelCount)) {
                        passComplete = false;
                    } else {
                        passMergedExitFragments += labelCount;
                    }
                }
            } else if (!PublishNativeAutomapExitLabels(
                           sessionGeneration,
                           sourceLevelId,
                           labels.data(),
                           labelCount)) {
                passComplete = false;
            } else {
                ++passCompleteExitOwners;
            }
            if (!publication.publishWaypoint) {
                // A missing or ambiguous preset chain cannot erase an exact
                // waypoint retained from an earlier proven collection.
                passComplete = false;
            } else if (!PublishNativeAutomapWaypointLabels(
                    sessionGeneration,
                    sourceLevelId,
                    hasWaypoint ? &waypoint : nullptr,
                    hasWaypoint ? 1U : 0U)) {
                passComplete = false;
            } else if (hasWaypoint) {
                ++passCapturedWaypoints;
            }
            ++passCapturedLevels;
            passCapturedLabels += labelCount;
        }
        settled = passLevelCount == InitializedLevelCaptureQueueCount;
        complete = passComplete && settled;
        capturedLevels = passCapturedLevels;
        capturedLabels = passCapturedLabels;
        capturedWaypoints = passCapturedWaypoints;
        completeExitOwners = passCompleteExitOwners;
        partialExitOwners = passPartialExitOwners;
        mergedExitFragments = passMergedExitFragments;
    }
    RevealedLevelsCaptured.fetch_add(
        capturedLevels,
        std::memory_order_relaxed);
    RevealedLabelsCaptured.fetch_add(
        capturedLabels,
        std::memory_order_relaxed);
    RevealedWaypointsCaptured.fetch_add(
        capturedWaypoints,
        std::memory_order_relaxed);
    if (Context != nullptr) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense reveal capture: session=%llu queued-levels=%zu captured-levels=%llu labels=%llu complete-exit-owners=%llu partial-exit-owners=%llu merged-fragments=%llu waypoints=%llu settled=%d overflow=%d.",
            static_cast<unsigned long long>(sessionGeneration),
            InitializedLevelCaptureQueueCount,
            static_cast<unsigned long long>(capturedLevels),
            static_cast<unsigned long long>(capturedLabels),
            static_cast<unsigned long long>(completeExitOwners),
            static_cast<unsigned long long>(partialExitOwners),
            static_cast<unsigned long long>(mergedExitFragments),
            static_cast<unsigned long long>(capturedWaypoints),
            settled ? 1 : 0,
            InitializedLevelCaptureQueueOverflow ? 1 : 0);
        Context->LogInfo(message);
    }
    return complete && !InitializedLevelCaptureQueueOverflow;
}

auto DiscoverProgressiveRevealLevelTargets(
        const ClientLevelView& current,
        std::int32_t levelId,
        std::array<std::int32_t, ProgressiveRevealVisCapacity>& targets,
        std::size_t& targetCount) noexcept -> bool {
    targets.fill(UnknownRevealLevelId);
    targetCount = 0U;
    if (!Active.load(std::memory_order_acquire)
        || current.dataContext >= 8U
        || !IsAlignedPointer(current.drlg)
        || levelId <= 0 || levelId > MaximumSupportedLevelId
        || GetLevelVis == nullptr) {
        return false;
    }
    __try {
        void* levelPointer{};
        if (!ResolveClientLevelById(current, levelId, levelPointer)
            || !IsAlignedPointer(levelPointer)) {
            return false;
        }
        auto* const level = static_cast<std::uint8_t*>(levelPointer);
        if (*reinterpret_cast<std::uint8_t**>(
                level + LevelDrlgOffset) != current.drlg
            || *reinterpret_cast<const std::int32_t*>(
                level + LevelIdOffset) != levelId) {
            return false;
        }
        auto* const vis = GetLevelVis(current.dataContext, level);
        if (vis == nullptr) return false;
        for (std::size_t slot = 0U;
                slot < ProgressiveRevealVisCapacity;
                ++slot) {
            const auto targetLevelId = vis[slot];
            if (targetLevelId <= 0
                || targetLevelId > MaximumSupportedLevelId
                || targetLevelId == levelId) {
                continue;
            }
            bool duplicate{};
            for (std::size_t index = 0U; index < targetCount; ++index) {
                if (targets[index] == targetLevelId) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            if (targetCount >= targets.size()) return false;
            targets[targetCount++] = targetLevelId;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto CaptureProgressiveRevealLevelPoiDefinitions(
        std::uint64_t sessionGeneration,
        const ClientLevelView& current,
        std::int32_t levelId) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration == 0U || levelId <= 0) {
        return false;
    }
    void* levelPointer{};
    if (!ResolveClientLevelById(current, levelId, levelPointer)
        || levelPointer == nullptr) {
        return false;
    }
    const auto before = GetRevealCounters();
    StaticPoiRoomLeaseSet leases{};
    const bool prepared = PrepareStaticPoiRoomsUnchecked(
        current.dataContext,
        levelPointer,
        leases);
    const bool captured = ObserveInitializedClientLevelPoiDefinitions(
        sessionGeneration,
        current.dataContext,
        levelPointer);
    const bool released = ReleaseStaticPoiRooms(leases);
    const auto after = GetRevealCounters();
    if (Context != nullptr
        && (after.staticRoomsMaterialized != before.staticRoomsMaterialized
            || after.staticRoomFailures != before.staticRoomFailures)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense static POI capture: level=%d candidates=%llu materialized=%llu released=%llu failures=%llu active-room-calls=0.",
            levelId,
            static_cast<unsigned long long>(
                after.staticRoomCandidates - before.staticRoomCandidates),
            static_cast<unsigned long long>(
                after.staticRoomsMaterialized
                    - before.staticRoomsMaterialized),
            static_cast<unsigned long long>(
                after.staticRoomsReleased - before.staticRoomsReleased),
            static_cast<unsigned long long>(
                after.staticRoomFailures - before.staticRoomFailures));
        Context->LogInfo(message);
    }
    return prepared && captured && released;
}

auto RefreshRevealedActPoiDefinitions(
        std::uint64_t sessionGeneration) noexcept
        -> NavigationRefreshResult {
    if (!Active.load(std::memory_order_acquire)) {
        return NavigationRefreshResult::Failed;
    }
    OutdoorSpanScratchLease scratchLease;
    if (!scratchLease) return NavigationRefreshResult::Failed;

    std::array<AutomapExitLabelDefinition, MaximumAutomapExitLabels> labels{};
    std::array<
        RevealedWaypointResolution,
        MaximumAutomapWaypointLabels> waypointResolutions{};
    std::size_t labelCount{};
    std::size_t waypointResolutionCount{};
    std::size_t levelCount{};
    std::uint64_t pendingCount{};
    const auto enumerated = EnumerateRevealedActPoiDefinitionsUnchecked(
            labels,
            labelCount,
            waypointResolutions,
            waypointResolutionCount,
            levelCount,
            pendingCount);
    if (!enumerated) {
        Failures.fetch_add(1U, std::memory_order_relaxed);
        if (Diagnostics.load(std::memory_order_acquire)
                && Context != nullptr) {
            Context->LogWarn(
                "MapSense revealed-act POIs: DRLG enumeration failed; the existing catalogs were preserved.");
        }
        return NavigationRefreshResult::Failed;
    }

    // During progressive generation, pendingCount == 0 for one scan does not
    // by itself prove that every Vis-connected Level has already joined the
    // list. The session/act reset is therefore the sole full-catalog
    // invalidation. Every scan updates only source Levels it can prove and
    // preserves earlier copied captures until the scheduler settles.
    bool published = true;
    std::size_t first{};
    while (first < labelCount) {
        const auto sourceLevelId = labels[first].sourceLevelId;
        auto end = first + 1U;
        while (end < labelCount
                && labels[end].sourceLevelId == sourceLevelId) {
            ++end;
        }
        if (!PublishNativeAutomapExitLabels(
                sessionGeneration,
                sourceLevelId,
                labels.data() + first,
                end - first)) {
            published = false;
            break;
        }
        first = end;
    }
    for (std::size_t index = 0U;
            published && index < waypointResolutionCount;
            ++index) {
        const auto& resolution = waypointResolutions[index];
        if (!PublishNativeAutomapWaypointLabels(
                sessionGeneration,
                resolution.levelId,
                resolution.hasWaypoint ? &resolution.definition : nullptr,
                resolution.hasWaypoint ? 1U : 0U)) {
            published = false;
        }
    }
    if (!published) {
        Failures.fetch_add(1U, std::memory_order_relaxed);
        if (Diagnostics.load(std::memory_order_acquire)
                && Context != nullptr) {
            Context->LogWarn(
                "MapSense revealed-act POIs: owner publication failed; no guessed definition was added.");
        }
        return NavigationRefreshResult::Failed;
    }
    if (Diagnostics.load(std::memory_order_acquire)
            && Context != nullptr) {
        char message[352]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense revealed-act POIs: retained-init-levels=%llu retained-init-labels=%llu retained-init-waypoints=%llu linked-levels=%zu linked-labels=%zu linked-waypoint-owners=%zu pending=%llu.",
            static_cast<unsigned long long>(
                RevealedLevelsCaptured.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                RevealedLabelsCaptured.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                RevealedWaypointsCaptured.load(std::memory_order_relaxed)),
            levelCount,
            labelCount,
            waypointResolutionCount,
            static_cast<unsigned long long>(pendingCount));
        Context->LogInfo(message);
    }
    if (pendingCount != 0U) {
        PartialRefreshes.fetch_add(1U, std::memory_order_relaxed);
        return NavigationRefreshResult::PartialRetryable;
    }
    return NavigationRefreshResult::Complete;
}

auto IsNavigationResolverActive() noexcept -> bool {
    return Active.load(std::memory_order_acquire);
}

auto GetNavigationResolverCounters() noexcept -> NavigationResolverCounters {
    return {
        .refreshes = Refreshes.load(std::memory_order_relaxed),
        .rooms = Rooms.load(std::memory_order_relaxed),
        .presets = Presets.load(std::memory_order_relaxed),
        .exits = Exits.load(std::memory_order_relaxed),
        .waypoints = Waypoints.load(std::memory_order_relaxed),
        .published = Published.load(std::memory_order_relaxed),
        .unresolvedNames = UnresolvedNames.load(std::memory_order_relaxed),
        .failures = Failures.load(std::memory_order_relaxed),
        .traversalLimits = TraversalLimits.load(std::memory_order_relaxed),
        .partialRefreshes = PartialRefreshes.load(std::memory_order_relaxed),
        .visibilitySlots = VisibilitySlots.load(std::memory_order_relaxed),
        .visibilityPairs = VisibilityPairs.load(std::memory_order_relaxed),
        .pendingVisibilityTargets = PendingVisibilityTargets.load(
            std::memory_order_relaxed),
        .lastLevelId = LastLevelId.load(std::memory_order_relaxed),
        .lastDestinationCount = LastDestinationCount.load(
            std::memory_order_relaxed),
        .lastWaypointX = LastWaypointX.load(std::memory_order_relaxed),
        .lastWaypointY = LastWaypointY.load(std::memory_order_relaxed),
        .lastProgressionX = LastProgressionX.load(std::memory_order_relaxed),
        .lastProgressionY = LastProgressionY.load(std::memory_order_relaxed),
    };
}

} // namespace RuffnecKk::MapSense
