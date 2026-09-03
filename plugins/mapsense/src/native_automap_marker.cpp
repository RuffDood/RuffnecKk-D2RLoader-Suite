#include "native_automap_marker.hpp"

#include "navigation_engine.hpp"
#include "native_automap_missile.hpp"
#include "native_automap_poi.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t GetNativeHeightRva = 0x07F4A0;
constexpr std::uintptr_t GetNativeWidthRva = 0x07F510;
constexpr std::uintptr_t ProjectClientToAutomapRva = 0x0D4910;
constexpr std::uintptr_t RenderAutomapUnitRva = 0x0D76E0;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitAlignmentRva = 0x2F4190;
constexpr std::uintptr_t GetUnitIdRva = 0x34A330;
constexpr std::uintptr_t GetUnitClassIdRva = 0x349860;
constexpr std::uintptr_t GetUnitDataContextRva = 0x34A0E0;
constexpr std::uintptr_t GetUnitModeRva = 0x34AB60;
constexpr std::uintptr_t GetDynamicPathRva = 0x34AE80;
constexpr std::uintptr_t GetUnitClientXRva = 0x34AF60;
constexpr std::uintptr_t GetUnitClientYRva = 0x34AFB0;
constexpr std::uintptr_t PathGetXRva = 0x341A20;
constexpr std::uintptr_t PathGetYRva = 0x341A30;
constexpr std::uintptr_t GetDataTablesForContextRva = 0x300A90;
constexpr std::uintptr_t GetMonStatsRecordRva = 0x0976E0;
constexpr std::uintptr_t GetSuperUniqueIndexRva = 0x38E3D0;
constexpr std::uintptr_t MonsterRankWitnessRva = 0x51F280;
constexpr std::uintptr_t GetUnitRoomRva = 0x34B440;
constexpr std::uintptr_t UnitRoomLayoutWitnessRva = 0x34B461;
constexpr std::uintptr_t ActiveRoomGetDrlgRoomRva = 0x192B20;
constexpr std::uintptr_t PathGetRoomRva = 0x341C30;
constexpr std::uintptr_t IsRoomInTownRva = 0x2F0750;
constexpr std::uintptr_t GetDrlgRoomLevelIdRva = 0x360FC0;
constexpr std::uintptr_t ClientUnitHashTableRva = 0x2A23910;
constexpr std::uintptr_t ClientUnitHashTableWitnessRva = 0x09A5D0;
constexpr std::uintptr_t ClientUnitHashLookupWitnessRva = 0x09F270;
constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitTypeDataOffset = 0x10;
constexpr std::size_t UnitFlagsOffset = 0x124;
constexpr std::size_t UnitHashNextOffset = 0x158;
constexpr std::size_t ActiveRoomDrlgRoomOffset = 0x18;
constexpr std::size_t MonsterRankFlagsOffset = 0x1A;
constexpr std::size_t MonStatsFlagsOffset = 0x3C;
constexpr std::size_t DataTablesMonStatsCountOffset = 0xF60;
constexpr std::size_t AutomapClipLeftOffset = 0x18;
constexpr std::size_t AutomapClipTopOffset = 0x1C;
constexpr std::size_t AutomapClipWidthOffset = 0x20;
constexpr std::size_t AutomapClipHeightOffset = 0x24;
constexpr std::int32_t AtlasProjectionBasisClientUnits = 256;

constexpr std::uint32_t UnitMonster = 1;
constexpr std::int32_t MaximumSupportedLevelId = 65'535;
constexpr std::uint32_t MonsterModeDeath = 0;
constexpr std::uint32_t MonsterModeDead = 12;
constexpr std::int32_t PhysicalResistanceStatId = 36;
constexpr std::int32_t MagicResistanceStatId = 37;
constexpr std::int32_t FireResistanceStatId = 39;
constexpr std::int32_t LightningResistanceStatId = 41;
constexpr std::int32_t ColdResistanceStatId = 43;
constexpr std::int32_t PoisonResistanceStatId = 45;
constexpr std::uint64_t MarkerLifetimeMilliseconds = 250;
// Discovery is deliberately slower than the bounded live refresh. Positions
// are refreshed from copied unit ids; repeating the full metadata/immunity
// walk at marker cadence would put the expensive work back on the renderer hot
// path.
constexpr std::uint64_t MonsterTableScanIntervalMilliseconds = 100;
constexpr std::size_t UnitHashBucketCount = 128;
constexpr std::size_t DistanceBandCount = 4;
constexpr std::size_t UnitHashTypeStride = UnitHashBucketCount
    * sizeof(void*);
constexpr std::size_t MaximumUnitsPerMonsterTableScan =
    MaximumNativeAutomapMarkers;
constexpr std::size_t MaximumUnitsPerMonsterBucket = 8'192;
constexpr std::uint64_t ObservationsPerChunk = 4'096;
constexpr std::uint64_t InitialObservationChunkCount = 16;
constexpr std::uint32_t ObservationBufferCount = 2;

struct NativePoint final {
    std::int32_t x{};
    std::int32_t y{};
};

struct Candidate final {
    std::uint32_t unitId{};
    std::int32_t classId{-1};
    std::int32_t superUniqueIndex{-1};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    MonsterRank rank{MonsterRank::Normal};
    std::uint8_t immunityMask{};
    std::uint64_t distanceSquared{};
    std::uint64_t observedTick{};
    std::uint64_t epoch{};
};

struct TrackedMonster final {
    std::uint32_t unitId{};
    std::int32_t classId{-1};
    std::int32_t superUniqueIndex{-1};
    MonsterRank rank{MonsterRank::Normal};
    std::uint8_t immunityMask{};
};

struct MonsterScanContext final {
    void* automapContext{};
    std::uint64_t monStatsRecordCount{};
    std::uint16_t playerSubtileX{};
    std::uint16_t playerSubtileY{};
    std::uint8_t dataContext{};
    std::int32_t nativeWidth{};
    std::int32_t nativeHeight{};
    std::int32_t clipLeft{};
    std::int32_t clipTop{};
    std::int32_t clipWidth{};
    std::int32_t clipHeight{};
    std::uint64_t currentTick{};
    std::uint64_t epoch{};
};

struct AtomicFlagRelease final {
    std::atomic_flag& flag;

    explicit AtomicFlagRelease(std::atomic_flag& value) noexcept
        : flag(value) {}
    AtomicFlagRelease(const AtomicFlagRelease&) = delete;
    auto operator=(const AtomicFlagRelease&) -> AtomicFlagRelease& = delete;

    ~AtomicFlagRelease() {
        flag.clear(std::memory_order_release);
    }
};

struct ObservationChunk final {
    std::array<Candidate, ObservationsPerChunk> observations{};
    std::atomic<ObservationChunk*> next{};
};

struct ObservationBuffer final {
    ObservationChunk* first{};
    std::atomic<std::uint64_t> count{};
    std::atomic<std::uint32_t> writers{};
    std::atomic<std::uint64_t> epoch{};

    ObservationBuffer() = default;
    ObservationBuffer(const ObservationBuffer&) = delete;
    auto operator=(const ObservationBuffer&) -> ObservationBuffer& = delete;

    ~ObservationBuffer() {
        auto* chunk = first;
        while (chunk != nullptr) {
            auto* const next = chunk->next.load(std::memory_order_relaxed);
            delete chunk;
            chunk = next;
        }
    }
};

using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using GetNativeDimensionFn = std::int32_t(__fastcall*)() noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void* unit,
    std::int32_t statId,
    std::uint16_t layer) noexcept;
using GetUnitValueFn = std::uint32_t(__fastcall*)(void*) noexcept;
using GetUnitSignedValueFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetUnitCoordinateFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetNativePointerFn = void*(__fastcall*)(void*) noexcept;
using GetLevelIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using IsRoomInTownFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetDataTablesForContextFn = std::uint8_t*(__fastcall*)(
    std::uint8_t dataContext) noexcept;
using GetMonStatsRecordFn = const std::uint8_t*(__fastcall*)(
    std::uint8_t dataContext,
    std::int32_t classId) noexcept;
using GetSuperUniqueIndexFn = std::int32_t(__fastcall*)(void*) noexcept;
using ProjectClientToAutomapFn = NativePoint*(__fastcall*)(
    void* automapContext,
    NativePoint* output,
    std::uint64_t packedClientCoordinates) noexcept;
using RenderAutomapUnitFn = void(__fastcall*)(
    void* unit,
    void* automapContext) noexcept;
using GetUnitByIdAndTypeFn = void*(__fastcall*)(
    std::uint32_t unitId,
    std::uint32_t unitType) noexcept;

std::uint8_t* Base{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
GetNativeDimensionFn GetNativeHeight{};
GetNativeDimensionFn GetNativeWidth{};
GetUnitStatFn GetUnitStat{};
GetUnitSignedValueFn GetUnitAlignment{};
GetUnitValueFn GetUnitId{};
GetUnitSignedValueFn GetUnitClassId{};
GetUnitDataContextFn GetUnitDataContext{};
GetUnitValueFn GetUnitMode{};
GetNativePointerFn GetDynamicPath{};
GetUnitCoordinateFn GetUnitClientX{};
GetUnitCoordinateFn GetUnitClientY{};
GetUnitCoordinateFn PathGetX{};
GetUnitCoordinateFn PathGetY{};
GetNativePointerFn GetUnitRoom{};
GetLevelIdFn GetDrlgRoomLevelId{};
IsRoomInTownFn IsRoomInTown{};
GetDataTablesForContextFn GetDataTablesForContext{};
GetMonStatsRecordFn GetMonStatsRecord{};
GetSuperUniqueIndexFn GetSuperUniqueIndex{};
GetUnitByIdAndTypeFn GetUnitByIdAndType{};
ProjectClientToAutomapFn ProjectClientToAutomap{};
RenderAutomapUnitFn OriginalRenderAutomapUnit{};
const D2RL::PluginContext* DiagnosticContext{};

std::atomic_bool Active{};
std::atomic_bool CollectionEnabled{};
std::atomic_bool ImmunityCollectionEnabled{};
std::atomic_bool LocalPlayerFrameAlive{};
std::atomic<std::uint64_t> Epoch{};
std::atomic<std::uint64_t> LastAutomapPulseTick{};
std::atomic<std::uint64_t> LastMonsterTableScanTick{};
std::atomic<std::uint64_t> PublishedSequence{};
std::atomic<std::uint64_t> TrackedMarkerCount{};
Detail::NavigationProjectionDiagnosticCache NavigationProjectionDiagnostics;
std::atomic_flag NavigationProjectionDiagnosticsLock = ATOMIC_FLAG_INIT;
std::array<ObservationBuffer, ObservationBufferCount> ObservationBuffers{};
std::atomic<std::uint64_t> ActiveObservationState{};
std::int32_t PendingObservationBuffer{-1};
std::uint64_t RendererEpoch{};
std::uint64_t LastPurgeTick{};
std::unordered_map<std::uint32_t, Candidate> MarkerCache;
std::atomic_flag NativeAutomapViewportLock = ATOMIC_FLAG_INIT;
NativeAutomapViewportSnapshot PublishedNativeAutomapViewport{};
std::vector<TrackedMonster> TrackedMonsters;
std::vector<TrackedMonster> DiscoveryScratch;
std::uint64_t TrackedMonsterEpoch{};
std::atomic_flag TrackedMonsterLock = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> TrackedMonsterCount{};
std::int64_t PerformanceCounterFrequency{};

std::atomic<std::uint64_t> AutomapPulses{};
std::atomic<std::uint64_t> MonsterTableScans{};
std::atomic<std::uint64_t> MonsterPositionRefreshes{};
std::atomic<std::uint64_t> TrackedIdsResolved{};
std::atomic<std::uint64_t> TrackedIdsMissing{};
std::atomic<std::uint64_t> MonsterBucketsVisited{};
std::atomic<std::uint64_t> MonsterTraversalLimits{};
std::atomic<std::uint64_t> UnitsObserved{};
std::atomic<std::uint64_t> MonstersObserved{};
std::atomic<std::uint64_t> ModeRejected{};
std::atomic<std::uint64_t> UnitFlagRejected{};
std::atomic<std::uint64_t> ClassRejected{};
std::atomic<std::uint64_t> AlignmentRejected{};
std::atomic<std::uint64_t> MetadataFaults{};
std::atomic<std::uint64_t> HostilesObserved{};
std::atomic<std::uint64_t> HostilesThrough80{};
std::atomic<std::uint64_t> HostilesFrom81Through140{};
std::atomic<std::uint64_t> HostilesFrom141Through220{};
std::atomic<std::uint64_t> HostilesBeyond220{};
std::atomic<std::uint64_t> ProjectionRejected{};
std::atomic<std::uint64_t> NativeClipRejected{};
std::array<std::atomic<std::uint64_t>, DistanceBandCount>
    AcceptedByDistanceBand{};
std::array<std::atomic<std::uint64_t>, DistanceBandCount>
    ClipRejectedByDistanceBand{};
std::atomic<std::uint64_t> CandidatesAccepted{};
std::atomic<std::uint64_t> MarkersInserted{};
std::atomic<std::uint64_t> MarkersRefreshed{};
std::atomic<std::uint64_t> MarkersExpired{};
std::atomic<std::uint64_t> ContentionWaits{};
std::atomic<std::uint64_t> StorageFailures{};
std::atomic<std::uint64_t> AccessFaults{};
std::atomic<std::uint64_t> MaximumDiscoveryMicroseconds{};
std::atomic<std::uint64_t> TotalDiscoveryMicroseconds{};
std::atomic<std::uint64_t> DiscoveryTimingSamples{};
std::atomic<std::uint64_t> MaximumRefreshMicroseconds{};
std::atomic<std::uint64_t> TotalRefreshMicroseconds{};
std::atomic<std::uint64_t> RefreshTimingSamples{};
std::atomic<std::uint64_t> MaximumHostileDistanceSquared{};
std::atomic<std::uint64_t> MaximumAcceptedDistanceSquared{};
std::atomic<std::uint64_t> MaximumPublishedDistanceSquared{};
std::atomic<NativeAutomapLevelObservedCallback> LevelObservedCallback{};
std::atomic<void*> LevelObservedUserData{};

static_assert(std::is_trivially_copyable_v<NativeAutomapMarkerSnapshot>);
static_assert(std::is_standard_layout_v<NativeAutomapMarkerSnapshot>);
static_assert(sizeof(Candidate) == 56U);
static_assert(sizeof(NativeAutomapMarkerSnapshot) == 48U);
static_assert(sizeof(NativePoint) == sizeof(std::uint64_t));
static_assert(UnitHashTypeStride == 0x400U);
static_assert(
    static_cast<std::size_t>(
        Detail::WorldSubtileDistanceBand::Beyond220) + 1U
    == DistanceBandCount);
static_assert(
    Detail::MaximumNavigationProjectionDiagnosticEntries
    >= MaximumNavigationDestinations);

template <typename Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
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

auto DistanceFromSquared(std::uint64_t squared) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::llround(
        std::sqrt(static_cast<long double>(squared))));
}

[[nodiscard]] auto PerformanceCounterMicroseconds() noexcept
        -> std::uint64_t {
    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) == FALSE
        || counter.QuadPart < 0 || PerformanceCounterFrequency <= 0) {
        return 0U;
    }
    const auto wholeSeconds = counter.QuadPart / PerformanceCounterFrequency;
    const auto remainder = counter.QuadPart % PerformanceCounterFrequency;
    return static_cast<std::uint64_t>(wholeSeconds) * UINT64_C(1000000)
        + static_cast<std::uint64_t>(
            (remainder * INT64_C(1000000))
                / PerformanceCounterFrequency);
}

auto IsRecent(
        std::uint64_t tick,
        std::uint64_t currentTick) noexcept -> bool {
    return tick != 0 && currentTick >= tick
        && (currentTick - tick) <= MarkerLifetimeMilliseconds;
}

[[nodiscard]] auto IsAlignedPointer(const void* pointer) noexcept -> bool {
    return pointer != nullptr
        && (reinterpret_cast<std::uintptr_t>(pointer)
            & (alignof(void*) - 1U)) == 0U;
}

[[nodiscard]] __declspec(noinline) auto TryReadCurrentLevelId(
        void* player,
        std::int32_t& levelId,
        bool& inTown) noexcept -> bool {
    __try {
        if (!IsAlignedPointer(player)) return false;
        if (GetUnitRoom == nullptr || GetDrlgRoomLevelId == nullptr
            || IsRoomInTown == nullptr) {
            return false;
        }
        auto* const activeRoom = static_cast<std::uint8_t*>(
            GetUnitRoom(player));
        if (!IsAlignedPointer(activeRoom)) return false;
        auto* const drlgRoom = *reinterpret_cast<std::uint8_t**>(
            activeRoom + ActiveRoomDrlgRoomOffset);
        if (!IsAlignedPointer(drlgRoom)) return false;
        const auto candidate = GetDrlgRoomLevelId(drlgRoom);
        if (candidate <= 0 || candidate > MaximumSupportedLevelId) {
            return false;
        }
        levelId = candidate;
        inTown = IsRoomInTown(activeRoom) != 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto ProjectNavigationClient(
        void* borrowedAutomapContext,
        std::int32_t clientX,
        std::int32_t clientY,
        NavigationNativePoint& output) noexcept -> bool {
    __try {
        if (borrowedAutomapContext == nullptr
            || ProjectClientToAutomap == nullptr) {
            return false;
        }
        NativePoint projected{};
        const auto packedClientCoordinates =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(clientX))
            | (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(clientY)) << 32U);
        if (ProjectClientToAutomap(
                borrowedAutomapContext,
                &projected,
                packedClientCoordinates) != &projected) {
            return false;
        }
        output = {.x = projected.x, .y = projected.y};
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void MixNavigationDiagnosticValue(
        std::uint64_t& hash,
        std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= UINT64_C(1099511628211);
    }
}

[[nodiscard]] auto ShouldLogNavigationProjectionDiagnostic(
        const NavigationProjectionDiagnostic& diagnostic,
        std::uint64_t fingerprint) noexcept -> bool {
    while (NavigationProjectionDiagnosticsLock.test_and_set(
            std::memory_order_acquire)) {
        YieldProcessor();
    }
    const auto shouldLog = NavigationProjectionDiagnostics.ShouldLog(
        diagnostic.currentLevelId,
        static_cast<std::uint8_t>(diagnostic.destination.kind),
        diagnostic.destination.destinationId,
        fingerprint);
    NavigationProjectionDiagnosticsLock.clear(std::memory_order_release);
    return shouldLog;
}

[[nodiscard]] auto NavigationKindName(
        NavigationLineKind kind) noexcept -> const char* {
    switch (kind) {
        case NavigationLineKind::Waypoint: return "waypoint";
        case NavigationLineKind::Progression: return "progression";
        case NavigationLineKind::CustomLevel: return "custom";
        case NavigationLineKind::Quest: return "quest";
    }
    return "unknown";
}

void LogNavigationProjectionDiagnostic(
        const NavigationProjectionDiagnostic& diagnostic,
        void* userData) noexcept {
    const auto* const context = static_cast<const D2RL::PluginContext*>(
        userData);
    if (context == nullptr || context != DiagnosticContext) return;

    auto fingerprint = UINT64_C(1469598103934665603);
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.currentLevelId));
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint8_t>(diagnostic.destination.kind));
    MixNavigationDiagnosticValue(
        fingerprint,
        diagnostic.destination.destinationId);
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.destination.subtileX));
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.destination.subtileY));
    MixNavigationDiagnosticValue(
        fingerprint,
        diagnostic.playerProjected ? 1U : 0U);
    MixNavigationDiagnosticValue(
        fingerprint,
        diagnostic.destinationProjected ? 1U : 0U);
    MixNavigationDiagnosticValue(
        fingerprint,
        diagnostic.lineClipped ? 1U : 0U);
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.clipLeft));
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.clipTop));
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.clipWidth));
    MixNavigationDiagnosticValue(
        fingerprint,
        static_cast<std::uint32_t>(diagnostic.clipHeight));

    if (!ShouldLogNavigationProjectionDiagnostic(diagnostic, fingerprint)) {
        return;
    }

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "MapSense diagnostic projection: level=%d kind=%s id=%llu; player-client=(%d,%d) projected=%d:(%d,%d); destination-subtile=(%d,%d) client=%d:(%d,%d) projected=%d:(%d,%d); clip=(%d,%d,%d,%d) accepted=%d line=(%d,%d)->(%d,%d).",
        diagnostic.currentLevelId,
        NavigationKindName(diagnostic.destination.kind),
        static_cast<unsigned long long>(
            diagnostic.destination.destinationId),
        diagnostic.playerClientX,
        diagnostic.playerClientY,
        diagnostic.playerProjected ? 1 : 0,
        diagnostic.projectedPlayer.x,
        diagnostic.projectedPlayer.y,
        diagnostic.destination.subtileX,
        diagnostic.destination.subtileY,
        diagnostic.destinationConverted ? 1 : 0,
        diagnostic.destinationClient.x,
        diagnostic.destinationClient.y,
        diagnostic.destinationProjected ? 1 : 0,
        diagnostic.projectedDestination.x,
        diagnostic.projectedDestination.y,
        diagnostic.clipLeft,
        diagnostic.clipTop,
        diagnostic.clipWidth,
        diagnostic.clipHeight,
        diagnostic.lineClipped ? 1 : 0,
        diagnostic.clippedStart.x,
        diagnostic.clippedStart.y,
        diagnostic.clippedEnd.x,
        diagnostic.clippedEnd.y);
    context->LogInfo(message);
}

[[nodiscard]] auto TryGetLocalPlayerPass(void* unit) noexcept -> void* {
    __try {
        if (unit == nullptr || GetLocalDataContext == nullptr
            || GetLocalPlayer == nullptr) {
            return nullptr;
        }
        const auto localDataContext = GetLocalDataContext();
        if (localDataContext < 0 || localDataContext >= 8) return nullptr;
        void* const player = GetLocalPlayer(localDataContext);
        return player == unit ? player : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

[[nodiscard]] auto IsObservedLocalPlayerAlive(void* player) noexcept -> bool {
    __try {
        return player != nullptr && GetUnitMode != nullptr
            && Detail::IsLocalPlayerAliveMode(GetUnitMode(player));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ObserveNavigationPlayerPass(
        void* player,
        void* automapContext) noexcept {
    __try {
        if (player == nullptr || automapContext == nullptr
            || GetNativeHeight == nullptr || GetNativeWidth == nullptr
            || GetUnitClientX == nullptr || GetUnitClientY == nullptr
            || ProjectClientToAutomap == nullptr) {
            return;
        }

        std::int32_t currentLevelId{UnknownNavigationLevelId};
        bool inTown{};
        if (!TryReadCurrentLevelId(player, currentLevelId, inTown)) return;

        const auto nativeWidth = GetNativeWidth();
        const auto nativeHeight = GetNativeHeight();
        if (nativeWidth <= 0 || nativeHeight <= 0
            || nativeWidth > 32768 || nativeHeight > 32768) {
            return;
        }
        const auto* const contextBytes = static_cast<const std::uint8_t*>(
            automapContext);
        const auto* const diagnosticContext = DiagnosticContext;
        const auto playerClientX = GetUnitClientX(player);
        const auto playerClientY = GetUnitClientY(player);
        const NavigationAutomapPass pass{
            .currentLevelId = currentLevelId,
            .inTown = inTown,
            .playerClientX = playerClientX,
            .playerClientY = playerClientY,
            .nativeWidth = nativeWidth,
            .nativeHeight = nativeHeight,
            .clipLeft = *reinterpret_cast<const std::int32_t*>(
                contextBytes + AutomapClipLeftOffset),
            .clipTop = *reinterpret_cast<const std::int32_t*>(
                contextBytes + AutomapClipTopOffset),
            .clipWidth = *reinterpret_cast<const std::int32_t*>(
                contextBytes + AutomapClipWidthOffset),
            .clipHeight = *reinterpret_cast<const std::int32_t*>(
                contextBytes + AutomapClipHeightOffset),
            .projectClient = ProjectNavigationClient,
            .borrowedAutomapContext = automapContext,
            .diagnostic = diagnosticContext != nullptr
                ? LogNavigationProjectionDiagnostic
                : nullptr,
            .diagnosticUserData = const_cast<D2RL::PluginContext*>(
                diagnosticContext),
        };
        NativeAutomapViewportSnapshot viewport{
            .nativeWidth = pass.nativeWidth,
            .nativeHeight = pass.nativeHeight,
            .clipLeft = pass.clipLeft,
            .clipTop = pass.clipTop,
            .clipWidth = pass.clipWidth,
            .clipHeight = pass.clipHeight,
            .observedTick = static_cast<std::uint64_t>(GetTickCount64()),
            .epoch = Epoch.load(std::memory_order_acquire),
        };
        NativeAutomapClipBounds clipBounds{};
        if (TryResolveNativeAutomapClipBounds(viewport, clipBounds)
            && !NativeAutomapViewportLock.test_and_set(
                std::memory_order_acquire)) {
            PublishedNativeAutomapViewport = viewport;
            NativeAutomapViewportLock.clear(std::memory_order_release);
        }
        const auto observation = ObserveNavigationAutomapPass(pass);
        ObserveNativeAutomapPoiPass(NativeAutomapPoiPass{
            .currentLevelId = currentLevelId,
            .inTown = inTown,
            .playerClientX = pass.playerClientX,
            .playerClientY = pass.playerClientY,
            .nativeWidth = nativeWidth,
            .nativeHeight = nativeHeight,
            .clipLeft = pass.clipLeft,
            .clipTop = pass.clipTop,
            .clipWidth = pass.clipWidth,
            .clipHeight = pass.clipHeight,
            .projectClient = pass.projectClient,
            .borrowedAutomapContext = automapContext,
        });
        const auto callback = LevelObservedCallback.load(
            std::memory_order_acquire);
        if (callback != nullptr) {
            callback(
                currentLevelId,
                ShouldRequestNavigationRefresh(observation, inTown),
                LevelObservedUserData.load(std::memory_order_acquire));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Navigation is an optional observer. A bad transient native object
        // suppresses this pulse without affecting D2R or monster markers.
    }
}

void ResetPublishedMarkers(bool resetCounters) noexcept {
    LocalPlayerFrameAlive.store(false, std::memory_order_release);
    Epoch.fetch_add(1U, std::memory_order_acq_rel);
    LastAutomapPulseTick.store(0U, std::memory_order_release);
    LastMonsterTableScanTick.store(0U, std::memory_order_release);
    TrackedMarkerCount.store(0U, std::memory_order_release);
    TrackedMonsterCount.store(0U, std::memory_order_release);
    PublishedSequence.fetch_add(1U, std::memory_order_acq_rel);
    if (!resetCounters) return;
    AutomapPulses.store(0U, std::memory_order_relaxed);
    MonsterTableScans.store(0U, std::memory_order_relaxed);
    MonsterPositionRefreshes.store(0U, std::memory_order_relaxed);
    TrackedIdsResolved.store(0U, std::memory_order_relaxed);
    TrackedIdsMissing.store(0U, std::memory_order_relaxed);
    MonsterBucketsVisited.store(0U, std::memory_order_relaxed);
    MonsterTraversalLimits.store(0U, std::memory_order_relaxed);
    UnitsObserved.store(0U, std::memory_order_relaxed);
    MonstersObserved.store(0U, std::memory_order_relaxed);
    ModeRejected.store(0U, std::memory_order_relaxed);
    UnitFlagRejected.store(0U, std::memory_order_relaxed);
    ClassRejected.store(0U, std::memory_order_relaxed);
    AlignmentRejected.store(0U, std::memory_order_relaxed);
    MetadataFaults.store(0U, std::memory_order_relaxed);
    HostilesObserved.store(0U, std::memory_order_relaxed);
    HostilesThrough80.store(0U, std::memory_order_relaxed);
    HostilesFrom81Through140.store(0U, std::memory_order_relaxed);
    HostilesFrom141Through220.store(0U, std::memory_order_relaxed);
    HostilesBeyond220.store(0U, std::memory_order_relaxed);
    ProjectionRejected.store(0U, std::memory_order_relaxed);
    NativeClipRejected.store(0U, std::memory_order_relaxed);
    for (auto& counter : AcceptedByDistanceBand) {
        counter.store(0U, std::memory_order_relaxed);
    }
    for (auto& counter : ClipRejectedByDistanceBand) {
        counter.store(0U, std::memory_order_relaxed);
    }
    CandidatesAccepted.store(0U, std::memory_order_relaxed);
    MarkersInserted.store(0U, std::memory_order_relaxed);
    MarkersRefreshed.store(0U, std::memory_order_relaxed);
    MarkersExpired.store(0U, std::memory_order_relaxed);
    ContentionWaits.store(0U, std::memory_order_relaxed);
    StorageFailures.store(0U, std::memory_order_relaxed);
    AccessFaults.store(0U, std::memory_order_relaxed);
    MaximumDiscoveryMicroseconds.store(0U, std::memory_order_relaxed);
    TotalDiscoveryMicroseconds.store(0U, std::memory_order_relaxed);
    DiscoveryTimingSamples.store(0U, std::memory_order_relaxed);
    MaximumRefreshMicroseconds.store(0U, std::memory_order_relaxed);
    TotalRefreshMicroseconds.store(0U, std::memory_order_relaxed);
    RefreshTimingSamples.store(0U, std::memory_order_relaxed);
    MaximumHostileDistanceSquared.store(0U, std::memory_order_relaxed);
    MaximumAcceptedDistanceSquared.store(0U, std::memory_order_relaxed);
    MaximumPublishedDistanceSquared.store(0U, std::memory_order_relaxed);
}

void BeginAutomapPulse(std::uint64_t currentTick) noexcept {
    AutomapPulses.fetch_add(1U, std::memory_order_relaxed);
    LastAutomapPulseTick.store(currentTick, std::memory_order_release);
}

auto EnsureObservationChunk(
        ObservationBuffer& buffer,
        std::uint64_t chunkIndex) noexcept -> ObservationChunk* {
    auto* chunk = buffer.first;
    if (chunk == nullptr) return nullptr;

    for (std::uint64_t index = 0U; index < chunkIndex; ++index) {
        auto* next = chunk->next.load(std::memory_order_acquire);
        if (next == nullptr) {
            auto* const allocated = new (std::nothrow) ObservationChunk{};
            if (allocated == nullptr) return nullptr;

            auto* expected = static_cast<ObservationChunk*>(nullptr);
            if (!chunk->next.compare_exchange_strong(
                    expected,
                    allocated,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                delete allocated;
                next = expected;
            } else {
                next = allocated;
            }
        }
        chunk = next;
    }
    return chunk;
}

constexpr auto ObservationBufferIndex(std::uint64_t state) noexcept
        -> std::uint32_t {
    return static_cast<std::uint32_t>(state & 1U);
}

constexpr auto NextObservationState(std::uint64_t state) noexcept
        -> std::uint64_t {
    const auto generation = (state >> 1U) + 1U;
    const auto nextIndex = ObservationBufferIndex(state) ^ 1U;
    return (generation << 1U) | nextIndex;
}

[[nodiscard]] auto BuildMonsterScanContext(
        void* player,
        void* automapContext,
        std::uint64_t currentTick,
        std::uint64_t epoch,
        MonsterScanContext& output) noexcept -> bool {
    __try {
        if (!IsAlignedPointer(player) || automapContext == nullptr
            || GetNativeHeight == nullptr || GetNativeWidth == nullptr
            || GetDynamicPath == nullptr || PathGetX == nullptr
            || PathGetY == nullptr || GetUnitDataContext == nullptr
            || GetDataTablesForContext == nullptr) {
            return false;
        }

        const auto dataContext = GetUnitDataContext(player);
        if (dataContext >= Detail::NativeDataContextCount) return false;
        const auto* const dataTables = GetDataTablesForContext(dataContext);
        if (!IsAlignedPointer(dataTables)) return false;
        const auto monStatsRecordCount =
            *reinterpret_cast<const std::uint64_t*>(
                dataTables + DataTablesMonStatsCountOffset);
        if (monStatsRecordCount == 0U
            || monStatsRecordCount
                > Detail::MaximumMonStatsRecordCount) {
            return false;
        }

        void* const playerPath = GetDynamicPath(player);
        if (!IsAlignedPointer(playerPath)) return false;
        const auto playerSubtileX = PathGetX(playerPath);
        const auto playerSubtileY = PathGetY(playerPath);
        constexpr auto MaximumPathCoordinate = static_cast<std::int32_t>(
            (std::numeric_limits<std::uint16_t>::max)());
        if (playerSubtileX < 0 || playerSubtileX > MaximumPathCoordinate
            || playerSubtileY < 0 || playerSubtileY > MaximumPathCoordinate) {
            return false;
        }

        const auto nativeWidth = GetNativeWidth();
        const auto nativeHeight = GetNativeHeight();
        if (nativeWidth <= 0 || nativeHeight <= 0
            || nativeWidth > 32768 || nativeHeight > 32768) {
            return false;
        }

        const auto* const contextBytes = static_cast<const std::uint8_t*>(
            automapContext);
        const auto clipLeft = *reinterpret_cast<const std::int32_t*>(
            contextBytes + AutomapClipLeftOffset);
        const auto clipTop = *reinterpret_cast<const std::int32_t*>(
            contextBytes + AutomapClipTopOffset);
        const auto clipWidth = *reinterpret_cast<const std::int32_t*>(
            contextBytes + AutomapClipWidthOffset);
        const auto clipHeight = *reinterpret_cast<const std::int32_t*>(
            contextBytes + AutomapClipHeightOffset);
        if (clipWidth <= 0 || clipHeight <= 0) return false;

        output = {
            .automapContext = automapContext,
            .monStatsRecordCount = monStatsRecordCount,
            .playerSubtileX = static_cast<std::uint16_t>(playerSubtileX),
            .playerSubtileY = static_cast<std::uint16_t>(playerSubtileY),
            .dataContext = dataContext,
            .nativeWidth = nativeWidth,
            .nativeHeight = nativeHeight,
            .clipLeft = clipLeft,
            .clipTop = clipTop,
            .clipWidth = clipWidth,
            .clipHeight = clipHeight,
            .currentTick = currentTick,
            .epoch = epoch,
        };
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
}

void RecordRefreshDistanceBand(
        std::array<std::atomic<std::uint64_t>, DistanceBandCount>& counters,
        std::uint64_t distanceSquared) noexcept {
    const auto band = Detail::ClassifyWorldSubtileDistanceSquared(
        distanceSquared);
    counters[static_cast<std::size_t>(band)].fetch_add(
        1U,
        std::memory_order_relaxed);
}

void RecordHostileDistanceBand(std::uint64_t distanceSquared) noexcept {
    switch (Detail::ClassifyWorldSubtileDistanceSquared(distanceSquared)) {
        case Detail::WorldSubtileDistanceBand::Through80:
            HostilesThrough80.fetch_add(1U, std::memory_order_relaxed);
            return;
        case Detail::WorldSubtileDistanceBand::From81Through140:
            HostilesFrom81Through140.fetch_add(1U, std::memory_order_relaxed);
            return;
        case Detail::WorldSubtileDistanceBand::From141Through220:
            HostilesFrom141Through220.fetch_add(1U, std::memory_order_relaxed);
            return;
        case Detail::WorldSubtileDistanceBand::Beyond220:
            HostilesBeyond220.fetch_add(1U, std::memory_order_relaxed);
            return;
    }
}

auto DiscoverTrackedMonster(
        void* unit,
        const MonsterScanContext& scan,
        TrackedMonster& tracked) noexcept -> bool {
    __try {
        if (!IsAlignedPointer(unit)
            || GetUnitStat == nullptr || GetUnitAlignment == nullptr
            || GetUnitId == nullptr || GetUnitClassId == nullptr
            || GetUnitDataContext == nullptr || GetUnitMode == nullptr
            || GetDynamicPath == nullptr
            || PathGetX == nullptr || PathGetY == nullptr
            || GetMonStatsRecord == nullptr) {
            return false;
        }

        UnitsObserved.fetch_add(1U, std::memory_order_relaxed);
        auto* const unitBytes = static_cast<std::uint8_t*>(unit);
        if (*reinterpret_cast<const std::uint32_t*>(
                unitBytes + UnitTypeOffset) != UnitMonster) {
            return false;
        }
        MonstersObserved.fetch_add(1U, std::memory_order_relaxed);

        const auto mode = GetUnitMode(unit);
        if (mode == MonsterModeDeath || mode == MonsterModeDead) {
            ModeRejected.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }

        // UnitMonster also covers mercenaries and asynchronous ambient actors.
        // D2R's native automap rejects those actors before consulting MonStats.
        const auto unitFlags = *reinterpret_cast<const std::uint32_t*>(
            unitBytes + UnitFlagsOffset);
        if (!Detail::IsEnemyMarkerUnitEligible(unitFlags)) {
            UnitFlagRejected.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }

        const auto classId = GetUnitClassId(unit);
        const auto unitDataContext = GetUnitDataContext(unit);
        if (!Detail::IsMonStatsLookupSafe(
                scan.dataContext,
                unitDataContext,
                classId,
                scan.monStatsRecordCount)) {
            MetadataFaults.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        const auto* const monStats = GetMonStatsRecord(
            scan.dataContext,
            classId);
        if (monStats == nullptr) {
            MetadataFaults.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        const auto monStatsFlags = *reinterpret_cast<const std::uint32_t*>(
            monStats + MonStatsFlagsOffset);
        if (!Detail::IsEnemyMarkerClassEligible(monStatsFlags)) {
            ClassRejected.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }

        // Use the dedicated native alignment getter. Generic GetUnitStat
        // returns zero for a missing stat, which is indistinguishable from
        // Evil and previously admitted NPCs and scenery actors by accident.
        if (!Detail::IsEnemyMarkerAlignmentEligible(
                GetUnitAlignment(unit))) {
            AlignmentRejected.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        HostilesObserved.fetch_add(1U, std::memory_order_relaxed);

        auto* const monsterData = *reinterpret_cast<const std::uint8_t* const*>(
            unitBytes + UnitTypeDataOffset);
        if (monsterData == nullptr) {
            MetadataFaults.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        tracked.rank = Detail::ClassifyMonsterRankFlags(
            monsterData[MonsterRankFlagsOffset]);
        tracked.classId = classId;
        tracked.superUniqueIndex = tracked.rank == MonsterRank::SuperUnique
                && GetSuperUniqueIndex != nullptr
            ? GetSuperUniqueIndex(unit)
            : -1;

        void* const unitPath = GetDynamicPath(unit);
        if (!IsAlignedPointer(unitPath)) {
            MetadataFaults.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        const auto unitSubtileX = PathGetX(unitPath);
        const auto unitSubtileY = PathGetY(unitPath);
        constexpr auto MaximumPathCoordinate = static_cast<std::int32_t>(
            (std::numeric_limits<std::uint16_t>::max)());
        if (unitSubtileX < 0 || unitSubtileX > MaximumPathCoordinate
            || unitSubtileY < 0 || unitSubtileY > MaximumPathCoordinate) {
            MetadataFaults.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        const auto distanceSquared = Detail::SquaredWorldSubtileDistance(
            static_cast<std::uint16_t>(unitSubtileX),
            static_cast<std::uint16_t>(unitSubtileY),
            scan.playerSubtileX,
            scan.playerSubtileY);
        UpdateMaximum(MaximumHostileDistanceSquared, distanceSquared);
        RecordHostileDistanceBand(distanceSquared);

        tracked.unitId = GetUnitId(unit);
        if (tracked.unitId == UINT32_MAX) return false;
        if (ImmunityCollectionEnabled.load(std::memory_order_acquire)) {
            tracked.immunityMask = Detail::BuildMonsterImmunityMask(
                std::array<std::int32_t, 6>{
                GetUnitStat(unit, PhysicalResistanceStatId, 0U),
                GetUnitStat(unit, FireResistanceStatId, 0U),
                GetUnitStat(unit, ColdResistanceStatId, 0U),
                GetUnitStat(unit, LightningResistanceStatId, 0U),
                GetUnitStat(unit, PoisonResistanceStatId, 0U),
                GetUnitStat(unit, MagicResistanceStatId, 0U),
            });
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
}

auto RefreshTrackedMonster(
        const TrackedMonster& tracked,
        const MonsterScanContext& scan,
        Candidate& candidate) noexcept -> bool {
    __try {
        if (GetUnitByIdAndType == nullptr || GetUnitMode == nullptr
            || GetDynamicPath == nullptr || PathGetX == nullptr
            || PathGetY == nullptr || GetUnitClientX == nullptr
            || GetUnitClientY == nullptr
            || ProjectClientToAutomap == nullptr) {
            return false;
        }

        void* const unit = GetUnitByIdAndType(
            tracked.unitId,
            UnitMonster);
        if (!IsAlignedPointer(unit)) {
            TrackedIdsMissing.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        TrackedIdsResolved.fetch_add(1U, std::memory_order_relaxed);

        const auto mode = GetUnitMode(unit);
        if (mode == MonsterModeDeath || mode == MonsterModeDead) return false;

        void* const unitPath = GetDynamicPath(unit);
        if (!IsAlignedPointer(unitPath)) return false;
        const auto unitSubtileX = PathGetX(unitPath);
        const auto unitSubtileY = PathGetY(unitPath);
        constexpr auto MaximumPathCoordinate = static_cast<std::int32_t>(
            (std::numeric_limits<std::uint16_t>::max)());
        if (unitSubtileX < 0 || unitSubtileX > MaximumPathCoordinate
            || unitSubtileY < 0 || unitSubtileY > MaximumPathCoordinate) {
            return false;
        }

        const auto distanceSquared = Detail::SquaredWorldSubtileDistance(
            static_cast<std::uint16_t>(unitSubtileX),
            static_cast<std::uint16_t>(unitSubtileY),
            scan.playerSubtileX,
            scan.playerSubtileY);
        const auto unitX = GetUnitClientX(unit);
        const auto unitY = GetUnitClientY(unit);
        NativePoint projected{};
        const auto packedClientCoordinates =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(unitX))
            | (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(unitY)) << 32U);
        if (ProjectClientToAutomap(
                scan.automapContext,
                &projected,
                packedClientCoordinates) != &projected) {
            ProjectionRejected.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }

        if (projected.x < scan.clipLeft || projected.y < scan.clipTop
            || static_cast<std::int64_t>(projected.x)
                >= static_cast<std::int64_t>(scan.clipLeft) + scan.clipWidth
            || static_cast<std::int64_t>(projected.y)
                >= static_cast<std::int64_t>(scan.clipTop) + scan.clipHeight) {
            NativeClipRejected.fetch_add(1U, std::memory_order_relaxed);
            RecordRefreshDistanceBand(
                ClipRejectedByDistanceBand,
                distanceSquared);
            return false;
        }

        RecordRefreshDistanceBand(AcceptedByDistanceBand, distanceSquared);

        candidate = {
            .unitId = tracked.unitId,
            .classId = tracked.classId,
            .superUniqueIndex = tracked.superUniqueIndex,
            .x = projected.x,
            .y = projected.y,
            .nativeWidth = scan.nativeWidth,
            .nativeHeight = scan.nativeHeight,
            .rank = tracked.rank,
            .immunityMask = tracked.immunityMask,
            .distanceSquared = distanceSquared,
            .observedTick = scan.currentTick,
            .epoch = scan.epoch,
        };
        UpdateMaximum(MaximumAcceptedDistanceSquared, distanceSquared);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
}

auto BeginObservationFrame(
        std::uint64_t epoch,
        ObservationBuffer*& output) noexcept -> bool {
    output = nullptr;
    for (std::uint32_t attempt = 0U; attempt < 2U; ++attempt) {
        const auto observationState = ActiveObservationState.load(
            std::memory_order_acquire);
        const auto bufferIndex = ObservationBufferIndex(observationState);
        auto& buffer = ObservationBuffers[bufferIndex];
        buffer.writers.fetch_add(1U, std::memory_order_acq_rel);
        if (observationState != ActiveObservationState.load(
                std::memory_order_acquire)) {
            buffer.writers.fetch_sub(1U, std::memory_order_release);
            ContentionWaits.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        if (epoch != Epoch.load(std::memory_order_acquire)) {
            buffer.writers.fetch_sub(1U, std::memory_order_acq_rel);
            return false;
        }
        if (buffer.epoch.load(std::memory_order_acquire) != epoch) {
            // The producer owns this active buffer until writers reaches zero.
            // Initialize a fresh epoch immediately instead of dropping the
            // first marker frame while waiting for Present to rotate buffers.
            buffer.count.store(0U, std::memory_order_relaxed);
            buffer.epoch.store(epoch, std::memory_order_release);
        }
        output = &buffer;
        return true;
    }
    ContentionWaits.fetch_add(1U, std::memory_order_relaxed);
    return false;
}

[[nodiscard]] auto ClaimMonsterTableScan(
        std::uint64_t currentTick) noexcept -> bool {
    auto previous = LastMonsterTableScanTick.load(std::memory_order_acquire);
    for (;;) {
        if (previous != 0U && currentTick >= previous
            && (currentTick - previous)
                < MonsterTableScanIntervalMilliseconds) {
            return false;
        }
        if (LastMonsterTableScanTick.compare_exchange_weak(
                previous,
                currentTick,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
}

void DiscoverClientMonsterTable(const MonsterScanContext& scan) noexcept {
    const auto started = PerformanceCounterMicroseconds();
    bool complete = true;
    __try {
        if (Base == nullptr) return;
        auto** const buckets = reinterpret_cast<void**>(
            Base + ClientUnitHashTableRva + UnitHashTypeStride);
        MonsterTableScans.fetch_add(1U, std::memory_order_relaxed);
        DiscoveryScratch.clear();

        std::size_t totalUnits{};
        for (std::size_t bucketIndex = 0U;
                bucketIndex < UnitHashBucketCount;
                ++bucketIndex) {
            MonsterBucketsVisited.fetch_add(1U, std::memory_order_relaxed);
            void* unit = buckets[bucketIndex];
            std::size_t bucketUnits{};
            while (unit != nullptr) {
                if (!IsAlignedPointer(unit)) {
                    AccessFaults.fetch_add(1U, std::memory_order_relaxed);
                    break;
                }
                if (totalUnits >= MaximumUnitsPerMonsterTableScan
                    || bucketUnits >= MaximumUnitsPerMonsterBucket) {
                    MonsterTraversalLimits.fetch_add(
                        1U,
                        std::memory_order_relaxed);
                    complete = false;
                    break;
                }

                auto* const unitBytes = static_cast<std::uint8_t*>(unit);
                void* const next = *reinterpret_cast<void**>(
                    unitBytes + UnitHashNextOffset);
                ++totalUnits;
                ++bucketUnits;

                TrackedMonster tracked{};
                if (DiscoverTrackedMonster(unit, scan, tracked)) {
                    DiscoveryScratch.push_back(tracked);
                }

                if (next == unit) {
                    MonsterTraversalLimits.fetch_add(
                        1U,
                        std::memory_order_relaxed);
                    break;
                }
                unit = next;
            }
            if (!complete) break;
        }
        if (complete
            && Epoch.load(std::memory_order_acquire) == scan.epoch) {
            TrackedMonsters.swap(DiscoveryScratch);
            TrackedMonsterCount.store(
                static_cast<std::uint64_t>(TrackedMonsters.size()),
                std::memory_order_release);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        AccessFaults.fetch_add(1U, std::memory_order_relaxed);
    }
    const auto finished = PerformanceCounterMicroseconds();
    if (started != 0U && finished >= started) {
        const auto elapsed = finished - started;
        UpdateMaximum(MaximumDiscoveryMicroseconds, elapsed);
        TotalDiscoveryMicroseconds.fetch_add(
            elapsed,
            std::memory_order_relaxed);
        DiscoveryTimingSamples.fetch_add(1U, std::memory_order_relaxed);
    }
}

void RefreshTrackedMonsters(const MonsterScanContext& scan) noexcept {
    const auto started = PerformanceCounterMicroseconds();
    MonsterPositionRefreshes.fetch_add(1U, std::memory_order_relaxed);

    ObservationBuffer* buffer{};
    if (!BeginObservationFrame(scan.epoch, buffer)
        || buffer == nullptr) {
        return;
    }

    auto* chunk = buffer->first;
    std::uint64_t published{};
    for (const auto& tracked : TrackedMonsters) {
        Candidate candidate{};
        if (!RefreshTrackedMonster(tracked, scan, candidate)) continue;

        if (published != 0U
            && published % ObservationsPerChunk == 0U) {
            chunk = chunk != nullptr
                ? chunk->next.load(std::memory_order_acquire)
                : nullptr;
        }
        if (chunk == nullptr) {
            StorageFailures.fetch_add(1U, std::memory_order_relaxed);
            break;
        }
        chunk->observations[published % ObservationsPerChunk] = candidate;
        ++published;
        CandidatesAccepted.fetch_add(1U, std::memory_order_relaxed);
    }
    if (scan.epoch == Epoch.load(std::memory_order_acquire)) {
        // Replace the producer buffer with one complete latest-position frame.
        // Repeated native pulses overwrite it instead of appending duplicate
        // unit IDs while Present is late.
        buffer->count.store(published, std::memory_order_release);
        PublishedSequence.fetch_add(1U, std::memory_order_acq_rel);
    } else {
        buffer->count.store(0U, std::memory_order_release);
    }
    buffer->writers.fetch_sub(1U, std::memory_order_acq_rel);

    const auto finished = PerformanceCounterMicroseconds();
    if (started != 0U && finished >= started) {
        const auto elapsed = finished - started;
        UpdateMaximum(MaximumRefreshMicroseconds, elapsed);
        TotalRefreshMicroseconds.fetch_add(elapsed, std::memory_order_relaxed);
        RefreshTimingSamples.fetch_add(1U, std::memory_order_relaxed);
    }
}

__declspec(noinline) void __fastcall HookRenderAutomapUnit(
        void* unit,
        void* automapContext) noexcept {
    const auto original = OriginalRenderAutomapUnit;
    if (original == nullptr) return;

    // Preserve D2R behavior first and exactly once. Marker observation is a
    // fail-closed side effect that cannot suppress the native automap pass.
    original(unit, automapContext);
    if (!Active.load(std::memory_order_acquire)) {
        return;
    }
    ObserveNativeAutomapRenderedUnit(unit);

    // Navigation and the complete client-unit scan share this one canonical
    // automap hook. Both begin only on the local-player pass and retain no
    // native pointer or context after the pass returns.
    void* const player = TryGetLocalPlayerPass(unit);
    if (player == nullptr) return;
    if (!IsObservedLocalPlayerAlive(player)) {
        LocalPlayerFrameAlive.store(false, std::memory_order_release);
        return;
    }
    ObserveNavigationPlayerPass(player, automapContext);
    LocalPlayerFrameAlive.store(true, std::memory_order_release);
    if (!CollectionEnabled.load(std::memory_order_acquire)) return;

    const auto currentTick = static_cast<std::uint64_t>(GetTickCount64());
    BeginAutomapPulse(currentTick);
    const auto epoch = Epoch.load(std::memory_order_acquire);
    MonsterScanContext scan{};
    if (!BuildMonsterScanContext(
            player,
            automapContext,
            currentTick,
            epoch,
            scan)) {
        return;
    }

    ObserveNativeAutomapMissilePlayerPass({
        .automapContext = automapContext,
        .playerSubtileX = scan.playerSubtileX,
        .playerSubtileY = scan.playerSubtileY,
        .nativeWidth = scan.nativeWidth,
        .nativeHeight = scan.nativeHeight,
        .clipLeft = scan.clipLeft,
        .clipTop = scan.clipTop,
        .clipWidth = scan.clipWidth,
        .clipHeight = scan.clipHeight,
        .observedTick = scan.currentTick,
    });

    if (TrackedMonsterLock.test_and_set(std::memory_order_acquire)) {
        ContentionWaits.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const AtomicFlagRelease trackedMonsterRelease{TrackedMonsterLock};
    if (TrackedMonsterEpoch != epoch) {
        TrackedMonsters.clear();
        DiscoveryScratch.clear();
        TrackedMonsterCount.store(0U, std::memory_order_release);
        TrackedMonsterEpoch = epoch;
        LastMonsterTableScanTick.store(0U, std::memory_order_release);
    }
    if (ClaimMonsterTableScan(currentTick)) {
        DiscoverClientMonsterTable(scan);
    }
    RefreshTrackedMonsters(scan);
}

auto ValidateRuntime(const D2RL::PluginContext* context) noexcept -> bool {
    constexpr std::array<std::uint8_t, 10> localContextExpected{
        0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
        0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 32> localPlayerExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
        0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C,
        0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19};
    constexpr std::array<std::uint8_t, 38> nativeHeightExpected{
        0x48, 0x83, 0xEC, 0x28, 0xE8, 0x67, 0x6D, 0x7C,
        0x00, 0x84, 0xC0, 0x74, 0x0E, 0xE8, 0x1E, 0x51,
        0x5D, 0x00, 0x48, 0xC1, 0xE8, 0x20, 0x48, 0x83,
        0xC4, 0x28, 0xC3, 0x8B, 0x05, 0xAB, 0x18, 0x22,
        0x02, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 33> nativeWidthExpected{
        0x48, 0x83, 0xEC, 0x28, 0xE8, 0xF7, 0x6C, 0x7C,
        0x00, 0x84, 0xC0, 0x74, 0x09, 0x48, 0x83, 0xC4,
        0x28, 0xE9, 0xAA, 0x50, 0x5D, 0x00, 0x8B, 0x05,
        0x3C, 0x18, 0x22, 0x02, 0x48, 0x83, 0xC4, 0x28,
        0xC3};
    constexpr std::array<std::uint8_t, 32> projectExpected{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x66, 0x0F, 0x6E, 0x41, 0x10, 0x4C, 0x8B, 0xFA};
    constexpr std::array<std::uint8_t, 32> renderUnitExpected{
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x57, 0x48, 0x83,
        0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x4C, 0x8D, 0x44,
        0x24, 0x68, 0x48, 0x8D, 0x54, 0x24, 0x60, 0x48,
        0x8B, 0xE9, 0xE8, 0xF1, 0x01, 0x00, 0x00, 0x84};
    constexpr std::array<std::uint8_t, 32> getUnitStatExpected{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
        0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9};
    constexpr std::array<std::uint8_t, 32> getUnitAlignmentExpected{
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
        0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0x8B, 0xF1, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88,
        0x4C, 0x24, 0x40, 0x48, 0x8D, 0x4C, 0x24, 0x40};
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
    constexpr std::array<std::uint8_t, 16> pathGetXExpected{
        0x0F, 0xB7, 0x41, 0x02, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 16> pathGetYExpected{
        0x0F, 0xB7, 0x41, 0x06, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
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
    constexpr std::array<std::uint8_t, 47> getUnitDataContextExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83,
        0xC4, 0x28, 0xC3, 0x0F, 0xB6, 0x81, 0xBD, 0x01,
        0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 43> getUnitModeExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
        0x24, 0x30, 0xE8, 0xF9, 0xA3, 0xFF, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83,
        0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x0C, 0x48, 0x83,
        0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 27> monsterRankWitnessExpected{
        0x48, 0x8B, 0xCB, 0xE8, 0x48, 0xC7, 0xE2, 0xFF,
        0x83, 0xF8, 0x01, 0x75, 0x06, 0x48, 0x8B, 0x43,
        0x10, 0xEB, 0x02, 0x33, 0xC0, 0xF6, 0x40, 0x1A,
        0x0E, 0x75, 0x30};
    constexpr std::array<std::uint8_t, 42> getMonStatsRecordExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0x63, 0xF2, 0xE8, 0x99, 0x93, 0x26, 0x00, 0x48,
        0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78,
        0x09, 0x48, 0x3B, 0x98, 0x60, 0x0F, 0x00, 0x00,
        0x72, 0x18};
    constexpr std::array<std::uint8_t, 67>
        getDataTablesForContextExpected{
            0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48,
            0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04,
            0x72, 0x19, 0x48, 0x8D, 0x44, 0x24, 0x38, 0x48,
            0x8D, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x44, 0x24,
            0x40, 0xE8, 0x7A, 0xD3, 0xFF, 0xFF, 0x84, 0xC0,
            0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44, 0x24, 0x38,
            0x48, 0x8D, 0x0D, 0xB9, 0x9A, 0x79, 0x02, 0x48,
            0x03, 0xC0, 0x48, 0x8B, 0x04, 0xC1, 0x48, 0x83,
            0xC4, 0x28, 0xC3};
    constexpr std::array<std::uint8_t, 92> getSuperUniqueIndexExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0xED,
        0xD5, 0xFB, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19,
        0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
        0x30, 0x00, 0xE8, 0xF9, 0x40, 0xE1, 0xFF, 0x84,
        0xC0, 0x74, 0x01, 0xCC, 0x48, 0x85, 0xDB, 0x74,
        0x20, 0x48, 0x8B, 0xCB, 0xE8, 0xC7, 0xD5, 0xFB,
        0xFF, 0x83, 0xF8, 0x01, 0x75, 0x13, 0x48, 0x8B,
        0x43, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x0A, 0x0F,
        0xB7, 0x40, 0x2A, 0x48, 0x83, 0xC4, 0x20, 0x5B,
        0xC3, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x83,
        0xC4, 0x20, 0x5B, 0xC3};
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
    constexpr std::array<std::uint8_t, 5> pathGetRoomExpected{
        0x48, 0x8B, 0x41, 0x20, 0xC3};
    constexpr std::array<std::uint8_t, 25> isRoomInTownExpected{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
        0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
        0x48, 0x8B, 0x49, 0x18, 0xE8, 0x57, 0x08, 0x07,
        0x00};
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
    return check(GetLocalDataContextRva, localContextExpected)
        && check(GetLocalPlayerRva, localPlayerExpected)
        && check(GetNativeHeightRva, nativeHeightExpected)
        && check(GetNativeWidthRva, nativeWidthExpected)
        && check(ProjectClientToAutomapRva, projectExpected)
        && check(GetUnitStatRva, getUnitStatExpected)
        && check(GetUnitAlignmentRva, getUnitAlignmentExpected)
        && check(GetUnitIdRva, getUnitIdExpected)
        && check(GetUnitClassIdRva, getUnitClassIdExpected)
        && check(GetUnitDataContextRva, getUnitDataContextExpected)
        && check(GetUnitModeRva, getUnitModeExpected)
        && check(GetDynamicPathRva, getDynamicPathExpected)
        && check(GetUnitClientXRva, getXExpected)
        && check(GetUnitClientYRva, getYExpected)
        && check(PathGetXRva, pathGetXExpected)
        && check(PathGetYRva, pathGetYExpected)
        && check(
            GetDataTablesForContextRva,
            getDataTablesForContextExpected)
        && check(GetMonStatsRecordRva, getMonStatsRecordExpected)
        && check(GetSuperUniqueIndexRva, getSuperUniqueIndexExpected)
        && check(MonsterRankWitnessRva, monsterRankWitnessExpected)
        && check(GetUnitRoomRva, getUnitRoomExpected)
        && check(UnitRoomLayoutWitnessRva, unitRoomLayoutWitnessExpected)
        && check(
            ActiveRoomGetDrlgRoomRva,
            activeRoomGetDrlgRoomExpected)
        && check(PathGetRoomRva, pathGetRoomExpected)
        && check(IsRoomInTownRva, isRoomInTownExpected)
        && check(GetDrlgRoomLevelIdRva, drlgRoomLevelIdExpected)
        && check(
            ClientUnitHashTableWitnessRva,
            clientUnitHashTableExpected)
        && check(
            ClientUnitHashLookupWitnessRva,
            clientUnitHashLookupExpected)
        && check(RenderAutomapUnitRva, renderUnitExpected);
}

} // namespace

auto InitializeNativeAutomapMarker(
        const D2RL::PluginContext* context,
        bool navigationProjectionDiagnosticsEnabled) noexcept -> bool {
    if (!D2RL::HasContext(context)
        || context->apiVersion != D2RL_PLUGIN_API_VERSION
        || context->exeBase == 0) {
        return false;
    }
    if (Active.load(std::memory_order_acquire)) return true;
    if (!ValidateRuntime(context)) {
        context->LogWarn(
            "MapSense: native automap marker signature or ABI mismatch; marker hook refused.");
        return false;
    }
    DiagnosticContext = navigationProjectionDiagnosticsEnabled
        ? context
        : nullptr;
    NavigationProjectionDiagnostics.Reset();
    for (auto& buffer : ObservationBuffers) {
        if (buffer.first == nullptr) {
            buffer.first = new (std::nothrow) ObservationChunk{};
        }
        if (buffer.first == nullptr
            || EnsureObservationChunk(
                buffer,
                InitialObservationChunkCount - 1U) == nullptr) {
            context->LogWarn(
                "MapSense: native automap observation reserve could not be allocated; marker hook refused.");
            return false;
        }
        buffer.count.store(0U, std::memory_order_relaxed);
        buffer.writers.store(0U, std::memory_order_relaxed);
        buffer.epoch.store(0U, std::memory_order_relaxed);
    }
    try {
        MarkerCache.clear();
        MarkerCache.reserve(MaximumRecentNativeAutomapMarkers);
        TrackedMonsters.clear();
        TrackedMonsters.reserve(MaximumUnitsPerMonsterTableScan);
        DiscoveryScratch.clear();
        DiscoveryScratch.reserve(MaximumUnitsPerMonsterTableScan);
    } catch (...) {
        context->LogWarn(
            "MapSense: renderer marker cache could not be reserved; marker hook refused.");
        return false;
    }
    ActiveObservationState.store(0U, std::memory_order_release);
    PendingObservationBuffer = -1;
    RendererEpoch = 0U;
    LastPurgeTick = 0U;
    TrackedMonsterEpoch = 0U;
    TrackedMonsterLock.clear(std::memory_order_release);
    LARGE_INTEGER performanceFrequency{};
    PerformanceCounterFrequency = QueryPerformanceFrequency(
        &performanceFrequency) != FALSE
        ? performanceFrequency.QuadPart
        : 0;

    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    GetLocalDataContext = At<GetLocalDataContextFn>(
        GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    GetNativeHeight = At<GetNativeDimensionFn>(GetNativeHeightRva);
    GetNativeWidth = At<GetNativeDimensionFn>(GetNativeWidthRva);
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    GetUnitAlignment = At<GetUnitSignedValueFn>(GetUnitAlignmentRva);
    GetUnitId = At<GetUnitValueFn>(GetUnitIdRva);
    GetUnitClassId = At<GetUnitSignedValueFn>(GetUnitClassIdRva);
    GetUnitDataContext = At<GetUnitDataContextFn>(GetUnitDataContextRva);
    GetUnitMode = At<GetUnitValueFn>(GetUnitModeRva);
    GetDynamicPath = At<GetNativePointerFn>(GetDynamicPathRva);
    GetUnitClientX = At<GetUnitCoordinateFn>(GetUnitClientXRva);
    GetUnitClientY = At<GetUnitCoordinateFn>(GetUnitClientYRva);
    PathGetX = At<GetUnitCoordinateFn>(PathGetXRva);
    PathGetY = At<GetUnitCoordinateFn>(PathGetYRva);
    GetUnitRoom = At<GetNativePointerFn>(GetUnitRoomRva);
    GetDrlgRoomLevelId = At<GetLevelIdFn>(GetDrlgRoomLevelIdRva);
    IsRoomInTown = At<IsRoomInTownFn>(IsRoomInTownRva);
    GetDataTablesForContext = At<GetDataTablesForContextFn>(
        GetDataTablesForContextRva);
    GetMonStatsRecord = At<GetMonStatsRecordFn>(GetMonStatsRecordRva);
    GetSuperUniqueIndex = At<GetSuperUniqueIndexFn>(
        GetSuperUniqueIndexRva);
    GetUnitByIdAndType = At<GetUnitByIdAndTypeFn>(
        ClientUnitHashTableWitnessRva);
    ProjectClientToAutomap = At<ProjectClientToAutomapFn>(
        ProjectClientToAutomapRva);
    const auto missileCollectorAvailable = InitializeNativeAutomapMissile(
        context);
    if (!missileCollectorAvailable) {
        context->LogWarn(
            "MapSense: the native missile source is unavailable; existing marker and navigation features remain active.");
    }
    CollectionEnabled.store(false, std::memory_order_release);
    ImmunityCollectionEnabled.store(false, std::memory_order_release);
    ResetPublishedMarkers(true);

    constexpr std::array<std::uint8_t, 32> renderUnitExpected{
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x57, 0x48, 0x83,
        0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x4C, 0x8D, 0x44,
        0x24, 0x68, 0x48, 0x8D, 0x54, 0x24, 0x60, 0x48,
        0x8B, 0xE9, 0xE8, 0xF1, 0x01, 0x00, 0x00, 0x84};
    if (!context->InstallInlineHook(
            RenderAutomapUnitRva,
            renderUnitExpected.data(),
            static_cast<std::uint32_t>(renderUnitExpected.size()),
            HookRenderAutomapUnit,
            &OriginalRenderAutomapUnit)) {
        ShutdownNativeAutomapMissile();
        Base = nullptr;
        GetLocalDataContext = nullptr;
        GetLocalPlayer = nullptr;
        GetNativeHeight = nullptr;
        GetNativeWidth = nullptr;
        GetUnitStat = nullptr;
        GetUnitAlignment = nullptr;
        GetUnitId = nullptr;
        GetUnitClassId = nullptr;
        GetUnitDataContext = nullptr;
        GetUnitMode = nullptr;
        GetDynamicPath = nullptr;
        GetUnitClientX = nullptr;
        GetUnitClientY = nullptr;
        PathGetX = nullptr;
        PathGetY = nullptr;
        GetUnitRoom = nullptr;
        GetDrlgRoomLevelId = nullptr;
        IsRoomInTown = nullptr;
        GetDataTablesForContext = nullptr;
        GetMonStatsRecord = nullptr;
        GetSuperUniqueIndex = nullptr;
        GetUnitByIdAndType = nullptr;
        ProjectClientToAutomap = nullptr;
        DiagnosticContext = nullptr;
        context->LogWarn(
            "MapSense: native automap marker hook installation was refused.");
        return false;
    }

    Active.store(true, std::memory_order_release);
    return true;
}

void ShutdownNativeAutomapMarker() noexcept {
    CollectionEnabled.store(false, std::memory_order_release);
    ImmunityCollectionEnabled.store(false, std::memory_order_release);
    Active.store(false, std::memory_order_release);
    ShutdownNativeAutomapMissile();
    LevelObservedCallback.store(nullptr, std::memory_order_release);
    LevelObservedUserData.store(nullptr, std::memory_order_release);
    ResetPublishedMarkers(false);
    // D2RLoader restores the inline hook after the unload callback. Keep the
    // trampoline and verified code addresses alive so an in-flight native call
    // can finish and pass through exactly once before the DLL is unmapped.
}

void ResetNativeAutomapMarker() noexcept {
    ResetPublishedMarkers(true);
    ResetNativeAutomapMissile();
}

void InvalidateNativeAutomapMarkerFrame() noexcept {
    ResetPublishedMarkers(false);
    InvalidateNativeAutomapMissileFrame();
}

void InvalidateNativeAutomapLocalPlayerFrame() noexcept {
    LocalPlayerFrameAlive.store(false, std::memory_order_release);
}

auto IsNativeAutomapLocalPlayerFrameAlive() noexcept -> bool {
    return Active.load(std::memory_order_acquire)
        && LocalPlayerFrameAlive.load(std::memory_order_acquire);
}

void SetNativeAutomapMarkerEnabled(bool enabled) noexcept {
    const auto previous = CollectionEnabled.exchange(
        enabled,
        std::memory_order_acq_rel);
    SetNativeAutomapMissileEnabled(enabled);
    if (previous != enabled) ResetPublishedMarkers(false);
}

void SetNativeAutomapImmunityCollectionEnabled(bool enabled) noexcept {
    const auto previous = ImmunityCollectionEnabled.exchange(
        enabled,
        std::memory_order_acq_rel);
    if (previous != enabled) ResetPublishedMarkers(false);
}

void SetNativeAutomapLevelObservedCallback(
        NativeAutomapLevelObservedCallback callback,
        void* userData) noexcept {
    if (callback == nullptr) {
        LevelObservedCallback.store(nullptr, std::memory_order_release);
        LevelObservedUserData.store(nullptr, std::memory_order_release);
        return;
    }
    LevelObservedUserData.store(userData, std::memory_order_release);
    LevelObservedCallback.store(callback, std::memory_order_release);
}

auto AcquireNativeAutomapMarkers(
        std::vector<NativeAutomapMarkerSnapshot>& snapshots,
        bool retainCurrentProjection) noexcept
        -> std::size_t {
    snapshots.clear();
    if (!Active.load(std::memory_order_acquire)
        || !CollectionEnabled.load(std::memory_order_acquire)) {
        return 0U;
    }

    const auto currentTick = static_cast<std::uint64_t>(GetTickCount64());
    if (!retainCurrentProjection && !IsRecent(
            LastAutomapPulseTick.load(std::memory_order_acquire),
            currentTick)) {
        return 0U;
    }

    try {
        const auto epoch = Epoch.load(std::memory_order_acquire);
        if (RendererEpoch != epoch) {
            MarkerCache.clear();
            RendererEpoch = epoch;
            LastPurgeTick = 0U;
            PendingObservationBuffer = -1;
        }

        if (PendingObservationBuffer < 0) {
            const auto activeState = ActiveObservationState.load(
                std::memory_order_acquire);
            const auto next = ObservationBufferIndex(activeState) ^ 1U;
            auto& nextBuffer = ObservationBuffers[next];
            if (nextBuffer.writers.load(std::memory_order_acquire) == 0U) {
                nextBuffer.count.store(0U, std::memory_order_relaxed);
                nextBuffer.epoch.store(epoch, std::memory_order_release);
                const auto sealedState = ActiveObservationState.exchange(
                    NextObservationState(activeState),
                    std::memory_order_acq_rel);
                PendingObservationBuffer = static_cast<std::int32_t>(
                    ObservationBufferIndex(sealedState));
            } else {
                ContentionWaits.fetch_add(1U, std::memory_order_relaxed);
            }
        }

        if (PendingObservationBuffer >= 0) {
            auto& sealed = ObservationBuffers[
                static_cast<std::size_t>(PendingObservationBuffer)];
            if (sealed.writers.load(std::memory_order_acquire) == 0U) {
                std::atomic_thread_fence(std::memory_order_acquire);
                auto remaining = sealed.count.load(std::memory_order_acquire);
                auto* chunk = sealed.first;
                while (remaining != 0U && chunk != nullptr) {
                    const auto chunkCount = std::min(
                        remaining,
                        ObservationsPerChunk);
                    for (std::uint64_t index = 0U;
                            index < chunkCount;
                            ++index) {
                        const auto& marker = chunk->observations[index];
                        if (marker.epoch != epoch
                            || !IsRecent(marker.observedTick, currentTick)) {
                            continue;
                        }
                        const auto existing = MarkerCache.find(marker.unitId);
                        if (existing == MarkerCache.end()) {
                            if (MarkerCache.size()
                                >= MaximumRecentNativeAutomapMarkers) {
                                StorageFailures.fetch_add(
                                    1U,
                                    std::memory_order_relaxed);
                                continue;
                            }
                            MarkerCache.emplace(marker.unitId, marker);
                            MarkersInserted.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                        } else {
                            existing->second = marker;
                            MarkersRefreshed.fetch_add(
                                1U,
                                std::memory_order_relaxed);
                        }
                    }
                    remaining -= chunkCount;
                    chunk = chunk->next.load(std::memory_order_acquire);
                }
                if (remaining != 0U) {
                    StorageFailures.fetch_add(1U, std::memory_order_relaxed);
                }
                PendingObservationBuffer = -1;
            } else {
                ContentionWaits.fetch_add(1U, std::memory_order_relaxed);
            }
        }

        if (!retainCurrentProjection
            && (LastPurgeTick == 0U || currentTick < LastPurgeTick
            || (currentTick - LastPurgeTick)
                >= MarkerLifetimeMilliseconds)) {
            std::uint64_t expired{};
            for (auto iterator = MarkerCache.begin();
                    iterator != MarkerCache.end();) {
                const auto& marker = iterator->second;
                if (marker.epoch != epoch
                    || !IsRecent(marker.observedTick, currentTick)) {
                    iterator = MarkerCache.erase(iterator);
                    ++expired;
                } else {
                    ++iterator;
                }
            }
            if (expired != 0U) {
                MarkersExpired.fetch_add(expired, std::memory_order_relaxed);
            }
            LastPurgeTick = currentTick;
        }

        if (Epoch.load(std::memory_order_acquire) != epoch) {
            MarkerCache.clear();
            TrackedMarkerCount.store(0U, std::memory_order_release);
            return 0U;
        }

        snapshots.reserve(MarkerCache.size());
        const auto sequence = PublishedSequence.load(
            std::memory_order_acquire);
        for (const auto& [unitId, marker] : MarkerCache) {
            if (marker.epoch != epoch
                || (!retainCurrentProjection
                    && !IsRecent(marker.observedTick, currentTick))) {
                continue;
            }
            snapshots.push_back(NativeAutomapMarkerSnapshot{
                .unitId = unitId,
                .classId = marker.classId,
                .superUniqueIndex = marker.superUniqueIndex,
                .x = marker.x,
                .y = marker.y,
                .nativeWidth = marker.nativeWidth,
                .nativeHeight = marker.nativeHeight,
                .rank = marker.rank,
                .immunityMask = marker.immunityMask,
                .epoch = marker.epoch,
                .sequence = sequence,
            });
            UpdateMaximum(
                MaximumPublishedDistanceSquared,
                marker.distanceSquared);
        }
        TrackedMarkerCount.store(
            static_cast<std::uint64_t>(snapshots.size()),
            std::memory_order_release);
        return snapshots.size();
    } catch (...) {
        snapshots.clear();
        StorageFailures.fetch_add(1U, std::memory_order_relaxed);
        return 0U;
    }
}

auto AcquireNativeAutomapViewport(
        NativeAutomapViewportSnapshot& snapshot,
        bool retainCurrentProjection) noexcept -> bool {
    snapshot = {};
    if (!Active.load(std::memory_order_acquire)
        || NativeAutomapViewportLock.test_and_set(
            std::memory_order_acquire)) {
        return false;
    }
    {
        const AtomicFlagRelease viewportRelease{NativeAutomapViewportLock};
        snapshot = PublishedNativeAutomapViewport;
    }

    const auto epoch = Epoch.load(std::memory_order_acquire);
    const auto currentTick = static_cast<std::uint64_t>(GetTickCount64());
    NativeAutomapClipBounds clipBounds{};
    if (snapshot.epoch != epoch
        || (!retainCurrentProjection
            && !IsRecent(snapshot.observedTick, currentTick))
        || !TryResolveNativeAutomapClipBounds(snapshot, clipBounds)) {
        snapshot = {};
        return false;
    }
    return true;
}

auto WantsNativeAutomapMarkerFrame(
        bool retainCurrentProjection) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || !CollectionEnabled.load(std::memory_order_acquire)) {
        return false;
    }
    if (retainCurrentProjection) {
        return LastAutomapPulseTick.load(std::memory_order_acquire) != 0U;
    }
    const auto currentTick = static_cast<std::uint64_t>(GetTickCount64());
    if (!IsRecent(
            LastAutomapPulseTick.load(std::memory_order_acquire),
            currentTick)) {
        return false;
    }
    // A recent native automap pulse is sufficient. This also lets the
    // renderer rotate the buffers immediately after an epoch reset, before
    // the first marker of the new gameplay epoch is published.
    return true;
}

auto GetNativeAutomapMarkerCounters() noexcept
        -> NativeAutomapMarkerCounters {
    return {
        .automapPulses = AutomapPulses.load(std::memory_order_relaxed),
        .monsterTableScans = MonsterTableScans.load(
            std::memory_order_relaxed),
        .monsterPositionRefreshes = MonsterPositionRefreshes.load(
            std::memory_order_relaxed),
        .trackedCurrent = TrackedMonsterCount.load(
            std::memory_order_relaxed),
        .trackedIdsResolved = TrackedIdsResolved.load(
            std::memory_order_relaxed),
        .trackedIdsMissing = TrackedIdsMissing.load(
            std::memory_order_relaxed),
        .monsterBucketsVisited = MonsterBucketsVisited.load(
            std::memory_order_relaxed),
        .monsterTraversalLimits = MonsterTraversalLimits.load(
            std::memory_order_relaxed),
        .unitsObserved = UnitsObserved.load(std::memory_order_relaxed),
        .monstersObserved = MonstersObserved.load(std::memory_order_relaxed),
        .modeRejected = ModeRejected.load(std::memory_order_relaxed),
        .unitFlagRejected = UnitFlagRejected.load(
            std::memory_order_relaxed),
        .classRejected = ClassRejected.load(
            std::memory_order_relaxed),
        .alignmentRejected = AlignmentRejected.load(
            std::memory_order_relaxed),
        .metadataFaults = MetadataFaults.load(
            std::memory_order_relaxed),
        .hostilesObserved = HostilesObserved.load(
            std::memory_order_relaxed),
        .hostilesThrough80 = HostilesThrough80.load(
            std::memory_order_relaxed),
        .hostilesFrom81Through140 = HostilesFrom81Through140.load(
            std::memory_order_relaxed),
        .hostilesFrom141Through220 = HostilesFrom141Through220.load(
            std::memory_order_relaxed),
        .hostilesBeyond220 = HostilesBeyond220.load(
            std::memory_order_relaxed),
        .projectionRejected = ProjectionRejected.load(
            std::memory_order_relaxed),
        .nativeClipRejected = NativeClipRejected.load(
            std::memory_order_relaxed),
        .acceptedThrough80 = AcceptedByDistanceBand[0].load(
            std::memory_order_relaxed),
        .acceptedFrom81Through140 = AcceptedByDistanceBand[1].load(
            std::memory_order_relaxed),
        .acceptedFrom141Through220 = AcceptedByDistanceBand[2].load(
            std::memory_order_relaxed),
        .acceptedBeyond220 = AcceptedByDistanceBand[3].load(
            std::memory_order_relaxed),
        .clipRejectedThrough80 = ClipRejectedByDistanceBand[0].load(
            std::memory_order_relaxed),
        .clipRejectedFrom81Through140 = ClipRejectedByDistanceBand[1].load(
            std::memory_order_relaxed),
        .clipRejectedFrom141Through220 = ClipRejectedByDistanceBand[2].load(
            std::memory_order_relaxed),
        .clipRejectedBeyond220 = ClipRejectedByDistanceBand[3].load(
            std::memory_order_relaxed),
        .candidatesAccepted = CandidatesAccepted.load(
            std::memory_order_relaxed),
        .markersInserted = MarkersInserted.load(
            std::memory_order_relaxed),
        .markersRefreshed = MarkersRefreshed.load(
            std::memory_order_relaxed),
        .markersExpired = MarkersExpired.load(
            std::memory_order_relaxed),
        .freshMarkers = TrackedMarkerCount.load(
            std::memory_order_relaxed),
        .contentionWaits = ContentionWaits.load(
            std::memory_order_relaxed),
        .storageFailures = StorageFailures.load(
            std::memory_order_relaxed),
        .accessFaults = AccessFaults.load(std::memory_order_relaxed),
        .maximumDiscoveryMicroseconds = MaximumDiscoveryMicroseconds.load(
            std::memory_order_relaxed),
        .totalDiscoveryMicroseconds = TotalDiscoveryMicroseconds.load(
            std::memory_order_relaxed),
        .discoveryTimingSamples = DiscoveryTimingSamples.load(
            std::memory_order_relaxed),
        .maximumRefreshMicroseconds = MaximumRefreshMicroseconds.load(
            std::memory_order_relaxed),
        .totalRefreshMicroseconds = TotalRefreshMicroseconds.load(
            std::memory_order_relaxed),
        .refreshTimingSamples = RefreshTimingSamples.load(
            std::memory_order_relaxed),
        .maximumHostileDistance = DistanceFromSquared(
            MaximumHostileDistanceSquared.load(std::memory_order_relaxed)),
        .maximumAcceptedDistance = DistanceFromSquared(
            MaximumAcceptedDistanceSquared.load(std::memory_order_relaxed)),
        .maximumPublishedDistance = DistanceFromSquared(
            MaximumPublishedDistanceSquared.load(std::memory_order_relaxed)),
    };
}

} // namespace RuffnecKk::MapSense
