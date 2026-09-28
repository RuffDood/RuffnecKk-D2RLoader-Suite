#include <D2RLPlugin/api.h>

#include "policy.hpp"
#include "auto_pickup_route_api.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace RuffnecKk::AutoPickup {
namespace {

constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uintptr_t GetGameRva = 0x34B440;
constexpr std::uintptr_t EnumerateRva = 0x2EFDE0;
constexpr std::uintptr_t FirstUnitRva = 0x2EFD90;
constexpr std::uintptr_t NextUnitRva = 0x34B4A0;
constexpr std::uintptr_t UnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t UnitIdRva = 0x34A330;
constexpr std::uintptr_t UnitModeRva = 0x34AB60;
constexpr std::uintptr_t UnitDistanceRva = 0x325140;
constexpr std::uintptr_t UnitCollisionRva = 0x350550;
constexpr std::uintptr_t PickupRva = 0x471950;
constexpr std::uintptr_t GetItemCodeRva = 0x36EF50;
constexpr std::uintptr_t GetInventoryRva = 0x34A360;
constexpr std::uintptr_t ResolveOccupancyGridRva = 0x38B070;
constexpr std::uintptr_t GetBeltTypeRva = 0x349720;
constexpr std::uintptr_t GetFreeBeltSlotRva = 0x3862D0;
constexpr std::uintptr_t BodyGridInfoRva = 0x237B620;
constexpr std::uintptr_t BeltGridInfoRva = 0x237B638;
constexpr std::uintptr_t ServerPacketTableRva = 0x1D2A790;
constexpr std::uint32_t ItemType = 4;
constexpr std::uint32_t GroundMode = 3;
constexpr std::uint32_t PickupCollisionMask = 0x804;
constexpr std::uint8_t FirstTriggerOpcode = 0x01;
constexpr std::uint8_t LastTriggerOpcode = 0x12;
constexpr std::uint8_t BeltBodySlot = 8;

constexpr std::array<std::uintptr_t, LastTriggerOpcode + 1> TriggerHandlerRvas{
    0, 0x4AC050, 0x4ACE20, 0x4ACE40, 0x4ACE60, 0x4ACE80, 0x4ACF80,
    0x4AD030, 0x4AD0E0, 0x4AD100, 0x4AD120, 0x4AD140, 0x4AD230,
    0x4AD330, 0x4AD3E0, 0x4AD490, 0x4AD4B0, 0x4AD4D0, 0x4AD4F0,
};
constexpr std::array<std::uint8_t, 32> GetFreeBeltSlotExpected{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
    0x81, 0xEC, 0x70, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xDF, 0x4F, 0x64,
    0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24,
};
constexpr std::array<std::uint8_t, 32> ResolveOccupancyGridExpected{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30,
    0x49, 0x8B, 0xF8, 0x48, 0x39, 0x51, 0x28, 0x0F, 0x86, 0x01, 0x01, 0x00,
    0x00, 0x49, 0x89, 0x73, 0x18, 0x48, 0x8D, 0x71,
};
constexpr std::array<std::uint8_t, 32> GetInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF1, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetBeltTypeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x15, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0xE0, 0xC0, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetItemCodeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x80, 0x83, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetGameExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
};
constexpr std::array<std::uint8_t, 13> EnumerateExpected{
    0x8B, 0x41, 0x40, 0x41, 0x89, 0x00, 0x48, 0x8B,
    0x01, 0x48, 0x89, 0x02, 0xC3,
};
constexpr std::array<std::uint8_t, 8> FirstUnitExpected{
    0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00, 0xC3,
};
constexpr std::array<std::uint8_t, 32> NextUnitExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0xB4, 0x9E, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
};
constexpr std::array<std::uint8_t, 32> UnitTypeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 32> UnitIdExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 43> UnitModeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0xF9, 0xA3, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83,
    0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x0C, 0x48, 0x83,
    0xC4, 0x28, 0xC3,
};
constexpr std::array<std::uint8_t, 32> UnitDistanceExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xE9, 0x48, 0x8B, 0xDA,
    0x48, 0x8B, 0xCA, 0xE8, 0x20, 0x64, 0x02, 0x00,
};
constexpr std::array<std::uint8_t, 30> UnitCollisionExpected{
    0x44, 0x89, 0x44, 0x24, 0x18, 0x53, 0x56, 0x57,
    0x48, 0x83, 0xEC, 0x60, 0x41, 0x8B, 0xD8, 0x48,
    0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
    0x0F, 0x84, 0xF0, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 28> PickupExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0xE9, 0x48, 0x81, 0xEC,
    0xA0, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 42> OccupancyLayoutExpected{
    0x0F, 0xB6, 0x07, 0x88, 0x43, 0x10, 0x0F, 0xB6,
    0x47, 0x01, 0x88, 0x43, 0x11, 0xE8, 0x96, 0x5A,
    0x69, 0x00, 0x0F, 0xB6, 0x4B, 0x10, 0x0F, 0xB6,
    0x53, 0x11, 0x48, 0x0F, 0xAF, 0xD1, 0x48, 0x8B,
    0xC8, 0xE8, 0x72, 0x7B, 0xFF, 0xFF, 0x48, 0x89,
    0x43, 0x18,
};
constexpr std::array<std::uint8_t, 35> BodyGridLayoutExpected{
    0x4C, 0x8D, 0x05, 0xDC, 0x51, 0xFF, 0x01, 0x33,
    0xD2, 0x48, 0x8B, 0xCF, 0xE8, 0x22, 0x4C, 0x00,
    0x00, 0x48, 0x85, 0xC0, 0x75, 0x05, 0x8D, 0x58,
    0x02, 0xEB, 0x19, 0x48, 0x8B, 0x40, 0x18, 0x48,
    0x8B, 0x48, 0x40,
};
constexpr std::array<std::uint8_t, 45> BeltGridLayoutExpected{
    0x4C, 0x8D, 0x05, 0x8A, 0x51, 0xFF, 0x01, 0xBA,
    0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8,
    0xB5, 0x4B, 0x00, 0x00, 0x4C, 0x8B, 0xF0, 0x48,
    0x85, 0xC0, 0x0F, 0x84, 0x52, 0xFE, 0xFF, 0xFF,
    0x33, 0xF6, 0x8B, 0xDE, 0x8B, 0xFE, 0x0F, 0x1F,
    0x00, 0x49, 0x8B, 0x4E, 0x18,
};

using TriggerFn = std::int64_t(__fastcall*)(void*, void*, void*, std::int32_t);
using GetGameFn = void*(__fastcall*)(void*);
using EnumerateFn = void(__fastcall*)(void*, void***, std::uint32_t*);
using UnitFn = void*(__fastcall*)(void*);
using UnitIntFn = std::uint32_t(__fastcall*)(void*);
using UnitPairFn = std::int32_t(__fastcall*)(void*, void*);
using CollisionFn = std::int32_t(__fastcall*)(void*, void*, std::uint32_t);
using PickupFn = bool(__fastcall*)(void*, std::uint32_t, bool, std::uint32_t, bool, bool);
using GetItemCodeFn = std::uint32_t(__fastcall*)(void*);
using GetInventoryFn = void*(__fastcall*)(void*);
using GetBeltTypeFn = std::int32_t(__fastcall*)(void*);
using ResolveOccupancyGridFn = void*(__fastcall*)(void*, std::uint64_t, const void*);
using GetFreeBeltSlotFn = std::int32_t(__fastcall*)(void*, void*, std::int32_t*, bool) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::array<TriggerFn, LastTriggerOpcode + 1> OriginalTriggers{};
GetFreeBeltSlotFn OriginalGetFreeBeltSlot{};
GetGameFn GetGame{}; EnumerateFn Enumerate{}; UnitFn FirstUnit{}, NextUnit{};
UnitIntFn UnitType{}, UnitId{}, UnitMode{}; UnitPairFn UnitDistance{}; CollisionFn UnitCollision{};
PickupFn Pickup{}; GetItemCodeFn GetItemCode{}; GetInventoryFn GetInventory{}; GetBeltTypeFn GetBeltType{};
ResolveOccupancyGridFn ResolveOccupancyGrid{};

struct RuntimeMetrics {
    std::atomic<std::uint64_t> actions{}, scans{}, potionRoutes{}, inventoryRoutes{}, picked{}, failed{};
    std::atomic<std::uint64_t> beltStateFailures{}, enumerationFailures{}, routeMatches{}, routeMismatches{};
};
RuntimeMetrics Metrics{};
std::atomic<std::uint64_t> DiagnosticMessages{};
std::atomic<bool> LoggedIncompatibleRouteAdvisor{};
thread_local bool Inside{};
thread_local std::uint32_t TriggerCounter{};
thread_local void* ForcedInventory{};
thread_local RoutingToken ForcedRoute{};
thread_local std::int32_t ForcedBeltSlot{-1};
thread_local bool ForceInventoryFallback{};
thread_local bool LoggedScanException{};
thread_local HMODULE RouteAdvisorModule{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize, .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-auto-pickup", .name = "Auto Pickup", .version = "2.0.0",
    .author = "RuffnecKk", .description = "Picks up configured items into belt columns or inventory.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class Function> auto At(std::uintptr_t rva) noexcept -> Function { return reinterpret_cast<Function>(Base + rva); }

enum class RouteAdvisorStatus : std::uint8_t {
    Absent,
    Inactive,
    Ready,
    Incompatible,
};

struct RouteAdvisorResolution {
    RouteAdvisorStatus status{RouteAdvisorStatus::Absent};
    const AutoPickupRoute::ApiV1* api{};
};

auto ResolveRouteAdvisor() noexcept -> RouteAdvisorResolution {
    HMODULE module{};
    if (!GetModuleHandleExW(
            0, L"d2rl-ruffneckk-stack-manager.dll", &module)) {
        return {};
    }
    // Hold the provider module until the enclosing scan has completed every
    // plan/begin/end call, including structured-exception cleanup.
    RouteAdvisorModule = module;
    const auto getApi = reinterpret_cast<AutoPickupRoute::GetApiFn>(
        GetProcAddress(module, AutoPickupRoute::ExportName));
    if (!getApi) return {RouteAdvisorStatus::Incompatible, nullptr};
    const auto* api = getApi(
        AutoPickupRoute::Version, sizeof(AutoPickupRoute::ApiV1));
    if (!AutoPickupRoute::ValidApi(api)) {
        return {RouteAdvisorStatus::Incompatible, nullptr};
    }
    const auto state = api->state();
    if (state == AutoPickupRoute::ProviderState::Ready) {
        return {RouteAdvisorStatus::Ready, api};
    }
    if (state == AutoPickupRoute::ProviderState::Inactive) {
        return {RouteAdvisorStatus::Inactive, api};
    }
    return {RouteAdvisorStatus::Incompatible, nullptr};
}

void ReleaseRouteAdvisor() noexcept {
    const auto module = RouteAdvisorModule;
    RouteAdvisorModule = nullptr;
    if (module) FreeLibrary(module);
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{}; std::uint32_t requiredSize{};
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &requiredSize)) {
        Context->LogError(requiredSize > buffer.size() ? "AutoPickup: configuration exceeds 65535 bytes." : "AutoPickup: configuration could not be read.");
        return false;
    }
    Config parsed{}; std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("AutoPickup: invalid TOML (") + error + "); no hook was installed.";
        Context->LogError(message.c_str()); return false;
    }
    Settings = parsed; return true;
}
void ResetRoutingScope() noexcept { ForceInventoryFallback = false; ForcedBeltSlot = -1; ForcedRoute.Reset(); ForcedInventory = nullptr; Inside = false; }
void ResetMetrics() noexcept {
    for (auto* metric : {&Metrics.actions, &Metrics.scans, &Metrics.potionRoutes, &Metrics.inventoryRoutes, &Metrics.picked, &Metrics.failed, &Metrics.beltStateFailures, &Metrics.enumerationFailures, &Metrics.routeMatches, &Metrics.routeMismatches}) metric->store(0, std::memory_order_relaxed);
    DiagnosticMessages.store(0, std::memory_order_relaxed);
    LoggedIncompatibleRouteAdvisor.store(false, std::memory_order_relaxed);
}
auto ShouldLogDiagnostic() noexcept -> bool {
    if (!Settings.diagnosticsEnabled || !Context) return false;
    const auto ordinal = DiagnosticMessages.fetch_add(1, std::memory_order_relaxed) + 1;
    return ordinal <= 8 || ordinal % 100 == 0;
}
auto FamilySettings(Family family) noexcept -> const FamilyConfig& {
    if (family == Family::Health) return Settings.health;
    if (family == Family::Mana) return Settings.mana;
    return Settings.rejuvenation;
}
auto FamilyRank(Family family) noexcept -> std::uint8_t {
    for (std::uint8_t index = 0; index < Settings.familyPriorityCount; ++index) if (Settings.familyPriority[index] == family) return index;
    return UINT8_MAX;
}
auto TierRank(const FamilyConfig& family, std::uint8_t tier) noexcept -> std::uint8_t {
    for (std::uint8_t index = 0; index < family.priorityCount; ++index) if (family.priority[index] == tier) return index;
    return UINT8_MAX;
}
auto ClassifyPackedCode(std::uint32_t packed) noexcept -> Item;
auto ReadBeltState(void* inventory, std::array<BeltSlot, 16>& slots, std::uint8_t& capacity) noexcept -> bool {
    __try {
        if (!inventory || !GetBeltType || !ResolveOccupancyGrid || !GetItemCode) return false;
        auto* bodyGrid = static_cast<std::uint8_t*>(ResolveOccupancyGrid(inventory, 0, Base + BodyGridInfoRva));
        if (!bodyGrid) return false;
        const auto bodyCells = static_cast<std::uint32_t>(bodyGrid[0x10])
            * static_cast<std::uint32_t>(bodyGrid[0x11]);
        if (bodyCells <= BeltBodySlot || bodyCells > 64) return false;
        auto** bodyItems = *reinterpret_cast<void***>(bodyGrid + 0x18);
        if (!bodyItems) return false;
        switch (const auto beltType = bodyItems[BeltBodySlot] ? GetBeltType(bodyItems[BeltBodySlot]) : 2) {
        case 0: case 5: capacity = 12; break; case 1: case 4: capacity = 8; break;
        case 2: capacity = 4; break; case 3: case 6: capacity = 16; break; default: return false;
        }
        auto* beltGrid = static_cast<std::uint8_t*>(ResolveOccupancyGrid(inventory, 1, Base + BeltGridInfoRva));
        if (!beltGrid) return false;
        const auto cells = static_cast<std::uint32_t>(beltGrid[0x10]) * static_cast<std::uint32_t>(beltGrid[0x11]);
        if (cells < capacity || cells > slots.size()) return false;
        auto** items = *reinterpret_cast<void***>(beltGrid + 0x18);
        if (!items) return false;
        for (std::uint8_t index = 0; index < capacity; ++index) if (items[index]) {
            slots[index].occupied = true;
            slots[index].family = ClassifyPackedCode(GetItemCode(items[index])).family;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
auto ClassifyPackedCode(std::uint32_t packed) noexcept -> Item {
    for (const auto& item : Items) if (PackItemCode(item.code) == packed) return item;
    return {{}, Family::Unknown, 0};
}
auto ReadUnitId(void* unit) noexcept -> std::uint32_t {
    __try { return unit && UnitId ? UnitId(unit) : RoutingToken::InvalidGuid; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return RoutingToken::InvalidGuid; }
}
auto __fastcall HookGetFreeBeltSlot(void* inventory, void* item, std::int32_t* freeSlot, bool allowAnyBeltable) noexcept -> std::int32_t {
    if (Inside) {
        const auto itemGuid = ReadUnitId(item);
        if (ForcedRoute.Matches(itemGuid)) {
            Metrics.routeMatches.fetch_add(1, std::memory_order_relaxed);
            if (ForceInventoryFallback) { if (freeSlot) *freeSlot = -1; return 0; }
            if (freeSlot && ForcedBeltSlot >= 0 && ForcedBeltSlot < 16) { *freeSlot = ForcedBeltSlot; return 1; }
            return 0;
        }
        Metrics.routeMismatches.fetch_add(1, std::memory_order_relaxed);
    }
    return OriginalGetFreeBeltSlot(inventory, item, freeSlot, allowAnyBeltable);
}

struct Candidate {
    void* unit{};
    Item potion{};
    std::uint32_t code{};
    std::uint32_t guid{RoutingToken::InvalidGuid};
    std::int32_t distance{INT_MAX};
    std::int32_t beltSlot{-1};
    bool inventory{};
    bool advised{};
    AutoPickupRoute::PlanV1 plan{};
    std::uint8_t familyRank{UINT8_MAX};
    std::uint8_t tierRank{UINT8_MAX};
    std::uint8_t inventoryRank{UINT8_MAX};
};
auto BetterPotion(const Candidate& candidate, const Candidate& best) noexcept -> bool {
    return !best.unit || candidate.familyRank < best.familyRank || (candidate.familyRank == best.familyRank && candidate.tierRank < best.tierRank) || (candidate.familyRank == best.familyRank && candidate.tierRank == best.tierRank && candidate.distance < best.distance);
}
auto BetterInventory(const Candidate& candidate, const Candidate& best) noexcept -> bool {
    return !best.unit || candidate.inventoryRank < best.inventoryRank || (candidate.inventoryRank == best.inventoryRank && candidate.distance < best.distance);
}
void ScanUnsafe(void* player) {
    if (!Settings.enabled || Inside || !player) return;
    Metrics.actions.fetch_add(1, std::memory_order_relaxed);
    if ((++TriggerCounter % Settings.interval) != 0) return;
    Metrics.scans.fetch_add(1, std::memory_order_relaxed);
    void* game = GetGame(player); void* inventory = GetInventory(player);
    if (!game || !inventory) return;
    std::array<BeltSlot, 16> belt{}; std::uint8_t beltCapacity{};
    if (!ReadBeltState(inventory, belt, beltCapacity)) { Metrics.beltStateFailures.fetch_add(1, std::memory_order_relaxed); return; }
    void** buckets{}; std::uint32_t count{}; Enumerate(game, &buckets, &count);
    if (!buckets || count == 0 || count > 4096) { Metrics.enumerationFailures.fetch_add(1, std::memory_order_relaxed); return; }
    const auto routeAdvisorResolution = ResolveRouteAdvisor();
    const auto* routeAdvisor = routeAdvisorResolution.status
            == RouteAdvisorStatus::Ready
        ? routeAdvisorResolution.api : nullptr;
    const auto refusePotionRoutes = routeAdvisorResolution.status
        == RouteAdvisorStatus::Incompatible;
    if (refusePotionRoutes
        && !LoggedIncompatibleRouteAdvisor.exchange(
            true, std::memory_order_relaxed)
        && Context) {
        Context->LogWarn(
            "AutoPickup: incompatible route-advisor ABI; potion routes are disabled.");
    }
    Candidate bestPotion{}, bestInventory{};
    for (std::uint32_t index = 0; index < count; ++index) for (void* unit = FirstUnit(buckets[index]); unit; unit = NextUnit(unit)) {
        if (UnitType(unit) != ItemType || UnitMode(unit) != GroundMode || UnitCollision(player, unit, PickupCollisionMask) != 0) continue;
        const auto distance = UnitDistance(player, unit);
        if (distance < 0 || static_cast<std::uint32_t>(distance) > Settings.distance) continue;
        const auto code = GetItemCode(unit); const auto potion = ClassifyPackedCode(code);
        if (potion.family != Family::Unknown) {
            if (refusePotionRoutes) continue;
            const auto& family = FamilySettings(potion.family);
            if (!family.policy.Accepts(potion)) continue;
            const auto guid = ReadUnitId(unit);
            if (guid == RoutingToken::InvalidGuid) continue;
            auto beltSlot = std::int32_t{-1};
            auto inventoryFallback = false;
            auto advised = false;
            AutoPickupRoute::PlanV1 plan{};
            if (routeAdvisor) {
                AutoPickupRoute::RequestV1 request{
                    sizeof(AutoPickupRoute::RequestV1),
                    AutoPickupRoute::Version,
                    inventory,
                    unit,
                    guid,
                    family.policy.columns,
                    family.policy.columnCount,
                    static_cast<std::uint8_t>(
                        family.policy.AllowsInventory(potion)),
                    0,
                };
                if (!routeAdvisor->plan(&request, &plan)
                    || !AutoPickupRoute::ValidPlan(&plan)) {
                    continue;
                }
                advised = true;
                beltSlot = plan.destination == AutoPickupRoute::Destination::Belt
                    ? plan.beltSlot : -1;
                inventoryFallback = plan.destination
                    == AutoPickupRoute::Destination::Inventory;
            } else {
                beltSlot = ChooseBeltSlot(
                    family.policy, potion, belt, beltCapacity);
                inventoryFallback = beltSlot < 0
                    && family.policy.AllowsInventory(potion);
            }
            if (beltSlot < 0 && !inventoryFallback) continue;
            Candidate candidate{};
            candidate.unit = unit;
            candidate.potion = potion;
            candidate.code = code;
            candidate.guid = guid;
            candidate.distance = distance;
            candidate.beltSlot = beltSlot;
            candidate.inventory = inventoryFallback;
            candidate.advised = advised;
            candidate.plan = plan;
            candidate.familyRank = FamilyRank(potion.family);
            candidate.tierRank = TierRank(family, potion.tier);
            if (BetterPotion(candidate, bestPotion)) bestPotion = candidate;
            continue;
        }
        const auto rank = Settings.InventoryRank(code);
        if (rank != UINT8_MAX) {
            Candidate candidate{}; candidate.unit = unit; candidate.code = code; candidate.distance = distance; candidate.inventoryRank = rank;
            if (BetterInventory(candidate, bestInventory)) bestInventory = candidate;
        }
    }
    if (bestPotion.unit) {
        const auto guid = ReadUnitId(bestPotion.unit);
        if (guid == RoutingToken::InvalidGuid || guid != bestPotion.guid) return;
        auto advisorBegun = false;
        if (bestPotion.advised) {
            const auto& family = FamilySettings(bestPotion.potion.family);
            AutoPickupRoute::RequestV1 request{
                sizeof(AutoPickupRoute::RequestV1),
                AutoPickupRoute::Version,
                inventory,
                bestPotion.unit,
                guid,
                family.policy.columns,
                family.policy.columnCount,
                static_cast<std::uint8_t>(
                    family.policy.AllowsInventory(bestPotion.potion)),
                0,
            };
            advisorBegun = routeAdvisor
                && routeAdvisor->begin(&request, &bestPotion.plan);
            if (!advisorBegun) return;
        }
        Metrics.potionRoutes.fetch_add(1, std::memory_order_relaxed);
        Inside = true; ForcedInventory = inventory; ForcedRoute.itemGuid = guid; ForcedBeltSlot = bestPotion.beltSlot; ForceInventoryFallback = bestPotion.inventory;
        bool picked{};
        __try {
            picked = Pickup(
                player, guid, true, Settings.distance, true, false);
        } __finally {
            if (advisorBegun) routeAdvisor->end();
        }
        (picked ? Metrics.picked : Metrics.failed).fetch_add(1, std::memory_order_relaxed);
        ResetRoutingScope(); return;
    }
    if (bestInventory.unit) {
        const auto guid = ReadUnitId(bestInventory.unit); if (guid == RoutingToken::InvalidGuid) return;
        Metrics.inventoryRoutes.fetch_add(1, std::memory_order_relaxed);
        const bool picked = Pickup(player, guid, true, Settings.distance, true, false);
        (picked ? Metrics.picked : Metrics.failed).fetch_add(1, std::memory_order_relaxed);
    } else if (Settings.logScans && ShouldLogDiagnostic()) Context->LogInfo("AutoPickup: scan selected no eligible route.");
}
auto ScanProtected(void* player) noexcept -> std::uint32_t {
    std::uint32_t result{};
    __try {
        __try {
            ScanUnsafe(player);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ResetRoutingScope();
            result = GetExceptionCode();
        }
    } __finally {
        ReleaseRouteAdvisor();
    }
    return result;
}
void Scan(void* player) noexcept {
    if (ScanProtected(player) != 0 && !LoggedScanException && Context) { LoggedScanException = true; Context->LogWarn("AutoPickup: one authoritative scan was skipped after a structured exception."); }
}
auto __fastcall HookTrigger(void* game, void* player, void* packet, std::int32_t size) noexcept -> std::int64_t {
    const auto opcode = packet && size > 0 ? *static_cast<const std::uint8_t*>(packet) : static_cast<std::uint8_t>(0);
    if (opcode < FirstTriggerOpcode || opcode > LastTriggerOpcode || !OriginalTriggers[opcode]) return 1;
    const auto result = OriginalTriggers[opcode](game, player, packet, size); Scan(player); return result;
}
auto ValidateRuntime() noexcept -> bool {
    bool valid = true;
    for (std::uint8_t opcode = FirstTriggerOpcode; opcode <= LastTriggerOpcode; ++opcode) {
        const auto expected = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        valid = Context->CheckExpectedBytes(ServerPacketTableRva + static_cast<std::uintptr_t>(opcode) * sizeof(std::uintptr_t), &expected, static_cast<std::uint32_t>(sizeof(expected))) && valid;
    }
    valid = Context->CheckExpectedBytes(GetFreeBeltSlotRva, GetFreeBeltSlotExpected.data(), static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetItemCodeRva, GetItemCodeExpected.data(), static_cast<std::uint32_t>(GetItemCodeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetGameRva, GetGameExpected.data(), static_cast<std::uint32_t>(GetGameExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(EnumerateRva, EnumerateExpected.data(), static_cast<std::uint32_t>(EnumerateExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(FirstUnitRva, FirstUnitExpected.data(), static_cast<std::uint32_t>(FirstUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(NextUnitRva, NextUnitExpected.data(), static_cast<std::uint32_t>(NextUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitTypeRva, UnitTypeExpected.data(), static_cast<std::uint32_t>(UnitTypeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitIdRva, UnitIdExpected.data(), static_cast<std::uint32_t>(UnitIdExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitModeRva, UnitModeExpected.data(), static_cast<std::uint32_t>(UnitModeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitDistanceRva, UnitDistanceExpected.data(), static_cast<std::uint32_t>(UnitDistanceExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitCollisionRva, UnitCollisionExpected.data(), static_cast<std::uint32_t>(UnitCollisionExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(PickupRva, PickupExpected.data(), static_cast<std::uint32_t>(PickupExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(ResolveOccupancyGridRva, ResolveOccupancyGridExpected.data(), static_cast<std::uint32_t>(ResolveOccupancyGridExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(0x38B108, OccupancyLayoutExpected.data(), static_cast<std::uint32_t>(OccupancyLayoutExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(0x38643D, BodyGridLayoutExpected.data(), static_cast<std::uint32_t>(BodyGridLayoutExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(0x3864A7, BeltGridLayoutExpected.data(), static_cast<std::uint32_t>(BeltGridLayoutExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetInventoryRva, GetInventoryExpected.data(), static_cast<std::uint32_t>(GetInventoryExpected.size())) && valid;
    return Context->CheckExpectedBytes(GetBeltTypeRva, GetBeltTypeExpected.data(), static_cast<std::uint32_t>(GetBeltTypeExpected.size())) && valid;
}
void ResolveRuntime() noexcept {
    GetGame = At<GetGameFn>(GetGameRva); Enumerate = At<EnumerateFn>(EnumerateRva); FirstUnit = At<UnitFn>(FirstUnitRva); NextUnit = At<UnitFn>(NextUnitRva);
    UnitType = At<UnitIntFn>(UnitTypeRva); UnitId = At<UnitIntFn>(UnitIdRva); UnitMode = At<UnitIntFn>(UnitModeRva); UnitDistance = At<UnitPairFn>(UnitDistanceRva); UnitCollision = At<CollisionFn>(UnitCollisionRva);
    Pickup = At<PickupFn>(PickupRva); GetItemCode = At<GetItemCodeFn>(GetItemCodeRva); GetInventory = At<GetInventoryFn>(GetInventoryRva); GetBeltType = At<GetBeltTypeFn>(GetBeltTypeRva); ResolveOccupancyGrid = At<ResolveOccupancyGridFn>(ResolveOccupancyGridRva);
}
auto InstallMutations() noexcept -> bool {
    if (!Context->InstallInlineHook(GetFreeBeltSlotRva, GetFreeBeltSlotExpected.data(), static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size()), HookGetFreeBeltSlot, &OriginalGetFreeBeltSlot)) { Context->LogError("AutoPickup: free-belt-slot hook installation failed."); return false; }
    const auto replacement = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&HookTrigger));
    for (std::uint8_t opcode = FirstTriggerOpcode; opcode <= LastTriggerOpcode; ++opcode) {
        const auto expected = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        OriginalTriggers[opcode] = reinterpret_cast<TriggerFn>(static_cast<std::uintptr_t>(expected));
        if (!Context->PatchWriteU64(ServerPacketTableRva + static_cast<std::uintptr_t>(opcode) * sizeof(std::uintptr_t), &expected, static_cast<std::uint32_t>(sizeof(expected)), replacement)) { Context->LogError("AutoPickup: authoritative action-table patch failed."); return false; }
    }
    return true;
}
auto Value(const std::atomic<std::uint64_t>& counter) noexcept -> std::uint64_t { return counter.load(std::memory_order_relaxed); }
auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[512]{};
    std::snprintf(message, sizeof(message), "Auto Pickup 2.0.0: enabled=%s; authority=server; distance=%u; interval=%u; potion-routes=%llu; inventory-routes=%llu; picked=%llu; failed=%llu; belt-state-failures=%llu; enumeration-failures=%llu; route-matches=%llu; route-mismatches=%llu.", Settings.enabled ? "true" : "false", Settings.distance, Settings.interval, static_cast<unsigned long long>(Value(Metrics.potionRoutes)), static_cast<unsigned long long>(Value(Metrics.inventoryRoutes)), static_cast<unsigned long long>(Value(Metrics.picked)), static_cast<unsigned long long>(Value(Metrics.failed)), static_cast<unsigned long long>(Value(Metrics.beltStateFailures)), static_cast<unsigned long long>(Value(Metrics.enumerationFailures)), static_cast<unsigned long long>(Value(Metrics.routeMatches)), static_cast<unsigned long long>(Value(Metrics.routeMismatches)));
    command->plugin->WriteConsoleMessage(message); return D2RL::ConsoleCommandResult::Handled;
}
void ResetRuntime() noexcept {
    ResetRoutingScope(); TriggerCounter = 0; LoggedScanException = false; OriginalTriggers.fill(nullptr); OriginalGetFreeBeltSlot = nullptr;
    GetGame = nullptr; Enumerate = nullptr; FirstUnit = nullptr; NextUnit = nullptr; UnitType = nullptr; UnitId = nullptr; UnitMode = nullptr; UnitDistance = nullptr; UnitCollision = nullptr; Pickup = nullptr; GetItemCode = nullptr; GetInventory = nullptr; GetBeltType = nullptr; ResolveOccupancyGrid = nullptr; Settings = {}; ResetMetrics();
}
} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }
D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->apiVersion < D2RL_PLUGIN_API_VERSION) return false;
    Context = context; Base = reinterpret_cast<std::uint8_t*>(context->exeBase); ResetRuntime();
    if (!Base) { context->LogError("AutoPickup: D2R executable base is unavailable."); return false; }
    if (!ReadConfiguration()) return false;
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    char buildMessage[192]{};
    std::snprintf(buildMessage, sizeof(buildMessage),
        "AutoPickup: observed D2R build-name=%s; validating the complete native fingerprint.",
        runtimeBuild && runtimeBuild[0] != '\0' ? runtimeBuild : "unknown");
    context->LogInfo(buildMessage);
    if (!Settings.enabled) context->LogInfo("Auto Pickup 2.0.0 by RuffnecKk disabled; no native mutation was installed.");
    else {
        if (!ValidateRuntime()) { context->LogError("AutoPickup: native fingerprint validation failed; no native mutation was installed."); return false; }
        ResolveRuntime(); if (!InstallMutations()) return false;
        context->LogInfo("Auto Pickup 2.0.0 by RuffnecKk active on authoritative player-action callbacks.");
    }
    if (!context->RegisterConsoleCommand("auto-pickup", Status, "Show auto-pickup policy and authoritative counters.")) context->LogWarn("AutoPickup: status command could not be registered.");
    return true;
}
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { ResetRuntime(); Base = nullptr; Context = nullptr; }
} // namespace RuffnecKk::AutoPickup
