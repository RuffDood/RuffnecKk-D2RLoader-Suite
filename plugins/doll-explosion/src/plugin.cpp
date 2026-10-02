#include <D2RLPlugin/api.h>

#include "doll_explosion_policy.hpp"
#include "native_fingerprint.hpp"
#include "damage_cleanup_api.hpp"

#include <RuffnecKk/native_stat_compat.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ruffneckk::doll_explosion;
namespace native = ruffneckk::doll_explosion::native;
namespace cleanup = RuffnecKk::DamageCleanup;
namespace native_stat = RuffnecKk::NativeStatCompat;

constexpr wchar_t ConfigFileName[] = L"ruffneckk-doll-explosion.toml";
constexpr std::int32_t MonsterUnitType = 1;
constexpr std::int32_t MissileUnitType = 3;
constexpr std::int32_t CorpseExplosionMissileId = 117;
constexpr std::int32_t DelayCarrierMissileId = 587;
constexpr std::int32_t DelayCarrierNativeFrames = 10;
constexpr std::int32_t ReviveStateId = 96;
constexpr std::int32_t MaximumLifeStatId = 7;
constexpr std::uint8_t MonStatsDeathDamageMask = 0x10;
constexpr std::uint32_t CorpseExplosionFlags = 0x581;
constexpr std::size_t DamageRecordSize = 0x180;
constexpr std::size_t MaximumCarrierSlots = 256;
constexpr auto RequiredStatHelpers = native_stat::ToMask(
    native_stat::Helper::GetUnitStat);

constexpr char DefaultConfig[] = R"toml(# Doll Explosion
# Delays undead Doll death blasts and lets you choose their physical damage.
# Defaults use Project Diablo 2 (PD2) Season 13's fixed damage ranges,
# a 25-frame delay and a radius of 4 native tiles.
#
# Edit the values below, then restart the game for the changes to take effect.
# Enter doll-explosion in the Loader console to check the active settings.
# Keep all sections, including the damage formula you are not currently using.
# Unknown keys, duplicate monster IDs and invalid values prevent plugin loading.

# Configuration format version, not the plugin's release version. Leave at 1.
config_version = 1

[targets]
# Retail D2R undead Doll variants: bonefetish1 through bonefetish7.
# These numbers are monster IDs from monstats.txt, not monster names.
# If your mod changes these IDs, replace them with that mod's equivalent IDs.
# Use 1–64 unique IDs, each an integer from 0 to 65535.
monstats_ids = [212, 213, 214, 215, 216, 690, 691]

[explosion]
# Time between the Doll's death and its blast. D2R uses 25 frames per second.
# Examples: 0 = immediate, 25 = 1 second, 50 = 2 seconds.
# Whole numbers only; allowed range: 0–32767 frames.
delay_frames = 25

# Blast radius in native game tiles. A larger value reaches farther.
# This changes the blast's reach, not the amount of damage it rolls.
# Whole numbers only; allowed range: 1–64. Default: 4.
radius = 4

[damage]
# Choose exactly one formula:
# "fixed" uses the difficulty's [damage.fixed] range, independent of Doll life.
# "source_max_life_percent" uses [damage.source_max_life_percent] instead.
# Both sections must remain present; only the selected formula applies damage.
# Default: "fixed", following PD2 Season 13's physical-damage ranges below.
formula = "fixed"

[damage.fixed]
# Used only with formula = "fixed". Values are physical damage in hit points.
# Each array is [minimum, maximum], with both endpoints included in the roll.
# For constant damage, set both endpoints equal, for example [100, 100].
# Use whole numbers from 0 to 8388607; minimum must not exceed maximum.
# These are the PD2 Season 13 defaults for each game difficulty.
normal = [18, 30]
nightmare = [54, 96]
hell = [318, 540]

[damage.source_max_life_percent]
# Optional retail-inspired ranges, used only with formula = "source_max_life_percent".
# Derived from retail difficulty modifiers (50/35/20%) and the native 60–100% roll.
# Applied to the dying Doll's actual maximum life; not an exact retail damage reproduction.
# Each array is [minimum percent, maximum percent], with both endpoints included.
# Example: a Doll with 1000 maximum life and a [12, 20] roll deals 120–200 raw damage.
# Changes to the Doll's maximum life can therefore change this formula's damage.
# Use whole percentages from 0 to 100; minimum must not exceed maximum.
normal = [30, 50]
nightmare = [21, 35]
hell = [12, 20]

[diagnostics]
# Diagnostic preference shown as counters=on/off by the doll-explosion command.
# The current version prints event counters with either value.
# This setting does not change targets, delay, radius or damage.
show_usage_counters = false
)toml";

using DeathModeFn = std::int32_t(__fastcall*)(void*, void*) noexcept;
using ServerDoOneFn = std::int32_t(__fastcall*)(void*, void*) noexcept;
using CreateSkillMissileFn = void*(__fastcall*)(
    void*, std::int32_t, void*, std::int32_t, std::int32_t,
    std::int32_t, std::int32_t, std::int32_t, std::int32_t,
    std::int32_t, std::int32_t) noexcept;
using GetDataRecordFn = void*(__fastcall*)(
    std::uint8_t, std::int32_t) noexcept;
using GetServerUnitFn = void*(__fastcall*)(void*, std::int32_t, std::int32_t) noexcept;
using GetUnitValueFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetPathCoordinateFn = std::int32_t(__fastcall*)(void*) noexcept;
using CheckStateFn = std::int32_t(__fastcall*)(
    void*, std::int32_t) noexcept;
using GetSeedFn = void*(__fastcall*)(void*) noexcept;
using RollRandomFn = std::uint32_t(__fastcall*)(
    void*, std::int32_t) noexcept;
using NextGameCounterFn = D2RL::Reimplementations::GenerateUnitCastIdFn;
using GetMissileFramesFn = std::int32_t(__fastcall*)(void*) noexcept;
using SetMissileFramesFn = void(__fastcall*)(
    void*, std::int32_t) noexcept;
using ApplyAreaDamageFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t, std::int32_t,
    void*, std::int32_t, std::int32_t, void*, std::uint32_t) noexcept;
using DestroyDamageFn = void(__fastcall*)(void*) noexcept;

enum class NativeAttempt : std::uint8_t {
    Success,
    Rejected,
    Fault,
};

struct DeathIdentity {
    void* unit{};
    std::int32_t monsterId{-1};
    bool monster{};
};

struct DeathSnapshot {
    void* unit{};
    std::int32_t ownerGuid{-1};
    std::int32_t ownerClassId{-1};
    std::int32_t x{};
    std::int32_t y{};
    std::uint8_t difficulty{};
    std::uint8_t dataContext{};
    bool eligible{};
    bool reviveSkipped{};
    bool nativeDeathDamageSkipped{};
};

struct CarrierSlot {
    void* game{};
    void* missile{};
    std::int32_t guid{-1};
    std::int32_t ownerGuid{-1};
    std::int32_t ownerClassId{-1};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t damage{};
    std::int32_t radius{};
    bool active{};
};

struct CarrierNativeResult {
    void* missile{};
    std::int32_t guid{-1};
};

struct alignas(16) DamageRecord {
    std::array<std::uint8_t, DamageRecordSize> bytes{};
};

struct CleanupBinding {
    HMODULE module{};
    cleanup::ApiV1 api{};
};

static_assert(sizeof(DamageRecord) == DamageRecordSize);

const D2RL::PluginContext* Context{};
const D2RL::DiagnosticsServiceV1* DiagnosticsService{};
const D2RL::LifecycleServiceV1* LifecycleService{};
std::uint8_t* Base{};
std::size_t ImageSize{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};

DeathModeFn OriginalDeathMode{};
ServerDoOneFn OriginalServerDoOne{};
CreateSkillMissileFn CreateSkillMissile{};
GetDataRecordFn GetMissilesRecord{};
GetDataRecordFn GetMonStatsRecord{};
GetUnitValueFn GetUnitType{};
GetUnitValueFn GetUnitId{};
GetServerUnitFn GetServerUnit{};
GetPathCoordinateFn GetPathX{};
GetPathCoordinateFn GetPathY{};
CheckStateFn CheckState{};
GetSeedFn GetSeed{};
RollRandomFn RollRandom{};
native_stat::Adapter NativeStatAdapter{};
NextGameCounterFn NextGameCounter{};
GetMissileFramesFn GetMissileCurrentFrames{};
GetMissileFramesFn GetMissileTotalFrames{};
SetMissileFramesFn SetMissileCurrentFrames{};
SetMissileFramesFn SetMissileTotalFrames{};
ApplyAreaDamageFn ApplyAreaDamage{};
DestroyDamageFn DestroyDamage{};

SRWLOCK CarrierLock = SRWLOCK_INIT;
std::array<CarrierSlot, MaximumCarrierSlots> CarrierSlots{};
std::array<D2RL::Lifecycle::ListenerHandle, 2> GameplayListenerHandles{};
std::atomic_bool Operational{};
std::atomic_bool ContractFailureReported{};
std::atomic<std::uint64_t> TargetDeaths{};
std::atomic<std::uint64_t> ReviveSkips{};
std::atomic<std::uint64_t> NativeDeathDamageSkips{};
std::atomic<std::uint64_t> ImmediateExplosions{};
std::atomic<std::uint64_t> ScheduledExplosions{};
std::atomic<std::uint64_t> CompletedExplosions{};
std::atomic<std::uint64_t> FailedExplosions{};
std::atomic<std::uint64_t> SidecarExhaustions{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "doll-explosion",
    .name = "Doll Explosion",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description = "Delays Stygian Doll death blasts and makes their physical damage configurable.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<typename Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto IsExecutableProtection(DWORD value) noexcept -> bool {
    const auto protection = value & 0xFFU;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

auto IsReadableProtection(DWORD value) noexcept -> bool {
    const auto protection = value & 0xFFU;
    return protection == PAGE_READONLY
        || protection == PAGE_READWRITE
        || protection == PAGE_WRITECOPY
        || IsExecutableProtection(value);
}

auto IsRangeAccessible(
        const void* address,
        std::size_t size,
        bool executable) noexcept -> bool {
    if (!address || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    const auto end = start + size;
    auto cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(cursor),
                &info,
                sizeof(info)) != sizeof(info)
                || info.State != MEM_COMMIT
                || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || (executable
                    ? !IsExecutableProtection(info.Protect)
                    : !IsReadableProtection(info.Protect))) {
            return false;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(
            info.BaseAddress);
        if (regionStart > std::numeric_limits<std::uintptr_t>::max()
                - info.RegionSize) {
            return false;
        }
        const auto regionEnd = regionStart + info.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = std::min(regionEnd, end);
    }
    return true;
}

auto IsWithinImage(std::uintptr_t rva, std::size_t size) noexcept -> bool {
    return Base != nullptr && ImageSize != 0
        && rva <= ImageSize && size <= ImageSize - rva;
}

auto InitializeImageBounds() noexcept -> bool {
    if (!Base) return false;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE
                || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            Base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE
                || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
                || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
                || nt->OptionalHeader.SizeOfImage == 0) {
            return false;
        }
        ImageSize = nt->OptionalHeader.SizeOfImage;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ImageSize = 0;
        return false;
    }
}

template<std::size_t Size>
auto Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    if (!IsWithinImage(rva, expected.size())
            || !IsRangeAccessible(Base + rva, expected.size(), true)) {
        return false;
    }
    __try {
        return native::MatchesBytes(Base + rva, expected.size(), expected);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto QueryDiagnosticsService() noexcept -> bool {
    const auto result = Context->QueryService(
        D2RL::ServiceId::Diagnostics,
        D2RL::DiagnosticsServiceV1Version,
        &DiagnosticsService);
    if (result != D2RL::ServiceQueryResult::Success) {
        DiagnosticsService = nullptr;
        Context->LogWarn(
            "DollExplosion: DiagnosticsService v1 is unavailable; exact vanilla hook signatures remain mandatory.");
        return true;
    }
    if (!D2RL::HasDiagnosticsServiceV1Field(
            DiagnosticsService,
            D2RL::DiagnosticsServiceV1RequiredSize)
            || DiagnosticsService->queryHookStatus == nullptr) {
        Context->LogError(
            "DollExplosion: DiagnosticsService v1 returned an invalid contract.");
        DiagnosticsService = nullptr;
        return false;
    }
    return true;
}

auto QueryLifecycleService() noexcept -> bool {
    LifecycleService = nullptr;
    const auto result = Context->QueryService(
        D2RL::ServiceId::Lifecycle,
        D2RL::LifecycleServiceV1Version,
        &LifecycleService);
    if (result != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(
                LifecycleService,
                D2RL::LifecycleServiceV1RequiredSize)
            || !LifecycleService->registerGameplayEventListener
            || !LifecycleService->unregisterGameplayEventListener) {
        Context->LogError(
            "DollExplosion: LifecycleService v1 cannot provide game-session cleanup; plugin refused.");
        LifecycleService = nullptr;
        return false;
    }
    return true;
}

auto ResolveCastIdExport() noexcept -> bool {
    NextGameCounter = nullptr;
    const auto core = GetModuleHandleW(L"D2RCore.dll");
    if (core) {
        constexpr auto info = D2RL::Reimplementations::GenerateUnitCastIdInfo;
        const auto named = GetProcAddress(core, info.name);
        const auto ordinal = GetProcAddress(core, MAKEINTRESOURCEA(info.ordinal));
        // Consume the SDK's supported ABI, not an internal RVA or relay stub.
        // Require the named and ordinal identities to agree before any hook.
        if (named && named == ordinal
                && IsRangeAccessible(reinterpret_cast<const void*>(named), 1, true)) {
            static_assert(sizeof(named) == sizeof(NextGameCounter));
            std::memcpy(&NextGameCounter, &named, sizeof(named));
            return true;
        }
    }
    Context->LogError(
        "DollExplosion: supported GenerateUnitCastId export is unavailable or inconsistent; plugin refused.");
    return false;
}

auto CleanupRequest() noexcept -> cleanup::RequestV1 {
    return {sizeof(cleanup::RequestV1), cleanup::Version, cleanup::DamageLayout,
        Base + native::DestroyDamageRva, native::DestroyDamageExpected.data(),
        static_cast<std::uint32_t>(native::DestroyDamageExpected.size()), 0};
}

auto ReadCleanupApi(HMODULE module, cleanup::ApiV1& result) noexcept -> bool {
    __try {
        const auto raw = GetProcAddress(module, cleanup::ExportName);
        if (!raw) return false;
        cleanup::GetApiFn getApi{};
        static_assert(sizeof(raw) == sizeof(getApi));
        std::memcpy(&getApi, &raw, sizeof(raw));
        const auto* api = getApi(cleanup::Version, sizeof(cleanup::ApiV1));
        if (!cleanup::ValidApi(api)) return false;
        result = *api;
        const auto request = CleanupRequest();
        return result.accepts(&request);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

auto AcquireCleanup(CleanupBinding& binding) noexcept -> bool {
    binding = {};
    HMODULE canonical{}, legacy{};
    // Hold a module reference for the whole operation, not a cached raw API
    // pointer. The provider separately serializes its logical shutdown.
    (void)GetModuleHandleExW(0, L"d2rl-ruffneckk-cast-triggers.dll", &canonical);
    (void)GetModuleHandleExW(0, L"CastTriggers.dll", &legacy);
    if (canonical && legacy && canonical != legacy) {
        FreeLibrary(canonical);
        FreeLibrary(legacy);
        return false;
    }
    if (canonical && legacy) FreeLibrary(legacy);
    binding.module = canonical ? canonical : legacy;
    if (!binding.module) return false;
    if (ReadCleanupApi(binding.module, binding.api)) return true;
    FreeLibrary(binding.module);
    binding = {};
    return false;
}

auto ValidateDamageCleanup() noexcept -> bool {
    if (Matches(native::DestroyDamageRva, native::DestroyDamageExpected)) return true;
    CleanupBinding binding{};
    if (AcquireCleanup(binding)) {
        FreeLibrary(binding.module);
        Context->LogInfo("DollExplosion: CastTriggers damage cleanup ABI v1 accepted.");
        return true;
    }
    Context->LogError(
        "DollExplosion: damage destructor changed without a ready compatible cleanup provider; plugin refused.");
    return false;
}

auto BindNativeStatAdapter() noexcept -> bool {
    NativeStatAdapter.Reset();
    if (NativeStatAdapter.BindCurrentProcess(
            reinterpret_cast<std::uintptr_t>(Base), RequiredStatHelpers)
            && NativeStatAdapter.IsBound(native_stat::Helper::GetUnitStat)) {
        return true;
    }
    Context->LogError(
        "DollExplosion: NativeStatCompat cannot admit the exact GetUnitStat provider; plugin refused.");
    return false;
}

template<std::size_t Size>
auto ValidateExclusiveSurface(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected,
        const char* label) noexcept -> bool {
    if (!Matches(rva, expected)) {
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "DollExplosion: %s signature mismatch; plugin refused.", label);
        Context->LogError(message);
        return false;
    }
    if (!DiagnosticsService) return true;
    const D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = rva,
        .expected = expected.data(),
        .expectedSize = static_cast<std::uint32_t>(expected.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    const auto result = DiagnosticsService->queryHookStatus(
        Context, &query, &status);
    if (result == D2RL::Diagnostics::Result::Success
            && status.structSize
                >= D2RL::Diagnostics::HookStatusRequiredSize
            && status.state
                == D2RL::Diagnostics::ModificationState::Unchanged
            && status.ownerCount == 0) {
        return true;
    }
    char message[256]{};
    std::snprintf(
        message, sizeof(message),
        "DollExplosion: %s is not an unowned vanilla surface; plugin refused.",
        label);
    Context->LogError(message);
    return false;
}

template<std::size_t Size>
auto ValidateWitness(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected,
        const char* label) noexcept -> bool {
    if (Matches(rva, expected)) return true;
    char message[256]{};
    std::snprintf(
        message, sizeof(message),
        "DollExplosion: %s fingerprint mismatch; plugin refused.", label);
    Context->LogError(message);
    return false;
}

auto ValidateServerDoTable() noexcept -> bool {
    if (!IsWithinImage(native::ServerDoTableRva, sizeof(void*) * 2)
            || !IsRangeAccessible(
                Base + native::ServerDoTableRva,
                sizeof(void*) * 2,
                false)) {
        Context->LogError(
            "DollExplosion: pSrvDo dispatch table is inaccessible; plugin refused.");
        return false;
    }
    __try {
        const auto* table = reinterpret_cast<void* const*>(
            Base + native::ServerDoTableRva);
        if (table[1] != Base + native::ServerDoOneRva) {
            Context->LogError(
                "DollExplosion: pSrvDo 1 no longer resolves to the basic missile handler; plugin refused.");
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Context->LogError(
            "DollExplosion: pSrvDo dispatch-table read faulted; plugin refused.");
        return false;
    }
}

auto ValidateNativeFingerprint() noexcept -> bool {
    return ValidateExclusiveSurface(
               native::DeathModeRva,
               native::DeathModeExpected,
               "monster death-mode callback")
        && ValidateExclusiveSurface(
               native::ServerDoOneRva,
               native::ServerDoOneExpected,
               "pSrvDo=1 relay")
        && ValidateWitness(
               native::DeathModeDataGuardRva,
               native::DeathModeDataGuardExpected,
               "deathDmg/data-context guard")
        && ValidateWitness(
               native::DeathCallContextRva,
               native::DeathCallContextExpected,
               "native corpse-explosion callsite")
        && ValidateWitness(
               native::CreateSkillMissileRva,
               native::CreateSkillMissileExpected,
               "skill-missile constructor")
        && ValidateWitness(
               native::ServerMissileStepRva,
               native::ServerMissileStepExpected,
               "server missile step")
        && ValidateWitness(native::ServerCountdownRva,
               native::ServerCountdownExpected, "server countdown")
        && ValidateWitness(native::ServerHitResolveRva,
               native::ServerHitResolveExpected, "server hit resolver")
        && ValidateWitness(native::ServerHitReturnRva,
               native::ServerHitReturnExpected, "server hit return")
        && ValidateWitness(native::ServerRemovalRva,
               native::ServerRemovalExpected, "post-callback missile removal")
        && ValidateWitness(
               native::ServerDoDispatchRva,
               native::ServerDoDispatchExpected,
               "pSrvDo dispatcher")
        && ValidateWitness(
               native::ServerDoTableWitnessRva,
               native::ServerDoTableWitnessExpected,
               "pSrvDo table callsite")
        && ValidateWitness(
               native::GameDataLayoutRva,
               native::GameDataLayoutExpected,
               "game difficulty/data-context layout")
        && ValidateWitness(
               native::DamageInitLayoutRva,
               native::DamageInitLayoutExpected,
               "corpse-explosion damage initialization")
        && ValidateWitness(
               native::DamageCallLayoutRva,
               native::DamageCallLayoutExpected,
               "corpse-explosion area-damage call")
        && ValidateWitness(
               native::MissileTimingLayoutRva,
               native::MissileTimingLayoutExpected,
               "missile timing layout")
        && ValidateWitness(
               native::MissileTotalFramesRva,
               native::MissileTotalFramesExpected,
               "missile total-frame layout")
        && ValidateWitness(
               native::GetMissileCurrentFramesRva,
               native::GetMissileCurrentFramesExpected,
               "missile current-frame getter")
        && ValidateWitness(
               native::GetMissileTotalFramesRva,
               native::GetMissileTotalFramesExpected,
               "missile total-frame getter")
        && ValidateWitness(
               native::SetMissileCurrentFramesRva,
               native::SetMissileCurrentFramesExpected,
               "missile current-frame setter")
        && ValidateWitness(
               native::SetMissileTotalFramesRva,
               native::SetMissileTotalFramesExpected,
               "missile total-frame setter")
        && ValidateWitness(
               native::ApplyAreaDamageRva,
               native::ApplyAreaDamageExpected,
               "area-damage helper")
        && ValidateWitness(
               native::GetMissilesRecordRva,
               native::GetMissilesRecordExpected,
               "Missiles record resolver")
        && ValidateWitness(
               native::GetMonStatsRecordRva,
               native::GetMonStatsRecordExpected,
               "MonStats record resolver")
        && ValidateWitness(
               native::GetUnitTypeRva,
               native::GetUnitTypeExpected,
               "unit-type helper")
        && ValidateWitness(
               native::UnitIdentityLayoutRva,
               native::UnitIdentityLayoutExpected,
               "unit identity layout")
        && ValidateWitness(
               native::GetUnitIdRva,
               native::GetUnitIdExpected,
               "unit-GUID helper")
        && ValidateWitness(native::GetServerUnitRva,
               native::GetServerUnitExpected, "server-unit lookup")
        && ValidateWitness(
               native::GetPathXRva,
               native::GetPathXExpected,
               "path X helper")
        && ValidateWitness(
               native::GetPathYRva,
               native::GetPathYExpected,
               "path Y helper")
        && ValidateWitness(
               native::CheckStateRva,
               native::CheckStateExpected,
               "state-check helper")
        && ValidateWitness(
               native::ReviveStateMarkerRva,
               native::ReviveStateMarkerExpected,
               "Revive-state semantic witness")
        && ValidateWitness(
               native::GetSeedRva,
               native::GetSeedExpected,
               "unit-seed helper")
        && ValidateWitness(
               native::RollRandomRva,
               native::RollRandomExpected,
               "bounded RNG helper")
        && ValidateDamageCleanup()
        && ValidateServerDoTable();
}

auto ConfigCandidates() -> std::vector<std::filesystem::path> {
    std::filesystem::path activeModConfigDirectory;
    std::filesystem::path scopeConfigDirectory;
    if (Context && Context->activeMod && Context->activeMod[0] != '\0'
            && Context->modSupportDirectory
            && Context->modSupportDirectory[0] != L'\0') {
        activeModConfigDirectory =
            std::filesystem::path(Context->modSupportDirectory) / L"config";
    }
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        scopeConfigDirectory =
            std::filesystem::path(Context->pluginConfigPath).parent_path();
    }
    std::error_code currentPathError;
    const auto currentPath = std::filesystem::current_path(currentPathError);
    const auto globalConfigDirectory = currentPathError
        ? std::filesystem::path{}
        : currentPath / L"d2rloader" / L"config";
    return BuildConfigCandidates(
        activeModConfigDirectory,
        scopeConfigDirectory,
        globalConfigDirectory,
        ConfigFileName);
}

auto MaterializeDefaultConfig(
        const std::vector<std::filesystem::path>& candidates) noexcept -> bool {
    std::filesystem::path path;
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        path = std::filesystem::path(
            Context->pluginConfigPath).parent_path() / ConfigFileName;
    } else if (!candidates.empty()) {
        path = candidates.front();
    }
    if (path.empty()) return false;
    try {
        std::error_code directoryError;
        std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError) return false;
        const auto handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        const auto ok = WriteFile(
            handle,
            DefaultConfig,
            static_cast<DWORD>(sizeof(DefaultConfig) - 1),
            &written,
            nullptr) != FALSE
            && written == sizeof(DefaultConfig) - 1;
        CloseHandle(handle);
        if (!ok) {
            (void)DeleteFileW(path.c_str());
            return false;
        }
        LoadedConfigPath = path.string();
        return true;
    } catch (...) {
        return false;
    }
}

auto LoadConfig() noexcept -> bool {
    Settings = {};
    LoadedConfigPath = "embedded defaults";
    const auto candidates = ConfigCandidates();
    for (const auto& path : candidates) {
        std::error_code statusError;
        if (!std::filesystem::is_regular_file(path, statusError)) continue;
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("configuration file cannot be opened");
            }
            const std::string text{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            Config parsed{};
            std::string error;
            if (!ParseToml(text, parsed, error)) {
                throw std::invalid_argument(error);
            }
            Settings = std::move(parsed);
            LoadedConfigPath = path.string();
            return true;
        } catch (const std::exception& exception) {
            const auto message = std::string("DollExplosion: invalid ")
                + path.string() + " (" + exception.what() + ").";
            Context->LogError(message.c_str());
            return false;
        }
    }
    if (MaterializeDefaultConfig(candidates)) {
        const auto message = std::string(
            "DollExplosion: created default configuration at ")
            + LoadedConfigPath + ".";
        Context->LogInfo(message.c_str());
    } else {
        Context->LogWarn(
            "DollExplosion: no TOML was found or created; embedded PD2 defaults are active.");
    }
    return true;
}

void InitializeNativeFunctions() noexcept {
    CreateSkillMissile = At<CreateSkillMissileFn>(
        native::CreateSkillMissileRva);
    GetMissilesRecord = At<GetDataRecordFn>(native::GetMissilesRecordRva);
    GetMonStatsRecord = At<GetDataRecordFn>(native::GetMonStatsRecordRva);
    GetUnitType = At<GetUnitValueFn>(native::GetUnitTypeRva);
    GetUnitId = At<GetUnitValueFn>(native::GetUnitIdRva);
    GetServerUnit = At<GetServerUnitFn>(native::GetServerUnitRva);
    GetPathX = At<GetPathCoordinateFn>(native::GetPathXRva);
    GetPathY = At<GetPathCoordinateFn>(native::GetPathYRva);
    CheckState = At<CheckStateFn>(native::CheckStateRva);
    GetSeed = At<GetSeedFn>(native::GetSeedRva);
    RollRandom = At<RollRandomFn>(native::RollRandomRva);
    GetMissileCurrentFrames = At<GetMissileFramesFn>(
        native::GetMissileCurrentFramesRva);
    GetMissileTotalFrames = At<GetMissileFramesFn>(
        native::GetMissileTotalFramesRva);
    SetMissileCurrentFrames = At<SetMissileFramesFn>(
        native::SetMissileCurrentFramesRva);
    SetMissileTotalFrames = At<SetMissileFramesFn>(
        native::SetMissileTotalFramesRva);
    ApplyAreaDamage = At<ApplyAreaDamageFn>(native::ApplyAreaDamageRva);
    DestroyDamage = At<DestroyDamageFn>(native::DestroyDamageRva);
}

void ResetNativeFunctions() noexcept {
    OriginalDeathMode = nullptr;
    OriginalServerDoOne = nullptr;
    CreateSkillMissile = nullptr;
    GetMissilesRecord = nullptr;
    GetMonStatsRecord = nullptr;
    GetUnitType = nullptr;
    GetUnitId = nullptr;
    GetServerUnit = nullptr;
    GetPathX = nullptr;
    GetPathY = nullptr;
    CheckState = nullptr;
    GetSeed = nullptr;
    RollRandom = nullptr;
    NativeStatAdapter.Reset();
    NextGameCounter = nullptr;
    GetMissileCurrentFrames = nullptr;
    GetMissileTotalFrames = nullptr;
    SetMissileCurrentFrames = nullptr;
    SetMissileTotalFrames = nullptr;
    ApplyAreaDamage = nullptr;
    DestroyDamage = nullptr;
}

void ClearCarriers() noexcept {
    AcquireSRWLockExclusive(&CarrierLock);
    CarrierSlots.fill({});
    ReleaseSRWLockExclusive(&CarrierLock);
}

void __cdecl OnGameplayEvent(
        const D2RL::PluginContext* context,
        const D2RL::Lifecycle::GameplayEvent* event,
        void*) noexcept {
    if (!Context || context != Context
            || !D2RL::Lifecycle::HasGameplayEventField(
                event,
                D2RL::Lifecycle::GameplayEventRequiredSize)) {
        return;
    }
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined
            || event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
        ClearCarriers();
    }
}

void UnregisterGameplayListeners() noexcept {
    if (LifecycleService && Context
            && LifecycleService->unregisterGameplayEventListener) {
        for (auto& handle : GameplayListenerHandles) {
            if (handle == D2RL::Lifecycle::InvalidHandle) continue;
            const auto result = LifecycleService->unregisterGameplayEventListener(
                Context, handle);
            if (result != D2RL::Lifecycle::Result::Success) {
                Context->LogWarn(
                    "DollExplosion: gameplay listener cleanup was deferred to the loader.");
            }
            handle = D2RL::Lifecycle::InvalidHandle;
        }
        return;
    }
    GameplayListenerHandles.fill(D2RL::Lifecycle::InvalidHandle);
}

auto RegisterGameplayListeners() noexcept -> bool {
    constexpr std::array kinds{
        D2RL::Lifecycle::GameplayEventKind::GameJoined,
        D2RL::Lifecycle::GameplayEventKind::GameLeft,
    };
    GameplayListenerHandles.fill(D2RL::Lifecycle::InvalidHandle);
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .flags = 0,
            .kind = kinds[index],
            .reserved = 0,
            .callback = OnGameplayEvent,
            .userData = nullptr,
        };
        auto& handle = GameplayListenerHandles[index];
        if (LifecycleService->registerGameplayEventListener(
                Context, &listener, &handle)
                    != D2RL::Lifecycle::Result::Success
                || handle == D2RL::Lifecycle::InvalidHandle) {
            Context->LogError(
                "DollExplosion: game-session listener registration failed; plugin refused.");
            UnregisterGameplayListeners();
            return false;
        }
    }
    return true;
}

auto AddCarrier(const CarrierSlot& carrier) noexcept -> bool {
    AcquireSRWLockExclusive(&CarrierLock);
    for (auto& slot : CarrierSlots) {
        if (slot.active) continue;
        slot = carrier;
        slot.active = true;
        ReleaseSRWLockExclusive(&CarrierLock);
        return true;
    }
    ReleaseSRWLockExclusive(&CarrierLock);
    return false;
}

auto FindCarrier(
        void* game,
        void* missile,
        CarrierSlot& result) noexcept -> bool {
    AcquireSRWLockShared(&CarrierLock);
    for (const auto& slot : CarrierSlots) {
        if (!slot.active || slot.game != game || slot.missile != missile) {
            continue;
        }
        result = slot;
        ReleaseSRWLockShared(&CarrierLock);
        return true;
    }
    ReleaseSRWLockShared(&CarrierLock);
    return false;
}

auto TakeCarrier(
        void* game,
        void* missile,
        std::int32_t guid,
        CarrierSlot& result) noexcept -> bool {
    AcquireSRWLockExclusive(&CarrierLock);
    for (auto& slot : CarrierSlots) {
        if (!slot.active || slot.game != game || slot.missile != missile
                || slot.guid != guid) {
            continue;
        }
        result = slot;
        slot = {};
        ReleaseSRWLockExclusive(&CarrierLock);
        return true;
    }
    ReleaseSRWLockExclusive(&CarrierLock);
    return false;
}

void DisableForNativeFailure(const char* detail) noexcept {
    Operational.store(false, std::memory_order_release);
    FailedExplosions.fetch_add(1, std::memory_order_relaxed);
    if (!ContractFailureReported.exchange(true, std::memory_order_acq_rel)
            && Context) {
        char message[320]{};
        std::snprintf(
            message, sizeof(message),
            "DollExplosion: native contract failure (%s); custom explosions disabled and vanilla callbacks preserved.",
            detail ? detail : "unknown");
        Context->LogError(message);
    }
}

auto TryReadDeathIdentity(
        void* modeChange,
        DeathIdentity& result) noexcept -> bool {
    __try {
        result = {};
        if (!modeChange || !GetUnitType) return true;
        auto* const bytes = static_cast<std::uint8_t*>(modeChange);
        result.unit = *reinterpret_cast<void**>(bytes + 0x08);
        if (!result.unit || GetUnitType(result.unit) != MonsterUnitType) {
            return true;
        }
        result.monster = true;
        result.monsterId = native::ReadUnitClassId(result.unit);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = {};
        return false;
    }
}

auto TryCaptureTarget(
        void* game,
        const DeathIdentity& identity,
        DeathSnapshot& result) noexcept -> bool {
    __try {
        result = {};
        result.unit = identity.unit;
        if (!game || !identity.unit || !GetMonStatsRecord || !CheckState
                || !GetPathX || !GetPathY || !GetUnitId) {
            return false;
        }
        if (CheckState(identity.unit, ReviveStateId) != 0) {
            result.reviveSkipped = true;
            return true;
        }
        auto* const gameBytes = static_cast<std::uint8_t*>(game);
        result.difficulty = *(gameBytes + 0x104);
        result.dataContext = *(gameBytes + 0x106);
        if (result.difficulty > 2) return true;
        auto* const monStats = static_cast<std::uint8_t*>(
            GetMonStatsRecord(result.dataContext, identity.monsterId));
        if (!monStats) return true;
        if ((*(monStats + 0x3E) & MonStatsDeathDamageMask) != 0) {
            result.nativeDeathDamageSkipped = true;
            return true;
        }
        auto* const unitBytes = static_cast<std::uint8_t*>(identity.unit);
        void* const path = *reinterpret_cast<void**>(unitBytes + 0x38);
        if (!path) return true;
        result.ownerGuid = GetUnitId(identity.unit);
        result.ownerClassId = identity.monsterId;
        if (result.ownerGuid < 0 || result.ownerClassId < 0) return true;
        result.x = GetPathX(path);
        result.y = GetPathY(path);
        result.eligible = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = {};
        return false;
    }
}

auto TryRollDamage(
        const DeathSnapshot& snapshot,
        std::int32_t& damage) noexcept -> NativeAttempt {
    const auto* range = Settings.formula == DamageFormula::Fixed
        ? SelectRange(Settings.fixed, snapshot.difficulty)
        : SelectRange(Settings.sourceMaxLifePercent, snapshot.difficulty);
    if (!range) return NativeAttempt::Rejected;
    __try {
        if (!GetSeed || !RollRandom || !snapshot.unit) {
            return NativeAttempt::Fault;
        }
        void* const seed = GetSeed(snapshot.unit);
        if (!seed) return NativeAttempt::Fault;
        const auto span = InclusiveSpan(*range);
        if (span == 0
                || span > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max())) {
            return NativeAttempt::Fault;
        }
        const auto roll = RollRandom(
            seed, static_cast<std::int32_t>(span));
        if (roll >= span) return NativeAttempt::Fault;
        const auto selected = ApplyInclusiveRoll(*range, roll);
        if (Settings.formula == DamageFormula::Fixed) {
            const auto scaled = ScaleFixedDamage(selected);
            if (!scaled) return NativeAttempt::Fault;
            damage = *scaled;
            return NativeAttempt::Success;
        }
        if (!NativeStatAdapter.IsBound(native_stat::Helper::GetUnitStat)) {
            return NativeAttempt::Fault;
        }
        const auto maximumLife = NativeStatAdapter.GetUnitStat(
            snapshot.unit, MaximumLifeStatId, static_cast<std::uint16_t>(0));
        const auto scaled = ScaleMaxLifePercent(maximumLife, selected);
        if (!scaled) return NativeAttempt::Rejected;
        damage = *scaled;
        return NativeAttempt::Success;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        damage = 0;
        return NativeAttempt::Fault;
    }
}

auto TryValidateMissileRecords(
        std::uint8_t dataContext,
        bool requireCarrier) noexcept
        -> NativeAttempt {
    __try {
        if (!GetMissilesRecord) return NativeAttempt::Fault;
        auto* const visualRecord = static_cast<std::uint8_t*>(
            GetMissilesRecord(dataContext, CorpseExplosionMissileId));
        if (!visualRecord) return NativeAttempt::Fault;
        const auto visualServerDo =
            *reinterpret_cast<const std::int16_t*>(visualRecord + 0x2C);
        const auto visualServerHit =
            *reinterpret_cast<const std::int16_t*>(visualRecord + 0x2E);
        if (visualServerDo != 1 || visualServerHit != 0) {
            return NativeAttempt::Fault;
        }
        if (!requireCarrier) return NativeAttempt::Success;
        auto* const carrierRecord = static_cast<std::uint8_t*>(
            GetMissilesRecord(dataContext, DelayCarrierMissileId));
        if (!carrierRecord) return NativeAttempt::Fault;
        const auto serverDo = *reinterpret_cast<const std::int16_t*>(
            carrierRecord + 0x2C);
        const auto serverHit = *reinterpret_cast<const std::int16_t*>(
            carrierRecord + 0x2E);
        return serverDo == 1 && serverHit == 0
            ? NativeAttempt::Success
            : NativeAttempt::Fault;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NativeAttempt::Fault;
    }
}

auto TryCreateMissile(
        void* game,
        std::int32_t missileId,
        void* owner,
        std::int32_t x,
        std::int32_t y,
        void*& missile) noexcept -> NativeAttempt {
    __try {
        missile = nullptr;
        if (!game || !owner || !CreateSkillMissile || !NextGameCounter
                || !GetUnitType) {
            return NativeAttempt::Fault;
        }
        // The native missile constructor asserts for every other source type.
        const auto ownerType = GetUnitType(owner);
        if (ownerType != 0 && ownerType != MonsterUnitType) {
            return NativeAttempt::Rejected;
        }
        const auto castId = NextGameCounter(game);
        if (castId == 0 || castId > INT32_MAX) return NativeAttempt::Fault;
        const auto counter = static_cast<std::int32_t>(castId);
        missile = CreateSkillMissile(
            game,
            missileId,
            owner,
            0,
            1,
            counter,
            0,
            0,
            x,
            y,
            1);
        if (!missile) return NativeAttempt::Rejected;
        return GetUnitType(missile) == MissileUnitType
                && native::ReadUnitClassId(missile) == missileId
            ? NativeAttempt::Success
            : NativeAttempt::Fault;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        missile = nullptr;
        return NativeAttempt::Fault;
    }
}

auto TryCreateAndValidateCarrier(
        void* game,
        const DeathSnapshot& snapshot,
        CarrierNativeResult& result) noexcept -> NativeAttempt {
    __try {
        result = {};
        if (!CreateSkillMissile || !NextGameCounter || !GetUnitType
                || !GetUnitId
                || !GetMissileCurrentFrames || !GetMissileTotalFrames) {
            return NativeAttempt::Fault;
        }
        // The native missile constructor asserts for every other source type.
        const auto ownerType = GetUnitType(snapshot.unit);
        if (ownerType != 0 && ownerType != MonsterUnitType) {
            return NativeAttempt::Rejected;
        }
        const auto castId = NextGameCounter(game);
        if (castId == 0 || castId > INT32_MAX) return NativeAttempt::Fault;
        const auto counter = static_cast<std::int32_t>(castId);
        result.missile = CreateSkillMissile(
            game,
            DelayCarrierMissileId,
            snapshot.unit,
            0,
            1,
            counter,
            0,
            0,
            snapshot.x,
            snapshot.y,
            1);
        if (!result.missile) return NativeAttempt::Rejected;
        if (GetUnitType(result.missile) != MissileUnitType
                || native::ReadUnitClassId(result.missile) != DelayCarrierMissileId) {
            return NativeAttempt::Fault;
        }
        result.guid = GetUnitId(result.missile);
        if (result.guid < 0) return NativeAttempt::Fault;
        const auto totalFrames = GetMissileTotalFrames(result.missile);
        const auto currentFrames = GetMissileCurrentFrames(result.missile);
        return totalFrames == DelayCarrierNativeFrames
                && currentFrames == DelayCarrierNativeFrames
            ? NativeAttempt::Success
            : NativeAttempt::Fault;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = {};
        return NativeAttempt::Fault;
    }
}

auto TrySetCarrierFrames(
        void* missile,
        std::int32_t delayFrames) noexcept -> bool {
    __try {
        if (!missile || !SetMissileTotalFrames
                || !SetMissileCurrentFrames || !GetMissileTotalFrames
                || !GetMissileCurrentFrames || delayFrames <= 0
                || delayFrames > MaximumDelayFrames) {
            return false;
        }
        SetMissileTotalFrames(missile, delayFrames);
        SetMissileCurrentFrames(missile, delayFrames);
        return GetMissileTotalFrames(missile) == delayFrames
            && GetMissileCurrentFrames(missile) == delayFrames;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void InitializeDamageRecord(
        DamageRecord& record,
        std::int32_t physicalDamage) noexcept {
    record.bytes.fill(0);
    auto* const base = record.bytes.data();
    *reinterpret_cast<std::int32_t*>(base + 0x18) = physicalDamage;
    *reinterpret_cast<void**>(base + 0x40) = base + 0x58;
    *reinterpret_cast<std::uint64_t*>(base + 0x48) = 0;
    *reinterpret_cast<std::uint64_t*>(base + 0x50) =
        0x8000000000000010ULL;
    *reinterpret_cast<std::uint64_t*>(base + 0x158) = 1;
    *reinterpret_cast<std::uint64_t*>(base + 0x178) = 0;
}

struct DamageOperation {
    void* game;
    void* sourceMissile;
    std::int32_t x, y, radius;
};

auto __cdecl ApplyDamageOperation(void* data, void* damage) noexcept -> bool {
    const auto& operation = *static_cast<const DamageOperation*>(data);
    if (!ApplyAreaDamage) return false;
    (void)ApplyAreaDamage(operation.game, operation.sourceMissile,
        operation.x, operation.y, operation.radius, damage,
        0, 0, nullptr, CorpseExplosionFlags);
    return true;
}

auto ApplyWithNativeCleanup(DamageOperation& operation, void* damage) noexcept -> bool {
    __try {
        bool success{};
        __try {
            success = ApplyDamageOperation(&operation, damage);
        } __finally {
            DestroyDamage(damage);
        }
        return success;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

auto RunProvider(const CleanupBinding& binding, DamageOperation& operation,
        void* damage) noexcept -> cleanup::Result {
    __try {
        const auto request = CleanupRequest();
        return binding.api.run(&request, damage, ApplyDamageOperation, &operation);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return cleanup::Result::Fault; }
}

auto TryApplyExplosion(
        void* game,
        void* sourceMissile,
        std::int32_t x,
        std::int32_t y,
        std::int32_t radius,
        std::int32_t physicalDamage) noexcept -> bool {
    DamageRecord damage{};
    InitializeDamageRecord(damage, physicalDamage);
    if (!ApplyAreaDamage || !DestroyDamage) return false;
    DamageOperation operation{game, sourceMissile, x, y, radius};
    // Re-resolve on each explosion: a provider may have loaded after us or
    // stopped since the last event. Never invoke a changed entry directly.
    if (Matches(native::DestroyDamageRva, native::DestroyDamageExpected)) {
        return ApplyWithNativeCleanup(operation, damage.bytes.data());
    }
    CleanupBinding binding{};
    if (!AcquireCleanup(binding)) return false;
    const auto result = RunProvider(binding, operation, damage.bytes.data());
    FreeLibrary(binding.module);
    // No retry/fallback after a provider attempt: effects or cleanup may
    // already have occurred even when the provider reports a fault.
    return result == cleanup::Result::Completed;
}

auto ExplodeWithVisual(
        void* game,
        void* visualOwner,
        void* damageSource,
        std::int32_t x,
        std::int32_t y,
        std::int32_t radius,
        std::int32_t damage) noexcept -> NativeAttempt {
    void* visual{};
    const auto visualResult = TryCreateMissile(
        game, CorpseExplosionMissileId, visualOwner, x, y, visual);
    if (visualResult != NativeAttempt::Success) return visualResult;
    return TryApplyExplosion(
        game, damageSource ? damageSource : visual,
        x, y, radius, damage)
        ? NativeAttempt::Success
        : NativeAttempt::Fault;
}

void ScheduleExplosion(
        void* game,
        const DeathSnapshot& snapshot,
        std::int32_t damage) noexcept {
    if (TryValidateMissileRecords(
            snapshot.dataContext, Settings.delayFrames > 0)
            != NativeAttempt::Success) {
        DisableForNativeFailure("explosion missile records");
        return;
    }
    if (Settings.delayFrames == 0) {
        const auto result = ExplodeWithVisual(
            game,
            snapshot.unit,
            nullptr,
            snapshot.x,
            snapshot.y,
            Settings.radius,
            damage);
        if (result == NativeAttempt::Success) {
            ImmediateExplosions.fetch_add(1, std::memory_order_relaxed);
        } else if (result == NativeAttempt::Fault) {
            DisableForNativeFailure("immediate explosion");
        } else {
            FailedExplosions.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    CarrierNativeResult nativeCarrier{};
    const auto createResult = TryCreateAndValidateCarrier(
        game, snapshot, nativeCarrier);
    if (createResult == NativeAttempt::Rejected) {
        FailedExplosions.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (createResult == NativeAttempt::Fault) {
        DisableForNativeFailure("delay-carrier instance");
        return;
    }
    const CarrierSlot carrier{
        .game = game,
        .missile = nativeCarrier.missile,
        .guid = nativeCarrier.guid,
        .ownerGuid = snapshot.ownerGuid,
        .ownerClassId = snapshot.ownerClassId,
        .x = snapshot.x,
        .y = snapshot.y,
        .damage = damage,
        .radius = Settings.radius,
        .active = true,
    };
    if (!AddCarrier(carrier)) {
        SidecarExhaustions.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!TrySetCarrierFrames(
            nativeCarrier.missile, Settings.delayFrames)) {
        CarrierSlot discarded{};
        (void)TakeCarrier(
            game, nativeCarrier.missile, nativeCarrier.guid, discarded);
        DisableForNativeFailure("delay-carrier duration write");
        return;
    }
    ScheduledExplosions.fetch_add(1, std::memory_order_relaxed);
}

auto FormulaName() noexcept -> const char* {
    return Settings.formula == DamageFormula::Fixed
        ? "fixed" : "source_max_life_percent";
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[1024]{};
    std::snprintf(
        message,
        sizeof(message),
        "Doll Explosion 1.0.0: active=%s; targets=%zu; delay=%df; radius=%d; formula=%s; counters=%s; target-deaths=%llu; scheduled=%llu; immediate=%llu; completed=%llu; failed=%llu; revive-skips=%llu; native-deathDmg-skips=%llu; sidecar-full=%llu; TOML=%s.",
        Operational.load(std::memory_order_acquire) ? "true" : "false",
        Settings.targetMonsterIds.size(),
        Settings.delayFrames,
        Settings.radius,
        FormulaName(),
        Settings.diagnostics ? "on" : "off",
        static_cast<unsigned long long>(
            TargetDeaths.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ScheduledExplosions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ImmediateExplosions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CompletedExplosions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            FailedExplosions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ReviveSkips.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeDeathDamageSkips.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            SidecarExhaustions.load(std::memory_order_relaxed)),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    ContractFailureReported.store(false, std::memory_order_relaxed);
    TargetDeaths.store(0, std::memory_order_relaxed);
    ReviveSkips.store(0, std::memory_order_relaxed);
    NativeDeathDamageSkips.store(0, std::memory_order_relaxed);
    ImmediateExplosions.store(0, std::memory_order_relaxed);
    ScheduledExplosions.store(0, std::memory_order_relaxed);
    CompletedExplosions.store(0, std::memory_order_relaxed);
    FailedExplosions.store(0, std::memory_order_relaxed);
    SidecarExhaustions.store(0, std::memory_order_relaxed);
    GameplayListenerHandles.fill(D2RL::Lifecycle::InvalidHandle);
    ClearCarriers();
}

std::int32_t __fastcall HookDeathMode(
        void* game,
        void* modeChange) noexcept {
    DeathSnapshot snapshot{};
    bool target{};
    if (Operational.load(std::memory_order_acquire)) {
        DeathIdentity identity{};
        if (!TryReadDeathIdentity(modeChange, identity)) {
            DisableForNativeFailure("death identity");
        } else if (identity.monster
                && IsTargetMonster(Settings, identity.monsterId)) {
            target = true;
            TargetDeaths.fetch_add(1, std::memory_order_relaxed);
            if (!TryCaptureTarget(game, identity, snapshot)) {
                DisableForNativeFailure("death snapshot");
                snapshot = {};
            } else if (snapshot.reviveSkipped) {
                ReviveSkips.fetch_add(1, std::memory_order_relaxed);
            } else if (snapshot.nativeDeathDamageSkipped) {
                NativeDeathDamageSkips.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
    }

    const auto originalResult = OriginalDeathMode
        ? OriginalDeathMode(game, modeChange) : 0;
    if (!target || !snapshot.eligible || originalResult == 0
            || !Operational.load(std::memory_order_acquire)) {
        return originalResult;
    }
    std::int32_t damage{};
    const auto rollResult = TryRollDamage(snapshot, damage);
    if (rollResult == NativeAttempt::Fault) {
        DisableForNativeFailure("damage roll");
    } else if (rollResult == NativeAttempt::Success) {
        ScheduleExplosion(game, snapshot, damage);
    } else {
        FailedExplosions.fetch_add(1, std::memory_order_relaxed);
    }
    return originalResult;
}

// Resolve scalar identity only at expiry; never retain a corpse pointer across frames.
auto TryResolveVisualOwner(void* game, const CarrierSlot& carrier,
        void*& owner) noexcept -> NativeAttempt {
    owner = nullptr;
    if (!game || !GetServerUnit || !GetUnitType || !GetUnitId || !CheckState) {
        return NativeAttempt::Fault;
    }
    if (carrier.ownerGuid < 0 || carrier.ownerClassId < 0) {
        return NativeAttempt::Rejected;
    }
    __try {
        void* const candidate = GetServerUnit(game, MonsterUnitType, carrier.ownerGuid);
        if (!candidate || GetUnitType(candidate) != MonsterUnitType
                || GetUnitId(candidate) != carrier.ownerGuid
                || native::ReadUnitClassId(candidate) != carrier.ownerClassId
                || CheckState(candidate, ReviveStateId) != 0) {
            return NativeAttempt::Rejected;
        }
        owner = candidate;
        return NativeAttempt::Success;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NativeAttempt::Fault;
    }
}

std::int32_t __fastcall HookServerDoOne(
        void* game,
        void* missile) noexcept {
    CarrierSlot observed{};
    if (!Operational.load(std::memory_order_acquire)
            || !FindCarrier(game, missile, observed)) {
        return OriginalServerDoOne
            ? OriginalServerDoOne(game, missile) : 0;
    }

    std::int32_t guid{-1};
    std::int32_t framesBefore{};
    __try {
        if (GetUnitType(missile) == MissileUnitType
                && native::ReadUnitClassId(missile) == DelayCarrierMissileId) {
            guid = GetUnitId(missile);
            framesBefore = GetMissileCurrentFrames(missile);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DisableForNativeFailure("delay-carrier GUID");
        return OriginalServerDoOne
            ? OriginalServerDoOne(game, missile) : 0;
    }
    if (guid != observed.guid) {
        CarrierSlot stale{};
        (void)TakeCarrier(game, missile, observed.guid, stale);
        return OriginalServerDoOne
            ? OriginalServerDoOne(game, missile) : 0;
    }

    return RunTrackedServerDo(framesBefore,
        [&]() noexcept { return OriginalServerDoOne(game, missile); },
        [&](bool countdownExpired) noexcept {
            CarrierSlot carrier{};
            if (!TakeCarrier(game, missile, guid, carrier)) return;
            if (!Operational.load(std::memory_order_acquire)) return;
            if (!countdownExpired) {
                FailedExplosions.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // The dispatcher removes the unit only after our return. Native
            // hit resolution has completed; this hook never frees the missile.
            void* visualOwner{};
            const auto ownerResult = TryResolveVisualOwner(game, carrier, visualOwner);
            const auto explosionResult = ownerResult == NativeAttempt::Success
                ? ExplodeWithVisual(game, visualOwner, missile, carrier.x, carrier.y,
                    carrier.radius, carrier.damage)
                : ownerResult;
            if (explosionResult == NativeAttempt::Success) {
                CompletedExplosions.fetch_add(1, std::memory_order_relaxed);
            } else if (explosionResult == NativeAttempt::Fault) {
                DisableForNativeFailure("delayed explosion");
            } else {
                FailedExplosions.fetch_add(1, std::memory_order_relaxed);
            }
        });
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    LifecycleService = nullptr;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ImageSize = 0;
    ResetNativeFunctions();
    ResetState();
    if (!Base) {
        context->LogError(
            "DollExplosion: D2R executable base is unavailable; plugin refused.");
        return false;
    }
    if (!LoadConfig()) return false;
    if (!context->RegisterConsoleCommand(
            "doll-explosion",
            Status,
            "Show Doll Explosion settings and diagnostic counters.")) {
        context->LogWarn(
            "DollExplosion: optional status command was not registered.");
    }

    const auto* const observedBuild = D2RL::GetBuildName(context);
    const auto* const runtimeBuild = observedBuild && observedBuild[0] != '\0'
        ? observedBuild : "<unavailable>";
    char fingerprintMessage[256]{};
    std::snprintf(
        fingerprintMessage,
        sizeof(fingerprintMessage),
        "DollExplosion: observed D2R build-name=%s; diagnostic only; validating the complete native fingerprint.",
        runtimeBuild);
    context->LogInfo(fingerprintMessage);

    if (!InitializeImageBounds()) {
        context->LogError(
            "DollExplosion: invalid PE64/AMD64 image metadata; plugin refused.");
        return false;
    }
    if (!QueryDiagnosticsService() || !QueryLifecycleService()
            || !ResolveCastIdExport() || !BindNativeStatAdapter()
            || !ValidateNativeFingerprint()) {
        return false;
    }
    InitializeNativeFunctions();
    if (!RegisterGameplayListeners()) return false;

    if (!context->InstallInlineHook(
            native::DeathModeRva,
            native::DeathModeExpected.data(),
            static_cast<std::uint32_t>(native::DeathModeExpected.size()),
            HookDeathMode,
            &OriginalDeathMode)) {
        context->LogError(
            "DollExplosion: monster death-mode callback is already owned; plugin refused.");
        UnregisterGameplayListeners();
        return false;
    }
    if (!context->InstallInlineHook(
            native::ServerDoOneRva,
            native::ServerDoOneExpected.data(),
            static_cast<std::uint32_t>(
                native::ServerDoOneExpected.size()),
            HookServerDoOne,
            &OriginalServerDoOne)) {
        context->LogError(
            "DollExplosion: pSrvDo=1 relay is already owned or cannot be hooked; plugin refused.");
        UnregisterGameplayListeners();
        return false;
    }
    Operational.store(true, std::memory_order_release);

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "Doll Explosion 1.0.0 by RuffnecKk active for observed D2R %s; targets=%zu; delay=%df; radius=%d; formula=%s; installation=%s; TOML=%s.",
        runtimeBuild,
        Settings.targetMonsterIds.size(),
        Settings.delayFrames,
        Settings.radius,
        FormulaName(),
        context->loadScope == D2RL::LoadScope::Mod ? "mod-local" : "global",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    UnregisterGameplayListeners();
    ClearCarriers();
    ResetNativeFunctions();
    LifecycleService = nullptr;
    DiagnosticsService = nullptr;
    Context = nullptr;
    Base = nullptr;
    ImageSize = 0;
}
