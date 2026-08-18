#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace RuffnecKk::PotionAutoPickup {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
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

constexpr std::array<std::uintptr_t, LastTriggerOpcode + 1>
    TriggerHandlerRvas{
        0,
        0x4AC050, 0x4ACE20, 0x4ACE40, 0x4ACE60, 0x4ACE80, 0x4ACF80,
        0x4AD030, 0x4AD0E0, 0x4AD100, 0x4AD120, 0x4AD140, 0x4AD230,
        0x4AD330, 0x4AD3E0, 0x4AD490, 0x4AD4B0, 0x4AD4D0, 0x4AD4F0,
    };

constexpr std::array<std::uint8_t, 32> GetFreeBeltSlotExpected{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0x05, 0xDF, 0x4F, 0x64,
    0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24,
};
constexpr std::array<std::uint8_t, 32> ResolveOccupancyGridExpected{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xF8, 0x48,
    0x39, 0x51, 0x28, 0x0F, 0x86, 0x01, 0x01, 0x00,
    0x00, 0x49, 0x89, 0x73, 0x18, 0x48, 0x8D, 0x71,
};
constexpr std::array<std::uint8_t, 32> GetInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetBeltTypeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x15, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0xE0, 0xC0, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetItemCodeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x80, 0x83, 0xFF, 0xFF,
};

struct PotionClass {
    std::uint32_t packedCode;
    std::string_view code;
    Family family;
    std::uint8_t tier;
};

constexpr std::array<PotionClass, 12> PotionClasses{
    PotionClass{PackItemCode("hp1"), "hp1", Family::Healing, 1},
    PotionClass{PackItemCode("hp2"), "hp2", Family::Healing, 2},
    PotionClass{PackItemCode("hp3"), "hp3", Family::Healing, 3},
    PotionClass{PackItemCode("hp4"), "hp4", Family::Healing, 4},
    PotionClass{PackItemCode("hp5"), "hp5", Family::Healing, 5},
    PotionClass{PackItemCode("mp1"), "mp1", Family::Mana, 1},
    PotionClass{PackItemCode("mp2"), "mp2", Family::Mana, 2},
    PotionClass{PackItemCode("mp3"), "mp3", Family::Mana, 3},
    PotionClass{PackItemCode("mp4"), "mp4", Family::Mana, 4},
    PotionClass{PackItemCode("mp5"), "mp5", Family::Mana, 5},
    PotionClass{PackItemCode("rvs"), "rvs", Family::Rejuvenation, 1},
    PotionClass{PackItemCode("rvl"), "rvl", Family::Rejuvenation, 2},
};

using TriggerFn = std::int64_t(__fastcall*)(
    void*, void*, void*, std::int32_t);
using GetGameFn = void*(__fastcall*)(void*);
using EnumerateFn = void(__fastcall*)(void*, void***, std::uint32_t*);
using UnitFn = void*(__fastcall*)(void*);
using UnitIntFn = std::uint32_t(__fastcall*)(void*);
using UnitPairFn = std::int32_t(__fastcall*)(void*, void*);
using CollisionFn = std::int32_t(__fastcall*)(void*, void*, std::uint32_t);
using PickupFn = bool(__fastcall*)(
    void*, std::uint32_t, bool, std::uint32_t, bool, bool);
using GetItemCodeFn = std::uint32_t(__fastcall*)(void*);
using GetInventoryFn = void*(__fastcall*)(void*);
using GetBeltTypeFn = std::int32_t(__fastcall*)(void*);
using ResolveOccupancyGridFn = void*(__fastcall*)(
    void*, std::uint64_t, const void*);
using GetFreeBeltSlotFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t*, bool) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::array<TriggerFn, LastTriggerOpcode + 1> OriginalTriggers{};
GetFreeBeltSlotFn OriginalGetFreeBeltSlot{};
GetGameFn GetGame{};
EnumerateFn Enumerate{};
UnitFn FirstUnit{};
UnitFn NextUnit{};
UnitIntFn UnitType{};
UnitIntFn UnitId{};
UnitIntFn UnitMode{};
UnitPairFn UnitDistance{};
CollisionFn UnitCollision{};
PickupFn Pickup{};
GetItemCodeFn GetItemCode{};
GetInventoryFn GetInventory{};
GetBeltTypeFn GetBeltType{};
ResolveOccupancyGridFn ResolveOccupancyGrid{};

struct RuntimeMetrics {
    std::atomic<std::uint64_t> actions{};
    std::atomic<std::uint64_t> scans{};
    std::atomic<std::uint64_t> beltStateFailures{};
    std::atomic<std::uint64_t> enumerationFailures{};
    std::atomic<std::uint64_t> tierRejects{};
    std::atomic<std::uint64_t> collisionRejects{};
    std::atomic<std::uint64_t> distanceRejects{};
    std::atomic<std::uint64_t> destinationRejects{};
    std::atomic<std::uint64_t> selections{};
    std::atomic<std::uint64_t> beltRoutes{};
    std::atomic<std::uint64_t> overflowRoutes{};
    std::atomic<std::uint64_t> routeMatches{};
    std::atomic<std::uint64_t> routeMismatches{};
    std::atomic<std::uint64_t> inventoryAliases{};
    std::atomic<std::uint64_t> pickupSuccesses{};
    std::atomic<std::uint64_t> pickupFailures{};
    std::array<std::atomic<std::uint64_t>, PotionClasses.size()> seenByCode{};
    std::array<std::atomic<std::uint64_t>, PotionClasses.size()>
        selectedByCode{};
    std::array<std::atomic<std::uint64_t>, PotionClasses.size()> pickedByCode{};
};

RuntimeMetrics Metrics{};
std::atomic<std::uint64_t> DiagnosticMessages{};
thread_local bool Inside{};
thread_local std::uint32_t TriggerCounter{};
thread_local void* ForcedInventory{};
thread_local RoutingToken ForcedRoute{};
thread_local std::int32_t ForcedBeltSlot{-1};
thread_local bool ForceInventoryOverflow{};
thread_local bool LoggedScanException{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-potion-auto-pickup",
    .name = "Potion Auto Pickup",
    .version = "1.3.0",
    .author = "RuffnecKk",
    .description =
        "Picks up configured potions into preferred belt columns or inventory.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "PotionAutoPickup: configuration exceeds 65535 bytes."
            : "PotionAutoPickup: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("PotionAutoPickup: invalid TOML (")
            + error + "); no hook was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    if (parsed.schema == ConfigSchema::Legacy) {
        Context->LogWarn(
            "PotionAutoPickup: legacy configuration names are deprecated; migrate to the player-friendly 1.3 format.");
    }
    Settings = parsed;
    return true;
}

void ResetRoutingScope() noexcept {
    ForceInventoryOverflow = false;
    ForcedBeltSlot = -1;
    ForcedRoute.Reset();
    ForcedInventory = nullptr;
    Inside = false;
}

template<std::size_t Size>
void ResetCounters(std::array<std::atomic<std::uint64_t>, Size>& counters) {
    for (auto& counter : counters) {
        counter.store(0, std::memory_order_relaxed);
    }
}

void ResetMetrics() noexcept {
    Metrics.actions.store(0, std::memory_order_relaxed);
    Metrics.scans.store(0, std::memory_order_relaxed);
    Metrics.beltStateFailures.store(0, std::memory_order_relaxed);
    Metrics.enumerationFailures.store(0, std::memory_order_relaxed);
    Metrics.tierRejects.store(0, std::memory_order_relaxed);
    Metrics.collisionRejects.store(0, std::memory_order_relaxed);
    Metrics.distanceRejects.store(0, std::memory_order_relaxed);
    Metrics.destinationRejects.store(0, std::memory_order_relaxed);
    Metrics.selections.store(0, std::memory_order_relaxed);
    Metrics.beltRoutes.store(0, std::memory_order_relaxed);
    Metrics.overflowRoutes.store(0, std::memory_order_relaxed);
    Metrics.routeMatches.store(0, std::memory_order_relaxed);
    Metrics.routeMismatches.store(0, std::memory_order_relaxed);
    Metrics.inventoryAliases.store(0, std::memory_order_relaxed);
    Metrics.pickupSuccesses.store(0, std::memory_order_relaxed);
    Metrics.pickupFailures.store(0, std::memory_order_relaxed);
    ResetCounters(Metrics.seenByCode);
    ResetCounters(Metrics.selectedByCode);
    ResetCounters(Metrics.pickedByCode);
    DiagnosticMessages.store(0, std::memory_order_relaxed);
}

auto ShouldLogDiagnostic() noexcept -> bool {
    if (!Settings.diagnosticsEnabled || !Context) return false;
    const auto ordinal = DiagnosticMessages.fetch_add(
        1, std::memory_order_relaxed) + 1;
    return ordinal <= 8 || ordinal % 100 == 0;
}

auto ClassifyPackedCode(std::uint32_t packedCode) noexcept
    -> const PotionClass* {
    for (const auto& potion : PotionClasses) {
        if (potion.packedCode == packedCode) return &potion;
    }
    return nullptr;
}

auto FamilySettings(Family family) noexcept -> const FamilyConfig& {
    if (family == Family::Healing) return Settings.healing;
    if (family == Family::Mana) return Settings.mana;
    return Settings.rejuvenation;
}

auto Accepted(const PotionClass& potion) noexcept -> bool {
    return FamilySettings(potion.family).policy.Accepts(
        {potion.code, potion.family, potion.tier});
}

auto FamilyRank(Family family) noexcept -> std::uint8_t {
    for (std::uint8_t index = 0;
        index < Settings.familyPriorityCount;
        ++index) {
        if (Settings.familyPriority[index] == family) return index;
    }
    return static_cast<std::uint8_t>(
        Settings.familyPriorityCount + static_cast<std::uint8_t>(family));
}

auto TierRank(
    const FamilyConfig& config,
    std::uint8_t tier
) noexcept -> std::uint8_t {
    for (std::uint8_t index = 0; index < config.tierPriorityCount; ++index) {
        if (config.tierPriority[index] == tier) return index;
    }
    return static_cast<std::uint8_t>(
        config.tierPriorityCount + 5U - tier);
}

auto ReadBeltState(
    void* inventory,
    std::array<BeltSlot, 16>& slots,
    std::uint8_t& capacity
) noexcept -> bool {
    __try {
        if (!inventory || !GetBeltType || !ResolveOccupancyGrid) return false;
        auto* bodyGrid = static_cast<std::uint8_t*>(ResolveOccupancyGrid(
            inventory, 0, Base + BodyGridInfoRva));
        if (!bodyGrid) return false;
        auto** bodyItems = *reinterpret_cast<void***>(bodyGrid + 0x18);
        if (!bodyItems) return false;
        void* belt = bodyItems[BeltBodySlot];
        const auto beltType = belt ? GetBeltType(belt) : 2;
        switch (beltType) {
        case 0:
        case 5:
            capacity = 12;
            break;
        case 1:
        case 4:
            capacity = 8;
            break;
        case 2:
            capacity = 4;
            break;
        case 3:
        case 6:
            capacity = 16;
            break;
        default:
            return false;
        }

        auto* beltGrid = static_cast<std::uint8_t*>(ResolveOccupancyGrid(
            inventory, 1, Base + BeltGridInfoRva));
        if (!beltGrid) return false;
        const auto cells = static_cast<std::uint32_t>(beltGrid[0x10])
            * static_cast<std::uint32_t>(beltGrid[0x11]);
        if (cells < capacity || cells > slots.size()) return false;
        auto** items = *reinterpret_cast<void***>(beltGrid + 0x18);
        if (!items) return false;
        for (std::uint8_t index = 0; index < capacity; ++index) {
            if (!items[index]) continue;
            slots[index].occupied = true;
            const auto* potion = ClassifyPackedCode(GetItemCode(items[index]));
            if (potion) slots[index].family = potion->family;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto PotionIndex(const PotionClass& potion) noexcept -> std::size_t {
    return static_cast<std::size_t>(&potion - PotionClasses.data());
}

auto ReadUnitId(void* unit) noexcept -> std::uint32_t {
    __try {
        return unit && UnitId ? UnitId(unit) : RoutingToken::InvalidGuid;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return RoutingToken::InvalidGuid;
    }
}

auto __fastcall HookGetFreeBeltSlot(
    void* inventory,
    void* item,
    std::int32_t* freeSlot,
    bool allowAnyBeltable
) noexcept -> std::int32_t {
    if (Inside) {
        const auto itemGuid = ReadUnitId(item);
        if (ForcedRoute.Matches(itemGuid)) {
            Metrics.routeMatches.fetch_add(1, std::memory_order_relaxed);
            if (inventory != ForcedInventory) {
                Metrics.inventoryAliases.fetch_add(
                    1, std::memory_order_relaxed);
            }
            if (ForceInventoryOverflow) {
                if (freeSlot) *freeSlot = -1;
                return 0;
            }
            if (freeSlot && ForcedBeltSlot >= 0 && ForcedBeltSlot < 16) {
                *freeSlot = ForcedBeltSlot;
                return 1;
            }
            return 0;
        }
        Metrics.routeMismatches.fetch_add(1, std::memory_order_relaxed);
    }
    return OriginalGetFreeBeltSlot(
        inventory, item, freeSlot, allowAnyBeltable);
}

void ScanUnsafe(void* player) {
    if (!Settings.enabled || Inside || !player) return;
    Metrics.actions.fetch_add(1, std::memory_order_relaxed);
    if ((++TriggerCounter % Settings.interval) != 0) return;
    Metrics.scans.fetch_add(1, std::memory_order_relaxed);

    void* game = GetGame(player);
    if (!game) return;
    void* inventory = GetInventory(player);
    if (!inventory) return;
    std::array<BeltSlot, 16> belt{};
    std::uint8_t beltCapacity{};
    if (!ReadBeltState(inventory, belt, beltCapacity)) {
        Metrics.beltStateFailures.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    void** buckets{};
    std::uint32_t count{};
    Enumerate(game, &buckets, &count);
    if (!buckets || count == 0 || count > 4096) {
        Metrics.enumerationFailures.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    void* best{};
    const PotionClass* bestPotion{};
    std::int32_t bestDistance = INT_MAX;
    std::int32_t bestBeltSlot = -1;
    bool bestOverflow{};
    std::uint8_t bestFamilyRank = UINT8_MAX;
    std::uint8_t bestTierRank = UINT8_MAX;

    for (std::uint32_t index = 0; index < count; ++index) {
        for (void* unit = FirstUnit(buckets[index]);
            unit;
            unit = NextUnit(unit)) {
            if (UnitType(unit) != ItemType || UnitMode(unit) != GroundMode) {
                continue;
            }
            const auto* potion = ClassifyPackedCode(GetItemCode(unit));
            if (!potion) continue;
            const auto potionIndex = PotionIndex(*potion);
            Metrics.seenByCode[potionIndex].fetch_add(
                1, std::memory_order_relaxed);
            if (!Accepted(*potion)) {
                Metrics.tierRejects.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (UnitCollision(player, unit, PickupCollisionMask) != 0) {
                Metrics.collisionRejects.fetch_add(
                    1, std::memory_order_relaxed);
                continue;
            }
            const auto distance = UnitDistance(player, unit);
            if (distance < 0
                || static_cast<std::uint32_t>(distance) > Settings.distance) {
                Metrics.distanceRejects.fetch_add(
                    1, std::memory_order_relaxed);
                continue;
            }

            const auto& family = FamilySettings(potion->family);
            const Item item{potion->code, potion->family, potion->tier};
            const auto beltSlot = ChooseBeltSlot(
                family.policy, item, belt, beltCapacity);
            const bool overflow = beltSlot < 0
                && family.policy.AllowsOverflow(item);
            if (beltSlot < 0 && !overflow) {
                Metrics.destinationRejects.fetch_add(
                    1, std::memory_order_relaxed);
                continue;
            }

            const auto familyRank = FamilyRank(potion->family);
            const auto tierRank = TierRank(family, potion->tier);
            const bool better = !best
                || familyRank < bestFamilyRank
                || (familyRank == bestFamilyRank && tierRank < bestTierRank)
                || (familyRank == bestFamilyRank && tierRank == bestTierRank
                    && distance < bestDistance);
            if (better) {
                best = unit;
                bestPotion = potion;
                bestDistance = distance;
                bestBeltSlot = beltSlot;
                bestOverflow = overflow;
                bestFamilyRank = familyRank;
                bestTierRank = tierRank;
            }
        }
    }

    if (!best || !bestPotion) {
        if (Settings.logScans && ShouldLogDiagnostic()) {
            char message[304]{};
            std::snprintf(
                message,
                sizeof(message),
                "PotionAutoPickup: scan #%llu selected no route; tier rejects=%llu; collision rejects=%llu; distance rejects=%llu; destination rejects=%llu.",
                static_cast<unsigned long long>(
                    Metrics.scans.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    Metrics.tierRejects.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    Metrics.collisionRejects.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    Metrics.distanceRejects.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    Metrics.destinationRejects.load(std::memory_order_relaxed)));
            Context->LogInfo(message);
        }
        return;
    }
    const auto bestGuid = ReadUnitId(best);
    if (bestGuid == RoutingToken::InvalidGuid) return;
    const auto bestIndex = PotionIndex(*bestPotion);
    Metrics.selections.fetch_add(1, std::memory_order_relaxed);
    Metrics.selectedByCode[bestIndex].fetch_add(1, std::memory_order_relaxed);
    if (bestOverflow) {
        Metrics.overflowRoutes.fetch_add(1, std::memory_order_relaxed);
    } else {
        Metrics.beltRoutes.fetch_add(1, std::memory_order_relaxed);
    }

    Inside = true;
    ForcedInventory = inventory;
    ForcedRoute.itemGuid = bestGuid;
    ForcedBeltSlot = bestBeltSlot;
    ForceInventoryOverflow = bestOverflow;
    const bool picked = Pickup(
        player, bestGuid, true, Settings.distance, true, false);
    if (picked) {
        Metrics.pickupSuccesses.fetch_add(1, std::memory_order_relaxed);
        Metrics.pickedByCode[bestIndex].fetch_add(
            1, std::memory_order_relaxed);
    } else {
        Metrics.pickupFailures.fetch_add(1, std::memory_order_relaxed);
    }

    if (ShouldLogDiagnostic()) {
        char message[260]{};
        if (bestOverflow) {
            std::snprintf(
                message,
                sizeof(message),
                "PotionAutoPickup: route code=%.*s guid=%u destination=inventory result=%s.",
                static_cast<int>(bestPotion->code.size()),
                bestPotion->code.data(),
                bestGuid,
                picked ? "success" : "failed");
        } else {
            std::snprintf(
                message,
                sizeof(message),
                "PotionAutoPickup: route code=%.*s guid=%u destination=belt-slot-%d result=%s.",
                static_cast<int>(bestPotion->code.size()),
                bestPotion->code.data(),
                bestGuid,
                bestBeltSlot,
                picked ? "success" : "failed");
        }
        Context->LogInfo(message);
    }
    ResetRoutingScope();
}

auto ScanProtected(void* player) noexcept -> std::uint32_t {
    __try {
        ScanUnsafe(player);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetRoutingScope();
        return GetExceptionCode();
    }
}

void Scan(void* player) noexcept {
    const auto exception = ScanProtected(player);
    if (exception != 0 && !LoggedScanException && Context) {
        LoggedScanException = true;
        Context->LogWarn(
            "PotionAutoPickup: one authoritative scan was skipped after a structured exception.");
    }
}

auto __fastcall HookTrigger(
    void* game,
    void* player,
    void* packet,
    std::int32_t size
) noexcept -> std::int64_t {
    const auto opcode = packet && size > 0
        ? *static_cast<const std::uint8_t*>(packet)
        : static_cast<std::uint8_t>(0);
    if (opcode < FirstTriggerOpcode || opcode > LastTriggerOpcode
        || !OriginalTriggers[opcode]) {
        return 1;
    }
    const auto result = OriginalTriggers[opcode](game, player, packet, size);
    Scan(player);
    return result;
}

auto ValidateRuntime() noexcept -> bool {
    bool valid = true;
    for (std::uint8_t opcode = FirstTriggerOpcode;
        opcode <= LastTriggerOpcode;
        ++opcode) {
        const auto expected = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        valid = Context->CheckExpectedBytes(
                ServerPacketTableRva
                    + static_cast<std::uintptr_t>(opcode)
                        * sizeof(std::uintptr_t),
                &expected,
                static_cast<std::uint32_t>(sizeof(expected)))
            && valid;
    }
    valid = Context->CheckExpectedBytes(
            GetFreeBeltSlotRva,
            GetFreeBeltSlotExpected.data(),
            static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size()))
        && valid;
    valid = Context->CheckExpectedBytes(
            GetItemCodeRva,
            GetItemCodeExpected.data(),
            static_cast<std::uint32_t>(GetItemCodeExpected.size()))
        && valid;
    valid = Context->CheckExpectedBytes(
            ResolveOccupancyGridRva,
            ResolveOccupancyGridExpected.data(),
            static_cast<std::uint32_t>(ResolveOccupancyGridExpected.size()))
        && valid;
    valid = Context->CheckExpectedBytes(
            GetInventoryRva,
            GetInventoryExpected.data(),
            static_cast<std::uint32_t>(GetInventoryExpected.size()))
        && valid;
    valid = Context->CheckExpectedBytes(
            GetBeltTypeRva,
            GetBeltTypeExpected.data(),
            static_cast<std::uint32_t>(GetBeltTypeExpected.size()))
        && valid;
    return valid;
}

void ResolveRuntime() noexcept {
    GetGame = At<GetGameFn>(GetGameRva);
    Enumerate = At<EnumerateFn>(EnumerateRva);
    FirstUnit = At<UnitFn>(FirstUnitRva);
    NextUnit = At<UnitFn>(NextUnitRva);
    UnitType = At<UnitIntFn>(UnitTypeRva);
    UnitId = At<UnitIntFn>(UnitIdRva);
    UnitMode = At<UnitIntFn>(UnitModeRva);
    UnitDistance = At<UnitPairFn>(UnitDistanceRva);
    UnitCollision = At<CollisionFn>(UnitCollisionRva);
    Pickup = At<PickupFn>(PickupRva);
    GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
    GetInventory = At<GetInventoryFn>(GetInventoryRva);
    GetBeltType = At<GetBeltTypeFn>(GetBeltTypeRva);
    ResolveOccupancyGrid = At<ResolveOccupancyGridFn>(
        ResolveOccupancyGridRva);
}

auto InstallMutations() noexcept -> bool {
    if (!Context->InstallInlineHook(
            GetFreeBeltSlotRva,
            GetFreeBeltSlotExpected.data(),
            static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size()),
            HookGetFreeBeltSlot,
            &OriginalGetFreeBeltSlot)) {
        Context->LogError(
            "PotionAutoPickup: free-belt-slot hook installation failed.");
        return false;
    }

    const auto replacement = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&HookTrigger));
    for (std::uint8_t opcode = FirstTriggerOpcode;
        opcode <= LastTriggerOpcode;
        ++opcode) {
        const auto expected = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        OriginalTriggers[opcode] = reinterpret_cast<TriggerFn>(
            static_cast<std::uintptr_t>(expected));
        if (!Context->PatchWriteU64(
                ServerPacketTableRva
                    + static_cast<std::uintptr_t>(opcode)
                        * sizeof(std::uintptr_t),
                &expected,
                static_cast<std::uint32_t>(sizeof(expected)),
                replacement)) {
            Context->LogError(
                "PotionAutoPickup: authoritative action-table patch failed.");
            return false;
        }
    }
    return true;
}

auto Value(const std::atomic<std::uint64_t>& counter) noexcept
    -> std::uint64_t {
    return counter.load(std::memory_order_relaxed);
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

    char message[720]{};
    std::snprintf(
        message,
        sizeof(message),
        "Potion Auto Pickup 1.3.0: enabled=%s; authority=server; distance=%u; interval=%u; diagnostics=%s; log-scans=%s; actions=%llu; scans=%llu; selections=%llu; belt=%llu; inventory=%llu; picked=%llu; failed=%llu; belt-state-failures=%llu; enumeration-failures=%llu; route-matches=%llu; route-mismatches=%llu.",
        Settings.enabled ? "true" : "false",
        Settings.distance,
        Settings.interval,
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        Settings.logScans ? "enabled" : "disabled",
        static_cast<unsigned long long>(Value(Metrics.actions)),
        static_cast<unsigned long long>(Value(Metrics.scans)),
        static_cast<unsigned long long>(Value(Metrics.selections)),
        static_cast<unsigned long long>(Value(Metrics.beltRoutes)),
        static_cast<unsigned long long>(Value(Metrics.overflowRoutes)),
        static_cast<unsigned long long>(Value(Metrics.pickupSuccesses)),
        static_cast<unsigned long long>(Value(Metrics.pickupFailures)),
        static_cast<unsigned long long>(Value(Metrics.beltStateFailures)),
        static_cast<unsigned long long>(Value(Metrics.enumerationFailures)),
        static_cast<unsigned long long>(Value(Metrics.routeMatches)),
        static_cast<unsigned long long>(Value(Metrics.routeMismatches)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetRuntime() noexcept {
    ResetRoutingScope();
    TriggerCounter = 0;
    LoggedScanException = false;
    OriginalTriggers.fill(nullptr);
    OriginalGetFreeBeltSlot = nullptr;
    GetGame = nullptr;
    Enumerate = nullptr;
    FirstUnit = nullptr;
    NextUnit = nullptr;
    UnitType = nullptr;
    UnitId = nullptr;
    UnitMode = nullptr;
    UnitDistance = nullptr;
    UnitCollision = nullptr;
    Pickup = nullptr;
    GetItemCode = nullptr;
    GetInventory = nullptr;
    GetBeltType = nullptr;
    ResolveOccupancyGrid = nullptr;
    Settings = {};
    ResetMetrics();
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    if (!D2RL::HasContext(context)
        || context->apiVersion < D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetRuntime();

    if (!Base) {
        context->LogError(
            "PotionAutoPickup: D2R executable base is unavailable.");
        return false;
    }
    if (!ReadConfiguration()) return false;
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "PotionAutoPickup: only D2R build 92777 is supported.");
        return false;
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "Potion Auto Pickup 1.3.0 by RuffnecKk disabled; no native mutation was installed.");
    } else {
        if (!ValidateRuntime()) {
            context->LogError(
                "PotionAutoPickup: 92777 preflight failed; no native mutation was installed.");
            return false;
        }
        ResolveRuntime();
        if (!InstallMutations()) return false;
        context->LogInfo(
            "Potion Auto Pickup 1.3.0 by RuffnecKk active on authoritative player-action callbacks.");
    }

    if (!context->RegisterConsoleCommand(
            "potion-auto-pickup",
            Status,
            "Show potion auto-pickup policy and authoritative counters.")) {
        context->LogWarn(
            "PotionAutoPickup: status command could not be registered.");
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ResetRuntime();
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::PotionAutoPickup
