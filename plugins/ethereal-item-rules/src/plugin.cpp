#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace RuffnecKk::EtherealItemRules {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t CheckItemTypeRva = 0x373890;
constexpr std::uintptr_t GetItemContextRva = 0x34A0E0;
constexpr std::uintptr_t GetDataTablesRva = 0x300A90;
constexpr std::uintptr_t EtherealWeaponCheckReturnRva = 0x4432DA;
constexpr std::uintptr_t EtherealArmorCheckReturnRva = 0x4432E9;
constexpr std::uintptr_t ItemTypesRecordsOffset = 0x1348;
constexpr std::uintptr_t ItemTypesCountOffset = 0x1350;
constexpr std::uintptr_t EtherealChanceRva = 0x4434DF;
constexpr std::uintptr_t SetQualityBranchRva = 0x443315;
constexpr std::uintptr_t DurabilityEligibilityCallRva = 0x4432F4;
constexpr std::uintptr_t IndestructibleHelperCaveRva = 0x46D840;
constexpr std::size_t MaximumConfigBytes = 65'536;

constexpr std::array<std::uint8_t, 15> ExpectedCheckItemType{
    0x48, 0x89, 0x5C, 0x24, 0x10,
    0x48, 0x89, 0x6C, 0x24, 0x18,
    0x48, 0x89, 0x74, 0x24, 0x20
};
constexpr std::array<std::uint8_t, 16> ExpectedGetItemContext{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C
};
constexpr std::array<std::uint8_t, 16> ExpectedGetDataTables{
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48,
    0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04
};
constexpr std::array<std::uint8_t, 1> ExpectedEtherealChance{0x05};
constexpr std::array<std::uint8_t, 6> ExpectedSetQualityBranch{
    0x0F, 0x84, 0x3D, 0x02, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 5> ExpectedDurabilityEligibilityCall{
    0xE8, 0x47, 0x02, 0xF3, 0xFF
};
constexpr auto ExpectedIndestructibleHelperCave = [] {
    std::array<std::uint8_t, 67> bytes{};
    bytes.fill(0xCC);
    return bytes;
}();
constexpr std::array<std::uint8_t, 67> IndestructibleHelper{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x4C, 0x24, 0x20,
    0xE8, 0xF2, 0x5C, 0xF0, 0xFF, 0x85, 0xC0, 0x75, 0x2C,
    0x48, 0x8B, 0x4C, 0x24, 0x20, 0x45, 0x33, 0xC0, 0xBA,
    0x98, 0x00, 0x00, 0x00, 0xE8, 0xBC, 0x77, 0xE8, 0xFF,
    0x85, 0xC0, 0x7E, 0x14, 0x48, 0x8B, 0x4C, 0x24, 0x20,
    0xE8, 0xEE, 0x72, 0xE8, 0xFF, 0x85, 0xC0, 0x0F, 0x9F,
    0xC0, 0x0F, 0xB6, 0xC0, 0xEB, 0x02, 0x33, 0xC0, 0x48,
    0x83, 0xC4, 0x28, 0xC3
};

struct ResolvedTypeCache {
    const void* dataTables{};
    const void* records{};
    std::uint64_t recordCount{};
    std::array<std::int32_t, MaximumExcludedItemTypes> ids{};
    std::size_t idCount{};
    std::size_t unresolvedCount{};
};

using CheckItemTypeFn = std::int32_t(__fastcall*)(
    const void*, std::int32_t) noexcept;
using GetItemContextFn = std::uint8_t(__fastcall*)(const void*) noexcept;
using GetDataTablesFn = std::uint8_t*(__fastcall*)(std::uint8_t) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
CheckItemTypeFn OriginalCheckItemType{};
GetItemContextFn GetItemContext{};
GetDataTablesFn GetDataTables{};
std::atomic<std::uint64_t> ExcludedEligibleItems{};
std::atomic<std::uint32_t> ResolvedTypeCount{};
std::atomic<std::uint32_t> UnresolvedTypeCount{};
std::atomic<std::uint64_t> DiagnosticMessages{};
std::atomic_flag UnresolvedWarningLogged = ATOMIC_FLAG_INIT;
thread_local ResolvedTypeCache TypeCache{};
thread_local const void* PendingGateItem{};
thread_local bool PendingGateExcluded{};
thread_local bool PendingGateWasWeapon{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-ethereal-item-rules",
    .name = "Ethereal Item Rules",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description = "Controls ethereal item chance and eligibility.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

template<class T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

void ResetState() noexcept {
    OriginalCheckItemType = nullptr;
    GetItemContext = nullptr;
    GetDataTables = nullptr;
    ExcludedEligibleItems.store(0, std::memory_order_relaxed);
    ResolvedTypeCount.store(0, std::memory_order_relaxed);
    UnresolvedTypeCount.store(0, std::memory_order_relaxed);
    DiagnosticMessages.store(0, std::memory_order_relaxed);
    UnresolvedWarningLogged.clear(std::memory_order_relaxed);
    TypeCache = {};
    PendingGateItem = nullptr;
    PendingGateExcluded = false;
    PendingGateWasWeapon = false;
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "EtherealItemRules: configuration exceeds 65535 bytes."
            : "EtherealItemRules: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("EtherealItemRules: invalid TOML (")
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
    const auto message = std::string("EtherealItemRules: ") + label
        + " signature mismatch; no mutation was started.";
    Context->LogError(message.c_str());
    return false;
}

auto ValidateRuntime() noexcept -> bool {
    if (!Settings.enabled) return true;
    bool valid = true;
    if (HasExcludedItemTypes(Settings)) {
        valid = Check(
            CheckItemTypeRva,
            ExpectedCheckItemType,
            "item-type entry") && valid;
        valid = Check(
            GetItemContextRva,
            ExpectedGetItemContext,
            "item-context helper") && valid;
        valid = Check(
            GetDataTablesRva,
            ExpectedGetDataTables,
            "data-table helper") && valid;
    }
    if (PatchChance(Settings)) {
        valid = Check(
            EtherealChanceRva,
            ExpectedEtherealChance,
            "ethereal chance byte") && valid;
    }
    if (PatchSetItems(Settings)) {
        valid = Check(
            SetQualityBranchRva,
            ExpectedSetQualityBranch,
            "set-quality branch") && valid;
    }
    if (PatchIndestructibleItems(Settings)) {
        valid = Check(
            DurabilityEligibilityCallRva,
            ExpectedDurabilityEligibilityCall,
            "durability eligibility call") && valid;
        valid = Check(
            IndestructibleHelperCaveRva,
            ExpectedIndestructibleHelperCave,
            "indestructible helper cave") && valid;
    }
    return valid;
}

auto ShouldLogDiagnostic() noexcept -> bool {
    if (!Settings.diagnosticsEnabled || !Context) return false;
    const auto ordinal = DiagnosticMessages.fetch_add(
        1, std::memory_order_relaxed) + 1;
    return ordinal <= 8 || ordinal % 100 == 0;
}

auto RefreshTypeCache(const void* item) noexcept -> bool {
    if (!item || !GetItemContext || !GetDataTables) return false;
    const auto dataContext = GetItemContext(item);
    auto* dataTables = GetDataTables(dataContext);
    if (!dataTables) return false;

    const auto* records = *reinterpret_cast<const std::uint8_t* const*>(
        dataTables + ItemTypesRecordsOffset);
    const auto recordCount = *reinterpret_cast<const std::uint64_t*>(
        dataTables + ItemTypesCountOffset);
    if (!records || recordCount == 0 || recordCount > 4096) return false;
    if (TypeCache.dataTables == dataTables
        && TypeCache.records == records
        && TypeCache.recordCount == recordCount) {
        return true;
    }

    TypeCache = {};
    TypeCache.dataTables = dataTables;
    TypeCache.records = records;
    TypeCache.recordCount = recordCount;
    for (std::size_t index = 0;
        index < Settings.exclusions.itemTypeCount;
        ++index) {
        const auto id = FindItemTypeId(
            records,
            recordCount,
            ItemTypeRecordStride,
            Settings.exclusions.itemTypes[index]);
        if (id < 0) {
            ++TypeCache.unresolvedCount;
            continue;
        }
        TypeCache.ids[TypeCache.idCount++] = id;
    }

    ResolvedTypeCount.store(
        static_cast<std::uint32_t>(TypeCache.idCount),
        std::memory_order_relaxed);
    UnresolvedTypeCount.store(
        static_cast<std::uint32_t>(TypeCache.unresolvedCount),
        std::memory_order_relaxed);
    if (TypeCache.unresolvedCount != 0
        && !UnresolvedWarningLogged.test_and_set(std::memory_order_relaxed)) {
        Context->LogWarn(
            "EtherealItemRules: one or more exclusion codes do not exist "
            "in the active itemtypes.txt table.");
    }
    if (ShouldLogDiagnostic()) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "EtherealItemRules: resolved %zu exclusion type(s); %zu code(s) were not found in itemtypes.txt.",
            TypeCache.idCount,
            TypeCache.unresolvedCount);
        Context->LogInfo(message);
    }
    return true;
}

auto IsExcluded(const void* item) noexcept -> bool {
    if (!HasExcludedItemTypes(Settings) || !RefreshTypeCache(item)) {
        return false;
    }
    for (std::size_t index = 0; index < TypeCache.idCount; ++index) {
        if (OriginalCheckItemType(item, TypeCache.ids[index]) != 0) {
            return true;
        }
    }
    return false;
}

std::int32_t __fastcall HookCheckItemType(
    const void* item,
    std::int32_t itemType
) noexcept {
    const auto result = OriginalCheckItemType(item, itemType);
    const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress())
        - Base;

    if (returnRva == EtherealWeaponCheckReturnRva) {
        PendingGateItem = item;
        PendingGateExcluded = IsExcluded(item);
        PendingGateWasWeapon = result != 0;
        return PendingGateExcluded ? 0 : result;
    }
    if (returnRva == EtherealArmorCheckReturnRva) {
        const bool excluded = PendingGateItem == item
            ? PendingGateExcluded
            : IsExcluded(item);
        const bool wasEligible = (PendingGateItem == item
            && PendingGateWasWeapon) || result != 0;
        PendingGateItem = nullptr;
        PendingGateExcluded = false;
        PendingGateWasWeapon = false;
        if (excluded && wasEligible) {
            const auto prevented = ExcludedEligibleItems.fetch_add(
                1, std::memory_order_relaxed) + 1;
            if (ShouldLogDiagnostic()) {
                char message[192]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "EtherealItemRules: prevented eligible item #%llu from rolling ethereal.",
                    static_cast<unsigned long long>(prevented));
                Context->LogInfo(message);
            }
            return 0;
        }
    }
    return result;
}

auto InstallExclusionHook() noexcept -> bool {
    if (!HasExcludedItemTypes(Settings)) return true;
    GetItemContext = At<GetItemContextFn>(GetItemContextRva);
    GetDataTables = At<GetDataTablesFn>(GetDataTablesRva);
    if (!Context->InstallInlineHook(
            CheckItemTypeRva,
            ExpectedCheckItemType.data(),
            static_cast<std::uint32_t>(ExpectedCheckItemType.size()),
            HookCheckItemType,
            &OriginalCheckItemType)) {
        Context->LogError(
            "EtherealItemRules: item-type hook installation failed.");
        return false;
    }
    return true;
}

auto InstallRulePatches() noexcept -> bool {
    if (PatchIndestructibleItems(Settings)) {
        if (!Context->PatchBytes(
                IndestructibleHelperCaveRva,
                ExpectedIndestructibleHelperCave.data(),
                static_cast<std::uint32_t>(
                    ExpectedIndestructibleHelperCave.size()),
                IndestructibleHelper.data(),
                static_cast<std::uint32_t>(IndestructibleHelper.size()))) {
            Context->LogError(
                "EtherealItemRules: indestructible helper patch failed.");
            return false;
        }
        if (!Context->PatchCallRel32(
                DurabilityEligibilityCallRva,
                ExpectedDurabilityEligibilityCall.data(),
                static_cast<std::uint32_t>(
                    ExpectedDurabilityEligibilityCall.size()),
                IndestructibleHelperCaveRva)) {
            Context->LogError(
                "EtherealItemRules: durability eligibility patch failed.");
            return false;
        }
    }
    if (PatchSetItems(Settings)
        && !Context->PatchNop(
            SetQualityBranchRva,
            ExpectedSetQualityBranch.data(),
            static_cast<std::uint32_t>(ExpectedSetQualityBranch.size()),
            static_cast<std::uint32_t>(ExpectedSetQualityBranch.size()))) {
        Context->LogError("EtherealItemRules: set eligibility patch failed.");
        return false;
    }
    if (PatchChance(Settings)
        && !Context->PatchWriteU8(
            EtherealChanceRva,
            ExpectedEtherealChance.data(),
            static_cast<std::uint32_t>(ExpectedEtherealChance.size()),
            Settings.generation.chancePercent)) {
        Context->LogError("EtherealItemRules: chance byte patch failed.");
        return false;
    }
    return true;
}

void FormatConfiguredTypes(char* output, std::size_t size) noexcept {
    if (!output || size == 0) return;
    output[0] = '\0';
    std::size_t used{};
    for (std::size_t index = 0;
        index < Settings.exclusions.itemTypeCount;
        ++index) {
        const auto* separator = index == 0 ? "" : ",";
        const auto written = std::snprintf(
            output + used,
            size - used,
            "%s%s",
            separator,
            Settings.exclusions.itemTypes[index].text.data());
        if (written < 0 || static_cast<std::size_t>(written) >= size - used) {
            output[size - 1] = '\0';
            return;
        }
        used += static_cast<std::size_t>(written);
    }
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char types[384]{};
    FormatConfiguredTypes(types, sizeof(types));
    char message[900]{};
    std::snprintf(
        message,
        sizeof(message),
        "Ethereal Item Rules 1.0.0: enabled=%s; exclusions=%s types=[%s] "
        "resolved=%u unresolved=%u prevented=%llu; generation=%s "
        "chance=%u%% set=%s indestructible=%s; diagnostics=%s.",
        Settings.enabled ? "true" : "false",
        Settings.exclusions.enabled ? "enabled" : "disabled",
        types,
        ResolvedTypeCount.load(std::memory_order_relaxed),
        UnresolvedTypeCount.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(
            ExcludedEligibleItems.load(std::memory_order_relaxed)),
        Settings.generation.enabled ? "enabled" : "disabled",
        static_cast<unsigned>(Settings.generation.chancePercent),
        Settings.generation.allowSetItems ? "allowed" : "vanilla-blocked",
        Settings.generation.allowIndestructibleItems
            ? "allowed"
            : "vanilla-filtered",
        Settings.diagnosticsEnabled ? "enabled" : "disabled");
    command->plugin->WriteConsoleMessage(message);
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
            "EtherealItemRules: D2R executable base is unavailable.");
        return false;
    }
    if (!ReadConfiguration()) return false;
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "EtherealItemRules: only D2R build 92777 is supported.");
        return false;
    }
    if (!ValidateRuntime()) return false;
    if (!InstallRulePatches() || !InstallExclusionHook()) return false;

    if (!context->RegisterConsoleCommand(
            "ethereal-item-rules",
            Status,
            "Show ethereal item rules and session counters.")) {
        context->LogWarn(
            "EtherealItemRules: status command could not be registered.");
    }

    char message[448]{};
    std::snprintf(
        message,
        sizeof(message),
        "Ethereal Item Rules 1.0.0 by RuffnecKk loaded: enabled=%s; exclusions=%s "
        "(%zu types); generation=%s; chance=%u%%; set=%s; "
        "indestructible=%s; diagnostics=%s.",
        Settings.enabled ? "true" : "false",
        Settings.exclusions.enabled ? "enabled" : "disabled",
        Settings.exclusions.itemTypeCount,
        Settings.generation.enabled ? "enabled" : "disabled",
        static_cast<unsigned>(Settings.generation.chancePercent),
        Settings.generation.allowSetItems ? "allowed" : "vanilla-blocked",
        Settings.generation.allowIndestructibleItems
            ? "allowed"
            : "vanilla-filtered",
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

} // namespace RuffnecKk::EtherealItemRules
