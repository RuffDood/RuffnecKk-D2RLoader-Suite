#include "reveal_engine.hpp"

#include "external_label_provider.hpp"

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/core_exports.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t InitLevelRva = 0x3271C0;
constexpr std::uintptr_t GetLevelRva = 0x3267C0;
constexpr std::uintptr_t CreateActiveRoomRva = 0x3289A0;
constexpr std::uintptr_t PrepareRoomWitnessRva = 0x3289B3;
constexpr std::uintptr_t BuildRoomPipelineWitnessRva = 0x328FDF;
constexpr std::uintptr_t BuildNearRoomLinksRva = 0x3608A0;
constexpr std::uintptr_t AddPresetUnitsRva = 0x3DE0E0;
constexpr std::uintptr_t InitializeStaticRoomRva = 0x3F38D0;
constexpr std::uintptr_t AddStaticRoomTilesRva = 0x3F3930;
constexpr std::uintptr_t LoadRoomTileLibrariesRva = 0x3F3970;
constexpr std::uintptr_t ReleaseStaticRoomRva = 0x3F3AA0;
constexpr std::uintptr_t ReleaseStaticRoomWitnessRva = 0x3F3ADC;
constexpr std::uintptr_t PathGetRoomRva = 0x341C30;
constexpr std::uintptr_t StandardAutomapCallbackRva = 0x0D2240;
constexpr std::uintptr_t StandardAutomapLayerWitnessRva = 0x0D22A5;
constexpr std::uintptr_t GetLevelDefRecordRva = 0x32C200;
constexpr std::uintptr_t GetOrCreateAutomapLayerWitnessRva = 0x0D5360;
constexpr std::uintptr_t AutomapLayerTreeLayoutWitnessRva = 0x0D53A5;
constexpr std::uintptr_t CurrentAutomapLayerOwnerWitnessRva = 0x0D541E;
constexpr std::uintptr_t CurrentAutomapLayerOwnerRva = 0x2A2CF68;
constexpr std::uintptr_t InsertAutomapCellRva = 0x0D1460;
constexpr std::uintptr_t FindAutomapCellRva = 0x0D4B70;
constexpr std::uintptr_t AutomapSerializerLimitWitnessRva = 0x0D7E3F;
constexpr std::uintptr_t ClientDrlgDifficultyWitnessRva = 0x32766A;
constexpr std::uintptr_t DrlgSeedInitializationWitnessRva = 0x326E89;

constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitDynamicPathOffset = 0x38;
constexpr std::size_t ActiveRoomDrlgRoomOffset = 0x18;
constexpr std::size_t DrlgRoomLevelOffset = 0x90;
constexpr std::size_t LevelFirstRoomOffset = 0x10;
constexpr std::size_t LevelDrlgOffset = 0x1C8;
constexpr std::size_t LevelIdOffset = 0x1F8;
constexpr std::size_t RoomNextOffset = 0x48;
constexpr std::size_t RoomNearCountOffset = 0x18;
constexpr std::size_t RoomFlagsOffset = 0x50;
constexpr std::size_t RoomActiveRoomOffset = 0x58;
constexpr std::size_t RoomTypeOffset = 0x74;
constexpr std::size_t RoomPresetUnitOffset = 0x98;
constexpr std::size_t DrlgDifficultyOffset = 0x830;
constexpr std::size_t DrlgAutomapCallbackOffset = 0x838;
constexpr std::size_t DrlgMapSeedOffset = 0x840;
constexpr std::size_t DrlgStartSeedOffset = 0x860;
constexpr std::uint32_t MaximumRoomsPerLevel = 4096;
constexpr std::size_t LevelDefAutomapLayerOffset = 0x08U;
constexpr std::size_t AutomapLayerIdOffset = 0x00U;
constexpr std::size_t AutomapLayerFloorTreeOffset = 0x08U;
constexpr std::size_t AutomapLayerWallTreeOffset = 0x30U;
constexpr std::size_t AutomapCellTreeSizeOffset = 0x20U;
constexpr std::uint32_t MaximumNativeAtlasCellsPerPump = 16'384U;
constexpr std::size_t MaximumNativeAtlasWitnessesPerTree = 8U;
constexpr std::uint8_t MaximumNativeAtlasWitnessPasses = 3U;

using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using InitLevelFn = void(__fastcall*)(std::uint8_t, void*);
using GetLevelFn = void*(__fastcall*)(std::uint8_t, void*, std::uint32_t);
using CreateActiveRoomFn = void*(__fastcall*)(std::uint8_t, void*);
using PrepareStaticRoomFn = void(__fastcall*)(std::uint8_t, void*);
using ReleaseStaticRoomFn = void(__fastcall*)(void*, std::int32_t);
using PathGetRoomFn = void*(__fastcall*)(void*) noexcept;
using RevealActiveRoomFn = void(__fastcall*)(void*);
using GetLevelDefRecordFn = void*(__fastcall*)(
    std::uint8_t,
    std::int32_t) noexcept;
struct NativeAutomapCellKey final {
    std::uint16_t tag{};
    std::int16_t frame{};
    std::int32_t x{};
    std::int32_t y{};
};

struct NativeAutomapInsertResult final {
    void* node{};
    std::uint8_t inserted{};
    std::array<std::uint8_t, 7U> reserved{};
};

struct NativeAutomapFindResult final {
    void* node{};
    void* insertionSlot{};
};

using FindAutomapCellFn = NativeAutomapFindResult*(__fastcall*)(
    void*,
    NativeAutomapFindResult*,
    const NativeAutomapCellKey*) noexcept;
using InsertAutomapCellFn = NativeAutomapInsertResult*(__fastcall*)(
    void*,
    NativeAutomapInsertResult*,
    const NativeAutomapCellKey*) noexcept;

struct NativeAtlasLevelWork final {
    std::size_t geometryLevelIndex{};
    std::int32_t levelId{UnknownRevealLevelId};
    std::int32_t nativeLayer{-1};
    std::uint32_t nextCell{};
};

struct NativeAtlasPublicationState final {
    std::uint64_t sessionGeneration{};
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::uint8_t act{};
    std::uint64_t geometryDigest{};
    std::shared_ptr<const ExternalAtlasGeometrySnapshot> snapshot{};
    std::vector<NativeAutomapLevelLayer> catalogLevels{};
    std::vector<NativeAtlasLevelWork> levels{};
    std::vector<std::int32_t> readyLayers{};
    std::size_t levelIndex{};
    std::size_t layerEndIndex{};
    std::int32_t activeLayer{-1};
    std::int32_t failedLayer{-1};
    std::array<NativeAutomapCellKey,
        MaximumNativeAtlasWitnessesPerTree> floorWitnesses{};
    std::array<NativeAutomapCellKey,
        MaximumNativeAtlasWitnessesPerTree> wallWitnesses{};
    std::size_t floorWitnessCount{};
    std::size_t wallWitnessCount{};
    std::uint8_t witnessPasses{};
    bool active{};
    bool waitingForOwner{};
    bool awaitingWitness{};
    bool failed{};
};

static_assert(sizeof(NativeAutomapCellKey) == 12U);
static_assert(offsetof(NativeAutomapCellKey, frame) == 0x02U);
static_assert(offsetof(NativeAutomapCellKey, x) == 0x04U);
static_assert(offsetof(NativeAutomapCellKey, y) == 0x08U);
static_assert(sizeof(NativeAutomapInsertResult) == 16U);
static_assert(sizeof(NativeAutomapFindResult) == 16U);
static_assert(std::is_standard_layout_v<NativeAutomapCellKey>);
static_assert(std::is_trivially_copyable_v<NativeAutomapCellKey>);

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
InitLevelFn InitLevel{};
InitLevelFn OriginalInitLevel{};
GetLevelFn GetLevel{};
CreateActiveRoomFn CreateActiveRoom{};
PrepareStaticRoomFn BuildNearRoomLinks{};
PrepareStaticRoomFn AddPresetUnits{};
PrepareStaticRoomFn InitializeStaticRoom{};
PrepareStaticRoomFn AddStaticRoomTiles{};
PrepareStaticRoomFn LoadRoomTileLibraries{};
ReleaseStaticRoomFn ReleaseStaticRoom{};
PathGetRoomFn PathGetRoom{};
GetLevelDefRecordFn GetLevelDefRecord{};
FindAutomapCellFn FindAutomapCell{};
InsertAutomapCellFn InsertAutomapCell{};
D2RL::CoreExports::IsInGameFn IsInGame{};
D2RL::CoreExports::ExecuteConsoleCommandFn ExecuteConsoleCommand{};

std::atomic_bool Active{};
std::atomic_bool RevealAllArmed{};
void* ActiveClientDrlg{};
std::uint8_t ActiveClientDataContext{};
std::atomic_flag ClientDrlgLock = ATOMIC_FLAG_INIT;
std::atomic_uint64_t LevelsRevealed{};
std::atomic_uint64_t RoomsRevealed{};
std::atomic_uint64_t RevealFailures{};
std::atomic_uint64_t TraversalLimits{};
std::atomic_uint64_t StaticRoomCandidates{};
std::atomic_uint64_t StaticRoomsMaterialized{};
std::atomic_uint64_t StaticRoomsReleased{};
std::atomic_uint64_t StaticRoomFailures{};
std::atomic<RevealLevelInitializedCallback> LevelInitializedCallback{};
std::atomic<void*> LevelInitializedUserData{};

std::mutex NativeAtlasMutex;
NativeAtlasPublicationState NativeAtlasPublication{};
std::atomic<std::shared_ptr<const NativeAutomapLayerCatalog>>
    PublishedNativeLayerCatalog{};
std::atomic_uint64_t NativeAtlasesAccepted{};
std::atomic_uint64_t NativeAtlasesCompleted{};
std::atomic_uint64_t NativeLayerCatalogsPublished{};
std::atomic_uint64_t NativeCellsAttempted{};
std::atomic_uint64_t NativeCellsInserted{};
std::atomic_uint64_t NativeDuplicateCells{};
std::atomic_uint64_t NativeAtlasFailures{};
std::atomic_uint64_t NativePendingCells{};
std::atomic_uint64_t NativeOwnerPending{};
std::atomic_uint64_t NativeOwnerMismatches{};
std::atomic_uint64_t NativeWitnessPasses{};
std::atomic_uint64_t NativeWitnessFailures{};
std::atomic_uint64_t NativeLayerTransitions{};
std::atomic_uint64_t NativeMaximumFloorTreeCells{};
std::atomic_uint64_t NativeMaximumWallTreeCells{};

auto ResolveCoreBridge() noexcept -> bool {
    const HMODULE core = GetModuleHandleA(D2RL::CoreExports::CoreDllName);
    if (core == nullptr) return false;
    IsInGame = reinterpret_cast<D2RL::CoreExports::IsInGameFn>(
        GetProcAddress(core, D2RL::CoreExports::IsInGameInfo.name));
    ExecuteConsoleCommand =
        reinterpret_cast<D2RL::CoreExports::ExecuteConsoleCommandFn>(
            GetProcAddress(
                core,
                D2RL::CoreExports::ExecuteConsoleCommandInfo.name));
    return IsInGame != nullptr && ExecuteConsoleCommand != nullptr;
}

auto SubmitNativeActReveal() noexcept -> bool {
    return IsInGame != nullptr && IsInGame()
        && ExecuteConsoleCommand != nullptr
        && ExecuteConsoleCommand("revealmap");
}

class ClientDrlgLockGuard {
public:
    ClientDrlgLockGuard() noexcept {
        while (ClientDrlgLock.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~ClientDrlgLockGuard() {
        ClientDrlgLock.clear(std::memory_order_release);
    }

    ClientDrlgLockGuard(const ClientDrlgLockGuard&) = delete;
    auto operator=(const ClientDrlgLockGuard&) -> ClientDrlgLockGuard& = delete;
};

void SnapshotClientDrlg(
        std::uint8_t& dataContext,
        std::uint8_t*& drlg) noexcept {
    ClientDrlgLockGuard lock;
    dataContext = ActiveClientDataContext;
    drlg = static_cast<std::uint8_t*>(ActiveClientDrlg);
}

void StoreClientDrlg(
        std::uint8_t dataContext,
        void* drlg) noexcept {
    ClientDrlgLockGuard lock;
    ActiveClientDataContext = dataContext;
    ActiveClientDrlg = drlg;
}

template <typename Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

[[nodiscard]] auto ResolveNativeAutomapLayer(
        std::uint8_t dataContext,
        std::int32_t levelId,
        std::int32_t& output) noexcept -> bool {
    output = -1;
    if (GetLevelDefRecord == nullptr || levelId <= 0) return false;
    __try {
        auto* const record = static_cast<const std::uint8_t*>(
            GetLevelDefRecord(dataContext, levelId));
        if (record == nullptr) return false;
        const auto layer = *reinterpret_cast<const std::int32_t*>(
            record + LevelDefAutomapLayerOffset);
        if (layer < 0) return false;
        output = layer;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output = -1;
        return false;
    }
}

[[nodiscard]] auto RemainingNativeAtlasCellsLocked() noexcept
        -> std::uint64_t {
    if (NativeAtlasPublication.snapshot == nullptr
        || NativeAtlasPublication.snapshot->geometry == nullptr) {
        return 0U;
    }
    const auto& geometry = *NativeAtlasPublication.snapshot->geometry;
    std::uint64_t remaining{};
    const auto end = std::min(
        NativeAtlasPublication.layerEndIndex,
        NativeAtlasPublication.levels.size());
    for (std::size_t index = NativeAtlasPublication.levelIndex;
            index < end;
            ++index) {
        const auto& work = NativeAtlasPublication.levels[index];
        if (work.geometryLevelIndex >= geometry.levels.size()) return 0U;
        const auto& level = geometry.levels[work.geometryLevelIndex];
        const auto consumed = index == NativeAtlasPublication.levelIndex
            ? work.nextCell : 0U;
        if (consumed > level.cellCount) return 0U;
        remaining += static_cast<std::uint64_t>(level.cellCount - consumed);
    }
    return remaining;
}

[[nodiscard]] auto BuildNativeLayerCatalogLocked() noexcept
        -> std::shared_ptr<const NativeAutomapLayerCatalog> {
    if (NativeAtlasPublication.snapshot == nullptr
        || NativeAtlasPublication.snapshot->geometry == nullptr) {
        return {};
    }
    try {
        auto catalog = std::make_shared<NativeAutomapLayerCatalog>();
        catalog->sessionGeneration =
            NativeAtlasPublication.sessionGeneration;
        catalog->seed = NativeAtlasPublication.seed;
        catalog->difficulty = NativeAtlasPublication.difficulty;
        catalog->act = NativeAtlasPublication.act;
        catalog->geometryDigest = NativeAtlasPublication.geometryDigest;
        catalog->levels.assign(
            NativeAtlasPublication.catalogLevels.begin(),
            NativeAtlasPublication.catalogLevels.end());
        catalog->readyLayers.assign(
            NativeAtlasPublication.readyLayers.begin(),
            NativeAtlasPublication.readyLayers.end());
        std::sort(catalog->readyLayers.begin(), catalog->readyLayers.end());
        catalog->readyLayers.erase(
            std::unique(
                catalog->readyLayers.begin(),
                catalog->readyLayers.end()),
            catalog->readyLayers.end());
        return catalog;
    } catch (...) {
        return {};
    }
}

[[nodiscard]] auto PublishNativeLayerCatalogLocked() noexcept -> bool {
    const auto catalog = BuildNativeLayerCatalogLocked();
    if (catalog == nullptr) return false;
    PublishedNativeLayerCatalog.store(catalog, std::memory_order_release);
    NativeLayerCatalogsPublished.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

[[nodiscard]] auto SameNativeAtlasIdentityLocked(
        std::uint64_t sessionGeneration,
        const ExternalAtlasGeometrySnapshot& snapshot) noexcept -> bool {
    return NativeAtlasPublication.sessionGeneration == sessionGeneration
        && NativeAtlasPublication.seed == snapshot.seed
        && NativeAtlasPublication.difficulty == snapshot.difficulty
        && NativeAtlasPublication.act == snapshot.act
        && NativeAtlasPublication.snapshot != nullptr
        && NativeAtlasPublication.snapshot->geometry != nullptr
        && snapshot.geometry != nullptr
        && NativeAtlasPublication.geometryDigest
            == snapshot.geometry->digest;
}

// MSVC does not permit SEH in a function that owns C++ objects requiring
// unwinding. Keep native fault containment in trivial leaf wrappers so the
// publisher can still use vectors and shared snapshots safely.
[[nodiscard]] auto TryAcquireCurrentNativeAutomapLayer(
        std::int32_t expectedLayer,
        void*& owner,
        std::int32_t& observedLayer) noexcept
        -> NativeAutomapActiveOwnerState {
    owner = nullptr;
    observedLayer = -1;
    __try {
        if (Base == nullptr) {
            return NativeAutomapActiveOwnerState::Mismatch;
        }
        owner = *reinterpret_cast<void**>(
            Base + CurrentAutomapLayerOwnerRva);
        if (owner == nullptr) {
            return NativeAutomapActiveOwnerState::Pending;
        }
        if ((reinterpret_cast<std::uintptr_t>(owner)
                & (alignof(void*) - 1U)) != 0U) {
            owner = nullptr;
            return NativeAutomapActiveOwnerState::Mismatch;
        }
        observedLayer = *reinterpret_cast<const std::int32_t*>(
            static_cast<const std::uint8_t*>(owner)
                + AutomapLayerIdOffset);
        const auto state = ClassifyNativeAutomapActiveOwner(
            true,
            observedLayer,
            expectedLayer);
        if (state != NativeAutomapActiveOwnerState::Ready) owner = nullptr;
        return state;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        owner = nullptr;
        observedLayer = -1;
        return NativeAutomapActiveOwnerState::Mismatch;
    }
}

[[nodiscard]] auto StartNativeLayerPublicationLocked(
        std::int32_t layer) noexcept -> bool {
    if (layer < 0 || (NativeAtlasPublication.failed
            && NativeAtlasPublication.failedLayer == layer)) {
        return false;
    }
    const auto first = std::lower_bound(
        NativeAtlasPublication.levels.begin(),
        NativeAtlasPublication.levels.end(),
        layer,
        [](const NativeAtlasLevelWork& work,
                std::int32_t requestedLayer) noexcept {
            return work.nativeLayer < requestedLayer;
        });
    if (first == NativeAtlasPublication.levels.end()
        || first->nativeLayer != layer) {
        return false;
    }
    const auto last = std::upper_bound(
        first,
        NativeAtlasPublication.levels.end(),
        layer,
        [](std::int32_t requestedLayer,
                const NativeAtlasLevelWork& work) noexcept {
            return requestedLayer < work.nativeLayer;
        });
    for (auto current = first; current != last; ++current) {
        current->nextCell = 0U;
    }
    if (NativeAtlasPublication.activeLayer >= 0
        && NativeAtlasPublication.activeLayer != layer) {
        NativeLayerTransitions.fetch_add(1U, std::memory_order_relaxed);
    }
    NativeAtlasPublication.levelIndex = static_cast<std::size_t>(
        std::distance(NativeAtlasPublication.levels.begin(), first));
    NativeAtlasPublication.layerEndIndex = static_cast<std::size_t>(
        std::distance(NativeAtlasPublication.levels.begin(), last));
    NativeAtlasPublication.activeLayer = layer;
    NativeAtlasPublication.failedLayer = -1;
    NativeAtlasPublication.floorWitnessCount = 0U;
    NativeAtlasPublication.wallWitnessCount = 0U;
    NativeAtlasPublication.witnessPasses = 0U;
    NativeAtlasPublication.active = true;
    NativeAtlasPublication.waitingForOwner = false;
    NativeAtlasPublication.awaitingWitness = false;
    NativeAtlasPublication.failed = false;
    NativePendingCells.store(
        RemainingNativeAtlasCellsLocked(),
        std::memory_order_relaxed);
    NativeAtlasesAccepted.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

[[nodiscard]] auto NativeCatalogContainsLayerLocked(
        std::int32_t layer) noexcept -> bool {
    return layer >= 0 && std::find_if(
        NativeAtlasPublication.catalogLevels.begin(),
        NativeAtlasPublication.catalogLevels.end(),
        [layer](const NativeAutomapLevelLayer& entry) noexcept {
            return entry.layer == layer;
        }) != NativeAtlasPublication.catalogLevels.end();
}

[[nodiscard]] auto NativeGeometryContainsLayerLocked(
        std::int32_t layer) noexcept -> bool {
    return layer >= 0 && std::find_if(
        NativeAtlasPublication.levels.begin(),
        NativeAtlasPublication.levels.end(),
        [layer](const NativeAtlasLevelWork& work) noexcept {
            return work.nativeLayer == layer;
        }) != NativeAtlasPublication.levels.end();
}

[[nodiscard]] auto CompleteNativeLabelOnlyLayerLocked(
        std::int32_t layer) noexcept -> bool {
    if (!NativeCatalogContainsLayerLocked(layer)
        || NativeGeometryContainsLayerLocked(layer)) {
        return false;
    }
    try {
        NativeAtlasPublication.readyLayers.push_back(layer);
        std::sort(
            NativeAtlasPublication.readyLayers.begin(),
            NativeAtlasPublication.readyLayers.end());
        NativeAtlasPublication.readyLayers.erase(
            std::unique(
                NativeAtlasPublication.readyLayers.begin(),
                NativeAtlasPublication.readyLayers.end()),
            NativeAtlasPublication.readyLayers.end());
    } catch (...) {
        return false;
    }
    NativeAtlasPublication.active = false;
    NativeAtlasPublication.waitingForOwner = false;
    NativeAtlasPublication.awaitingWitness = false;
    NativeAtlasPublication.failed = false;
    NativeAtlasPublication.failedLayer = -1;
    NativeAtlasPublication.activeLayer = layer;
    NativePendingCells.store(0U, std::memory_order_relaxed);
    if (!PublishNativeLayerCatalogLocked()) return false;
    NativeAtlasesCompleted.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

void FailNativeLayerPublicationLocked(std::int32_t layer) noexcept {
    NativeAtlasPublication.active = false;
    NativeAtlasPublication.waitingForOwner = false;
    NativeAtlasPublication.awaitingWitness = false;
    NativeAtlasPublication.failed = true;
    NativeAtlasPublication.failedLayer = layer;
    NativePendingCells.store(0U, std::memory_order_relaxed);
    NativeAtlasFailures.fetch_add(1U, std::memory_order_relaxed);
}

[[nodiscard]] auto TryInsertNativeAutomapCell(
        void* tree,
        NativeAutomapInsertResult& result,
        const NativeAutomapCellKey& key) noexcept -> bool {
    result = {};
    __try {
        return InsertAutomapCell(tree, &result, &key) == &result
            && result.node != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = {};
        return false;
    }
}

[[nodiscard]] auto TryFindNativeAutomapCell(
        void* tree,
        NativeAutomapFindResult& result,
        const NativeAutomapCellKey& key) noexcept -> bool {
    result = {};
    __try {
        return FindAutomapCell != nullptr
            && FindAutomapCell(tree, &result, &key) == &result
            && result.node != nullptr
            && (result.insertionSlot == nullptr
                || (reinterpret_cast<std::uintptr_t>(result.insertionSlot)
                    & (alignof(void*) - 1U)) == 0U);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = {};
        return false;
    }
}

[[nodiscard]] constexpr auto SameNativeAutomapCellKey(
        const NativeAutomapCellKey& left,
        const NativeAutomapCellKey& right) noexcept -> bool {
    return left.tag == right.tag && left.frame == right.frame
        && left.x == right.x && left.y == right.y;
}

void RememberNativeAtlasWitnessLocked(
        bool wallTree,
        const NativeAutomapCellKey& key) noexcept {
    auto& witnesses = wallTree
        ? NativeAtlasPublication.wallWitnesses
        : NativeAtlasPublication.floorWitnesses;
    auto& count = wallTree
        ? NativeAtlasPublication.wallWitnessCount
        : NativeAtlasPublication.floorWitnessCount;
    for (std::size_t index = 0U; index < count; ++index) {
        if (SameNativeAutomapCellKey(witnesses[index], key)) return;
    }
    if (count < witnesses.size()) witnesses[count++] = key;
}

[[nodiscard]] auto VerifyNativeAtlasWitnesses(
        void* tree,
        const NativeAutomapCellKey* witnesses,
        std::size_t count) noexcept -> bool {
    if (tree == nullptr || witnesses == nullptr) return count == 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        NativeAutomapFindResult found{};
        if (!TryFindNativeAutomapCell(tree, found, witnesses[index])
            || found.insertionSlot != nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto TryReadNativeAutomapTreeCellCount(
        const void* tree,
        std::uint64_t& output) noexcept -> bool {
    output = 0U;
    __try {
        if (tree == nullptr) return false;
        output = *reinterpret_cast<const std::uint64_t*>(
            static_cast<const std::uint8_t*>(tree)
                + AutomapCellTreeSizeOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output = 0U;
        return false;
    }
}

void RecordMaximumNativeTreeCellCount(
        std::atomic_uint64_t& maximum,
        std::uint64_t value) noexcept {
    auto observed = maximum.load(std::memory_order_relaxed);
    while (observed < value
        && !maximum.compare_exchange_weak(
            observed,
            value,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}

[[nodiscard]] auto InsertNativeAtlasCellsLocked(
        const ClientLevelView& current,
        std::uint32_t maximumCells) noexcept
        -> NativeAutomapAtlasPublicationStatus {
    if (!NativeAtlasPublication.active
        || NativeAtlasPublication.snapshot == nullptr
        || NativeAtlasPublication.snapshot->geometry == nullptr
        || FindAutomapCell == nullptr || InsertAutomapCell == nullptr) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    const auto& geometry = *NativeAtlasPublication.snapshot->geometry;
    std::int32_t currentLayer{};
    if (current.mapSeed != NativeAtlasPublication.seed
        || current.difficulty != NativeAtlasPublication.difficulty
        || current.levelId <= 0
        || !ResolveNativeAutomapLayer(
            current.dataContext, current.levelId, currentLayer)) {
        return NativeAutomapAtlasPublicationStatus::Stale;
    }
    if (currentLayer != NativeAtlasPublication.activeLayer) {
        return NativeAutomapAtlasPublicationStatus::Stale;
    }

    void* owner{};
    std::int32_t observedLayer{};
    const auto ownerState = TryAcquireCurrentNativeAutomapLayer(
        currentLayer,
        owner,
        observedLayer);
    if (NativeAutomapOwnerRequiresPulse(ownerState) || owner == nullptr) {
        (ownerState == NativeAutomapActiveOwnerState::Pending
                ? NativeOwnerPending
                : NativeOwnerMismatches)
            .fetch_add(1U, std::memory_order_relaxed);
        NativeAtlasPublication.active = false;
        NativeAtlasPublication.waitingForOwner = true;
        NativePendingCells.store(
            RemainingNativeAtlasCellsLocked(),
            std::memory_order_relaxed);
        return NativeAutomapAtlasPublicationStatus::WaitingForOwner;
    }

    auto* const ownerBytes = static_cast<std::uint8_t*>(owner);
    std::uint32_t attemptedThisPump{};
    while (NativeAtlasPublication.levelIndex
            < NativeAtlasPublication.layerEndIndex
        && attemptedThisPump < maximumCells) {
        auto& work = NativeAtlasPublication.levels[
            NativeAtlasPublication.levelIndex];
        if (work.nativeLayer != currentLayer
            || work.geometryLevelIndex >= geometry.levels.size()) {
            FailNativeLayerPublicationLocked(currentLayer);
            return NativeAutomapAtlasPublicationStatus::Failed;
        }
        const auto& level = geometry.levels[work.geometryLevelIndex];
        if (level.levelId != work.levelId
            || level.firstCell > geometry.cells.size()
            || level.cellCount > geometry.cells.size() - level.firstCell
            || work.nextCell > level.cellCount) {
            FailNativeLayerPublicationLocked(currentLayer);
            return NativeAutomapAtlasPublicationStatus::Failed;
        }
        while (work.nextCell < level.cellCount
            && attemptedThisPump < maximumCells) {
            const auto& cell = geometry.cells[
                static_cast<std::size_t>(level.firstCell) + work.nextCell];
            NativeAutomapCellKeyValue value{};
            if (!BuildNativeAutomapCellKeyValue(
                    cell.frame,
                    cell.tileX,
                    cell.tileY,
                    cell.raised,
                    value)) {
                FailNativeLayerPublicationLocked(currentLayer);
                return NativeAutomapAtlasPublicationStatus::Failed;
            }
            const NativeAutomapCellKey key{
                .tag = NativeAutomapSyntheticCellTag,
                .frame = value.frame,
                .x = value.x,
                .y = value.y,
            };
            auto* const tree = ownerBytes + (cell.wallTree
                ? AutomapLayerWallTreeOffset
                : AutomapLayerFloorTreeOffset);
            std::uint64_t countBefore{};
            if (!TryReadNativeAutomapTreeCellCount(tree, countBefore)) {
                FailNativeLayerPublicationLocked(currentLayer);
                return NativeAutomapAtlasPublicationStatus::Failed;
            }
            auto& totalMaximum = cell.wallTree
                ? NativeMaximumWallTreeCells
                : NativeMaximumFloorTreeCells;
            RecordMaximumNativeTreeCellCount(totalMaximum, countBefore);

            NativeAutomapFindResult found{};
            if (!TryFindNativeAutomapCell(tree, found, key)) {
                FailNativeLayerPublicationLocked(currentLayer);
                return NativeAutomapAtlasPublicationStatus::Failed;
            }

            bool inserted{};
            if (found.insertionSlot != nullptr) {
                NativeAutomapInsertResult result{};
                if (!TryInsertNativeAutomapCell(tree, result, key)
                    || result.inserted == 0U) {
                    FailNativeLayerPublicationLocked(currentLayer);
                    return NativeAutomapAtlasPublicationStatus::Failed;
                }
                std::uint64_t countAfter{};
                if (!TryReadNativeAutomapTreeCellCount(tree, countAfter)
                    || countAfter != countBefore + 1U) {
                    FailNativeLayerPublicationLocked(currentLayer);
                    return NativeAutomapAtlasPublicationStatus::Failed;
                }
                inserted = true;
                RecordMaximumNativeTreeCellCount(totalMaximum, countAfter);
            }
            RememberNativeAtlasWitnessLocked(cell.wallTree, key);
            ++work.nextCell;
            ++attemptedThisPump;
            NativeCellsAttempted.fetch_add(1U, std::memory_order_relaxed);
            if (inserted) {
                NativeCellsInserted.fetch_add(1U, std::memory_order_relaxed);
            } else {
                NativeDuplicateCells.fetch_add(1U, std::memory_order_relaxed);
            }
        }
        if (work.nextCell == level.cellCount) {
            ++NativeAtlasPublication.levelIndex;
        }
    }

    const auto remaining = RemainingNativeAtlasCellsLocked();
    NativePendingCells.store(remaining, std::memory_order_relaxed);
    if (NativeAtlasPublication.levelIndex
            >= NativeAtlasPublication.layerEndIndex) {
        if (NativeAtlasPublication.floorWitnessCount == 0U
            && NativeAtlasPublication.wallWitnessCount == 0U) {
            FailNativeLayerPublicationLocked(currentLayer);
            return NativeAutomapAtlasPublicationStatus::Failed;
        }
        NativeAtlasPublication.active = false;
        NativeAtlasPublication.waitingForOwner = false;
        NativeAtlasPublication.awaitingWitness = true;
        NativeAtlasPublication.witnessPasses = 0U;
        NativePendingCells.store(0U, std::memory_order_relaxed);
        return NativeAutomapAtlasPublicationStatus::AwaitingWitness;
    }
    return NativeAutomapAtlasPublicationStatus::InProgress;
}

auto RevealLevelUnchecked(
        std::uint8_t dataContext,
        void* level) noexcept -> bool {
    __try {
        if (level == nullptr || CreateActiveRoom == nullptr || Base == nullptr) {
            return false;
        }

        auto* const levelBytes = static_cast<std::uint8_t*>(level);
        auto* const drlg = *reinterpret_cast<std::uint8_t**>(
            levelBytes + LevelDrlgOffset);
        if (drlg == nullptr) return false;

        const auto callback = *reinterpret_cast<RevealActiveRoomFn*>(
            drlg + DrlgAutomapCallbackOffset);
        if (callback == nullptr) return false;
        if (reinterpret_cast<void*>(callback)
            != Base + StandardAutomapCallbackRva) {
            return false;
        }

        auto* room = *reinterpret_cast<std::uint8_t**>(
            levelBytes + LevelFirstRoomOffset);
        if (room == nullptr) return false;
        bool complete = true;
        std::uint32_t roomCount{};
        while (room != nullptr && roomCount < MaximumRoomsPerLevel) {
            if (void* const activeRoom = CreateActiveRoom(dataContext, room)) {
                callback(activeRoom);
                RoomsRevealed.fetch_add(1, std::memory_order_relaxed);
            } else {
                complete = false;
            }
            room = *reinterpret_cast<std::uint8_t**>(room + RoomNextOffset);
            ++roomCount;
        }
        if (room != nullptr) {
            TraversalLimits.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (complete) {
            LevelsRevealed.fetch_add(1, std::memory_order_relaxed);
        }
        return complete;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto ResolveClientLevel(ClientLevelView& output) noexcept -> bool {
    std::uint8_t clientDataContext{};
    std::uint8_t* clientDrlg{};
    SnapshotClientDrlg(clientDataContext, clientDrlg);
    __try {
        if (GetLocalDataContext == nullptr || GetLocalPlayer == nullptr
            || PathGetRoom == nullptr || GetLevel == nullptr
            || InitLevel == nullptr || Base == nullptr) {
            return false;
        }

        const std::int32_t context = GetLocalDataContext();
        if (context < 0 || context >= 8) return false;
        auto* const player = static_cast<std::uint8_t*>(GetLocalPlayer(context));
        if (player == nullptr
            || *reinterpret_cast<std::uint32_t*>(player + UnitTypeOffset) != 0) {
            return false;
        }
        void* const path = *reinterpret_cast<void**>(
            player + UnitDynamicPathOffset);
        if (path == nullptr) return false;
        auto* const activeRoom = static_cast<std::uint8_t*>(PathGetRoom(path));
        if (activeRoom == nullptr) return false;
        auto* const drlgRoom = *reinterpret_cast<std::uint8_t**>(
            activeRoom + ActiveRoomDrlgRoomOffset);
        if (drlgRoom == nullptr) return false;
        auto* const serverLevel = *reinterpret_cast<std::uint8_t**>(
            drlgRoom + DrlgRoomLevelOffset);
        if (serverLevel == nullptr) return false;
        const auto currentLevelId = *reinterpret_cast<std::uint32_t*>(
            serverLevel + LevelIdOffset);

        if (clientDrlg == nullptr) return false;
        const auto callback = *reinterpret_cast<RevealActiveRoomFn*>(
            clientDrlg + DrlgAutomapCallbackOffset);
        if (reinterpret_cast<void*>(callback)
            != Base + StandardAutomapCallbackRva) {
            return false;
        }
        const auto difficulty = *reinterpret_cast<const std::uint8_t*>(
            clientDrlg + DrlgDifficultyOffset);
        if (difficulty > 2U) return false;
        const auto mapSeed = *reinterpret_cast<const std::uint32_t*>(
            clientDrlg + DrlgMapSeedOffset);
        const auto drlgStartSeed = *reinterpret_cast<const std::uint32_t*>(
            clientDrlg + DrlgStartSeedOffset);
        if (DeriveDrlgStartSeed(mapSeed) != drlgStartSeed) return false;

        auto* const currentLevel = static_cast<std::uint8_t*>(GetLevel(
            clientDataContext, clientDrlg, currentLevelId));
        if (currentLevel == nullptr) return false;
        if (*reinterpret_cast<void**>(
                currentLevel + LevelFirstRoomOffset) == nullptr) {
            InitLevel(clientDataContext, currentLevel);
        }
        output = {
            .dataContext = clientDataContext,
            .difficulty = difficulty,
            .levelId = static_cast<std::int32_t>(currentLevelId),
            .mapSeed = mapSeed,
            .drlgStartSeed = drlgStartSeed,
            .activeRoom = activeRoom,
            .drlg = clientDrlg,
            .level = currentLevel,
        };
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto ResolveClientLevelByIdUnchecked(
        const ClientLevelView& current,
        std::int32_t levelId,
        void*& outputLevel) noexcept -> bool {
    outputLevel = nullptr;
    __try {
        if (GetLevel == nullptr || InitLevel == nullptr
            || current.drlg == nullptr || levelId <= 0) {
            return false;
        }
        auto* const level = static_cast<std::uint8_t*>(GetLevel(
            current.dataContext,
            current.drlg,
            static_cast<std::uint32_t>(levelId)));
        if (level == nullptr
            || *reinterpret_cast<void**>(level + LevelDrlgOffset)
                != current.drlg
            || *reinterpret_cast<const std::int32_t*>(level + LevelIdOffset)
                != levelId) {
            return false;
        }
        if (*reinterpret_cast<void**>(level + LevelFirstRoomOffset) == nullptr) {
            InitLevel(current.dataContext, level);
        }
        if (*reinterpret_cast<void**>(level + LevelFirstRoomOffset) == nullptr) {
            return false;
        }
        outputLevel = level;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CaptureClientDrlg(
        std::uint8_t dataContext,
        void* level) noexcept {
    void* capturedDrlg{};
    __try {
        auto* const levelBytes = static_cast<std::uint8_t*>(level);
        auto* const drlg = levelBytes
            ? *reinterpret_cast<std::uint8_t**>(
                levelBytes + LevelDrlgOffset)
            : nullptr;
        const auto callback = drlg
            ? *reinterpret_cast<RevealActiveRoomFn*>(
                drlg + DrlgAutomapCallbackOffset)
            : nullptr;
        if (reinterpret_cast<void*>(callback)
            == Base + StandardAutomapCallbackRva) {
            capturedDrlg = drlg;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (capturedDrlg != nullptr) {
        StoreClientDrlg(dataContext, capturedDrlg);
    }
}

__declspec(noinline) void __fastcall HookInitLevel(
        std::uint8_t dataContext,
        void* level) {
    const auto original = OriginalInitLevel;
    if (original == nullptr) return;
    original(dataContext, level);
    if (!Active.load(std::memory_order_acquire)) return;
    CaptureClientDrlg(dataContext, level);
    const auto callback = LevelInitializedCallback.load(
        std::memory_order_acquire);
    if (callback != nullptr) {
        callback(
            dataContext,
            level,
            LevelInitializedUserData.load(std::memory_order_acquire));
    }
}

auto ValidateRuntime() noexcept -> bool {
    constexpr std::array<std::uint8_t, 10> localContextExpected{
        0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 19> localPlayerExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
        0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 32> getLevelExpected{
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x02,
        0x00, 0x00, 0x48, 0x8B, 0x82, 0x68, 0x08, 0x00,
        0x00, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0xF2, 0x0F};
    constexpr std::array<std::uint8_t, 16> pathGetRoomExpected{
        0x48, 0x8B, 0x41, 0x20, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 32> initLevelExpected{
        0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0x82, 0xC8, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xF2,
        0x44, 0x0F, 0xB6, 0xF9, 0x8B, 0x90, 0x60, 0x08};
    constexpr std::array<std::uint8_t, 32> createRoomExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x8B, 0x42, 0x50, 0x48, 0x8B, 0xDA,
        0x0F, 0xB6, 0xF9, 0x0F, 0xBA, 0xE0, 0x18, 0x72,
        0x08, 0xE8, 0xB2, 0xAF, 0x0C, 0x00, 0x8B, 0x43};
    // The native wrapper proves the exact static preconditions: tile-library
    // bit 24, preset-descriptor bit 25 for preset rooms, then HAS_ROOM bit 20.
    constexpr std::array<std::uint8_t, 59> prepareRoomExpected{
        0x0F, 0xBA, 0xE0, 0x18, 0x72, 0x08, 0xE8, 0xB2,
        0xAF, 0x0C, 0x00, 0x8B, 0x43, 0x50, 0x0F, 0xBA,
        0xE0, 0x19, 0x72, 0x15, 0x83, 0x7B, 0x74, 0x02,
        0x75, 0x0F, 0x48, 0x8B, 0xD3, 0x40, 0x0F, 0xB6,
        0xCF, 0xE8, 0x07, 0x57, 0x0B, 0x00, 0x8B, 0x43,
        0x50, 0x0F, 0xBA, 0xE0, 0x14, 0x72, 0x0C, 0x48,
        0x8B, 0xD3, 0x40, 0x0F, 0xB6, 0xCF, 0xE8, 0xE2,
        0x05, 0x00, 0x00};
    // This witness is the safety boundary: near-room/static-grid/static-tile
    // calls occur before the separate ActiveRoom allocator at 0x326480.
    constexpr std::array<std::uint8_t, 76> buildRoomPipelineExpected{
        0x48, 0x83, 0x7A, 0x18, 0x00, 0x48, 0x8B, 0xDA,
        0x48, 0x8B, 0x82, 0x90, 0x00, 0x00, 0x00, 0x0F,
        0xB6, 0xF1, 0x48, 0x8B, 0xB8, 0xC8, 0x01, 0x00,
        0x00, 0x75, 0x05, 0xE8, 0xA1, 0x78, 0x03, 0x00,
        0x48, 0x8B, 0xD3, 0x40, 0x0F, 0xB6, 0xCE, 0xE8,
        0xC5, 0xA8, 0x0C, 0x00, 0x48, 0x8B, 0xD3, 0x40,
        0x0F, 0xB6, 0xCE, 0xE8, 0x19, 0xA9, 0x0C, 0x00,
        0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x5E,
        0xD4, 0xFF, 0xFF, 0xFE, 0x87, 0x24, 0x01, 0x00,
        0x00, 0xFF, 0x47, 0x08};
    constexpr std::array<std::uint8_t, 31> buildNearRoomLinksExpected{
        0x88, 0x4C, 0x24, 0x08, 0x53, 0x56, 0x41, 0x55,
        0x48, 0x83, 0xEC, 0x70, 0x48, 0x89, 0x7C, 0x24,
        0x60, 0x48, 0x8B, 0xF2, 0x0F, 0xB6, 0xF9, 0x48,
        0x8B, 0xCA, 0xE8, 0x01, 0x0C, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 30> addPresetUnitsExpected{
        0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0xAC, 0x24, 0x60, 0xFC, 0xFF, 0xFF,
        0x48, 0x81, 0xEC, 0xA0, 0x04, 0x00, 0x00, 0x48,
        0x8B, 0x05, 0xCA, 0xD1, 0x5E, 0x02};
    constexpr std::array<std::uint8_t, 31> initializeStaticRoomExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x0F, 0xB6, 0xF9, 0x48, 0x8B, 0xDA,
        0x48, 0x8D, 0x4A, 0x30, 0x8B, 0x52, 0x08, 0xE8,
        0x24, 0x38, 0xF7, 0xFF, 0x8B, 0x53, 0x74};
    constexpr std::array<std::uint8_t, 43> addStaticRoomTilesExpected{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xDA, 0x8B, 0x52, 0x74, 0x83, 0xEA, 0x01, 0x74,
        0x1A, 0x83, 0xFA, 0x01, 0x75, 0x1D, 0x48, 0x8B,
        0xD3, 0xE8, 0x92, 0xAD, 0xFE, 0xFF, 0x81, 0x4B,
        0x50, 0x00, 0x00, 0x10, 0x00, 0x48, 0x83, 0xC4,
        0x20, 0x5B, 0xC3};
    constexpr std::array<std::uint8_t, 32> loadRoomTileLibrariesExpected{
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
        0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x48,
        0x8B, 0x05, 0x42, 0x79, 0x5D, 0x02, 0x48, 0x33,
        0xC4, 0x48, 0x89, 0x84, 0x24, 0x30, 0x01, 0x00};
    constexpr std::array<std::uint8_t, 32> releaseStaticRoomExpected{
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x57, 0x41,
        0x54, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x81,
        0x90, 0x00, 0x00, 0x00, 0x8B, 0xEA, 0x48, 0x8B,
        0xF9, 0x48, 0x8B, 0x98, 0xC8, 0x01, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 43> releaseStaticRoomWitnessExpected{
        0x48, 0x8B, 0xCF, 0x4C, 0x89, 0x67, 0x58, 0xE8,
        0x88, 0xDB, 0xFE, 0xFF, 0x8B, 0x47, 0x50, 0x0F,
        0xBA, 0xE0, 0x14, 0x0F, 0x83, 0x47, 0x01, 0x00,
        0x00, 0x0F, 0xBA, 0xF8, 0x14, 0x4C, 0x89, 0x7C,
        0x24, 0x50, 0x89, 0x47, 0x50, 0xFF, 0x83, 0x50,
        0x08, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 32> callbackExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0xF1, 0xE8, 0x79, 0x90, 0xFB, 0xFF, 0x8B,
        0xC8, 0xE8, 0x22, 0x82, 0xFC, 0xFF, 0x48, 0x85};
    constexpr std::array<std::uint8_t, 32> getLevelDefExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xDA, 0x0F,
        0xB6, 0xF1, 0xE8, 0x79, 0x48, 0xFD, 0xFF, 0x48,
        0x8B, 0xE8, 0x85, 0xDB, 0x78, 0x34, 0x40, 0x0F};
    constexpr std::array<std::uint8_t, 32> getLayerExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0x1D, 0xEF, 0x7B, 0x95,
        0x02, 0x8B, 0xF9, 0x48, 0x85, 0xDB, 0x74, 0x14,
        0x39, 0x3B, 0x0F, 0x84, 0x9E, 0x00, 0x00, 0x00};
    // These two witnesses close every owner surface MapSense reads: layer id at
    // +0x00, floor/wall tree layouts at +0x08/+0x30, and the current-owner
    // global at D2R+0x2A2CF68. The latter also proves that a foreign owner would
    // enter D2R's destructive layer-switch routine, which MapSense never calls.
    constexpr std::array<std::uint8_t, 45> layerTreeLayoutExpected{
        0x48, 0x8D, 0x43, 0x08, 0x48, 0x89, 0x4B, 0x30,
        0x48, 0x89, 0x08, 0x48, 0x89, 0x40, 0x08, 0x48,
        0x89, 0x40, 0x10, 0x88, 0x48, 0x18, 0x48, 0x89,
        0x48, 0x20, 0x48, 0x8D, 0x43, 0x30, 0x48, 0x89,
        0x40, 0x08, 0x48, 0x89, 0x40, 0x10, 0x88, 0x48,
        0x18, 0x48, 0x89, 0x48, 0x20};
    constexpr std::array<std::uint8_t, 32> currentOwnerExpected{
        0x48, 0x3B, 0x1D, 0x43, 0x7B, 0x95, 0x02, 0x74,
        0x0F, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0x48, 0x89,
        0x5C, 0x24, 0x38, 0xE8, 0xDA, 0xC2, 0xFF, 0xFF,
        0x48, 0x8B, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x30};
    constexpr std::array<std::uint8_t, 32> insertCellExpected{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
        0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xFA, 0x49,
        0x8B, 0xE8, 0x48, 0x8D, 0x54, 0x24, 0x20, 0x48};
    constexpr std::array<std::uint8_t, 32> findCellExpected{
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x41, 0x56, 0x4C, 0x8B, 0x09, 0x4C,
        0x8D, 0x35, 0xDA, 0x63, 0x95, 0x02, 0x4D, 0x8B,
        0xD8, 0x48, 0x8B, 0xF1, 0x4C, 0x8B, 0xD1, 0x4D};
    // Unique serializer epilogue: [RSI+8] is the emitted uint16 element count,
    // not tree+0x20's total node count. Each zero-tag key contributes three
    // words; the doubled signed result bounds only those emitted records.
    constexpr std::array<std::uint8_t, 13> serializerLimitExpected{
        0x0F, 0xB7, 0x4E, 0x08, 0x66, 0x03, 0xC9,
        0x0F, 0xBF, 0xC9, 0x41, 0x89, 0x0F};
    // The standard room callback proves both the Levels record +0x08 Layer
    // field and the GetOrCreateLayer ABI before it reveals the ActiveRoom.
    constexpr std::array<std::uint8_t, 31> layerWitnessExpected{
        0x8B, 0xD0, 0x40, 0x0F, 0xB6, 0xCF, 0xE8, 0x50,
        0x9F, 0x25, 0x00, 0x8B, 0x48, 0x08, 0xE8, 0xA8,
        0x30, 0x00, 0x00, 0x4C, 0x8B, 0xC8, 0x41, 0xB8,
        0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD6};
    // RBP is the client Drlg and R8 is a Level in this unique witness.
    // Both Drlg+0x830 reads index three per-difficulty LevelDef arrays.
    constexpr std::array<std::uint8_t, 59> clientDifficultyExpected{
        0x48, 0x8B, 0xEA, 0x49, 0x8B, 0xD8, 0x41, 0x8B,
        0x90, 0xF8, 0x01, 0x00, 0x00, 0x44, 0x0F, 0xB6,
        0xF1, 0xE8, 0x80, 0x4B, 0x00, 0x00, 0x44, 0x0F,
        0xB6, 0x8D, 0x30, 0x08, 0x00, 0x00, 0x45, 0x33,
        0xDB, 0x48, 0x8B, 0xF8, 0x41, 0x8B, 0xF3, 0x44,
        0x8B, 0x40, 0x2C, 0x46, 0x8B, 0x54, 0x88, 0x0C,
        0x44, 0x89, 0x53, 0x2C, 0x0F, 0xB6, 0x8D, 0x30,
        0x08, 0x00, 0x00};
    // DRLG allocation initializes {seed, 0x29A}, advances the native LCG once,
    // stores the original seed at +0x840 and its low result at +0x860.
    constexpr std::array<std::uint8_t, 129> seedInitializationExpected{
        0x8B, 0xD3, 0x48, 0x89, 0x87, 0x58, 0x08, 0x00,
        0x00, 0x44, 0x88, 0xAF, 0x70, 0x08, 0x00, 0x00,
        0xE8, 0x72, 0x02, 0x04, 0x00, 0x8B, 0x07, 0x4C,
        0x69, 0xC8, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x47,
        0x04, 0x4C, 0x03, 0xC8, 0x89, 0x9F, 0x40, 0x08,
        0x00, 0x00, 0x8B, 0x85, 0x28, 0x08, 0x00, 0x00,
        0x49, 0x8B, 0xD1, 0x89, 0x87, 0x10, 0x01, 0x00,
        0x00, 0x48, 0x8B, 0x85, 0x30, 0x08, 0x00, 0x00,
        0x48, 0x89, 0x87, 0x28, 0x01, 0x00, 0x00, 0x0F,
        0xB6, 0x85, 0x38, 0x08, 0x00, 0x00, 0x88, 0x87,
        0x30, 0x08, 0x00, 0x00, 0x48, 0x8B, 0x85, 0x40,
        0x08, 0x00, 0x00, 0x48, 0xC1, 0xEA, 0x20, 0x48,
        0x89, 0x87, 0x38, 0x08, 0x00, 0x00, 0x48, 0x8B,
        0x85, 0x48, 0x08, 0x00, 0x00, 0x48, 0x89, 0x87,
        0x78, 0x08, 0x00, 0x00, 0x89, 0x57, 0x04, 0x44,
        0x89, 0x0F, 0x44, 0x89, 0x8F, 0x60, 0x08, 0x00,
        0x00};
    const auto check = [](std::uintptr_t rva, const auto& expected) noexcept {
        return Context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()));
    };
    return check(GetLocalDataContextRva, localContextExpected)
        && check(GetLocalPlayerRva, localPlayerExpected)
        && check(GetLevelRva, getLevelExpected)
        && check(PathGetRoomRva, pathGetRoomExpected)
        && check(InitLevelRva, initLevelExpected)
        && check(CreateActiveRoomRva, createRoomExpected)
        && check(PrepareRoomWitnessRva, prepareRoomExpected)
        && check(BuildRoomPipelineWitnessRva, buildRoomPipelineExpected)
        && check(BuildNearRoomLinksRva, buildNearRoomLinksExpected)
        && check(AddPresetUnitsRva, addPresetUnitsExpected)
        && check(InitializeStaticRoomRva, initializeStaticRoomExpected)
        && check(AddStaticRoomTilesRva, addStaticRoomTilesExpected)
        && check(LoadRoomTileLibrariesRva, loadRoomTileLibrariesExpected)
        && check(ReleaseStaticRoomRva, releaseStaticRoomExpected)
        && check(
            ReleaseStaticRoomWitnessRva,
            releaseStaticRoomWitnessExpected)
        && check(StandardAutomapCallbackRva, callbackExpected)
        && check(GetLevelDefRecordRva, getLevelDefExpected)
        && check(GetOrCreateAutomapLayerWitnessRva, getLayerExpected)
        && check(
            AutomapLayerTreeLayoutWitnessRva,
            layerTreeLayoutExpected)
        && check(CurrentAutomapLayerOwnerWitnessRva, currentOwnerExpected)
        && check(FindAutomapCellRva, findCellExpected)
        && check(InsertAutomapCellRva, insertCellExpected)
        && check(
            AutomapSerializerLimitWitnessRva,
            serializerLimitExpected)
        && check(
            StandardAutomapLayerWitnessRva,
            layerWitnessExpected)
        && check(
            ClientDrlgDifficultyWitnessRva,
            clientDifficultyExpected)
        && check(
            DrlgSeedInitializationWitnessRva,
            seedInitializationExpected);
}

} // namespace

auto InitializeRevealEngine(
        const D2RL::PluginContext* context,
        bool diagnostics) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->exeBase == 0) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    (void)diagnostics;
    RevealAllArmed.store(false, std::memory_order_release);
    ResetRevealSession();
    if (!ResolveCoreBridge()) {
        Context->LogError(
            "MapSense: D2RCore public reveal command bridge is unavailable.");
        Context = nullptr;
        Base = nullptr;
        return false;
    }
    if (!ValidateRuntime()) {
        Context->LogError(
            "MapSense: D2R automap native fingerprint or ABI mismatch; plugin refused.");
        Context = nullptr;
        Base = nullptr;
        return false;
    }

    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    InitLevel = At<InitLevelFn>(InitLevelRva);
    GetLevel = At<GetLevelFn>(GetLevelRva);
    CreateActiveRoom = At<CreateActiveRoomFn>(CreateActiveRoomRva);
    BuildNearRoomLinks = At<PrepareStaticRoomFn>(BuildNearRoomLinksRva);
    AddPresetUnits = At<PrepareStaticRoomFn>(AddPresetUnitsRva);
    InitializeStaticRoom = At<PrepareStaticRoomFn>(InitializeStaticRoomRva);
    AddStaticRoomTiles = At<PrepareStaticRoomFn>(AddStaticRoomTilesRva);
    LoadRoomTileLibraries = At<PrepareStaticRoomFn>(
        LoadRoomTileLibrariesRva);
    ReleaseStaticRoom = At<ReleaseStaticRoomFn>(ReleaseStaticRoomRva);
    PathGetRoom = At<PathGetRoomFn>(PathGetRoomRva);
    GetLevelDefRecord = At<GetLevelDefRecordFn>(GetLevelDefRecordRva);
    FindAutomapCell = At<FindAutomapCellFn>(FindAutomapCellRva);
    InsertAutomapCell = At<InsertAutomapCellFn>(InsertAutomapCellRva);
    ResetNativeAutomapAtlasPublication(0U);

    constexpr std::array<std::uint8_t, 14> hookExpected{
        0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x41,
        0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};
    if (!Context->InstallInlineHook(
            InitLevelRva,
            hookExpected.data(),
            static_cast<std::uint32_t>(hookExpected.size()),
            HookInitLevel,
            &OriginalInitLevel)) {
        Context->LogError(
            "MapSense: native level-initialization hook was refused.");
        ShutdownRevealEngine();
        return false;
    }

    Active.store(true, std::memory_order_release);
    return true;
}

void SetRevealLevelInitializedCallback(
        RevealLevelInitializedCallback callback,
        void* userData) noexcept {
    if (callback == nullptr) {
        LevelInitializedCallback.store(nullptr, std::memory_order_release);
        LevelInitializedUserData.store(nullptr, std::memory_order_release);
        return;
    }
    LevelInitializedUserData.store(userData, std::memory_order_release);
    LevelInitializedCallback.store(callback, std::memory_order_release);
}

void ShutdownRevealEngine() noexcept {
    Active.store(false, std::memory_order_release);
    SetRevealLevelInitializedCallback(nullptr, nullptr);
    RevealAllArmed.store(false, std::memory_order_release);
    ResetNativeAutomapAtlasPublication(0U);
    ResetRevealSession();
    // D2RLoader owns the inline hook and restores it after the plugin unload
    // callback. Keep the trampoline and native addresses valid until the DLL
    // is actually unmapped so an already-running hook can safely finish.
}

void BeginRevealSession() noexcept {
    LevelsRevealed.store(0, std::memory_order_relaxed);
    RoomsRevealed.store(0, std::memory_order_relaxed);
    RevealFailures.store(0, std::memory_order_relaxed);
    TraversalLimits.store(0, std::memory_order_relaxed);
    StaticRoomCandidates.store(0U, std::memory_order_relaxed);
    StaticRoomsMaterialized.store(0U, std::memory_order_relaxed);
    StaticRoomsReleased.store(0U, std::memory_order_relaxed);
    StaticRoomFailures.store(0U, std::memory_order_relaxed);
    NativeAtlasesAccepted.store(0U, std::memory_order_relaxed);
    NativeAtlasesCompleted.store(0U, std::memory_order_relaxed);
    NativeLayerCatalogsPublished.store(0U, std::memory_order_relaxed);
    NativeCellsAttempted.store(0U, std::memory_order_relaxed);
    NativeCellsInserted.store(0U, std::memory_order_relaxed);
    NativeDuplicateCells.store(0U, std::memory_order_relaxed);
    NativeAtlasFailures.store(0U, std::memory_order_relaxed);
    NativePendingCells.store(0U, std::memory_order_relaxed);
    NativeOwnerPending.store(0U, std::memory_order_relaxed);
    NativeOwnerMismatches.store(0U, std::memory_order_relaxed);
    NativeLayerTransitions.store(0U, std::memory_order_relaxed);
    NativeMaximumFloorTreeCells.store(0U, std::memory_order_relaxed);
    NativeMaximumWallTreeCells.store(0U, std::memory_order_relaxed);
}

void ResetRevealSession() noexcept {
    BeginRevealSession();
    ClientDrlgLockGuard lock;
    ActiveClientDrlg = nullptr;
    ActiveClientDataContext = 0;
}

auto RevealCurrentZone() noexcept -> RevealOutcome {
    if (!Active.load(std::memory_order_acquire)) {
        return RevealOutcome::Unavailable;
    }
    ClientLevelView current{};
    if (ResolveClientLevel(current)
        && RevealLevelUnchecked(current.dataContext, current.level)) {
        return RevealOutcome::Complete;
    }
    RevealFailures.fetch_add(1, std::memory_order_relaxed);
    return RevealOutcome::Unavailable;
}

auto ResolveCurrentClientLevelView(
        ClientLevelView& output) noexcept -> bool {
    output = {};
    if (!Active.load(std::memory_order_acquire)) return false;
    return ResolveClientLevel(output);
}

auto ResolveClientLevelById(
        const ClientLevelView& current,
        std::int32_t levelId,
        void*& outputLevel) noexcept -> bool {
    outputLevel = nullptr;
    if (!Active.load(std::memory_order_acquire)) return false;
    return ResolveClientLevelByIdUnchecked(current, levelId, outputLevel);
}

auto BeginNativeAutomapAtlasPublication(
        std::uint64_t sessionGeneration,
        std::shared_ptr<const ExternalAtlasGeometrySnapshot> snapshot,
        const ClientLevelView& current,
        std::int32_t resolvedAct) noexcept
        -> NativeAutomapAtlasPublicationStatus {
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration == 0U || snapshot == nullptr
        || snapshot->geometry == nullptr
        || snapshot->sessionGeneration != sessionGeneration
        || snapshot->seed == 0U || snapshot->seed != current.mapSeed
        || snapshot->difficulty != current.difficulty
        || snapshot->act >= RevealPersistenceActCount
        || resolvedAct < 0
        || resolvedAct != snapshot->act
        || snapshot->geometry->seed != snapshot->seed
        || snapshot->geometry->difficulty != snapshot->difficulty
        || snapshot->geometry->act != snapshot->act
        || snapshot->visibleLevelIds.empty()
        || snapshot->geometry->levels.empty()
        || snapshot->geometry->cells.empty()) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }

    std::int32_t currentLayer{};
    if (!ResolveNativeAutomapLayer(
            current.dataContext, current.levelId, currentLayer)) {
        NativeAtlasFailures.fetch_add(1U, std::memory_order_relaxed);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }

    std::scoped_lock lock(NativeAtlasMutex);
    if (SameNativeAtlasIdentityLocked(sessionGeneration, *snapshot)) {
        const auto ready = std::lower_bound(
            NativeAtlasPublication.readyLayers.begin(),
            NativeAtlasPublication.readyLayers.end(),
            currentLayer);
        const bool layerReady = ready
                != NativeAtlasPublication.readyLayers.end()
            && *ready == currentLayer;
        if (NativeAutomapLayerCompletionIsReusable(
                layerReady,
                NativeAtlasPublication.activeLayer,
                currentLayer)) {
            return NativeAutomapAtlasPublicationStatus::Complete;
        }
        if (layerReady) {
            NativeAtlasPublication.readyLayers.erase(ready);
            if (!PublishNativeLayerCatalogLocked()) {
                FailNativeLayerPublicationLocked(currentLayer);
                return NativeAutomapAtlasPublicationStatus::Failed;
            }
        }
        if (NativeAtlasPublication.failed
            && NativeAtlasPublication.failedLayer == currentLayer) {
            return NativeAutomapAtlasPublicationStatus::Failed;
        }
        if (NativeAtlasPublication.awaitingWitness
            && NativeAtlasPublication.activeLayer == currentLayer) {
            return NativeAutomapAtlasPublicationStatus::AwaitingWitness;
        }
        if (NativeAtlasPublication.active
            && NativeAtlasPublication.activeLayer == currentLayer) {
            return NativeAutomapAtlasPublicationStatus::InProgress;
        }
        if (NativeAtlasPublication.waitingForOwner
            && NativeAtlasPublication.activeLayer == currentLayer) {
            return NativeAutomapAtlasPublicationStatus::WaitingForOwner;
        }
        if (!NativeGeometryContainsLayerLocked(currentLayer)) {
            return CompleteNativeLabelOnlyLayerLocked(currentLayer)
                ? NativeAutomapAtlasPublicationStatus::Complete
                : NativeAutomapAtlasPublicationStatus::Failed;
        }
        if (!StartNativeLayerPublicationLocked(currentLayer)) {
            FailNativeLayerPublicationLocked(currentLayer);
            return NativeAutomapAtlasPublicationStatus::Failed;
        }
        return NativeAutomapAtlasPublicationStatus::Accepted;
    }

    NativeAtlasPublicationState candidate{
        .sessionGeneration = sessionGeneration,
        .seed = snapshot->seed,
        .difficulty = snapshot->difficulty,
        .act = snapshot->act,
        .geometryDigest = snapshot->geometry->digest,
        .snapshot = std::move(snapshot),
    };

    try {
        candidate.catalogLevels.reserve(
            candidate.snapshot->visibleLevelIds.size());
        for (const auto levelId : candidate.snapshot->visibleLevelIds) {
            std::int32_t nativeLayer{};
            if (levelId <= 0 || !ResolveNativeAutomapLayer(
                    current.dataContext, levelId, nativeLayer)) {
                const bool required = levelId == current.levelId
                    || std::find_if(
                        candidate.snapshot->geometry->levels.begin(),
                        candidate.snapshot->geometry->levels.end(),
                        [levelId](
                                const ExternalAtlasGeometryLevel& level)
                                noexcept {
                            return level.levelId == levelId;
                        }) != candidate.snapshot->geometry->levels.end();
                if (!required) continue;
                NativeAtlasFailures.fetch_add(
                    1U, std::memory_order_relaxed);
                return NativeAutomapAtlasPublicationStatus::Failed;
            }
            candidate.catalogLevels.push_back({
                .levelId = levelId,
                .layer = nativeLayer,
            });
        }
        std::sort(
            candidate.catalogLevels.begin(),
            candidate.catalogLevels.end(),
            [](const NativeAutomapLevelLayer& left,
                    const NativeAutomapLevelLayer& right) noexcept {
                return left.levelId < right.levelId;
            });
        if (std::adjacent_find(
                candidate.catalogLevels.begin(),
                candidate.catalogLevels.end(),
                [](const NativeAutomapLevelLayer& left,
                        const NativeAutomapLevelLayer& right) noexcept {
                    return left.levelId == right.levelId;
                }) != candidate.catalogLevels.end()) {
            NativeAtlasFailures.fetch_add(1U, std::memory_order_relaxed);
            return NativeAutomapAtlasPublicationStatus::Failed;
        }
        candidate.levels.reserve(candidate.snapshot->geometry->levels.size());
        candidate.readyLayers.reserve(candidate.snapshot->geometry->levels.size());
        for (std::size_t index = 0U;
                index < candidate.snapshot->geometry->levels.size();
                ++index) {
            const auto& level = candidate.snapshot->geometry->levels[index];
            std::int32_t nativeLayer{};
            if (level.levelId <= 0
                || level.firstCell > candidate.snapshot->geometry->cells.size()
                || level.cellCount == 0U
                || level.cellCount > candidate.snapshot->geometry->cells.size()
                    - level.firstCell
                || !ResolveNativeAutomapLayer(
                    current.dataContext,
                    level.levelId,
                    nativeLayer)) {
                NativeAtlasFailures.fetch_add(
                    1U, std::memory_order_relaxed);
                return NativeAutomapAtlasPublicationStatus::Failed;
            }
            candidate.levels.push_back({
                .geometryLevelIndex = index,
                .levelId = level.levelId,
                .nativeLayer = nativeLayer,
            });
        }
        std::sort(
            candidate.levels.begin(),
            candidate.levels.end(),
            [](const NativeAtlasLevelWork& left,
                    const NativeAtlasLevelWork& right) noexcept {
                if (left.nativeLayer != right.nativeLayer) {
                    return left.nativeLayer < right.nativeLayer;
                }
                return left.levelId < right.levelId;
            });
    } catch (...) {
        NativeAtlasFailures.fetch_add(1U, std::memory_order_relaxed);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }
    NativeAtlasPublication = std::move(candidate);
    if (!PublishNativeLayerCatalogLocked()) {
        NativeAtlasPublication = {};
        NativeAtlasFailures.fetch_add(1U, std::memory_order_relaxed);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }
    if (!NativeGeometryContainsLayerLocked(currentLayer)) {
        return CompleteNativeLabelOnlyLayerLocked(currentLayer)
            ? NativeAutomapAtlasPublicationStatus::Complete
            : NativeAutomapAtlasPublicationStatus::Failed;
    }
    if (!StartNativeLayerPublicationLocked(currentLayer)) {
        FailNativeLayerPublicationLocked(currentLayer);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }
    return NativeAutomapAtlasPublicationStatus::Accepted;
}

auto PumpNativeAutomapAtlasPublication(
        const ClientLevelView& current,
        std::uint32_t maximumCells) noexcept
        -> NativeAutomapAtlasPublicationStatus {
    if (!Active.load(std::memory_order_acquire) || maximumCells == 0U) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    maximumCells = std::min(
        maximumCells, MaximumNativeAtlasCellsPerPump);
    std::scoped_lock lock(NativeAtlasMutex);
    return InsertNativeAtlasCellsLocked(current, maximumCells);
}

auto QueryNativeAutomapAtlasPublication(
        std::uint64_t sessionGeneration,
        const ClientLevelView& current,
        std::int32_t resolvedAct) noexcept
        -> NativeAutomapAtlasPublicationStatus {
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration == 0U || current.levelId <= 0) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    std::int32_t currentLayer{};
    if (!ResolveNativeAutomapLayer(
            current.dataContext, current.levelId, currentLayer)) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    std::scoped_lock lock(NativeAtlasMutex);
    if (NativeAtlasPublication.sessionGeneration != sessionGeneration
        || NativeAtlasPublication.seed != current.mapSeed
        || NativeAtlasPublication.difficulty != current.difficulty
        || resolvedAct < 0
        || NativeAtlasPublication.act != resolvedAct
        || NativeAtlasPublication.snapshot == nullptr) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    const bool layerReady = std::binary_search(
        NativeAtlasPublication.readyLayers.begin(),
        NativeAtlasPublication.readyLayers.end(),
        currentLayer);
    if (NativeAutomapLayerCompletionIsReusable(
            layerReady,
            NativeAtlasPublication.activeLayer,
            currentLayer)) {
        return NativeAutomapAtlasPublicationStatus::Complete;
    }
    if (layerReady) return NativeAutomapAtlasPublicationStatus::Stale;
    if (NativeAtlasPublication.failed
        && NativeAtlasPublication.failedLayer == currentLayer) {
        return NativeAutomapAtlasPublicationStatus::Failed;
    }
    if (NativeAtlasPublication.awaitingWitness
        && NativeAtlasPublication.activeLayer == currentLayer) {
        return NativeAutomapAtlasPublicationStatus::AwaitingWitness;
    }
    if (NativeAtlasPublication.active
        && NativeAtlasPublication.activeLayer == currentLayer) {
        return NativeAutomapAtlasPublicationStatus::InProgress;
    }
    if (NativeAtlasPublication.waitingForOwner
        && NativeAtlasPublication.activeLayer == currentLayer) {
        return NativeAutomapAtlasPublicationStatus::WaitingForOwner;
    }
    return NativeAutomapAtlasPublicationStatus::Stale;
}

auto TryWakeNativeAutomapAtlasPublication(
        std::uint64_t sessionGeneration,
        std::int32_t observedLevelId) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration == 0U || observedLevelId <= 0) {
        return false;
    }
    std::scoped_lock lock(NativeAtlasMutex);
    if (NativeAtlasPublication.sessionGeneration != sessionGeneration
        || !NativeAtlasPublication.waitingForOwner
        || NativeAtlasPublication.active
        || NativeAtlasPublication.failed
        || NativeAtlasPublication.activeLayer < 0) {
        return false;
    }
    const auto observed = std::find_if(
        NativeAtlasPublication.levels.begin(),
        NativeAtlasPublication.levels.end(),
        [observedLevelId](const NativeAtlasLevelWork& work) noexcept {
            return work.levelId == observedLevelId;
        });
    if (observed == NativeAtlasPublication.levels.end()
        || observed->nativeLayer != NativeAtlasPublication.activeLayer) {
        return false;
    }
    NativeAtlasPublication.waitingForOwner = false;
    NativeAtlasPublication.active = true;
    return true;
}

auto ObserveNativeAutomapAtlasPublication(
        std::uint64_t sessionGeneration,
        std::int32_t observedLevelId) noexcept
        -> NativeAutomapAtlasPublicationStatus {
    if (!Active.load(std::memory_order_acquire)
        || sessionGeneration == 0U || observedLevelId <= 0) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    std::scoped_lock lock(NativeAtlasMutex);
    if (NativeAtlasPublication.sessionGeneration != sessionGeneration
        || NativeAtlasPublication.snapshot == nullptr
        || !NativeAtlasPublication.awaitingWitness
        || NativeAtlasPublication.activeLayer < 0) {
        return NativeAutomapAtlasPublicationStatus::Unavailable;
    }
    const auto observed = std::find_if(
        NativeAtlasPublication.catalogLevels.begin(),
        NativeAtlasPublication.catalogLevels.end(),
        [observedLevelId](const NativeAutomapLevelLayer& level) noexcept {
            return level.levelId == observedLevelId;
        });
    if (observed == NativeAtlasPublication.catalogLevels.end()
        || observed->layer != NativeAtlasPublication.activeLayer) {
        return NativeAutomapAtlasPublicationStatus::WaitingForOwner;
    }

    void* owner{};
    std::int32_t observedLayer{};
    const auto ownerState = TryAcquireCurrentNativeAutomapLayer(
        NativeAtlasPublication.activeLayer,
        owner,
        observedLayer);
    if (NativeAutomapOwnerRequiresPulse(ownerState) || owner == nullptr) {
        (ownerState == NativeAutomapActiveOwnerState::Pending
                ? NativeOwnerPending
                : NativeOwnerMismatches)
            .fetch_add(1U, std::memory_order_relaxed);
        return NativeAutomapAtlasPublicationStatus::WaitingForOwner;
    }

    ++NativeAtlasPublication.witnessPasses;
    NativeWitnessPasses.fetch_add(1U, std::memory_order_relaxed);
    auto* const ownerBytes = static_cast<std::uint8_t*>(owner);
    const bool floorReady = VerifyNativeAtlasWitnesses(
        ownerBytes + AutomapLayerFloorTreeOffset,
        NativeAtlasPublication.floorWitnesses.data(),
        NativeAtlasPublication.floorWitnessCount);
    const bool wallReady = VerifyNativeAtlasWitnesses(
        ownerBytes + AutomapLayerWallTreeOffset,
        NativeAtlasPublication.wallWitnesses.data(),
        NativeAtlasPublication.wallWitnessCount);
    if (!floorReady || !wallReady) {
        if (NativeAtlasPublication.witnessPasses
            < MaximumNativeAtlasWitnessPasses) {
            return NativeAutomapAtlasPublicationStatus::AwaitingWitness;
        }
        NativeWitnessFailures.fetch_add(1U, std::memory_order_relaxed);
        FailNativeLayerPublicationLocked(
            NativeAtlasPublication.activeLayer);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }

    const auto completedLayer = NativeAtlasPublication.activeLayer;
    try {
        NativeAtlasPublication.readyLayers.push_back(completedLayer);
        std::sort(
            NativeAtlasPublication.readyLayers.begin(),
            NativeAtlasPublication.readyLayers.end());
        NativeAtlasPublication.readyLayers.erase(
            std::unique(
                NativeAtlasPublication.readyLayers.begin(),
                NativeAtlasPublication.readyLayers.end()),
            NativeAtlasPublication.readyLayers.end());
    } catch (...) {
        FailNativeLayerPublicationLocked(completedLayer);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }
    NativeAtlasPublication.active = false;
    NativeAtlasPublication.waitingForOwner = false;
    NativeAtlasPublication.awaitingWitness = false;
    NativeAtlasPublication.failed = false;
    NativeAtlasPublication.failedLayer = -1;
    NativePendingCells.store(0U, std::memory_order_relaxed);
    if (!PublishNativeLayerCatalogLocked()) {
        const auto ready = std::lower_bound(
            NativeAtlasPublication.readyLayers.begin(),
            NativeAtlasPublication.readyLayers.end(),
            completedLayer);
        if (ready != NativeAtlasPublication.readyLayers.end()
            && *ready == completedLayer) {
            NativeAtlasPublication.readyLayers.erase(ready);
        }
        FailNativeLayerPublicationLocked(completedLayer);
        return NativeAutomapAtlasPublicationStatus::Failed;
    }
    NativeAtlasesCompleted.fetch_add(1U, std::memory_order_relaxed);
    return NativeAutomapAtlasPublicationStatus::Complete;
}

void ResetNativeAutomapAtlasPublication(
        std::uint64_t sessionGeneration) noexcept {
    std::scoped_lock lock(NativeAtlasMutex);
    NativeAtlasPublication = {
        .sessionGeneration = sessionGeneration,
    };
    PublishedNativeLayerCatalog.store(
        std::shared_ptr<const NativeAutomapLayerCatalog>{},
        std::memory_order_release);
    NativePendingCells.store(0U, std::memory_order_relaxed);
}

auto AcquireNativeAutomapLayerCatalog() noexcept
        -> std::shared_ptr<const NativeAutomapLayerCatalog> {
    return PublishedNativeLayerCatalog.load(std::memory_order_acquire);
}

auto GetNativeAutomapAtlasCounters() noexcept
        -> NativeAutomapAtlasCounters {
    return {
        .atlasesAccepted = NativeAtlasesAccepted.load(
            std::memory_order_relaxed),
        .atlasesCompleted = NativeAtlasesCompleted.load(
            std::memory_order_relaxed),
        .layerCatalogsPublished = NativeLayerCatalogsPublished.load(
            std::memory_order_relaxed),
        .cellsAttempted = NativeCellsAttempted.load(
            std::memory_order_relaxed),
        .cellsInserted = NativeCellsInserted.load(
            std::memory_order_relaxed),
        .duplicateCells = NativeDuplicateCells.load(
            std::memory_order_relaxed),
        .failures = NativeAtlasFailures.load(std::memory_order_relaxed),
        .pendingCells = NativePendingCells.load(std::memory_order_relaxed),
        .ownerPending = NativeOwnerPending.load(std::memory_order_relaxed),
        .ownerMismatches = NativeOwnerMismatches.load(
            std::memory_order_relaxed),
        .witnessPasses = NativeWitnessPasses.load(
            std::memory_order_relaxed),
        .witnessFailures = NativeWitnessFailures.load(
            std::memory_order_relaxed),
        .layerTransitions = NativeLayerTransitions.load(
            std::memory_order_relaxed),
        .maximumFloorTreeCells = NativeMaximumFloorTreeCells.load(
            std::memory_order_relaxed),
        .maximumWallTreeCells = NativeMaximumWallTreeCells.load(
            std::memory_order_relaxed),
    };
}

auto MaterializeClientRoom(
        std::uint8_t dataContext,
        void* drlgRoom) noexcept -> void* {
    if (!Active.load(std::memory_order_acquire)
        || CreateActiveRoom == nullptr || drlgRoom == nullptr) {
        return nullptr;
    }
    __try {
        return CreateActiveRoom(dataContext, drlgRoom);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

auto PrepareStaticClientRoom(
        std::uint8_t dataContext,
        void* drlgRoom,
        StaticClientRoomLease& lease) noexcept -> bool {
    lease = {};
    if (!Active.load(std::memory_order_acquire)
        || dataContext >= 8U || drlgRoom == nullptr
        || BuildNearRoomLinks == nullptr || AddPresetUnits == nullptr
        || InitializeStaticRoom == nullptr || AddStaticRoomTiles == nullptr
        || LoadRoomTileLibraries == nullptr || ReleaseStaticRoom == nullptr) {
        StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    StaticRoomCandidates.fetch_add(1U, std::memory_order_relaxed);
    __try {
        auto* const room = static_cast<std::uint8_t*>(drlgRoom);
        auto flags = *reinterpret_cast<std::uint32_t*>(
            room + RoomFlagsOffset);
        if ((flags & (StaticPoiRoomWarpMask | StaticPoiRoomWaypointMask))
                == 0U) {
            StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        if (*reinterpret_cast<void**>(room + RoomActiveRoomOffset) != nullptr
            || (flags & StaticPoiRoomHasRoomMask) != 0U) {
            return true;
        }

        if ((flags & 0x01000000U) == 0U) {
            LoadRoomTileLibraries(dataContext, room);
        }
        flags = *reinterpret_cast<std::uint32_t*>(room + RoomFlagsOffset);
        if ((flags & 0x02000000U) == 0U
            && *reinterpret_cast<std::int32_t*>(room + RoomTypeOffset) == 2) {
            AddPresetUnits(dataContext, room);
        }
        flags = *reinterpret_cast<std::uint32_t*>(room + RoomFlagsOffset);
        if ((flags & StaticPoiRoomWarpMask) == 0U) {
            if ((flags & StaticPoiRoomWaypointMask) == 0U
                || (flags & StaticPoiRoomPresetUnitsAddedMask) == 0U
                || *reinterpret_cast<void**>(
                    room + RoomPresetUnitOffset) == nullptr) {
                StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }
            return true;
        }
        if (*reinterpret_cast<void**>(room + RoomActiveRoomOffset) != nullptr
            || (flags & StaticPoiRoomHasRoomMask) != 0U) {
            StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }

        if (*reinterpret_cast<std::uint64_t*>(
                room + RoomNearCountOffset) == 0U) {
            BuildNearRoomLinks(dataContext, room);
        }
        InitializeStaticRoom(dataContext, room);
        AddStaticRoomTiles(dataContext, room);
        flags = *reinterpret_cast<std::uint32_t*>(room + RoomFlagsOffset);
        if (*reinterpret_cast<void**>(room + RoomActiveRoomOffset) != nullptr
            || (flags & StaticPoiRoomHasRoomMask) == 0U) {
            if (*reinterpret_cast<void**>(room + RoomActiveRoomOffset)
                    == nullptr
                && (flags & StaticPoiRoomHasRoomMask) != 0U) {
                ReleaseStaticRoom(room, 0);
            }
            StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        lease = {.room = room, .owned = true};
        StaticRoomsMaterialized.fetch_add(1U, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
        lease = {};
        return false;
    }
}

auto ReleaseStaticClientRoom(
        StaticClientRoomLease& lease) noexcept -> bool {
    if (!lease.owned || lease.room == nullptr || ReleaseStaticRoom == nullptr) {
        lease = {};
        return false;
    }
    auto* const room = static_cast<std::uint8_t*>(lease.room);
    lease = {};
    __try {
        const auto flags = *reinterpret_cast<std::uint32_t*>(
            room + RoomFlagsOffset);
        if (*reinterpret_cast<void**>(room + RoomActiveRoomOffset) != nullptr
            || (flags & StaticPoiRoomHasRoomMask) == 0U) {
            StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        ReleaseStaticRoom(room, 0);
        if (*reinterpret_cast<void**>(room + RoomActiveRoomOffset) != nullptr
            || (*reinterpret_cast<std::uint32_t*>(room + RoomFlagsOffset)
                & StaticPoiRoomHasRoomMask) != 0U) {
            StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        StaticRoomsReleased.fetch_add(1U, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        StaticRoomFailures.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
}

auto RevealCurrentAct() noexcept -> RevealOutcome {
    if (!Active.load(std::memory_order_acquire)) {
        return RevealOutcome::Unavailable;
    }
    if (SubmitNativeActReveal()) return RevealOutcome::Complete;
    RevealFailures.fetch_add(1U, std::memory_order_relaxed);
    return RevealOutcome::Unavailable;
}

auto ArmRevealAll() noexcept -> RevealOutcome {
    if (!Active.load(std::memory_order_acquire)) {
        return RevealOutcome::Unavailable;
    }
    if (RevealAllArmed.load(std::memory_order_acquire)) {
        return RevealOutcome::Armed;
    }
    RevealAllArmed.store(true, std::memory_order_release);
    return RevealOutcome::Armed;
}

auto ToggleRevealAll() noexcept -> RevealOutcome {
    if (!Active.load(std::memory_order_acquire)) {
        return RevealOutcome::Unavailable;
    }
    if (RevealAllArmed.exchange(true, std::memory_order_acq_rel)) {
        RevealAllArmed.store(false, std::memory_order_release);
        return RevealOutcome::Disarmed;
    }
    return RevealOutcome::Armed;
}

auto DisableRevealAll() noexcept -> RevealOutcome {
    RevealAllArmed.store(false, std::memory_order_release);
    return RevealOutcome::Disarmed;
}

auto IsRevealEngineActive() noexcept -> bool {
    return Active.load(std::memory_order_acquire);
}

auto IsRevealAllArmed() noexcept -> bool {
    return RevealAllArmed.load(std::memory_order_acquire);
}

auto GetRevealCounters() noexcept -> RevealCounters {
    return {
        .levels = LevelsRevealed.load(std::memory_order_relaxed),
        .rooms = RoomsRevealed.load(std::memory_order_relaxed),
        .failures = RevealFailures.load(std::memory_order_relaxed),
        .traversalLimits = TraversalLimits.load(std::memory_order_relaxed),
        .staticRoomCandidates = StaticRoomCandidates.load(
            std::memory_order_relaxed),
        .staticRoomsMaterialized = StaticRoomsMaterialized.load(
            std::memory_order_relaxed),
        .staticRoomsReleased = StaticRoomsReleased.load(
            std::memory_order_relaxed),
        .staticRoomFailures = StaticRoomFailures.load(
            std::memory_order_relaxed),
    };
}

} // namespace RuffnecKk::MapSense
