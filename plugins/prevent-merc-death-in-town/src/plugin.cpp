#include <D2RLPlugin/api.h>
#include <RuffnecKk/native_stat_compat.hpp>
#include <RuffnecKk/tracked_native_transform_d2rl.hpp>

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

constexpr std::uintptr_t ApplyMonsterStatRegenRva = 0x448C00;
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
// STATLIST_GetUnitStat at 0x2F5020 and STATLIST_GetUnitBaseStat at 0x2F48C0
// are admitted by NativeStatCompat.
RuffnecKk::NativeStatCompat::Adapter NativeStats{};

auto NativeStatFailureLabel() noexcept -> const char* {
    using Failure = RuffnecKk::NativeStatCompat::Failure;
    switch (NativeStats.LastFailure()) {
    case Failure::ReadFailed: return "memory read failed";
    case Failure::CanonicalMismatch: return "canonical entry mismatch";
    case Failure::ProviderEncoding: return "provider relay encoding mismatch";
    case Failure::ProviderPointer: return "provider relay target mismatch";
    case Failure::ProviderWitness: return "provider witness mismatch";
    default: return "invalid compatibility contract";
    }
}
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
    .version = "1.1.2",
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

auto ValidateRuntime() noexcept -> bool {
    // Validate every native dependency before the single component mutation.
    if (!NativeStats.BindCurrentProcess(
            Base,
            RuffnecKk::NativeStatCompat::ToMask(
                RuffnecKk::NativeStatCompat::Helper::GetUnitStat)
                | RuffnecKk::NativeStatCompat::ToMask(
                    RuffnecKk::NativeStatCompat::Helper::GetUnitBaseStat),
            RuffnecKk::TrackedNativeTransform::D2RLDiagnosticsContext(Context))) {
        char message[192]{};
        std::snprintf(message, sizeof(message),
            "PreventMercDeathInTown: stat compatibility admission failed (%s).",
            NativeStatFailureLabel());
        Context->LogError(message);
        return false;
    }
    return Context->CheckExpectedBytes(
            ApplyMonsterStatRegenRva,
            ExpectedApplyMonsterStatRegen.data(),
            static_cast<std::uint32_t>(ExpectedApplyMonsterStatRegen.size()))
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

        auto regeneration = NativeStats.GetUnitStat(unit, HitpointRegenStat, 0);
        if (CheckLifeStateMask(unit)) {
            regeneration -= NativeStats.GetUnitBaseStat(unit, HitpointRegenStat, 0);
        }
        const auto hitpoints = NativeStats.GetUnitStat(unit, HitpointsStat, 0);
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
        "Prevent Merc Death in Town 1.1.2: %s; diagnostics=%s; "
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
        "Prevent Merc Death in Town 1.1.2 by RuffnecKk loaded disabled; no hook or service registered.");
        return true;
    }

    Base = context->exeBase;

    if (!Base) {
        context->LogError(
            "PreventMercDeathInTown: D2R executable base is unavailable.");
        return false;
    }
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    char buildMessage[192]{};
    std::snprintf(buildMessage, sizeof(buildMessage),
        "PreventMercDeathInTown: observed D2R build-name=%s; validating the complete native fingerprint.",
        runtimeBuild && runtimeBuild[0] != '\0' ? runtimeBuild : "unknown");
    context->LogInfo(buildMessage);
    if (!ValidateRuntime()) {
        context->LogError(
            "PreventMercDeathInTown: native fingerprint validation failed; no hook installed.");
        return false;
    }

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
        "Prevent Merc Death in Town by RuffnecKk active after complete native fingerprint validation.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ScheduleEvent = nullptr;
    IsRoomInTown = nullptr;
    GetUnitRoom = nullptr;
    CheckLifeStateMask = nullptr;
    NativeStats.Reset();
    OriginalApplyMonsterStatRegen = nullptr;
    PreventedDeaths.store(0, std::memory_order_relaxed);
    DiagnosticLogs.store(0, std::memory_order_relaxed);
    Settings = {};
    Base = 0;
    Context = nullptr;
}

} // namespace RuffnecKk::PreventMercDeathInTown
