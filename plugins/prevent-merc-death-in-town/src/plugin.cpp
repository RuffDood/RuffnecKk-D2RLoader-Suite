#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace RuffnecKk::PreventMercDeathInTown {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::uintptr_t ApplyMonsterStatRegenRva = 0x448C00;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitBaseStatRva = 0x2F48C0;
constexpr std::uintptr_t GetUnitBaseStatSignatureRva = GetUnitBaseStatRva + 5;
constexpr std::uintptr_t CheckLifeStateMaskRva = 0x335E80;
constexpr std::uintptr_t GetUnitRoomRva = 0x34B440;
constexpr std::uintptr_t IsRoomInTownRva = 0x2F0750;
constexpr std::uintptr_t SetEventRva = 0x48B720;
constexpr std::ptrdiff_t GameFrameOffset = 0x170;
constexpr std::uint32_t MonsterUnitType = 1;
constexpr std::int32_t HitpointsStat = 6;
constexpr std::int32_t HitpointRegenStat = 74;
constexpr std::int32_t StatRegenEvent = 3;
constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uint64_t MaximumDiagnosticLogs = 8;

constexpr std::array<std::uint8_t, 32> ExpectedApplyMonsterStatRegen{
    0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x90,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xB6, 0x26,
    0x58, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44,
    0x24, 0x70, 0x48, 0x8B, 0xFA, 0x45, 0x33, 0xC0
};
constexpr std::array<std::uint8_t, 32> ExpectedGetUnitStat{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
    0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9
};
constexpr std::array<std::uint8_t, 5> ExpectedGetUnitBaseStatEntry{
    0x48, 0x89, 0x5C, 0x24, 0x10
};
constexpr std::array<std::uint8_t, 50> ExpectedGetUnitBaseStatBody{
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x0F, 0xB7, 0xE8, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
    0x48, 0x85, 0xC9, 0x75, 0x2A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x00,
    0xD2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x33, 0xC0
};
constexpr std::array<std::uint8_t, 32> ExpectedCheckLifeStateMask{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0xE8, 0x52, 0x42, 0x01, 0x00, 0x0F, 0xB6,
    0xC8, 0xE8, 0xFA, 0xAB, 0xFC, 0xFF, 0x48, 0x8B,
    0xCB, 0x48, 0x8B, 0x90, 0xD0, 0x03, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 32> ExpectedGetUnitRoom{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01
};
constexpr std::array<std::uint8_t, 32> ExpectedIsRoomInTown{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
    0x48, 0x8B, 0x49, 0x18, 0xE8, 0x57, 0x08, 0x07,
    0x00, 0x8B, 0xC8, 0x48, 0x83, 0xC4, 0x28, 0xE9
};
constexpr std::array<std::uint8_t, 32> ExpectedSetEvent{
    0x48, 0x83, 0xEC, 0x48, 0x8B, 0x84, 0x24, 0x80,
    0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x38, 0x8B,
    0x44, 0x24, 0x78, 0x89, 0x44, 0x24, 0x30, 0x8B,
    0x44, 0x24, 0x70, 0x89, 0x44, 0x24, 0x28, 0x48
};

struct UnitHeader {
    std::uint32_t unitType{};
    std::uint32_t classId{};
};
static_assert(offsetof(UnitHeader, unitType) == 0x00);
static_assert(offsetof(UnitHeader, classId) == 0x04);

using ApplyMonsterStatRegenFn = void(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using CheckLifeStateMaskFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitRoomFn = void*(__fastcall*)(void*) noexcept;
using IsRoomInTownFn = std::int32_t(__fastcall*)(void*) noexcept;
using SetEventFn = void(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t
) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
ApplyMonsterStatRegenFn OriginalApplyMonsterStatRegen{};
GetUnitStatFn GetUnitStat{};
GetUnitStatFn GetUnitBaseStat{};
CheckLifeStateMaskFn CheckLifeStateMask{};
GetUnitRoomFn GetUnitRoom{};
IsRoomInTownFn IsRoomInTown{};
SetEventFn ScheduleEvent{};
std::atomic<std::uint64_t> PreventedDeaths{};
std::atomic<std::uint64_t> DiagnosticLogs{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-prevent-merc-death-in-town",
    .name = "Prevent Merc Death in Town",
    .version = "0.1.1",
    .author = "RuffnecKk",
    .description = "Prevents mercenaries from dying to lingering damage while in town.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
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
            ? "PreventMercDeathInTown: configuration exceeds 65535 bytes."
            : "PreventMercDeathInTown: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string(
            "PreventMercDeathInTown: invalid TOML (")
            + error + "); no hook was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

auto ValidateComposableBaseStatEntry() noexcept -> bool {
    if (!Context->CheckExpectedBytes(
            GetUnitBaseStatSignatureRva,
            ExpectedGetUnitBaseStatBody.data(),
            static_cast<std::uint32_t>(ExpectedGetUnitBaseStatBody.size()))) {
        Context->LogError(
            "PreventMercDeathInTown: base-stat helper body signature mismatch.");
        return false;
    }
    if (std::memcmp(
            reinterpret_cast<const void*>(Base + GetUnitBaseStatRva),
            ExpectedGetUnitBaseStatEntry.data(),
            ExpectedGetUnitBaseStatEntry.size()) == 0) {
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
            "PreventMercDeathInTown: Diagnostics v1 is required to validate the shared base-stat entry.");
        return false;
    }

    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = GetUnitBaseStatRva,
        .expected = ExpectedGetUnitBaseStatEntry.data(),
        .expectedSize = static_cast<std::uint32_t>(
            ExpectedGetUnitBaseStatEntry.size()),
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
            "PreventMercDeathInTown: base-stat entry has an untracked or non-composable modification.");
        return false;
    }

    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "PreventMercDeathInTown: composing through loader-owned base-stat hook (%.*s).",
        63,
        status.ownerPluginId);
    Context->LogInfo(message);
    return true;
}

auto ValidateRuntime() noexcept -> bool {
    // Validate every native dependency before the single component mutation.
    return Context->CheckExpectedBytes(
            ApplyMonsterStatRegenRva,
            ExpectedApplyMonsterStatRegen.data(),
            static_cast<std::uint32_t>(ExpectedApplyMonsterStatRegen.size()))
        && Context->CheckExpectedBytes(
            GetUnitStatRva,
            ExpectedGetUnitStat.data(),
            static_cast<std::uint32_t>(ExpectedGetUnitStat.size()))
        && ValidateComposableBaseStatEntry()
        && Context->CheckExpectedBytes(
            CheckLifeStateMaskRva,
            ExpectedCheckLifeStateMask.data(),
            static_cast<std::uint32_t>(ExpectedCheckLifeStateMask.size()))
        && Context->CheckExpectedBytes(
            GetUnitRoomRva,
            ExpectedGetUnitRoom.data(),
            static_cast<std::uint32_t>(ExpectedGetUnitRoom.size()))
        && Context->CheckExpectedBytes(
            IsRoomInTownRva,
            ExpectedIsRoomInTown.data(),
            static_cast<std::uint32_t>(ExpectedIsRoomInTown.size()))
        && Context->CheckExpectedBytes(
            SetEventRva,
            ExpectedSetEvent.data(),
            static_cast<std::uint32_t>(ExpectedSetEvent.size()));
}

auto IsLethalHirelingTickInTown(void* game, void* unit) noexcept -> bool {
    if (!game || !unit) return false;
    __try {
        const auto& header = *static_cast<const UnitHeader*>(unit);
        if (header.unitType != MonsterUnitType
            || !IsHirelingClass(header.classId)) {
            return false;
        }

        auto regeneration = GetUnitStat(unit, HitpointRegenStat, 0);
        if (CheckLifeStateMask(unit)) {
            regeneration -= GetUnitBaseStat(unit, HitpointRegenStat, 0);
        }
        const auto hitpoints = GetUnitStat(unit, HitpointsStat, 0);
        if (!IsProjectedLethal(hitpoints, regeneration)) return false;

        auto* room = GetUnitRoom(unit);
        return room && IsRoomInTown(room) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall HookApplyMonsterStatRegen(
    void* game,
    void* unit,
    std::int32_t a3,
    std::int32_t a4
) noexcept {
    if (!IsLethalHirelingTickInTown(game, unit)) {
        OriginalApplyMonsterStatRegen(game, unit, a3, a4);
        return;
    }

    __try {
        const auto frame = *reinterpret_cast<const std::int32_t*>(
            static_cast<const std::uint8_t*>(game) + GameFrameOffset);
        ScheduleEvent(game, unit, StatRegenEvent, frame + 1, 0, 0);
        const auto prevented = PreventedDeaths.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (Settings.diagnosticsEnabled
            && DiagnosticLogs.fetch_add(1, std::memory_order_relaxed)
                < MaximumDiagnosticLogs) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "PreventMercDeathInTown: prevented lethal town tick (total=%llu).",
                static_cast<unsigned long long>(prevented));
            Context->LogInfo(message);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OriginalApplyMonsterStatRegen(game, unit, a3, a4);
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
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "Prevent Merc Death in Town 0.1.1: %s; diagnostics=%s; "
        "prevented lethal ticks=%llu.",
        Settings.enabled ? "active" : "disabled",
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        static_cast<unsigned long long>(
            PreventedDeaths.load(std::memory_order_relaxed)));
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
    Base = 0;
    Settings = {};
    PreventedDeaths.store(0, std::memory_order_relaxed);
    DiagnosticLogs.store(0, std::memory_order_relaxed);

    if (!ReadConfiguration()) return false;
    if (!Settings.enabled) {
        context->LogInfo(
            "Prevent Merc Death in Town 0.1.1 by RuffnecKk loaded disabled; no hook or service registered.");
        return true;
    }

    Base = context->exeBase;

    if (!Base) {
        context->LogError(
            "PreventMercDeathInTown: D2R executable base is unavailable.");
        return false;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "PreventMercDeathInTown: only D2R build 92777 is supported.");
        return false;
    }
    if (!ValidateRuntime()) {
        context->LogError(
            "PreventMercDeathInTown: 92777 preflight failed; no hook installed.");
        return false;
    }

    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    GetUnitBaseStat = At<GetUnitStatFn>(GetUnitBaseStatRva);
    CheckLifeStateMask = At<CheckLifeStateMaskFn>(CheckLifeStateMaskRva);
    GetUnitRoom = At<GetUnitRoomFn>(GetUnitRoomRva);
    IsRoomInTown = At<IsRoomInTownFn>(IsRoomInTownRva);
    ScheduleEvent = At<SetEventFn>(SetEventRva);
    if (!context->InstallInlineHook(
            ApplyMonsterStatRegenRva,
            ExpectedApplyMonsterStatRegen.data(),
            static_cast<std::uint32_t>(ExpectedApplyMonsterStatRegen.size()),
            HookApplyMonsterStatRegen,
            &OriginalApplyMonsterStatRegen)) {
        context->LogError(
            "PreventMercDeathInTown: stat-regen hook failed.");
        return false;
    }

    if (!context->RegisterConsoleCommand(
            "prevent-merc-death-in-town",
            Status,
            "Show persistent-damage protection counters.")) {
        context->LogWarn(
            "PreventMercDeathInTown: status command could not be registered.");
    }
    context->LogInfo(
        "Prevent Merc Death in Town 0.1.1 by RuffnecKk active for D2R 3.2.92777.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ScheduleEvent = nullptr;
    IsRoomInTown = nullptr;
    GetUnitRoom = nullptr;
    CheckLifeStateMask = nullptr;
    GetUnitBaseStat = nullptr;
    GetUnitStat = nullptr;
    OriginalApplyMonsterStatRegen = nullptr;
    PreventedDeaths.store(0, std::memory_order_relaxed);
    DiagnosticLogs.store(0, std::memory_order_relaxed);
    Settings = {};
    Base = 0;
    Context = nullptr;
}

} // namespace RuffnecKk::PreventMercDeathInTown
