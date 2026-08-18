#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace RuffnecKk::ItemDurability {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t UpdateDurabilityRva = 0x441B10;
constexpr std::uintptr_t GetBaseStatRva = 0x2F48C0;
constexpr std::uintptr_t GetDataTablesRva = 0x300A90;
constexpr std::uintptr_t CompileItemsTxtRva = 0x315FD0;
constexpr std::uintptr_t UnitSeedRva = 0x34A1E0;
constexpr std::uintptr_t RandomRva = 0x153B00;
constexpr std::uintptr_t CheckItemFlagRva = 0x36E2D0;
constexpr std::uintptr_t EtherealMaximumReturnRva = 0x44351F;
constexpr std::uint32_t EtherealItemFlag = 0x00400000;
constexpr std::int32_t MaximumDurabilityStat = 73;
constexpr std::uintptr_t ItemsRecordsOffset = 0x15A0;
constexpr std::uintptr_t ItemsCountOffset = 0x15A8;
constexpr std::uintptr_t ItemTypesRecordsOffset = 0x1348;
constexpr std::uintptr_t ItemTypesCountOffset = 0x1350;
constexpr std::size_t MaximumConfigBytes = 65'536;

constexpr std::array<std::uint8_t, 28> ExpectedUpdateDurability{
    0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57, 0x41,
    0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
    0x30, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0xBA,
    0x32, 0x00, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 55> ExpectedGetBaseStat{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
    0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
    0x75, 0x2A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x00, 0xD2, 0xFF, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0
};
constexpr std::array<std::uint8_t, 32> ExpectedCompileItemsTxt{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xE6, 0xFF, 0xFF
};
constexpr std::array<std::uint8_t, 16> ExpectedGetDataTables{
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48,
    0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04
};
constexpr std::array<std::uint8_t, 32> ExpectedUnitSeed{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x94, 0xBB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01
};
constexpr std::array<std::uint8_t, 14> ExpectedRandom{
    0x44, 0x8B, 0xCA, 0x48, 0x8B, 0xD1, 0x45,
    0x85, 0xC9, 0x7F, 0x03, 0x33, 0xC0, 0xC3
};
constexpr std::array<std::uint8_t, 15> ExpectedCheckItemFlag{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9
};

using UpdateDurabilityFn = void(__fastcall*)(void*, void*, void*) noexcept;
using GetBaseStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::uint16_t) noexcept;
using GetDataTablesFn = std::uint8_t*(__fastcall*)(std::uint8_t) noexcept;
using CompileItemsTxtFn = void(__fastcall*)(std::uint8_t) noexcept;
using UnitSeedFn = void*(__fastcall*)(void*) noexcept;
using RandomFn = std::uint32_t(__fastcall*)(void*, std::int32_t) noexcept;
using CheckItemFlagFn = std::int32_t(__fastcall*)(
    void*, std::uint32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
UpdateDurabilityFn OriginalUpdateDurability{};
GetBaseStatFn OriginalGetBaseStat{};
GetDataTablesFn GetDataTables{};
CompileItemsTxtFn OriginalCompileItemsTxt{};
UnitSeedFn GetUnitSeed{};
RandomFn RollRandom{};
CheckItemFlagFn CheckItemFlag{};
bool ItemTableHookInstalled{};

std::atomic<std::uint64_t> PreventedNormal{};
std::atomic<std::uint64_t> PreventedEthereal{};
std::atomic<std::uint64_t> RangedWeaponRecordsEnabled{};
std::atomic<std::uint64_t> RepairTypeRecordsEnabled{};
std::atomic<std::uint64_t> ItemTableCompilePasses{};
std::atomic<std::uint64_t> ItemTableCompileFailures{};
std::atomic<std::uint64_t> DiagnosticMessages{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-item-durability",
    .name = "Item Durability",
    .version = "1.2.2",
    .author = "RuffnecKk",
    .description = "Controls durability loss, ethereal durability, and bow durability.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

void ResetTelemetry() noexcept {
    PreventedNormal.store(0, std::memory_order_relaxed);
    PreventedEthereal.store(0, std::memory_order_relaxed);
    RangedWeaponRecordsEnabled.store(0, std::memory_order_relaxed);
    RepairTypeRecordsEnabled.store(0, std::memory_order_relaxed);
    ItemTableCompilePasses.store(0, std::memory_order_relaxed);
    ItemTableCompileFailures.store(0, std::memory_order_relaxed);
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
            ? "ItemDurability: configuration exceeds 65535 bytes."
            : "ItemDurability: configuration could not be read.");
        return false;
    }

    std::string error;
    Config parsed{};
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("ItemDurability: invalid TOML (")
            + error + "); no hook was installed.";
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
    const auto message = std::string("ItemDurability: ") + label
        + " signature mismatch; no hook was installed.";
    Context->LogError(message.c_str());
    return false;
}

auto ValidateRuntime() noexcept -> bool {
    if (!Settings.enabled) return true;
    const bool needsLoss = Settings.durabilityLossEnabled
        && (Settings.normalResistancePercent != 0
            || Settings.etherealResistancePercent != 0);
    const bool needsMaximum = Settings.forceMaximumDurability
        || Settings.etherealMaximumPercent != 50;

    bool valid = true;
    if (needsLoss) {
        valid = Check(
            UpdateDurabilityRva, ExpectedUpdateDurability, "durability-loss hook")
            && valid;
        valid = Check(UnitSeedRva, ExpectedUnitSeed, "unit-seed helper") && valid;
        valid = Check(RandomRva, ExpectedRandom, "random helper") && valid;
        valid = Check(
            CheckItemFlagRva, ExpectedCheckItemFlag, "item-flag helper") && valid;
    }
    if (needsMaximum) {
        valid = Check(
            GetBaseStatRva, ExpectedGetBaseStat, "base-stat hook") && valid;
    }
    if (Settings.bowsAndCrossbowsHaveDurability) {
        valid = Check(
            GetDataTablesRva, ExpectedGetDataTables, "data-table helper")
            && valid;
        valid = Check(
            CompileItemsTxtRva,
            ExpectedCompileItemsTxt,
            "compiled-items hook") && valid;
    }
    return valid;
}

auto IsEtherealItem(void* item) noexcept -> bool {
    return item && CheckItemFlag
        && CheckItemFlag(item, EtherealItemFlag) != 0;
}

void __fastcall HookCompileItemsTxt(std::uint8_t context) noexcept {
    OriginalCompileItemsTxt(context);

    auto* dataTables = GetDataTables ? GetDataTables(context) : nullptr;
    if (!dataTables) {
        ItemTableCompileFailures.fetch_add(1, std::memory_order_relaxed);
        Context->LogError(
            "ItemDurability: compiled item tables could not be resolved.");
        return;
    }

    auto* itemRecords = *reinterpret_cast<std::uint8_t**>(
        dataTables + ItemsRecordsOffset);
    const auto itemRecordCount = *reinterpret_cast<const std::uint64_t*>(
        dataTables + ItemsCountOffset);
    auto* itemTypeRecords = *reinterpret_cast<std::uint8_t**>(
        dataTables + ItemTypesRecordsOffset);
    const auto itemTypeCount = *reinterpret_cast<const std::uint64_t*>(
        dataTables + ItemTypesCountOffset);
    const auto result = ApplyRangedDurabilityToCompiledTables(
        itemRecords,
        itemRecordCount,
        itemTypeRecords,
        itemTypeCount);
    if (!result.valid) {
        ItemTableCompileFailures.fetch_add(1, std::memory_order_relaxed);
        Context->LogError(
            "ItemDurability: invalid compiled item tables were rejected.");
        return;
    }

    ItemTableCompilePasses.fetch_add(1, std::memory_order_relaxed);
    RangedWeaponRecordsEnabled.fetch_add(
        result.itemRecordsUpdated, std::memory_order_relaxed);
    RepairTypeRecordsEnabled.fetch_add(
        result.itemTypesUpdated, std::memory_order_relaxed);
    if (ShouldLogDiagnostic()) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "ItemDurability: compiled context %u; ranged records=%llu; repair types=%llu.",
            static_cast<unsigned>(context),
            static_cast<unsigned long long>(result.itemRecordsUpdated),
            static_cast<unsigned long long>(result.itemTypesUpdated));
        Context->LogInfo(message);
    }
}

void __fastcall HookUpdateDurability(
    void* game,
    void* unit,
    void* item
) noexcept {
    if (!unit || !item) {
        OriginalUpdateDurability(game, unit, item);
        return;
    }
    const bool ethereal = IsEtherealItem(item);
    const auto resistance = ethereal
        ? Settings.etherealResistancePercent
        : Settings.normalResistancePercent;
    if (resistance == 0) {
        OriginalUpdateDurability(game, unit, item);
        return;
    }

    auto* seed = GetUnitSeed(unit);
    if (!seed || !PreventsLoss(resistance, RollRandom(seed, 100))) {
        OriginalUpdateDurability(game, unit, item);
        return;
    }
    if (ethereal) {
        const auto prevented = PreventedEthereal.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (ShouldLogDiagnostic()) {
            char message[160]{};
            std::snprintf(
                message,
                sizeof(message),
                "ItemDurability: prevented ethereal durability-loss event #%llu.",
                static_cast<unsigned long long>(prevented));
            Context->LogInfo(message);
        }
    } else {
        const auto prevented = PreventedNormal.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (ShouldLogDiagnostic()) {
            char message[160]{};
            std::snprintf(
                message,
                sizeof(message),
                "ItemDurability: prevented normal durability-loss event #%llu.",
                static_cast<unsigned long long>(prevented));
            Context->LogInfo(message);
        }
    }
}

__declspec(noinline) std::int32_t __fastcall HookGetBaseStat(
    void* unit,
    std::int32_t stat,
    std::uint16_t layer
) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto value = OriginalGetBaseStat(unit, stat, layer);
    if (returnAddress != Base + EtherealMaximumReturnRva
        || stat != MaximumDurabilityStat) {
        return value;
    }
    if (Settings.forceMaximumDurability && value > 0) {
        return EncodeEtherealMaximumTarget(255);
    }
    return EncodeForVanillaEtherealHalving(
        value, Settings.etherealMaximumPercent);
}

void FormatChance(
    char* output,
    std::size_t size,
    std::uint32_t basisPoints
) noexcept {
    std::snprintf(
        output, size, "%u.%02u%%", basisPoints / 100, basisPoints % 100);
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

    char normalWeapon[16]{};
    char normalArmor[16]{};
    char etherealWeapon[16]{};
    char etherealArmor[16]{};
    FormatChance(normalWeapon, sizeof(normalWeapon),
        EffectiveChanceBasisPoints(4, Settings.normalResistancePercent));
    FormatChance(normalArmor, sizeof(normalArmor),
        EffectiveChanceBasisPoints(10, Settings.normalResistancePercent));
    FormatChance(etherealWeapon, sizeof(etherealWeapon),
        EffectiveChanceBasisPoints(4, Settings.etherealResistancePercent));
    FormatChance(etherealArmor, sizeof(etherealArmor),
        EffectiveChanceBasisPoints(10, Settings.etherealResistancePercent));

    char message[700]{};
    std::snprintf(
        message,
        sizeof(message),
        "Item Durability 1.2.2: enabled=%s; loss=%s; normal=%u%% (weapon %s, armor %s); "
        "ethereal=%u%% (weapon %s, armor %s); ethereal maximum=%u%s; "
        "bows/crossbows=%s; item records=%llu; repair types=%llu; "
        "compile passes=%llu failures=%llu; prevented normal=%llu ethereal=%llu; diagnostics=%s.",
        Settings.enabled ? "true" : "false",
        Settings.durabilityLossEnabled ? "enabled" : "disabled",
        Settings.normalResistancePercent,
        normalWeapon,
        normalArmor,
        Settings.etherealResistancePercent,
        etherealWeapon,
        etherealArmor,
        Settings.forceMaximumDurability ? 255U : Settings.etherealMaximumPercent,
        Settings.forceMaximumDurability ? " points" : "%",
        Settings.bowsAndCrossbowsHaveDurability ? "enabled" : "disabled",
        static_cast<unsigned long long>(
            RangedWeaponRecordsEnabled.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RepairTypeRecordsEnabled.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ItemTableCompilePasses.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ItemTableCompileFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PreventedNormal.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PreventedEthereal.load(std::memory_order_relaxed)),
        Settings.diagnosticsEnabled ? "enabled" : "disabled");
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

auto InstallHooks() noexcept -> bool {
    if (!Settings.enabled) return true;
    const bool needsLoss = Settings.durabilityLossEnabled
        && (Settings.normalResistancePercent != 0
            || Settings.etherealResistancePercent != 0);
    const bool needsMaximum = Settings.forceMaximumDurability
        || Settings.etherealMaximumPercent != 50;

    if (needsLoss) {
        GetUnitSeed = At<UnitSeedFn>(UnitSeedRva);
        RollRandom = At<RandomFn>(RandomRva);
        CheckItemFlag = At<CheckItemFlagFn>(CheckItemFlagRva);
        if (!Context->InstallInlineHook(
                UpdateDurabilityRva,
                ExpectedUpdateDurability.data(),
                static_cast<std::uint32_t>(ExpectedUpdateDurability.size()),
                HookUpdateDurability,
                &OriginalUpdateDurability)) {
            Context->LogError(
                "ItemDurability: durability-loss hook installation failed.");
            return false;
        }
    }
    if (needsMaximum
        && !Context->InstallInlineHook(
            GetBaseStatRva,
            ExpectedGetBaseStat.data(),
            static_cast<std::uint32_t>(ExpectedGetBaseStat.size()),
            HookGetBaseStat,
            &OriginalGetBaseStat)) {
        Context->LogError(
            "ItemDurability: ethereal maximum hook installation failed.");
        return false;
    }
    if (Settings.bowsAndCrossbowsHaveDurability) {
        GetDataTables = At<GetDataTablesFn>(GetDataTablesRva);
        if (!Context->InstallInlineHook(
                CompileItemsTxtRva,
                ExpectedCompileItemsTxt.data(),
                static_cast<std::uint32_t>(ExpectedCompileItemsTxt.size()),
                HookCompileItemsTxt,
                &OriginalCompileItemsTxt)) {
            Context->LogError(
                "ItemDurability: compiled-items hook installation failed.");
            return false;
        }
        ItemTableHookInstalled = true;
    }
    return true;
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
    ItemTableHookInstalled = false;
    ResetTelemetry();

    if (!Base) {
        context->LogError("ItemDurability: D2R executable base is unavailable.");
        return false;
    }
    if (!ReadConfiguration()) return false;
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("ItemDurability: only D2R build 92777 is supported.");
        return false;
    }
    if (!ValidateRuntime() || !InstallHooks()) return false;

    if (!context->RegisterConsoleCommand(
            "item-durability",
            Status,
            "Show item durability policy and session counters.")) {
        context->LogWarn(
            "ItemDurability: status command could not be registered.");
    }

    char message[448]{};
    std::snprintf(
        message,
        sizeof(message),
        "Item Durability 1.2.2 by RuffnecKk loaded: enabled=%s; loss=%s; normal=%u%%; "
        "ethereal=%u%%; ethereal maximum=%u%s; bows/crossbows=%s; "
        "compiled-table hook=%s; diagnostics=%s.",
        Settings.enabled ? "true" : "false",
        Settings.durabilityLossEnabled ? "enabled" : "disabled",
        Settings.normalResistancePercent,
        Settings.etherealResistancePercent,
        Settings.forceMaximumDurability ? 255U : Settings.etherealMaximumPercent,
        Settings.forceMaximumDurability ? " points" : "%",
        Settings.bowsAndCrossbowsHaveDurability ? "enabled" : "disabled",
        ItemTableHookInstalled ? "active" : "inactive",
        Settings.diagnosticsEnabled ? "enabled" : "disabled");
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    OriginalCompileItemsTxt = nullptr;
    OriginalGetBaseStat = nullptr;
    OriginalUpdateDurability = nullptr;
    CheckItemFlag = nullptr;
    RollRandom = nullptr;
    GetUnitSeed = nullptr;
    GetDataTables = nullptr;
    ItemTableHookInstalled = false;
    Settings = {};
    ResetTelemetry();
    Base = 0;
    Context = nullptr;
}

} // namespace RuffnecKk::ItemDurability
