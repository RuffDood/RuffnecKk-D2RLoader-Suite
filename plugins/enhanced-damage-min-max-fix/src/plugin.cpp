#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace RuffnecKk::EnhancedDamageMinMaxFix {
namespace {

constexpr std::uintptr_t EvaluateAndUpdateStatRva = 0x2FA430;
constexpr std::uintptr_t GetTotalStatRva = 0x2F9B10;
constexpr std::uintptr_t UpdateUnitStatRva = 0x2F9DB0;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t CheckItemTypeRva = 0x373890;
constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uint64_t MaximumDiagnosticLogs = 8;

constexpr std::size_t StatListUnitOffset = 0x00;
constexpr std::size_t StatListOwnerTypeOffset = 0x08;
constexpr std::size_t StatListOwnerOffset = 0xA0;
constexpr std::size_t ItemStatCostOperationOffset = 0x50;

constexpr std::array<std::uint8_t, 15> ExpectedEvaluateAndUpdateStat{
    0x4C, 0x89, 0x4C, 0x24, 0x20,
    0x4C, 0x89, 0x44, 0x24, 0x18,
    0x89, 0x54, 0x24, 0x10,
    0x53
};
constexpr std::array<std::uint8_t, 14> ExpectedGetTotalStat{
    0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
    0x83, 0xEC, 0x20, 0x48, 0x63, 0x41, 0x1C
};
constexpr std::array<std::uint8_t, 20> ExpectedUpdateUnitStat{
    0x48, 0x89, 0x5C, 0x24, 0x08,
    0x48, 0x89, 0x6C, 0x24, 0x10,
    0x48, 0x89, 0x74, 0x24, 0x18,
    0x57, 0x41, 0x56, 0x41, 0x57
};
constexpr std::array<std::uint8_t, 13> ExpectedGetUnitType{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9,
    0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30
};
constexpr std::array<std::uint8_t, 15> ExpectedCheckItemTypeEntry{
    0x48, 0x89, 0x5C, 0x24, 0x10,
    0x48, 0x89, 0x6C, 0x24, 0x18,
    0x48, 0x89, 0x74, 0x24, 0x20
};
constexpr std::array<std::uint8_t, 9> ExpectedCheckItemTypeBody{
    0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20
};

using EvaluateAndUpdateStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, void*, void*) noexcept;
using GetTotalStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, void*) noexcept;
using UpdateUnitStatFn = void(__fastcall*)(
    void*, std::int32_t, std::int32_t, void*, void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(const void*) noexcept;
using CheckItemTypeFn = std::int32_t(__fastcall*)(
    const void*, std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
EvaluateAndUpdateStatFn OriginalEvaluateAndUpdateStat{};
GetTotalStatFn GetTotalStat{};
UpdateUnitStatFn UpdateUnitStat{};
GetUnitTypeFn GetUnitType{};
CheckItemTypeFn CheckItemType{};
thread_local bool CorrectionWriteActive{};

std::atomic<std::uint64_t> RestoredUpdates{};
std::atomic<std::uint64_t> RestoredMaximumComponents{};
std::atomic<std::uint64_t> RestoredMinimumComponents{};
std::atomic<std::uint64_t> WeaponUpdatesLeftVanilla{};
std::atomic<std::uint64_t> PostWriteVerificationFailures{};
std::atomic<std::uint64_t> DiagnosticLogs{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-enhanced-damage-min-max-fix",
    .name = "Enhanced Damage Min/Max Fix",
    .version = "1.2.2",
    .author = "RuffnecKk",
    .description = "Restores off-weapon Enhanced Damage when an item also adds minimum or maximum damage.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "EnhancedDamageMinMaxFix: configuration exceeds 65535 bytes."
            : "EnhancedDamageMinMaxFix: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string(
            "EnhancedDamageMinMaxFix: invalid TOML (")
            + error + "); no hook was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

template<class T>
auto ReadAt(const void* address, std::size_t offset) noexcept -> T {
    return *reinterpret_cast<const T*>(
        static_cast<const std::uint8_t*>(address) + offset);
}

struct CorrectionWriteScope {
    CorrectionWriteScope() noexcept { CorrectionWriteActive = true; }
    ~CorrectionWriteScope() { CorrectionWriteActive = false; }
};

void ResetTelemetry() noexcept {
    RestoredUpdates.store(0, std::memory_order_relaxed);
    RestoredMaximumComponents.store(0, std::memory_order_relaxed);
    RestoredMinimumComponents.store(0, std::memory_order_relaxed);
    WeaponUpdatesLeftVanilla.store(0, std::memory_order_relaxed);
    PostWriteVerificationFailures.store(0, std::memory_order_relaxed);
    DiagnosticLogs.store(0, std::memory_order_relaxed);
}

auto ResolveEffectiveItem(const void* statList) noexcept -> void* {
    auto* activeUnit = ReadAt<void*>(statList, StatListUnitOffset);
    if (activeUnit && GetUnitType(activeUnit) == ItemUnitType) return activeUnit;
    auto* originalOwner = ReadAt<void*>(statList, StatListOwnerOffset);
    if (originalOwner && GetUnitType(originalOwner) == ItemUnitType) {
        return originalOwner;
    }
    return nullptr;
}

void LogCorrection(
    std::int32_t packedStat,
    std::int32_t retainedValue,
    std::int32_t evaluatedValue
) noexcept {
    if (!Context || !Settings.diagnosticsEnabled
        || DiagnosticLogs.fetch_add(1, std::memory_order_relaxed)
            >= MaximumDiagnosticLogs) {
        return;
    }
    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "Enhanced Damage Min/Max Fix 1.2.1 restored an off-weapon update "
        "(stat=%u, retained=%d, evaluated=%d).",
        static_cast<unsigned>(static_cast<std::uint32_t>(packedStat) >> 16U),
        retainedValue,
        evaluatedValue);
    Context->LogInfo(message);
}

auto __fastcall HookEvaluateAndUpdateStat(
    void* statList,
    std::int32_t packedStat,
    void* itemStatCost,
    void* callbackUnit
) noexcept -> std::int32_t {
    const auto evaluatedValue = OriginalEvaluateAndUpdateStat(
        statList, packedStat, itemStatCost, callbackUnit);
    if (CorrectionWriteActive || !statList || !itemStatCost
        || !IsEnhancedDamagePackedStat(packedStat)) {
        return evaluatedValue;
    }

    const auto ownerType = ReadAt<std::int32_t>(
        statList, StatListOwnerTypeOffset);
    const auto operation = ReadAt<std::uint8_t>(
        itemStatCost, ItemStatCostOperationOffset);
    if (ownerType != ItemUnitType) return evaluatedValue;

    auto* effectiveItem = ResolveEffectiveItem(statList);
    if (!effectiveItem) return evaluatedValue;
    const bool effectiveItemIsWeapon =
        CheckItemType(effectiveItem, WeaponItemTypeId) != 0;
    if (effectiveItemIsWeapon) {
        WeaponUpdatesLeftVanilla.fetch_add(1, std::memory_order_relaxed);
        return evaluatedValue;
    }

    const auto retainedValue = GetTotalStat(
        statList, packedStat, itemStatCost);
    if (!ShouldRestoreSuppressedUpdate(
            ownerType,
            operation,
            packedStat,
            effectiveItemIsWeapon,
            evaluatedValue,
            retainedValue)) {
        return evaluatedValue;
    }

    {
        const CorrectionWriteScope scope;
        UpdateUnitStat(
            statList, packedStat, evaluatedValue, itemStatCost, callbackUnit);
    }
    const auto repairedValue = GetTotalStat(
        statList, packedStat, itemStatCost);
    if (repairedValue != evaluatedValue) {
        PostWriteVerificationFailures.fetch_add(1, std::memory_order_relaxed);
        return evaluatedValue;
    }

    RestoredUpdates.fetch_add(1, std::memory_order_relaxed);
    const auto stat = static_cast<std::uint32_t>(packedStat) >> 16U;
    if (stat == static_cast<std::uint32_t>(ItemMaxDamagePercentStat)) {
        RestoredMaximumComponents.fetch_add(1, std::memory_order_relaxed);
    } else {
        RestoredMinimumComponents.fetch_add(1, std::memory_order_relaxed);
    }
    LogCorrection(packedStat, retainedValue, evaluatedValue);
    return evaluatedValue;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "Enhanced Damage Min/Max Fix 1.2.1: %s; diagnostics=%s; restored=%llu; "
        "maximum=%llu; minimum=%llu; weapons left vanilla=%llu; "
        "post-write failures=%llu.",
        Settings.enabled ? "active" : "disabled",
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        static_cast<unsigned long long>(
            RestoredUpdates.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RestoredMaximumComponents.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RestoredMinimumComponents.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            WeaponUpdatesLeftVanilla.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PostWriteVerificationFailures.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

auto Check(
    std::uintptr_t rva,
    const std::uint8_t* expected,
    std::size_t size,
    const char* label
) noexcept -> bool {
    if (Context->CheckExpectedBytes(
            rva, expected, static_cast<std::uint32_t>(size))) {
        return true;
    }
    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "EnhancedDamageMinMaxFix: %s signature mismatch.",
        label);
    Context->LogError(message);
    return false;
}

auto ValidateComposableItemTypeEntry() noexcept -> bool {
    if (!Check(
            CheckItemTypeRva + ExpectedCheckItemTypeEntry.size(),
            ExpectedCheckItemTypeBody.data(),
            ExpectedCheckItemTypeBody.size(),
            "item-type helper body")) {
        return false;
    }
    if (Context->CheckExpectedBytes(
            CheckItemTypeRva,
            ExpectedCheckItemTypeEntry.data(),
            static_cast<std::uint32_t>(ExpectedCheckItemTypeEntry.size()))) {
        return true;
    }

    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (Context->QueryService(
            D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version,
            &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(
            diagnostics,
            D2RL::DiagnosticsServiceV1RequiredSize)
        || !diagnostics->queryHookStatus) {
        Context->LogError(
            "EnhancedDamageMinMaxFix: Diagnostics v1 is required to validate the shared item-type entry.");
        return false;
    }

    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = CheckItemTypeRva,
        .expected = ExpectedCheckItemTypeEntry.data(),
        .expectedSize = static_cast<std::uint32_t>(
            ExpectedCheckItemTypeEntry.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    if (diagnostics->queryHookStatus(Context, &query, &status)
            != D2RL::Diagnostics::Result::Success
        || status.state != D2RL::Diagnostics::ModificationState::Tracked
        || status.kind != D2RL::Diagnostics::ModificationKind::InlineHook
        || status.ownerCount == 0) {
        Context->LogError(
            "EnhancedDamageMinMaxFix: item-type entry has an untracked or non-composable modification.");
        return false;
    }

    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "EnhancedDamageMinMaxFix: composing through loader-owned item-type hook (%.*s).",
        63,
        status.ownerPluginId);
    Context->LogInfo(message);
    return true;
}

auto ValidateRuntime() noexcept -> bool {
    return Check(
            EvaluateAndUpdateStatRva,
            ExpectedEvaluateAndUpdateStat.data(),
            ExpectedEvaluateAndUpdateStat.size(),
            "evaluate/update hook")
        && Check(
            GetTotalStatRva,
            ExpectedGetTotalStat.data(),
            ExpectedGetTotalStat.size(),
            "total-stat helper")
        && Check(
            UpdateUnitStatRva,
            ExpectedUpdateUnitStat.data(),
            ExpectedUpdateUnitStat.size(),
            "update-unit-stat helper")
        && Check(
            GetUnitTypeRva,
            ExpectedGetUnitType.data(),
            ExpectedGetUnitType.size(),
            "unit-type helper")
        && ValidateComposableItemTypeEntry();
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
    Base = 0;
    Settings = {};
    ResetTelemetry();

    if (!ReadConfiguration()) return false;
    if (!Settings.enabled) {
        context->LogInfo(
            "Enhanced Damage Min/Max Fix 1.2.1 by RuffnecKk loaded disabled; no hook or service registered.");
        return true;
    }

    Base = context->exeBase;

    if (!Base) {
        context->LogError(
            "EnhancedDamageMinMaxFix: D2R executable base is unavailable.");
        return false;
    }
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (runtimeBuild == nullptr
        || (std::strcmp(runtimeBuild, "92777") != 0
            && std::strcmp(runtimeBuild, "93847") != 0)) {
        context->LogError(
            "EnhancedDamageMinMaxFix: only D2R builds 92777 and 93847 are supported.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError(
            "EnhancedDamageMinMaxFix: 92777 preflight failed; no hook installed.");
        return false;
    }

    GetTotalStat = At<GetTotalStatFn>(GetTotalStatRva);
    UpdateUnitStat = At<UpdateUnitStatFn>(UpdateUnitStatRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    CheckItemType = At<CheckItemTypeFn>(CheckItemTypeRva);
    if (!context->InstallInlineHook(
            EvaluateAndUpdateStatRva,
            ExpectedEvaluateAndUpdateStat.data(),
            static_cast<std::uint32_t>(ExpectedEvaluateAndUpdateStat.size()),
            HookEvaluateAndUpdateStat,
            &OriginalEvaluateAndUpdateStat)) {
        context->LogError(
            "EnhancedDamageMinMaxFix: evaluate/update hook failed.");
        return false;
    }

    if (!context->RegisterConsoleCommand(
            "enhanced-damage-min-max-fix",
            Status,
            "Show off-weapon Enhanced Damage repair counters.")) {
        context->LogWarn(
            "EnhancedDamageMinMaxFix: status command could not be registered.");
    }
    context->LogInfo(
        "Enhanced Damage Min/Max Fix 1.2.1 by RuffnecKk active for D2R 3.2.92777.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    OriginalEvaluateAndUpdateStat = nullptr;
    CheckItemType = nullptr;
    GetUnitType = nullptr;
    UpdateUnitStat = nullptr;
    GetTotalStat = nullptr;
    ResetTelemetry();
    Settings = {};
    Base = 0;
    Context = nullptr;
}

} // namespace RuffnecKk::EnhancedDamageMinMaxFix
