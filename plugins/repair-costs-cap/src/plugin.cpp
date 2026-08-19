#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace RuffnecKk::RepairCostsCap {
namespace {

constexpr std::uintptr_t TransactionCostBodyRva = 0x36F0C0;
constexpr std::uintptr_t RepairAllCostRva = 0x375330;
constexpr std::uintptr_t RepairAllZeroCostBranchSignatureRva = 0x53FF65;
constexpr std::uintptr_t RepairItemRva = 0x53BB50;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitBaseStatRva = 0x2F48C0;
constexpr std::uintptr_t GetUnitBaseStatSignatureRva =
    GetUnitBaseStatRva + 5;
constexpr std::uintptr_t GetMaxDurabilityRva = 0x2F4B60;
constexpr std::uintptr_t GetUnitSeedRva = 0x34A1E0;
constexpr std::uintptr_t RollRandomRva = 0x153B00;
constexpr std::uintptr_t GetClientFromPlayerRva = 0x48FDE0;
constexpr std::uintptr_t SetStatAndNotifyRva = 0x43EB30;
constexpr std::size_t RepairAllBranchDisplacementOffset = 11;
constexpr std::int32_t DurabilityStat = 72;
constexpr std::int32_t MaximumDurabilityStat = 73;
constexpr std::size_t MaximumConfigBytes = 65'536;

constexpr std::array<std::uint8_t, 29> TransactionCostBodyExpected{
    0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44,
    0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48,
    0x89, 0x4C, 0x24, 0x08, 0x55, 0x57, 0x41, 0x55,
    0x48, 0x8D, 0x6C, 0x24, 0xC9
};
constexpr std::array<std::uint8_t, 33> RepairAllCostExpected{
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x81, 0xEC, 0x88, 0x02, 0x00, 0x00, 0x48,
    0x8B, 0x05, 0x82, 0x5F, 0x65, 0x02, 0x48, 0x33,
    0xC4, 0x48, 0x89, 0x84, 0x24, 0x50, 0x02, 0x00,
    0x00
};
constexpr std::array<std::uint8_t, 16> RepairAllZeroCostBranchExpected{
    0x3B, 0xC7, 0x0F, 0x82, 0xAD, 0x00, 0x00, 0x00,
    0x85, 0xFF, 0x74, 0x6F, 0x48, 0x8B, 0x55, 0x48
};
constexpr std::array<std::uint8_t, 32> RepairItemExpected{
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x8B, 0xE9, 0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xCA,
    0x48, 0x8B, 0xFA, 0xE8, 0xD0, 0xEF, 0xE2, 0xFF
};
constexpr std::array<std::uint8_t, 16> GetUnitStatExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
};
// Item Durability may own a five-byte entry jump. The untouched tail stays
// stable and the live entry address intentionally participates in that chain.
constexpr std::array<std::uint8_t, 50> GetUnitBaseStatExpected{
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x0F, 0xB7, 0xE8, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
    0x48, 0x85, 0xC9, 0x75, 0x2A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x00,
    0xD2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x33, 0xC0
};
constexpr std::array<std::uint8_t, 16> GetMaxDurabilityExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48
};
constexpr std::array<std::uint8_t, 32> GetUnitSeedExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x94, 0xBB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01
};
constexpr std::array<std::uint8_t, 14> RollRandomExpected{
    0x44, 0x8B, 0xCA, 0x48, 0x8B, 0xD1, 0x45,
    0x85, 0xC9, 0x7F, 0x03, 0x33, 0xC0, 0xC3
};
constexpr std::array<std::uint8_t, 16> GetClientFromPlayerExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x1E, 0xE8, 0xDD
};
constexpr std::array<std::uint8_t, 32> SetStatAndNotifyExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x30, 0x41, 0x8B, 0xF1, 0x45, 0x0F, 0xB6,
    0xF0, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xD9, 0xE8
};

using TransactionCostFn = std::int32_t(__fastcall*)(
    void*, void*, std::uint32_t, void*, std::int32_t, std::int32_t) noexcept;
using RepairAllCostFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::uint32_t, void*, void*) noexcept;
using RepairItemFn = void(__fastcall*)(void*, void*, void*) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::uint16_t) noexcept;
using GetMaxDurabilityFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitSeedFn = void*(__fastcall*)(void*) noexcept;
using RollRandomFn = std::uint32_t(__fastcall*)(void*, std::int32_t) noexcept;
using GetClientFromPlayerFn = void*(__fastcall*)(void*) noexcept;
using SetStatAndNotifyFn = void(__fastcall*)(
    void*, void*, bool, std::int32_t, std::int32_t, std::uint16_t) noexcept;

struct TelemetryCounters {
    std::atomic<std::uint64_t> itemEvaluations{};
    std::atomic<std::uint64_t> itemAdjustments{};
    std::atomic<std::uint64_t> itemQuotedGoldReduced{};
    std::atomic<std::uint64_t> repairAllEvaluations{};
    std::atomic<std::uint64_t> repairAllAdjustments{};
    std::atomic<std::uint64_t> repairAllExtraGoldReduced{};
    std::atomic<std::uint64_t> physicalRepairsEvaluated{};
    std::atomic<std::uint64_t> maximumDurabilityLost{};

    void Reset() noexcept {
        itemEvaluations.store(0, std::memory_order_relaxed);
        itemAdjustments.store(0, std::memory_order_relaxed);
        itemQuotedGoldReduced.store(0, std::memory_order_relaxed);
        repairAllEvaluations.store(0, std::memory_order_relaxed);
        repairAllAdjustments.store(0, std::memory_order_relaxed);
        repairAllExtraGoldReduced.store(0, std::memory_order_relaxed);
        physicalRepairsEvaluated.store(0, std::memory_order_relaxed);
        maximumDurabilityLost.store(0, std::memory_order_relaxed);
    }
};

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
RepairPolicy Settings{};
TransactionCostFn OriginalTransactionCost{};
RepairAllCostFn OriginalRepairAllCost{};
RepairItemFn OriginalRepairItem{};
GetUnitStatFn GetUnitStat{};
GetUnitStatFn GetUnitBaseStat{};
GetMaxDurabilityFn GetMaxDurability{};
GetUnitSeedFn GetUnitSeed{};
RollRandomFn RollRandom{};
GetClientFromPlayerFn GetClientFromPlayer{};
SetStatAndNotifyFn SetStatAndNotify{};
TelemetryCounters Telemetry{};
std::atomic<std::uint64_t> DiagnosticMessages{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-repair-costs-cap",
    .name = "Repair Costs Cap",
    .version = "1.4.2",
    .author = "RuffnecKk",
    .description = "Controls NPC repair prices and optional permanent durability wear.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

void ResetState() noexcept {
    OriginalRepairItem = nullptr;
    OriginalRepairAllCost = nullptr;
    OriginalTransactionCost = nullptr;
    SetStatAndNotify = nullptr;
    GetClientFromPlayer = nullptr;
    RollRandom = nullptr;
    GetUnitSeed = nullptr;
    GetMaxDurability = nullptr;
    GetUnitBaseStat = nullptr;
    GetUnitStat = nullptr;
    Telemetry.Reset();
    DiagnosticMessages.store(0, std::memory_order_relaxed);
}

auto ShouldLogDiagnostic() noexcept -> bool {
    if (!Settings.diagnosticsEnabled || !Context) return false;
    const auto ordinal = DiagnosticMessages.fetch_add(
        1, std::memory_order_relaxed) + 1;
    return ordinal <= 8 || ordinal % 100 == 0;
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "RepairCostsCap: configuration exceeds 65535 bytes."
            : "RepairCostsCap: configuration could not be read.");
        return false;
    }

    RepairPolicy parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("RepairCostsCap: invalid TOML (")
            + error + "); no hook or patch was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

template<std::size_t Size>
auto Check(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected,
    const char* label
) noexcept -> bool {
    if (Context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()))) {
        return true;
    }
    const auto message = std::string("RepairCostsCap: ") + label
        + " signature mismatch; no mutation was started.";
    Context->LogError(message.c_str());
    return false;
}

auto ValidateRuntime() noexcept -> bool {
    if (!Settings.pluginEnabled || !Settings.enabled) return true;
    bool valid = Check(
        TransactionCostBodyRva,
        TransactionCostBodyExpected,
        "transaction-cost entry");

    valid = Check(
        RepairAllCostRva,
        RepairAllCostExpected,
        "Repair All cost entry") && valid;
    valid = Check(
        RepairAllZeroCostBranchSignatureRva,
        RepairAllZeroCostBranchExpected,
        "Repair All zero-cost branch") && valid;

    if (Settings.durabilityWearEnabled) {
        valid = Check(
            RepairItemRva,
            RepairItemExpected,
            "physical repair entry") && valid;
        valid = Check(
            GetUnitStatRva,
            GetUnitStatExpected,
            "unit-stat helper") && valid;
        valid = Check(
            GetUnitBaseStatSignatureRva,
            GetUnitBaseStatExpected,
            "base-stat helper tail") && valid;
        valid = Check(
            GetMaxDurabilityRva,
            GetMaxDurabilityExpected,
            "maximum-durability helper") && valid;
        valid = Check(
            GetUnitSeedRva,
            GetUnitSeedExpected,
            "unit-seed helper") && valid;
        valid = Check(
            RollRandomRva,
            RollRandomExpected,
            "random helper") && valid;
        valid = Check(
            GetClientFromPlayerRva,
            GetClientFromPlayerExpected,
            "player-client helper") && valid;
        valid = Check(
            SetStatAndNotifyRva,
            SetStatAndNotifyExpected,
            "stat notification helper") && valid;
    }
    return valid;
}

void BindWearHelpers() noexcept {
    if (!Settings.pluginEnabled || !Settings.enabled
        || !Settings.durabilityWearEnabled) return;
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    // This deliberately calls the live executable entry. If Item Durability
    // owns that entry, its return-address filter preserves this vanilla call.
    GetUnitBaseStat = At<GetUnitStatFn>(GetUnitBaseStatRva);
    GetMaxDurability = At<GetMaxDurabilityFn>(GetMaxDurabilityRva);
    GetUnitSeed = At<GetUnitSeedFn>(GetUnitSeedRva);
    RollRandom = At<RollRandomFn>(RollRandomRva);
    GetClientFromPlayer = At<GetClientFromPlayerFn>(GetClientFromPlayerRva);
    SetStatAndNotify = At<SetStatAndNotifyFn>(SetStatAndNotifyRva);
}

void __fastcall HookRepairItem(
    void* game,
    void* item,
    void* player
) noexcept {
    const auto durabilityBefore = item && player
        ? GetUnitStat(item, DurabilityStat, 0)
        : 0;
    const auto maximumBefore = item && player
        ? GetMaxDurability(item)
        : 0;
    const auto baseMaximumBefore = item && player
        ? GetUnitBaseStat(item, MaximumDurabilityStat, 0)
        : 0;

    OriginalRepairItem(game, item, player);

    if (!item || !player || baseMaximumBefore <= 1
        || !Settings.pluginEnabled || !Settings.enabled
        || !Settings.durabilityWearEnabled) {
        return;
    }
    const auto durabilityAfter = GetUnitStat(item, DurabilityStat, 0);
    if (!DidPhysicalRepairSucceed(
            durabilityBefore, maximumBefore, durabilityAfter)) {
        return;
    }

    Telemetry.physicalRepairsEvaluated.fetch_add(
        1, std::memory_order_relaxed);
    auto* seed = GetUnitSeed(item);
    if (!seed) return;
    const auto roll = RollRandom(seed, ChanceBasisPointScale);
    if (!ShouldLoseMaximumDurability(
            Settings.durabilityWearEnabled,
            Settings.durabilityWearChance,
            roll)) {
        return;
    }

    const auto reducedBaseMaximum =
        ReducedMaximumDurability(baseMaximumBefore);
    auto* client = GetClientFromPlayer(player);
    SetStatAndNotify(
        item,
        client,
        true,
        MaximumDurabilityStat,
        reducedBaseMaximum,
        0);
    const auto reducedEffectiveMaximum = GetMaxDurability(item);
    if (reducedEffectiveMaximum > 0) {
        SetStatAndNotify(
            item,
            client,
            true,
            DurabilityStat,
            reducedEffectiveMaximum,
            0);
    }
    Telemetry.maximumDurabilityLost.fetch_add(1, std::memory_order_relaxed);
    if (ShouldLogDiagnostic()) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "RepairCostsCap: physical repair permanently reduced maximum durability from %d to %d.",
            baseMaximumBefore,
            reducedBaseMaximum);
        Context->LogInfo(message);
    }
}

std::int32_t __fastcall HookTransactionCost(
    void* player,
    void* item,
    std::uint32_t difficulty,
    void* questFlags,
    std::int32_t vendorId,
    std::int32_t transactionType
) noexcept {
    const auto vanillaCost = OriginalTransactionCost(
        player,
        item,
        difficulty,
        questFlags,
        vendorId,
        transactionType);
    if (!player || !item || transactionType != RepairTransactionType
        || vanillaCost <= 0) {
        return vanillaCost;
    }

    Telemetry.itemEvaluations.fetch_add(1, std::memory_order_relaxed);
    const auto adjustedCost = ApplyRepairCostCap(
        vanillaCost, transactionType, Settings);
    if (adjustedCost != vanillaCost) {
        Telemetry.itemAdjustments.fetch_add(1, std::memory_order_relaxed);
        Telemetry.itemQuotedGoldReduced.fetch_add(
            GoldReduction(vanillaCost, adjustedCost),
            std::memory_order_relaxed);
        if (ShouldLogDiagnostic()) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "RepairCostsCap: capped item repair quote from %d to %d gold.",
                vanillaCost,
                adjustedCost);
            Context->LogInfo(message);
        }
    }
    return adjustedCost;
}

std::int32_t __fastcall HookRepairAllCost(
    void* game,
    void* player,
    std::int32_t vendorId,
    std::uint32_t difficulty,
    void* questFlags,
    void* repairCallback
) noexcept {
    const auto adjustedItemTotal = OriginalRepairAllCost(
        game,
        player,
        vendorId,
        difficulty,
        questFlags,
        repairCallback);
    if (!game || !player || adjustedItemTotal <= 0) {
        return adjustedItemTotal;
    }

    Telemetry.repairAllEvaluations.fetch_add(1, std::memory_order_relaxed);
    const auto cappedTotal = ApplyRepairAllCap(adjustedItemTotal, Settings);
    if (cappedTotal != adjustedItemTotal) {
        Telemetry.repairAllAdjustments.fetch_add(1, std::memory_order_relaxed);
        Telemetry.repairAllExtraGoldReduced.fetch_add(
            GoldReduction(adjustedItemTotal, cappedTotal),
            std::memory_order_relaxed);
        if (ShouldLogDiagnostic()) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "RepairCostsCap: capped Repair All total from %d to %d gold.",
                adjustedItemTotal,
                cappedTotal);
            Context->LogInfo(message);
        }
    }
    return cappedTotal;
}

auto InstallChanges() noexcept -> bool {
    if (!Settings.pluginEnabled || !Settings.enabled) return true;
    if (!Context->InstallInlineHook(
            TransactionCostBodyRva,
            TransactionCostBodyExpected.data(),
            static_cast<std::uint32_t>(TransactionCostBodyExpected.size()),
            HookTransactionCost,
            &OriginalTransactionCost)) {
        Context->LogError(
            "RepairCostsCap: transaction-cost hook installation failed.");
        return false;
    }
    if (!Context->InstallInlineHook(
            RepairAllCostRva,
            RepairAllCostExpected.data(),
            static_cast<std::uint32_t>(RepairAllCostExpected.size()),
            HookRepairAllCost,
            &OriginalRepairAllCost)) {
        Context->LogError(
            "RepairCostsCap: Repair All hook installation failed.");
        return false;
    }

    auto replacement = RepairAllZeroCostBranchExpected;
    replacement[RepairAllBranchDisplacementOffset] = 0x21;
    if (!Context->PatchBytes(
            RepairAllZeroCostBranchSignatureRva,
            RepairAllZeroCostBranchExpected.data(),
            static_cast<std::uint32_t>(
                RepairAllZeroCostBranchExpected.size()),
            replacement.data(),
            static_cast<std::uint32_t>(replacement.size()))) {
        Context->LogError(
            "RepairCostsCap: Repair All zero-cost patch failed.");
        return false;
    }
    if (Settings.durabilityWearEnabled
        && !Context->InstallInlineHook(
            RepairItemRva,
            RepairItemExpected.data(),
            static_cast<std::uint32_t>(RepairItemExpected.size()),
            HookRepairItem,
            &OriginalRepairItem)) {
        Context->LogError(
            "RepairCostsCap: durability-wear hook installation failed.");
        return false;
    }
    return true;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char pricing[256]{};
    std::snprintf(
        pricing,
        sizeof(pricing),
        "Repair Costs Cap 1.4.1: enabled=%s; repairCosts=%s; maximumGold=%d "
        "(per item and Repair All); diagnostics=%s.",
        Settings.pluginEnabled ? "true" : "false",
        Settings.enabled ? "true" : "false",
        Settings.maximumGold,
        Settings.diagnosticsEnabled ? "enabled" : "disabled");
    command->plugin->WriteConsoleMessage(pricing);

    char wear[192]{};
    std::snprintf(
        wear,
        sizeof(wear),
        "Permanent durability wear: enabled=%s; chance=%.2f%% per "
        "physically repaired item.",
        Settings.durabilityWearEnabled ? "true" : "false",
        Settings.durabilityWearChance * 100.0);
    command->plugin->WriteConsoleMessage(wear);

    char telemetry[448]{};
    std::snprintf(
        telemetry,
        sizeof(telemetry),
        "Session pricing evaluations: items=%llu adjusted=%llu "
        "quotedGoldReduced=%llu; RepairAll=%llu capped=%llu "
        "extraGoldReduced=%llu; physicalRepairs=%llu durabilityLost=%llu.",
        static_cast<unsigned long long>(
            Telemetry.itemEvaluations.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.itemAdjustments.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.itemQuotedGoldReduced.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.repairAllEvaluations.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.repairAllAdjustments.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.repairAllExtraGoldReduced.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.physicalRepairsEvaluated.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            Telemetry.maximumDurabilityLost.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(telemetry);
    return D2RL::ConsoleCommandResult::Handled;
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
        || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = context->exeBase;
    Settings = {};
    ResetState();

    if (!Base) {
        context->LogError(
            "RepairCostsCap: D2R executable base is unavailable.");
        return false;
    }
    if (!ReadConfiguration()) return false;
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (runtimeBuild == nullptr
        || (std::strcmp(runtimeBuild, "92777") != 0
            && std::strcmp(runtimeBuild, "93847") != 0)) {
        context->LogError(
            "RepairCostsCap: only D2R builds 92777 and 93847 are supported.");
        return false;
    }
    if (!ValidateRuntime()) return false;
    BindWearHelpers();
    if (!InstallChanges()) return false;

    if (!context->RegisterConsoleCommand(
            "repair-costs-cap",
            Status,
            "Show NPC repair policy and session counters.")) {
        context->LogWarn(
            "RepairCostsCap: status command could not be registered.");
    }

    char message[448]{};
    std::snprintf(
        message,
        sizeof(message),
        "Repair Costs Cap 1.4.1 by RuffnecKk loaded: enabled=%s; repairCosts=%s; "
        "maximumGold=%d (per item and Repair All); durability wear=%s "
        "at %.2f%%; diagnostics=%s.",
        Settings.pluginEnabled ? "true" : "false",
        Settings.enabled ? "true" : "false",
        Settings.maximumGold,
        Settings.durabilityWearEnabled ? "enabled" : "disabled",
        Settings.durabilityWearChance * 100.0,
        Settings.diagnosticsEnabled ? "enabled" : "disabled");
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ResetState();
    Settings = {};
    Base = 0;
    Context = nullptr;
}

} // namespace RuffnecKk::RepairCostsCap
