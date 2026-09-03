#define NOMINMAX

#include <D2RLPlugin/api.h>

#include "cast_triggers_policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace RuffnecKk::CastTriggers {
namespace {

using namespace ruffneckk::cast_triggers;

constexpr wchar_t ConfigFileName[] = L"ruffneckk-cast-triggers.toml";
constexpr std::int32_t DoActiveEvent = 4;
constexpr std::int32_t MeleeDamageEvent = 5;
constexpr std::int32_t MissileDamageEvent = 6;
constexpr std::int32_t PassiveCriticalStatId = 337;
constexpr std::size_t GameDataContextOffset = 0x106;
constexpr std::size_t GameFrameOffset = 0x170;
constexpr std::size_t DamageHitFlagsOffset = 0x000;
constexpr std::size_t DamageResultFlagsOffset = 0x004;
constexpr std::size_t SkillFlagsOffset = 0x24;
constexpr std::size_t SkillAnimationOffset = 0x30;
constexpr std::size_t SkillSequenceTransitionOffset = 0x32;
constexpr std::size_t ActiveSkillRecordPointerOffset = 0x00;
constexpr std::size_t SkillRecordIdOffset = 0x00;

constexpr std::uintptr_t SkillHandlerRva = 0x43ACB0;
constexpr std::uintptr_t SkillHandlerContextWitnessRva = 0x43ACEC;
constexpr std::uintptr_t GameFrameLayoutWitnessRva = 0x42E615;
constexpr std::uintptr_t DispatchUnitStatEventRva = 0x44D570;
constexpr std::uintptr_t PlayerSkillPositionInputRva = 0x4FDB40;
constexpr std::uintptr_t PlayerSkillUnitInputRva = 0x4F8DE0;
constexpr std::uintptr_t GetTargetUnitRva = 0x48FE20;
constexpr std::uintptr_t GetServerUnitRva = 0x48FE80;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t GetDynamicPathRva = 0x34AE80;
constexpr std::uintptr_t PathGetFirstPointXRva = 0x341CC0;
constexpr std::uintptr_t PathGetFirstPointYRva = 0x341CD0;
constexpr std::uintptr_t GetSkillsRecordRva = 0x097790;
constexpr std::uintptr_t SkillsRecordStrideWitnessRva = 0x09780B;
constexpr std::uintptr_t CastItemSkillOnTargetRva = 0x5896E0;
constexpr std::uintptr_t CastItemSkillAtPositionRva = 0x589820;
constexpr std::uintptr_t FillDamageValuesRva = 0x44C030;
constexpr std::uintptr_t CopyDamageRva = 0x4494B0;
constexpr std::uintptr_t MoveDamageRva = 0x449760;
constexpr std::uintptr_t DestroyDamageRva = 0x4496E0;
constexpr std::uintptr_t EventFunc15Rva = 0x584170;
constexpr std::uintptr_t EventFunc16Rva = 0x583150;
constexpr std::uintptr_t EventFunc20Rva = 0x583B30;
constexpr std::uintptr_t ResolveActiveWeaponRva = 0x4242B0;
constexpr std::uintptr_t GetWeaponMasteryChanceRva = 0x33D4F0;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetSeedRva = 0x34A1E0;
constexpr std::uintptr_t ActiveSkillLayoutWitnessRva = 0x33DBA0;
constexpr std::size_t MaximumPlayerInputEntries = 64;
constexpr std::size_t MaximumChannelingCadenceEntries = 64;
constexpr std::size_t MaximumCriticalDamageMarkers = 256;
constexpr std::size_t MaximumDiagnosticTraceEntries = 64;
constexpr std::size_t MaximumDiagnosticTraceMessageLength = 448;
constexpr std::uint32_t PreventCriticalHitFlag = 0x00000001;
constexpr std::uint16_t ResultSuccess = 0x0001;
constexpr std::uint16_t CriticalOrDeadlyResult = 0x2000;

constexpr auto SkillHandlerExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x10,0x44,0x89,0x4C,
    0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x55,
    0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,
    0x41,0x57,0x48,0x81,0xEC,0x80,0x00,0x00,
    0x00,
});
constexpr auto SkillHandlerContextWitnessExpected =
        std::to_array<std::uint8_t>({
    0x0F,0xB6,0x89,0x06,0x01,0x00,0x00,0x45,
    0x33,0xFF,0x41,0x8B,0xD0,0x45,0x8B,0xE7,
    0xE8,0x8F,0xCA,0xC5,0xFF,0x48,0x8B,0xF0,
    0x48,0x85,0xC0,
    });
constexpr auto GameFrameLayoutWitnessExpected = std::to_array<std::uint8_t>({
    0x44,0x8B,0x89,0x70,0x01,0x00,0x00,0x48,
    0x8B,0xDA,0x89,0x44,0x24,0x28,0x41,0xFF,
    0xC1,
});
constexpr auto DispatchUnitStatEventExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,
    0x83,0xEC,0x60,0x48,0x8B,0x05,0x46,0xDD,
    0x57,0x02,0x48,0x33,0xC4,0x48,0x89,0x44,
    0x24,0x58,
});
constexpr auto PlayerSkillPositionInputExpected =
        std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x83,0xEC,0x50,0x48,0x8B,0xB4,0x24,
    0xA8,0x00,0x00,0x00,
});
constexpr auto PlayerSkillUnitInputExpected =
        std::to_array<std::uint8_t>({
    0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,
    0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,
    0x24,0x28,0xFE,0xFF,0xFF,0x48,0x81,0xEC,
    0xD8,0x02,0x00,0x00,
});
constexpr auto GetTargetUnitExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xDA,0xE8,0x22,0x1D,0x00,0x00,0x48,0x8B,
    0xCB,0xE8,0x4A,0xB0,0xEB,0xFF,0x48,0x8B,
    0xC8,0xE8,0x02,0x1C,0xEB,0xFF,
});
constexpr auto GetServerUnitExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,
    0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x38,
    0x48,0x8D,0x4C,0x24,0x38,0xE8,0x46,0xD1,
    0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x83,
    0xFE,0x05,
});
constexpr auto GetUnitTypeExpected = std::to_array<std::uint8_t>({
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,
    0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,
    0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00,
});
constexpr auto GetDynamicPathExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xD9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,
    0xD4,0xB9,0xFF,0xFF,0x84,0xC0,0x74,0x01,
    0xCC,0x83,0x3B,0x05,0x75,0x14,0x48,0x8D,
    0x4C,0x24,0x30,0xC6,0x44,0x24,0x30,0x00,
    0xE8,0xEB,0xA4,0xFF,0xFF,0x84,0xC0,0x74,
    0x01,0xCC,0x48,0x8B,0x43,0x38,0x48,0x83,
    0xC4,0x20,0x5B,0xC3,
});
constexpr auto PathGetFirstPointXExpected = std::to_array<std::uint8_t>({
    0x0F,0xB7,0x41,0x10,0xC3,0xCC,0xCC,0xCC,
    0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
});
constexpr auto PathGetFirstPointYExpected = std::to_array<std::uint8_t>({
    0x0F,0xB7,0x41,0x12,0xC3,0xCC,0xCC,0xCC,
    0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
});
constexpr auto GetSkillsRecordExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x20,0x57,0x48,0x83,0xEC,0x30,0x48,
    0x63,0xF2,0xE8,0xE9,0x92,0x26,0x00,0x48,
    0x8B,0xF8,0x48,0x8B,0xDE,0x85,0xF6,0x78,
    0x09,0x48,0x3B,0x98,0xB8,0x11,0x00,0x00,
});
constexpr auto SkillsRecordStrideWitnessExpected =
        std::to_array<std::uint8_t>({
    0x48,0x69,0x44,0x24,0x50,0xEC,0x02,0x00,
    0x00,0x48,0x03,0x07,
});
constexpr auto CastItemSkillOnTargetExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,
    0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x70,
    0x49,0x8B,0xF1,0x41,0x8B,0xE8,0x44,0x8B,
    0xF2,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x0F,
    0x84,0xF0,0x00,0x00,0x00,
});
constexpr auto CastItemSkillAtPositionExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,
    0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,
    0x41,0x57,0x48,0x83,0xEC,0x70,
    0x41,0x8B,0xE9,0x45,0x8B,0xF0,0x44,0x8B,
    0xFA,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x0F,
    0x84,0xCA,0x00,0x00,0x00,
});
constexpr auto FillDamageValuesExpected = std::to_array<std::uint8_t>({
    0x40,0x56,0x57,0x41,0x54,0x48,0x81,0xEC,
    0x10,0x05,0x00,0x00,0x48,0x8B,0x05,0x85,
    0xF2,0x57,0x02,0x48,0x33,0xC4,0x48,0x89,
    0x84,0x24,0xD0,0x04,0x00,0x00,
});
constexpr auto CopyDamageExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,
    0xEC,0x20,0x8B,0x02,0x4C,0x8D,0x41,0x58,
});
constexpr auto MoveDamageExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x56,0x41,0x56,0x48,0x83,0xEC,
    0x20,0x8B,0x02,0x4C,0x8B,0xF2,0x89,0x01,
    0x48,0x8B,0xF1,0x0F,0xB7,0x42,0x04,0x66,
    0x89,0x41,0x04,0x8B,0x42,0x08,0x89,0x41,
    0x08,
});
constexpr auto DestroyDamageExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,
    0xEC,0x20,0xF6,0x81,0x58,0x01,0x00,0x00,
    0x01,0x48,0x8B,0xF9,0x74,0x0D,0x48,0x8B,
    0x89,0x58,0x01,0x00,0x00,0x48,0x83,0xE1,
});
constexpr auto EventFunc15Expected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x48,0x83,0xEC,0x50,0x49,0x8B,0xF9,0x49,
});
constexpr auto EventFunc16Expected = std::to_array<std::uint8_t>({
    0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,
    0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,
    0x83,0xEC,0x60,0x49,0x8B,0xF1,0x49,0x8B,
    0xF8,0x44,0x8B,0xFA,0x4C,0x8B,0xF1,
});
constexpr auto EventFunc20Expected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x54,0x41,0x56,
    0x41,0x57,0x48,0x83,0xEC,0x40,0x49,0x8B,
    0xF1,
});
constexpr auto ResolveActiveWeaponExpected = std::to_array<std::uint8_t>({
    0xE9,0x4B,0x6D,0xF2,0xFF,
});
constexpr auto GetWeaponMasteryChanceExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,
    0x57,0x48,0x81,0xEC,0x48,0x02,0x00,0x00,
    0x48,0x8B,0x05,0xC1,0xDD,0x68,0x02,0x48,
    0x33,0xC4,0x48,0x89,0x84,0x24,0x30,0x02,
    0x00,0x00,
});
constexpr auto GetUnitStatExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,
    0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x48,0x83,0xEC,0x20,0x41,0x0F,0xB7,0xE8,
    0x8B,0xFA,0x48,0x8B,0xD9,0x48,0x85,0xC9,
});
constexpr auto GetSeedExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xD9,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,
    0x94,0xBB,0xFF,0xFF,0x84,0xC0,0x74,0x01,
    });
constexpr auto ActiveSkillLayoutWitnessExpected =
        std::to_array<std::uint8_t>({
    0x48,0x85,0xC9,0x74,0x0E,0x48,0x8B,0x01,
    0x66,0x83,0x38,0x00,0xB8,0xFF,0xFF,0xFF,
    0xFF,0x75,0x03,0x8B,0x41,0x30,0xC3,0xCC,
    });

using SkillHandlerFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t,
    std::int32_t, std::int32_t, std::int32_t) noexcept;
using DispatchUnitStatEventFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, void*, void*, void*) noexcept;
using PlayerSkillPositionInputFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t,
    std::int32_t, void*) noexcept;
using PlayerSkillUnitInputFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t,
    void*, std::int32_t, std::int32_t) noexcept;
using GetTargetUnitFn = void*(__fastcall*)(void*, void*) noexcept;
using GetServerUnitFn = void*(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetDynamicPathFn = void*(__fastcall*)(void*) noexcept;
using PathGetFirstPointFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetSkillsRecordFn = const std::uint8_t*(__fastcall*)(
    std::uint8_t, std::int32_t) noexcept;
using CastItemSkillOnTargetFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::int32_t, void*, std::int32_t) noexcept;
using CastItemSkillAtPositionFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::int32_t,
    std::int32_t, std::int32_t, std::int32_t) noexcept;
using FillDamageValuesFn = void(__fastcall*)(
    void*, void*, void*, void*, std::int32_t, std::uint8_t) noexcept;
using DamageCopyFn = void*(__fastcall*)(void*, const void*) noexcept;
using DamageMoveFn = void*(__fastcall*)(void*, void*) noexcept;
using DamageDestroyFn = void(__fastcall*)(void*) noexcept;
using EventFunctionFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, void*, void*, void*, std::int32_t, std::int32_t,
    std::int32_t, void*) noexcept;
using ResolveActiveWeaponFn = void*(__fastcall*)(void*) noexcept;
using GetWeaponMasteryChanceFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::uint16_t) noexcept;

struct NativeSeedPair {
    std::uint32_t low{};
    std::uint32_t high{};
};

using GetSeedFn = NativeSeedPair*(__fastcall*)(void*) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"built-in defaults"};
std::atomic_bool Operational{};
SkillHandlerFn OriginalSkillHandler{};
CastItemSkillOnTargetFn OriginalCastItemSkillOnTarget{};
CastItemSkillAtPositionFn OriginalCastItemSkillAtPosition{};
DispatchUnitStatEventFn OriginalDispatchUnitStatEvent{};
PlayerSkillPositionInputFn OriginalPlayerSkillPositionInput{};
PlayerSkillUnitInputFn OriginalPlayerSkillUnitInput{};
GetTargetUnitFn OriginalGetTargetUnit{};
GetServerUnitFn GetServerUnit{};
GetUnitTypeFn GetUnitType{};
GetDynamicPathFn GetDynamicPath{};
PathGetFirstPointFn OriginalPathGetFirstPointX{};
PathGetFirstPointFn OriginalPathGetFirstPointY{};
GetSkillsRecordFn GetSkillsRecord{};
FillDamageValuesFn OriginalFillDamageValues{};
DamageCopyFn OriginalCopyDamage{};
DamageMoveFn OriginalMoveDamage{};
DamageDestroyFn OriginalDestroyDamage{};
EventFunctionFn OriginalEventFunc15{};
EventFunctionFn OriginalEventFunc16{};
EventFunctionFn OriginalEventFunc20{};
ResolveActiveWeaponFn ResolveActiveWeapon{};
GetWeaponMasteryChanceFn GetWeaponMasteryChance{};
GetUnitStatFn GetUnitStat{};
GetSeedFn GetSeed{};

thread_local std::uint32_t CastDispatchDepth{};
thread_local std::uint32_t ProcExecutionDepth{};
thread_local SourceTriggerKind ActiveSourceTrigger{SourceTriggerKind::None};
thread_local std::int32_t ActiveSourceSkillId{-1};
thread_local CombatTriggerKind ActiveCombatTrigger{CombatTriggerKind::None};
thread_local std::int32_t CastDispatchSourceLevel{};
thread_local void* CastDispatchSourceUnit{};
thread_local NativeSourceTargetKind CastDispatchSourceTargetKind{
    NativeSourceTargetKind::None};
thread_local void* CastDispatchSourceTarget{};
thread_local std::int32_t CastDispatchTargetX{};
thread_local std::int32_t CastDispatchTargetY{};

thread_local bool SourceTargetObservationActive{};
thread_local void* SourceTargetObservationGame{};
thread_local void* SourceTargetObservationUnit{};
thread_local void* SourceTargetObservationPath{};
thread_local NativeSourceTargetKind ObservedSourceTargetKind{
    NativeSourceTargetKind::None};
thread_local void* ObservedSourceTarget{};
thread_local std::int32_t ObservedSourceTargetX{};
thread_local std::int32_t ObservedSourceTargetY{};
thread_local bool ObservedSourceTargetXReady{};
thread_local bool ObservedSourceTargetYReady{};

std::atomic_uint64_t ManualCastsObserved{};
std::atomic_uint64_t EligibleCasts{};
std::atomic_uint64_t EventDispatches{};
std::atomic_uint64_t FixedLevelProcs{};
std::atomic_uint64_t SameLevelProcs{};
std::atomic_uint64_t NativePositionProcs{};
std::atomic_uint64_t NativeUnitTargetProcs{};
std::atomic_uint64_t ChannelingTicksObserved{};
std::atomic_uint64_t ChannelingDispatches{};
std::atomic_uint64_t ChannelingTicksThrottled{};
std::atomic_uint64_t PlayerInputsCaptured{};
std::atomic_uint64_t PlayerInputsConsumed{};
std::atomic_uint64_t PlayerInputsExpired{};
std::atomic_uint64_t ChannelTargetsReused{};
std::atomic_uint64_t CriticalOutcomes{};
std::atomic_uint64_t CrushingBlowOutcomes{};
std::atomic_uint64_t OpenWoundsOutcomes{};
std::atomic_uint64_t CombatDispatches{};
std::atomic_uint64_t CombatChainsSuppressed{};
std::atomic_uint64_t SyntheticStatsFiltered{};
std::atomic_uint64_t CriticalMarkerOverflows{};
std::atomic_uint64_t CriticalPredictionChecks{};
std::atomic_uint64_t CriticalPassiveChanceSeen{};
std::atomic_uint64_t CriticalPredicted{};
std::atomic_uint64_t CriticalNativeFlagsConfirmed{};
std::atomic_uint64_t CriticalMarkersCreated{};
std::atomic_uint64_t CriticalMarkersPropagated{};
std::atomic_uint64_t CriticalMarkersConsumed{};
std::atomic_uint64_t CriticalMarkersRemoved{};
std::atomic_uint64_t CriticalEventFlagsWithoutMarker{};

struct ChannelingCadenceEntry {
    void* game{};
    void* unit{};
    std::int32_t skillId{-1};
    std::uint32_t lastDispatchFrame{};
    std::uint64_t lastObservationSequence{};
};

std::mutex ChannelingCadenceMutex;
std::array<ChannelingCadenceEntry, MaximumChannelingCadenceEntries>
    ChannelingCadenceEntries{};
std::uint64_t ChannelingObservationSequence{};

struct CombatObservationFrame {
    void* game{};
    void* attacker{};
    void* target{};
    void* damage{};
    bool crushingBlow{};
    bool openWounds{};
};

thread_local CombatObservationFrame* ActiveCombatObservation{};

std::mutex CriticalDamageMutex;
std::array<void*, MaximumCriticalDamageMarkers> CriticalDamageMarkers{};

struct DiagnosticTraceEntry {
    std::uint64_t sequence{};
    std::array<char, MaximumDiagnosticTraceMessageLength> message{};
};

struct DiagnosticTraceSnapshot {
    std::uint64_t total{};
    std::size_t count{};
    std::array<DiagnosticTraceEntry, MaximumDiagnosticTraceEntries> entries{};
};

std::mutex DiagnosticTraceMutex;
std::array<DiagnosticTraceEntry, MaximumDiagnosticTraceEntries>
    DiagnosticTraceEntries{};
std::uint64_t DiagnosticTraceTotal{};
std::size_t DiagnosticTraceCount{};
std::size_t DiagnosticTraceNext{};

void RecordDiagnostic(const char* message) noexcept {
    if (!Settings.diagnostics || !message) return;

    std::scoped_lock lock(DiagnosticTraceMutex);
    auto& entry = DiagnosticTraceEntries[DiagnosticTraceNext];
    entry.sequence = ++DiagnosticTraceTotal;
    std::snprintf(
        entry.message.data(),
        entry.message.size(),
        "%s",
        message);
    DiagnosticTraceNext =
        (DiagnosticTraceNext + 1) % MaximumDiagnosticTraceEntries;
    if (DiagnosticTraceCount < MaximumDiagnosticTraceEntries) {
        ++DiagnosticTraceCount;
    }
}

auto SnapshotDiagnosticTrace() noexcept -> DiagnosticTraceSnapshot {
    DiagnosticTraceSnapshot snapshot{};
    std::scoped_lock lock(DiagnosticTraceMutex);
    snapshot.total = DiagnosticTraceTotal;
    snapshot.count = DiagnosticTraceCount;
    const auto first = DiagnosticTraceCount == MaximumDiagnosticTraceEntries
        ? DiagnosticTraceNext
        : 0;
    for (std::size_t index = 0; index < DiagnosticTraceCount; ++index) {
        snapshot.entries[index] = DiagnosticTraceEntries[
            (first + index) % MaximumDiagnosticTraceEntries];
    }
    return snapshot;
}

template <typename Fn>
Fn At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<Fn>(Base + rva);
}

template <typename Value>
Value ReadRecordValue(
        const std::uint8_t* record,
        std::size_t offset) noexcept {
    Value result{};
    if (record) std::memcpy(&result, record + offset, sizeof(result));
    return result;
}

struct NativeSourceTargetDescriptor {
    NativeSourceTargetKind kind{NativeSourceTargetKind::None};
    void* unit{};
    std::int32_t x{};
    std::int32_t y{};
};

struct PlayerInputTargetEntry {
    void* game{};
    void* unit{};
    std::int32_t skillId{-1};
    NativeSourceTargetKind kind{NativeSourceTargetKind::None};
    std::int32_t targetType{};
    std::int32_t targetGuid{};
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t capturedFrame{};
    std::uint64_t sequence{};
    bool retainedForChannel{};
};

std::mutex PlayerInputTargetMutex;
std::array<PlayerInputTargetEntry, MaximumPlayerInputEntries>
    PlayerInputTargetEntries{};
std::uint64_t PlayerInputTargetSequence{};

class SourceTargetObservationScope final {
public:
    SourceTargetObservationScope(
            void* game,
            void* unit,
            void* path) noexcept
        : previousActive_(SourceTargetObservationActive),
          previousGame_(SourceTargetObservationGame),
          previousUnit_(SourceTargetObservationUnit),
          previousPath_(SourceTargetObservationPath),
          previousKind_(ObservedSourceTargetKind),
          previousTarget_(ObservedSourceTarget),
          previousX_(ObservedSourceTargetX),
          previousY_(ObservedSourceTargetY),
          previousXReady_(ObservedSourceTargetXReady),
          previousYReady_(ObservedSourceTargetYReady) {
        SourceTargetObservationActive = game != nullptr
            && unit != nullptr
            && path != nullptr;
        SourceTargetObservationGame = game;
        SourceTargetObservationUnit = unit;
        SourceTargetObservationPath = path;
        ObservedSourceTargetKind = NativeSourceTargetKind::None;
        ObservedSourceTarget = nullptr;
        ObservedSourceTargetX = 0;
        ObservedSourceTargetY = 0;
        ObservedSourceTargetXReady = false;
        ObservedSourceTargetYReady = false;
    }

    ~SourceTargetObservationScope() {
        SourceTargetObservationActive = previousActive_;
        SourceTargetObservationGame = previousGame_;
        SourceTargetObservationUnit = previousUnit_;
        SourceTargetObservationPath = previousPath_;
        ObservedSourceTargetKind = previousKind_;
        ObservedSourceTarget = previousTarget_;
        ObservedSourceTargetX = previousX_;
        ObservedSourceTargetY = previousY_;
        ObservedSourceTargetXReady = previousXReady_;
        ObservedSourceTargetYReady = previousYReady_;
    }

    auto Snapshot() const noexcept -> NativeSourceTargetDescriptor {
        return {
            ObservedSourceTargetKind,
            ObservedSourceTarget,
            ObservedSourceTargetX,
            ObservedSourceTargetY,
        };
    }

    SourceTargetObservationScope(const SourceTargetObservationScope&) = delete;
    auto operator=(const SourceTargetObservationScope&)
        -> SourceTargetObservationScope& = delete;

private:
    bool previousActive_{};
    void* previousGame_{};
    void* previousUnit_{};
    void* previousPath_{};
    NativeSourceTargetKind previousKind_{NativeSourceTargetKind::None};
    void* previousTarget_{};
    std::int32_t previousX_{};
    std::int32_t previousY_{};
    bool previousXReady_{};
    bool previousYReady_{};
};

class DispatchScope final {
public:
    DispatchScope(
            std::int32_t sourceLevel,
            void* sourceUnit,
            NativeSourceTargetKind sourceTargetKind,
            void* sourceTarget,
            std::int32_t targetX,
            std::int32_t targetY,
            SourceTriggerKind sourceTrigger = SourceTriggerKind::None,
            std::int32_t sourceSkillId = -1,
            CombatTriggerKind combatTrigger = CombatTriggerKind::None) noexcept
        : previousDepth_(CastDispatchDepth),
          previousSourceTrigger_(ActiveSourceTrigger),
          previousSourceSkillId_(ActiveSourceSkillId),
          previousCombatTrigger_(ActiveCombatTrigger),
          previousLevel_(CastDispatchSourceLevel),
          previousSourceUnit_(CastDispatchSourceUnit),
          previousSourceTargetKind_(CastDispatchSourceTargetKind),
          previousSourceTarget_(CastDispatchSourceTarget),
          previousTargetX_(CastDispatchTargetX),
          previousTargetY_(CastDispatchTargetY) {
        ++CastDispatchDepth;
        ActiveSourceTrigger = sourceTrigger;
        ActiveSourceSkillId = sourceSkillId;
        ActiveCombatTrigger = combatTrigger;
        CastDispatchSourceLevel = sourceLevel;
        CastDispatchSourceUnit = sourceUnit;
        CastDispatchSourceTargetKind = sourceTargetKind;
        CastDispatchSourceTarget = sourceTarget;
        CastDispatchTargetX = targetX;
        CastDispatchTargetY = targetY;
    }

    ~DispatchScope() {
        CastDispatchDepth = previousDepth_;
        ActiveSourceTrigger = previousSourceTrigger_;
        ActiveSourceSkillId = previousSourceSkillId_;
        ActiveCombatTrigger = previousCombatTrigger_;
        CastDispatchSourceLevel = previousLevel_;
        CastDispatchSourceUnit = previousSourceUnit_;
        CastDispatchSourceTargetKind = previousSourceTargetKind_;
        CastDispatchSourceTarget = previousSourceTarget_;
        CastDispatchTargetX = previousTargetX_;
        CastDispatchTargetY = previousTargetY_;
    }

    DispatchScope(const DispatchScope&) = delete;
    auto operator=(const DispatchScope&) -> DispatchScope& = delete;

private:
    std::uint32_t previousDepth_{};
    SourceTriggerKind previousSourceTrigger_{SourceTriggerKind::None};
    std::int32_t previousSourceSkillId_{-1};
    CombatTriggerKind previousCombatTrigger_{CombatTriggerKind::None};
    std::int32_t previousLevel_{};
    void* previousSourceUnit_{};
    NativeSourceTargetKind previousSourceTargetKind_{
        NativeSourceTargetKind::None};
    void* previousSourceTarget_{};
    std::int32_t previousTargetX_{};
    std::int32_t previousTargetY_{};
};

class SuspendDispatchScope final {
public:
    SuspendDispatchScope() noexcept
        : previousDepth_(CastDispatchDepth),
          previousSourceTrigger_(ActiveSourceTrigger),
          previousSourceSkillId_(ActiveSourceSkillId),
          previousCombatTrigger_(ActiveCombatTrigger),
          previousLevel_(CastDispatchSourceLevel),
          previousSourceUnit_(CastDispatchSourceUnit),
          previousSourceTargetKind_(CastDispatchSourceTargetKind),
          previousSourceTarget_(CastDispatchSourceTarget),
          previousTargetX_(CastDispatchTargetX),
          previousTargetY_(CastDispatchTargetY) {
        CastDispatchDepth = 0;
        ActiveSourceTrigger = SourceTriggerKind::None;
        ActiveSourceSkillId = -1;
        ActiveCombatTrigger = CombatTriggerKind::None;
        CastDispatchSourceLevel = 0;
        CastDispatchSourceUnit = nullptr;
        CastDispatchSourceTargetKind = NativeSourceTargetKind::None;
        CastDispatchSourceTarget = nullptr;
        CastDispatchTargetX = 0;
        CastDispatchTargetY = 0;
    }

    ~SuspendDispatchScope() {
        CastDispatchDepth = previousDepth_;
        ActiveSourceTrigger = previousSourceTrigger_;
        ActiveSourceSkillId = previousSourceSkillId_;
        ActiveCombatTrigger = previousCombatTrigger_;
        CastDispatchSourceLevel = previousLevel_;
        CastDispatchSourceUnit = previousSourceUnit_;
        CastDispatchSourceTargetKind = previousSourceTargetKind_;
        CastDispatchSourceTarget = previousSourceTarget_;
        CastDispatchTargetX = previousTargetX_;
        CastDispatchTargetY = previousTargetY_;
    }

    SuspendDispatchScope(const SuspendDispatchScope&) = delete;
    auto operator=(const SuspendDispatchScope&)
        -> SuspendDispatchScope& = delete;

private:
    std::uint32_t previousDepth_{};
    SourceTriggerKind previousSourceTrigger_{SourceTriggerKind::None};
    std::int32_t previousSourceSkillId_{-1};
    CombatTriggerKind previousCombatTrigger_{CombatTriggerKind::None};
    std::int32_t previousLevel_{};
    void* previousSourceUnit_{};
    NativeSourceTargetKind previousSourceTargetKind_{
        NativeSourceTargetKind::None};
    void* previousSourceTarget_{};
    std::int32_t previousTargetX_{};
    std::int32_t previousTargetY_{};
};

class ProcExecutionScope final {
public:
    ProcExecutionScope() noexcept { ++ProcExecutionDepth; }
    ~ProcExecutionScope() { --ProcExecutionDepth; }

    ProcExecutionScope(const ProcExecutionScope&) = delete;
    auto operator=(const ProcExecutionScope&)
        -> ProcExecutionScope& = delete;
};

class CombatObservationScope final {
public:
    explicit CombatObservationScope(CombatObservationFrame* frame) noexcept
        : previous_(ActiveCombatObservation) {
        ActiveCombatObservation = frame;
    }

    ~CombatObservationScope() { ActiveCombatObservation = previous_; }

    CombatObservationScope(const CombatObservationScope&) = delete;
    auto operator=(const CombatObservationScope&)
        -> CombatObservationScope& = delete;

private:
    CombatObservationFrame* previous_{};
};

std::vector<std::filesystem::path> ConfigCandidates() {
    std::vector<std::filesystem::path> directories;
    if (Context && Context->activeMod && Context->activeMod[0] != '\0'
            && Context->modSupportDirectory
            && Context->modSupportDirectory[0] != L'\0') {
        directories.emplace_back(
            std::filesystem::path(Context->modSupportDirectory) / L"config");
    }
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        directories.emplace_back(
            std::filesystem::path(Context->pluginConfigPath).parent_path());
    }
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    if (!error) directories.emplace_back(current / L"d2rloader" / L"config");
    return BuildConfigCandidates(directories, ConfigFileName);
}

bool LoadConfig() noexcept {
    Settings = {};
    LoadedConfigPath = "built-in defaults";
    for (const auto& path : ConfigCandidates()) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) continue;
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("configuration file cannot be opened");
            }
            const std::string text{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            Config parsed{};
            std::string parseError;
            if (!ParseToml(text, parsed, parseError)) {
                throw std::runtime_error(parseError);
            }
            Settings = std::move(parsed);
            LoadedConfigPath = path.string();
            return true;
        } catch (const std::exception& exception) {
            const auto message = std::string("CastTriggers: invalid ")
                + path.string() + " (" + exception.what() + ").";
            Context->LogError(message.c_str());
            return false;
        }
    }
    return true;
}

template <std::size_t Size>
bool Check(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected,
        const char* label) noexcept {
    if (std::memcmp(Base + rva, expected.data(), expected.size()) == 0) {
        return true;
    }
    const auto message = std::string("CastTriggers: ") + label
        + " signature mismatch; plugin refused.";
    Context->LogError(message.c_str());
    return false;
}

bool ValidateNativeFingerprint() noexcept {
    return Check(SkillHandlerRva, SkillHandlerExpected, "skill handler")
        && Check(
            SkillHandlerContextWitnessRva,
            SkillHandlerContextWitnessExpected,
            "skill handler data-context witness")
        && Check(
            GameFrameLayoutWitnessRva,
            GameFrameLayoutWitnessExpected,
            "server game-frame layout witness")
        && Check(
            DispatchUnitStatEventRva,
            DispatchUnitStatEventExpected,
            "unit-stat event dispatcher")
        && Check(
            PlayerSkillPositionInputRva,
            PlayerSkillPositionInputExpected,
            "player position-skill input executor")
        && Check(
            PlayerSkillUnitInputRva,
            PlayerSkillUnitInputExpected,
            "player unit-skill input executor")
        && Check(GetTargetUnitRva, GetTargetUnitExpected, "target resolver")
        && Check(
            GetServerUnitRva,
            GetServerUnitExpected,
            "server unit resolver")
        && Check(GetUnitTypeRva, GetUnitTypeExpected, "unit type helper")
        && Check(
            GetDynamicPathRva,
            GetDynamicPathExpected,
            "dynamic path helper")
        && Check(
            PathGetFirstPointXRva,
            PathGetFirstPointXExpected,
            "path first-point X accessor")
        && Check(
            PathGetFirstPointYRva,
            PathGetFirstPointYExpected,
            "path first-point Y accessor")
        && Check(
            GetSkillsRecordRva,
            GetSkillsRecordExpected,
            "SkillsTxt lookup")
        && Check(
            SkillsRecordStrideWitnessRva,
            SkillsRecordStrideWitnessExpected,
            "SkillsTxt stride witness")
        && Check(
            CastItemSkillOnTargetRva,
            CastItemSkillOnTargetExpected,
            "target item-skill caster")
        && Check(
            CastItemSkillAtPositionRva,
            CastItemSkillAtPositionExpected,
            "position item-skill caster")
        && Check(
            FillDamageValuesRva,
            FillDamageValuesExpected,
            "damage builder")
        && Check(CopyDamageRva, CopyDamageExpected, "damage copy constructor")
        && Check(MoveDamageRva, MoveDamageExpected, "damage move constructor")
        && Check(DestroyDamageRva, DestroyDamageExpected, "damage destructor")
        && Check(EventFunc15Rva, EventFunc15Expected, "Open Wounds callback")
        && Check(EventFunc16Rva, EventFunc16Expected, "Crushing Blow callback")
        && Check(EventFunc20Rva, EventFunc20Expected, "item-skill callback")
        && Check(
            ResolveActiveWeaponRva,
            ResolveActiveWeaponExpected,
            "active weapon resolver")
        && Check(
            GetWeaponMasteryChanceRva,
            GetWeaponMasteryChanceExpected,
            "weapon-mastery Critical helper")
        && Check(GetUnitStatRva, GetUnitStatExpected, "unit stat getter")
        && Check(GetSeedRva, GetSeedExpected, "unit seed accessor")
        && Check(
            ActiveSkillLayoutWitnessRva,
            ActiveSkillLayoutWitnessExpected,
            "active-skill SkillsTxt layout witness");
}

void LogRuntimeIdentity() noexcept {
    const auto* buildName = D2RL::GetBuildName(Context);
    const auto* buildVersion = D2RL::GetBuildVersion(Context);
    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "CastTriggers: observed D2R build-name=%s; version=%s; validating native fingerprint.",
        buildName && buildName[0] != '\0' ? buildName : "<unavailable>",
        buildVersion && buildVersion[0] != '\0'
            ? buildVersion
            : "<unavailable>");
    Context->LogInfo(message);
}

void ResolveNativeFunctions() noexcept {
    GetServerUnit = At<GetServerUnitFn>(GetServerUnitRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetDynamicPath = At<GetDynamicPathFn>(GetDynamicPathRva);
    GetSkillsRecord = At<GetSkillsRecordFn>(GetSkillsRecordRva);
    ResolveActiveWeapon = At<ResolveActiveWeaponFn>(ResolveActiveWeaponRva);
    GetWeaponMasteryChance = At<GetWeaponMasteryChanceFn>(
        GetWeaponMasteryChanceRva);
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    GetSeed = At<GetSeedFn>(GetSeedRva);
}

const char* CombatTriggerName(CombatTriggerKind kind) noexcept {
    switch (kind) {
    case CombatTriggerKind::AttackAttempt: return "attack-attempt";
    case CombatTriggerKind::CriticalStrike: return "critical-strike";
    case CombatTriggerKind::CrushingBlow: return "crushing-blow";
    case CombatTriggerKind::OpenWounds: return "open-wounds";
    case CombatTriggerKind::None: return "none";
    }
    return "unknown";
}

struct CriticalPredictionProbe {
    std::int32_t masteryChance{};
    std::int32_t passiveChance{};
    bool prevented{};
};

bool PredictNativeCriticalStrike(
        void* attacker,
        void* damage,
        std::int32_t mode,
        CriticalPredictionProbe* probe) noexcept {
    if (probe) *probe = {};
    if (!attacker || !damage || !GetSeed || !GetUnitStat
            || !IsCombatTriggerEnabled(
                Settings.combatTriggers,
                CombatTriggerKind::CriticalStrike)) {
        return false;
    }
    const bool prevented = (ReadRecordValue<std::uint32_t>(
        static_cast<const std::uint8_t*>(damage),
        DamageHitFlagsOffset) & PreventCriticalHitFlag) != 0;
    if (probe) probe->prevented = prevented;
    if (prevented) return false;

    const auto* nativeSeed = GetSeed(attacker);
    if (!nativeSeed) return false;
    auto predictedSeed = *nativeSeed;

    const auto passiveChance = GetUnitStat(
        attacker,
        PassiveCriticalStatId,
        0);
    if (probe) probe->passiveChance = passiveChance;

    if (mode == 0 && ResolveActiveWeapon && GetWeaponMasteryChance) {
        if (void* const weapon = ResolveActiveWeapon(attacker)) {
            const auto chance = GetWeaponMasteryChance(
                attacker,
                weapon,
                0,
                2);
            if (probe) probe->masteryChance = chance;
            if (chance > 0
                    && RollNativePercent(
                        predictedSeed.low,
                        predictedSeed.high,
                        chance)) {
                return true;
            }
        }
    }

    return passiveChance > 0
        && RollNativePercent(
            predictedSeed.low,
            predictedSeed.high,
            passiveChance);
}

void MarkCriticalDamage(void* damage) noexcept {
    if (!damage) return;
    std::scoped_lock lock(CriticalDamageMutex);
    void** empty{};
    for (auto& marker : CriticalDamageMarkers) {
        if (marker == damage) return;
        if (!marker && !empty) empty = &marker;
    }
    if (empty) {
        *empty = damage;
        CriticalMarkersCreated.fetch_add(1, std::memory_order_relaxed);
        if (Settings.diagnostics) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "CastTriggers diagnostic: critical marker created damage=%p.",
                damage);
            RecordDiagnostic(message);
        }
        return;
    }
    CriticalMarkerOverflows.fetch_add(1, std::memory_order_relaxed);
}

void PropagateCriticalDamageMarker(
        const void* source,
        void* destination) noexcept {
    if (!source || !destination) return;
    std::scoped_lock lock(CriticalDamageMutex);
    bool sourceMarked{};
    void** empty{};
    for (auto& marker : CriticalDamageMarkers) {
        if (marker == source) sourceMarked = true;
        if (marker == destination) return;
        if (!marker && !empty) empty = &marker;
    }
    if (!sourceMarked) return;
    if (empty) {
        *empty = destination;
        CriticalMarkersPropagated.fetch_add(1, std::memory_order_relaxed);
        if (Settings.diagnostics) {
            char message[224]{};
            std::snprintf(
                message,
                sizeof(message),
                "CastTriggers diagnostic: critical marker propagated source=%p destination=%p.",
                source,
                destination);
            RecordDiagnostic(message);
        }
        return;
    }
    CriticalMarkerOverflows.fetch_add(1, std::memory_order_relaxed);
}

void TransferCriticalDamageMarker(
        const void* source,
        void* destination) noexcept {
    if (!source || !destination || source == destination) return;
    bool transferred{};
    {
        std::scoped_lock lock(CriticalDamageMutex);
        for (auto& marker : CriticalDamageMarkers) {
            if (marker != source) continue;
            marker = destination;
            transferred = true;
            break;
        }
    }
    if (!transferred) return;
    CriticalMarkersPropagated.fetch_add(1, std::memory_order_relaxed);
    if (Settings.diagnostics) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: critical marker moved source=%p destination=%p.",
            source,
            destination);
        RecordDiagnostic(message);
    }
}

bool TakeCriticalDamageMarker(void* damage) noexcept {
    if (!damage) return false;
    std::scoped_lock lock(CriticalDamageMutex);
    for (auto& marker : CriticalDamageMarkers) {
        if (marker != damage) continue;
        marker = nullptr;
        CriticalMarkersConsumed.fetch_add(1, std::memory_order_relaxed);
        if (Settings.diagnostics) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "CastTriggers diagnostic: critical marker consumed damage=%p.",
                damage);
            RecordDiagnostic(message);
        }
        return true;
    }
    return false;
}

void RemoveCriticalDamageMarker(void* damage) noexcept {
    if (!damage) return;
    std::scoped_lock lock(CriticalDamageMutex);
    bool removed{};
    for (auto& marker : CriticalDamageMarkers) {
        if (marker != damage) continue;
        marker = nullptr;
        removed = true;
    }
    if (removed) {
        CriticalMarkersRemoved.fetch_add(1, std::memory_order_relaxed);
        if (Settings.diagnostics) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "CastTriggers diagnostic: critical marker removed damage=%p.",
                damage);
            RecordDiagnostic(message);
        }
    }
}

bool DispatchCombatTrigger(
        void* game,
        void* attacker,
        void* target,
        CombatTriggerKind kind) noexcept {
    if (!game || !attacker || !target || !OriginalDispatchUnitStatEvent
            || CastDispatchDepth != 0
            || !IsCombatTriggerEnabled(Settings.combatTriggers, kind)) {
        return false;
    }
    if (ProcExecutionDepth != 0) {
        CombatChainsSuppressed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const DispatchScope dispatch(
        0,
        attacker,
        NativeSourceTargetKind::Unit,
        target,
        0,
        0,
        SourceTriggerKind::None,
        -1,
        kind);
    OriginalDispatchUnitStatEvent(
        game,
        DoActiveEvent,
        attacker,
        target,
        nullptr);
    CombatDispatches.fetch_add(1, std::memory_order_relaxed);
    if (Settings.diagnostics) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: dispatched combat trigger=%s.",
            CombatTriggerName(kind));
        RecordDiagnostic(message);
    }
    return true;
}

void __fastcall HookFillDamageValues(
        void* game,
        void* attacker,
        void* defender,
        void* damage,
        std::int32_t mode,
        std::uint8_t sourceDamage) noexcept {
    const bool inspectCritical = Operational.load(std::memory_order_acquire)
        && ProcExecutionDepth == 0
        && attacker
        && GetUnitType(attacker) == PlayerUnitType;
    CriticalPredictionProbe probe{};
    bool predictedCritical{};
    if (inspectCritical) {
        // D2R reuses D2Damage storage between logical hits. Retire any marker
        // left on this address before establishing provenance for the new hit.
        RemoveCriticalDamageMarker(damage);
        CriticalPredictionChecks.fetch_add(1, std::memory_order_relaxed);
        predictedCritical = PredictNativeCriticalStrike(
            attacker,
            damage,
            mode,
            &probe);
        if (probe.passiveChance > 0) {
            CriticalPassiveChanceSeen.fetch_add(1, std::memory_order_relaxed);
        }
        if (predictedCritical) {
            CriticalPredicted.fetch_add(1, std::memory_order_relaxed);
        }
    }
    OriginalFillDamageValues(
        game,
        attacker,
        defender,
        damage,
        mode,
        sourceDamage);
    const auto hitFlags = damage
        ? ReadRecordValue<std::uint32_t>(
            static_cast<const std::uint8_t*>(damage),
            DamageHitFlagsOffset)
        : 0;
    const auto resultFlags = damage
        ? ReadRecordValue<std::uint16_t>(
            static_cast<const std::uint8_t*>(damage),
            DamageResultFlagsOffset)
        : 0;
    const bool nativeCriticalOrDeadly =
        (resultFlags & CriticalOrDeadlyResult) != 0;
    if (predictedCritical && nativeCriticalOrDeadly) {
        CriticalNativeFlagsConfirmed.fetch_add(1, std::memory_order_relaxed);
        MarkCriticalDamage(damage);
    }
    if (inspectCritical && Settings.diagnostics) {
        char message[416]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: critical probe fill damage=%p mode=%d mastery=%d passive=%d prevented=%d predicted=%d success=%d native-critical-or-deadly=%d hit-flags=0x%08X result-flags=0x%04X.",
            damage,
            mode,
            probe.masteryChance,
            probe.passiveChance,
            probe.prevented ? 1 : 0,
            predictedCritical ? 1 : 0,
            (resultFlags & ResultSuccess) != 0 ? 1 : 0,
            nativeCriticalOrDeadly ? 1 : 0,
            static_cast<unsigned>(hitFlags),
            static_cast<unsigned>(resultFlags));
        RecordDiagnostic(message);
    }
}

void* __fastcall HookCopyDamage(
        void* destination,
        const void* source) noexcept {
    void* const result = OriginalCopyDamage(destination, source);
    if (Operational.load(std::memory_order_acquire)) {
        PropagateCriticalDamageMarker(source, destination);
    }
    return result;
}

void* __fastcall HookMoveDamage(
        void* destination,
        void* source) noexcept {
    void* const result = OriginalMoveDamage(destination, source);
    if (Operational.load(std::memory_order_acquire)) {
        TransferCriticalDamageMarker(source, destination);
    }
    return result;
}

void __fastcall HookDestroyDamage(void* damage) noexcept {
    if (Operational.load(std::memory_order_acquire)) {
        RemoveCriticalDamageMarker(damage);
    }
    OriginalDestroyDamage(damage);
}

std::int32_t __fastcall HookEventFunc15(
        void* game,
        std::int32_t event,
        void* attacker,
        void* target,
        void* damage,
        std::int32_t argument6,
        std::int32_t argument7,
        std::int32_t argument8,
        void* argument9) noexcept {
    const auto result = OriginalEventFunc15(
        game, event, attacker, target, damage,
        argument6, argument7, argument8, argument9);
    auto* const frame = ActiveCombatObservation;
    if (result != 0 && frame
            && frame->game == game
            && frame->attacker == attacker
            && frame->target == target
            && frame->damage == damage
            && IsCombatTriggerEnabled(
                Settings.combatTriggers,
                CombatTriggerKind::OpenWounds)) {
        frame->openWounds = true;
    }
    return result;
}

std::int32_t __fastcall HookEventFunc16(
        void* game,
        std::int32_t event,
        void* attacker,
        void* target,
        void* damage,
        std::int32_t argument6,
        std::int32_t argument7,
        std::int32_t argument8,
        void* argument9) noexcept {
    const auto result = OriginalEventFunc16(
        game, event, attacker, target, damage,
        argument6, argument7, argument8, argument9);
    auto* const frame = ActiveCombatObservation;
    if (result != 0 && frame
            && frame->game == game
            && frame->attacker == attacker
            && frame->target == target
            && frame->damage == damage
            && IsCombatTriggerEnabled(
                Settings.combatTriggers,
                CombatTriggerKind::CrushingBlow)) {
        frame->crushingBlow = true;
    }
    return result;
}

std::int32_t __fastcall HookEventFunc20(
        void* game,
        std::int32_t event,
        void* attacker,
        void* target,
        void* damage,
        std::int32_t argument6,
        std::int32_t argument7,
        std::int32_t argument8,
        void* argument9) noexcept {
    if (Operational.load(std::memory_order_acquire)
            && !ShouldExposeSyntheticStat(
                Settings,
                ActiveSourceTrigger,
                ActiveSourceSkillId,
                ActiveCombatTrigger,
                PackedEventStatId(argument6))) {
        SyntheticStatsFiltered.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return OriginalEventFunc20(
        game, event, attacker, target, damage,
        argument6, argument7, argument8, argument9);
}

std::int32_t __fastcall HookDispatchUnitStatEvent(
        void* game,
        std::int32_t event,
        void* attacker,
        void* target,
        void* damage) noexcept {
    const bool observe = Operational.load(std::memory_order_acquire)
        && (event == MeleeDamageEvent || event == MissileDamageEvent)
        && game && attacker && target && damage
        && GetUnitType(attacker) == PlayerUnitType;
    if (!observe) {
        return OriginalDispatchUnitStatEvent(
            game, event, attacker, target, damage);
    }

    CombatObservationFrame frame{game, attacker, target, damage};
    std::int32_t result{};
    {
        const CombatObservationScope observation(&frame);
        result = OriginalDispatchUnitStatEvent(
            game, event, attacker, target, damage);
    }

    const auto resultFlags = ReadRecordValue<std::uint16_t>(
        static_cast<const std::uint8_t*>(damage),
        DamageResultFlagsOffset);
    const bool successful = (resultFlags & ResultSuccess) != 0;
    const bool hadCriticalMarker = TakeCriticalDamageMarker(damage);
    const bool critical = hadCriticalMarker && successful;
    if (successful
            && (resultFlags & CriticalOrDeadlyResult) != 0
            && !hadCriticalMarker) {
        CriticalEventFlagsWithoutMarker.fetch_add(
            1,
            std::memory_order_relaxed);
        if (Settings.diagnostics) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "CastTriggers diagnostic: combat event has native critical-or-deadly flag without marker damage=%p event=%d result-flags=0x%04X.",
                damage,
                event,
                static_cast<unsigned>(resultFlags));
            RecordDiagnostic(message);
        }
    } else if (hadCriticalMarker && Settings.diagnostics) {
        char message[240]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: combat event received critical marker damage=%p event=%d success=%d result-flags=0x%04X.",
            damage,
            event,
            successful ? 1 : 0,
            static_cast<unsigned>(resultFlags));
        RecordDiagnostic(message);
    }
    if (!successful) return result;

    if (ProcExecutionDepth != 0) {
        const auto suppressed = static_cast<std::uint64_t>(critical)
            + static_cast<std::uint64_t>(frame.crushingBlow)
            + static_cast<std::uint64_t>(frame.openWounds);
        CombatChainsSuppressed.fetch_add(suppressed, std::memory_order_relaxed);
        return result;
    }
    if (critical) {
        CriticalOutcomes.fetch_add(1, std::memory_order_relaxed);
        DispatchCombatTrigger(
            game, attacker, target, CombatTriggerKind::CriticalStrike);
    }
    if (frame.crushingBlow) {
        CrushingBlowOutcomes.fetch_add(1, std::memory_order_relaxed);
        DispatchCombatTrigger(
            game, attacker, target, CombatTriggerKind::CrushingBlow);
    }
    if (frame.openWounds) {
        OpenWoundsOutcomes.fetch_add(1, std::memory_order_relaxed);
        DispatchCombatTrigger(
            game, attacker, target, CombatTriggerKind::OpenWounds);
    }
    return result;
}

SourceTriggerKind ClassifyEligibleSourceRecord(
        void* game,
        std::int32_t skillId,
        std::uint64_t& flags) noexcept {
    flags = 0;
    if (!game || skillId < MinimumSkillId
            || skillId > MaximumSkillId) {
        return SourceTriggerKind::None;
    }
    const auto dataContext = ReadRecordValue<std::uint8_t>(
        static_cast<const std::uint8_t*>(game),
        GameDataContextOffset);
    const auto* record = GetSkillsRecord(dataContext, skillId);
    if (!record) return SourceTriggerKind::None;
    flags = ReadRecordValue<std::uint64_t>(
        record,
        SkillFlagsOffset);
    const auto animation = ReadRecordValue<std::uint8_t>(
        record,
        SkillAnimationOffset);
    const auto sequenceTransition = ReadRecordValue<std::uint8_t>(
        record,
        SkillSequenceTransitionOffset);
    const auto kind = ClassifySourceSkillRecord(
        animation,
        sequenceTransition,
        flags);
    if (kind == SourceTriggerKind::OnCast) {
        return IsConfiguredSourceSkill(Settings, skillId)
            ? kind
            : SourceTriggerKind::None;
    }
    if (kind == SourceTriggerKind::WhileChanneling) {
        return IsConfiguredChannelingSourceSkill(Settings, skillId)
            ? kind
            : SourceTriggerKind::None;
    }
    return SourceTriggerKind::None;
}

bool IsEligibleAttackAttemptRecord(
        void* game,
        std::int32_t skillId) noexcept {
    if (!game || skillId < MinimumSkillId || skillId > MaximumSkillId
            || !IsCombatTriggerEnabled(
                Settings.combatTriggers,
                CombatTriggerKind::AttackAttempt)) {
        return false;
    }
    const auto dataContext = ReadRecordValue<std::uint8_t>(
        static_cast<const std::uint8_t*>(game),
        GameDataContextOffset);
    const auto* record = GetSkillsRecord(dataContext, skillId);
    if (!record) return false;
    return IsAttackAnimation(
        ReadRecordValue<std::uint8_t>(record, SkillAnimationOffset),
        ReadRecordValue<std::uint8_t>(
            record,
            SkillSequenceTransitionOffset));
}

const char* NativeTargetKindName(NativeSourceTargetKind kind) noexcept;

bool DispatchAttackAttemptFromInput(
        void* game,
        void* unit,
        std::int32_t skillId,
        const NativeSourceTargetDescriptor& descriptor,
        std::int32_t inputResult) noexcept {
    if (!IsAcceptedPlayerSkillInput(inputResult) || !game || !unit
            || descriptor.kind == NativeSourceTargetKind::None
            || !OriginalDispatchUnitStatEvent
            || !Operational.load(std::memory_order_acquire)
            || CastDispatchDepth != 0
            || ProcExecutionDepth != 0
            || GetUnitType(unit) != PlayerUnitType
            || !IsEligibleAttackAttemptRecord(game, skillId)) {
        return false;
    }
    const DispatchScope dispatch(
        0,
        unit,
        descriptor.kind,
        descriptor.unit,
        descriptor.x,
        descriptor.y,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::AttackAttempt);
    OriginalDispatchUnitStatEvent(
        game,
        DoActiveEvent,
        unit,
        unit,
        nullptr);
    EventDispatches.fetch_add(1, std::memory_order_relaxed);
    CombatDispatches.fetch_add(1, std::memory_order_relaxed);
    if (Settings.diagnostics) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: dispatched attack-attempt source skill=%d input-result=%d target=%s target-unit=%d target-position=(%d,%d).",
            skillId,
            inputResult,
            NativeTargetKindName(descriptor.kind),
            descriptor.unit ? 1 : 0,
            descriptor.x,
            descriptor.y);
        RecordDiagnostic(message);
    }
    return true;
}

std::uint32_t ReadCurrentGameFrame(void* game) noexcept {
    return ReadRecordValue<std::uint32_t>(
        static_cast<const std::uint8_t*>(game),
        GameFrameOffset);
}

std::int32_t ReadActiveSkillId(void* activeSkill) noexcept {
    if (!activeSkill) return -1;
    const auto* const record = ReadRecordValue<const std::uint8_t*>(
        static_cast<const std::uint8_t*>(activeSkill),
        ActiveSkillRecordPointerOffset);
    if (!record) return -1;
    return static_cast<std::int32_t>(ReadRecordValue<std::uint16_t>(
        record,
        SkillRecordIdOffset));
}

const char* NativeTargetKindName(NativeSourceTargetKind kind) noexcept {
    switch (kind) {
    case NativeSourceTargetKind::Unit: return "unit";
    case NativeSourceTargetKind::Position: return "position";
    case NativeSourceTargetKind::None: return "none";
    }
    return "unknown";
}

void CapturePlayerInputTarget(
        void* game,
        void* unit,
        std::int32_t skillId,
        NativeSourceTargetKind kind,
        std::int32_t targetType,
        std::int32_t targetGuid,
        std::int32_t x,
        std::int32_t y) noexcept {
    if (!game || !unit || skillId < MinimumSkillId
            || skillId > MaximumSkillId
            || kind == NativeSourceTargetKind::None) {
        return;
    }
    const auto currentFrame = ReadCurrentGameFrame(game);
    {
        std::scoped_lock lock(PlayerInputTargetMutex);
        ++PlayerInputTargetSequence;
        PlayerInputTargetEntry* matchingEntry{};
        PlayerInputTargetEntry* emptyEntry{};
        PlayerInputTargetEntry* oldestEntry =
            &PlayerInputTargetEntries.front();
        for (auto& entry : PlayerInputTargetEntries) {
            if (entry.game == game && entry.unit == unit) {
                matchingEntry = &entry;
                break;
            }
            if (!entry.game && !emptyEntry) emptyEntry = &entry;
            if (entry.sequence < oldestEntry->sequence) {
                oldestEntry = &entry;
            }
        }
        auto* const selected = matchingEntry
            ? matchingEntry
            : (emptyEntry ? emptyEntry : oldestEntry);
        *selected = {
            game,
            unit,
            skillId,
            kind,
            targetType,
            targetGuid,
            x,
            y,
            currentFrame,
            PlayerInputTargetSequence,
            false,
        };
    }
    PlayerInputsCaptured.fetch_add(1, std::memory_order_relaxed);
    if (Settings.diagnostics) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: input captured skill=%d frame=%u target=%s target-type=%d target-guid=%d target-position=(%d,%d).",
            skillId,
            currentFrame,
            NativeTargetKindName(kind),
            targetType,
            targetGuid,
            x,
            y);
        RecordDiagnostic(message);
    }
}

bool ResolvePlayerInputTarget(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::uint32_t currentFrame,
        bool channeling,
        NativeSourceTargetDescriptor& descriptor,
        bool& reusedChannelTarget,
        bool& expired) noexcept {
    descriptor = {};
    reusedChannelTarget = false;
    expired = false;
    PlayerInputTargetEntry captured{};
    {
        std::scoped_lock lock(PlayerInputTargetMutex);
        auto* matchingEntry = static_cast<PlayerInputTargetEntry*>(nullptr);
        for (auto& entry : PlayerInputTargetEntries) {
            if (entry.game == game && entry.unit == unit
                    && entry.skillId == skillId) {
                matchingEntry = &entry;
                break;
            }
        }
        if (!matchingEntry) return false;
        if (!IsPendingInputFresh(
                currentFrame,
                matchingEntry->capturedFrame,
                matchingEntry->retainedForChannel)) {
            *matchingEntry = {};
            matchingEntry->skillId = -1;
            expired = true;
            PlayerInputsExpired.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        captured = *matchingEntry;
        reusedChannelTarget = captured.retainedForChannel;
        if (channeling) {
            matchingEntry->retainedForChannel = true;
        } else {
            *matchingEntry = {};
            matchingEntry->skillId = -1;
        }
    }

    if (captured.kind == NativeSourceTargetKind::Unit) {
        descriptor.unit = GetServerUnit
            ? GetServerUnit(
                game,
                captured.targetType,
                captured.targetGuid)
            : nullptr;
        if (!descriptor.unit) return false;
        descriptor.kind = NativeSourceTargetKind::Unit;
    } else if (captured.kind == NativeSourceTargetKind::Position) {
        descriptor.kind = NativeSourceTargetKind::Position;
        descriptor.x = captured.x;
        descriptor.y = captured.y;
    } else {
        return false;
    }

    if (reusedChannelTarget) {
        ChannelTargetsReused.fetch_add(1, std::memory_order_relaxed);
    } else {
        PlayerInputsConsumed.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

bool ShouldDispatchChanneling(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::uint32_t currentFrame) noexcept {
    std::scoped_lock lock(ChannelingCadenceMutex);
    ++ChannelingObservationSequence;

    ChannelingCadenceEntry* emptyEntry{};
    ChannelingCadenceEntry* oldestEntry = &ChannelingCadenceEntries.front();
    for (auto& entry : ChannelingCadenceEntries) {
        if (entry.game == game && entry.unit == unit
                && entry.skillId == skillId) {
            entry.lastObservationSequence = ChannelingObservationSequence;
            if (!HasChannelingIntervalElapsed(
                    currentFrame,
                    entry.lastDispatchFrame,
                    Settings.whileChanneling.intervalFrames)) {
                return false;
            }
            entry.lastDispatchFrame = currentFrame;
            return true;
        }
        if (!entry.game && !emptyEntry) emptyEntry = &entry;
        if (entry.lastObservationSequence
                < oldestEntry->lastObservationSequence) {
            oldestEntry = &entry;
        }
    }

    auto* const selected = emptyEntry ? emptyEntry : oldestEntry;
    *selected = {
        game,
        unit,
        skillId,
        currentFrame,
        ChannelingObservationSequence,
    };
    return true;
}

void CommitObservedSourcePosition() noexcept {
    if (!ObservedSourceTargetXReady || !ObservedSourceTargetYReady) return;
    ObservedSourceTargetKind = NativeSourceTargetKind::Position;
    ObservedSourceTarget = nullptr;
    ObservedSourceTargetXReady = false;
    ObservedSourceTargetYReady = false;
}

void* __fastcall HookGetTargetUnit(void* game, void* unit) noexcept {
    void* const target = OriginalGetTargetUnit(game, unit);
    if (!SourceTargetObservationActive
            || game != SourceTargetObservationGame
            || unit != SourceTargetObservationUnit) {
        return target;
    }

    ObservedSourceTargetXReady = false;
    ObservedSourceTargetYReady = false;
    if (target) {
        ObservedSourceTargetKind = NativeSourceTargetKind::Unit;
        ObservedSourceTarget = target;
    } else {
        ObservedSourceTargetKind = NativeSourceTargetKind::None;
        ObservedSourceTarget = nullptr;
    }
    return target;
}

std::int32_t __fastcall HookPathGetFirstPointX(void* path) noexcept {
    const auto x = OriginalPathGetFirstPointX(path);
    if (SourceTargetObservationActive
            && path == SourceTargetObservationPath
            && ObservedSourceTargetKind != NativeSourceTargetKind::Unit) {
        ObservedSourceTargetX = x;
        ObservedSourceTargetXReady = true;
        CommitObservedSourcePosition();
    }
    return x;
}

std::int32_t __fastcall HookPathGetFirstPointY(void* path) noexcept {
    const auto y = OriginalPathGetFirstPointY(path);
    if (SourceTargetObservationActive
            && path == SourceTargetObservationPath
            && ObservedSourceTargetKind != NativeSourceTargetKind::Unit) {
        ObservedSourceTargetY = y;
        ObservedSourceTargetYReady = true;
        CommitObservedSourcePosition();
    }
    return y;
}

std::int32_t __fastcall HookPlayerSkillPositionInput(
        void* game,
        void* unit,
        std::int32_t x,
        std::int32_t y,
        std::int32_t argument,
        void* activeSkill) noexcept {
    const auto skillId = ReadActiveSkillId(activeSkill);
    CapturePlayerInputTarget(
        game,
        unit,
        skillId,
        NativeSourceTargetKind::Position,
        0,
        0,
        x,
        y);
    const auto result = OriginalPlayerSkillPositionInput(
        game,
        unit,
        x,
        y,
        argument,
        activeSkill);
    DispatchAttackAttemptFromInput(
        game,
        unit,
        skillId,
        {NativeSourceTargetKind::Position, nullptr, x, y},
        result);
    return result;
}

std::int32_t __fastcall HookPlayerSkillUnitInput(
        void* game,
        void* unit,
        std::int32_t targetType,
        std::int32_t targetGuid,
        void* activeSkill,
        std::int32_t argument6,
        std::int32_t argument7) noexcept {
    void* const target = game && GetServerUnit
        ? GetServerUnit(game, targetType, targetGuid)
        : nullptr;
    const auto skillId = ReadActiveSkillId(activeSkill);
    if (target) {
        CapturePlayerInputTarget(
            game,
            unit,
            skillId,
            NativeSourceTargetKind::Unit,
            targetType,
            targetGuid,
            0,
            0);
    }
    const auto result = OriginalPlayerSkillUnitInput(
        game,
        unit,
        targetType,
        targetGuid,
        activeSkill,
        argument6,
        argument7);
    if (target) {
        DispatchAttackAttemptFromInput(
            game,
            unit,
            skillId,
            {NativeSourceTargetKind::Unit, target, 0, 0},
            result);
    }
    return result;
}

std::int32_t __fastcall HookCastItemSkillOnTarget(
        void* caster,
        std::int32_t skillId,
        std::int32_t skillLevel,
        void* target,
        std::int32_t flag) noexcept {
    const bool inCastDispatch = Operational.load(std::memory_order_acquire)
        && CastDispatchDepth != 0;
    const bool sameLevel = inCastDispatch
        && skillLevel == SameLevelMarker
        && CastDispatchSourceLevel > 0;
    const auto requestedLevel = skillLevel;
    if (sameLevel) skillLevel = CastDispatchSourceLevel;

    const bool resolveTarget = inCastDispatch
        && ShouldResolveTriggeredSkillTarget(
            caster == CastDispatchSourceUnit,
            target == CastDispatchSourceUnit,
            flag);
    const auto sourceTargetKind = CastDispatchSourceTargetKind;
    void* const sourceTarget = CastDispatchSourceTarget;
    const auto positionX = CastDispatchTargetX;
    const auto positionY = CastDispatchTargetY;

    const SuspendDispatchScope suspend;
    const ProcExecutionScope procExecution;
    std::int32_t positionResult = -1;
    std::int32_t unitTargetResult = -1;
    std::int32_t result{};
    const bool useUnitTarget = ShouldUseNativeUnitTarget(
        resolveTarget,
        sourceTargetKind);
    const bool usePositionTarget = ShouldUseNativePositionTarget(
        resolveTarget,
        sourceTargetKind);
    const char* routeName = "original-target";
    if (useUnitTarget) {
        routeName = "native-unit-target";
        unitTargetResult = OriginalCastItemSkillOnTarget(
            caster,
            skillId,
            skillLevel,
            sourceTarget,
            NativeNormalSkillTargetFlag);
        result = unitTargetResult;
    } else if (usePositionTarget) {
        routeName = "native-position";
        positionResult = OriginalCastItemSkillAtPosition(
            caster,
            skillId,
            skillLevel,
            positionX,
            positionY,
            NativeNormalSkillTargetFlag);
        result = positionResult;
    } else if (resolveTarget) {
        routeName = "native-self-target";
        unitTargetResult = OriginalCastItemSkillOnTarget(
            caster,
            skillId,
            skillLevel,
            caster,
            NativeNormalSkillTargetFlag);
        result = unitTargetResult;
    } else {
        result = OriginalCastItemSkillOnTarget(
            caster, skillId, skillLevel, target, flag);
    }
    if (inCastDispatch && result != 0) {
        if (sameLevel) {
            SameLevelProcs.fetch_add(1, std::memory_order_relaxed);
        } else {
            FixedLevelProcs.fetch_add(1, std::memory_order_relaxed);
        }
        if (usePositionTarget) {
            NativePositionProcs.fetch_add(1, std::memory_order_relaxed);
        } else if (useUnitTarget) {
            NativeUnitTargetProcs.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (inCastDispatch && Settings.diagnostics) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: %s proc skill=%d requested-level=%d effective-level=%d position-result=%d unit-target-result=%d result=%d.",
            routeName,
            skillId,
            requestedLevel,
            skillLevel,
            positionResult,
            unitTargetResult,
            result);
        RecordDiagnostic(message);
    }
    return result;
}

std::int32_t __fastcall HookCastItemSkillAtPosition(
        void* caster,
        std::int32_t skillId,
        std::int32_t skillLevel,
        std::int32_t x,
        std::int32_t y,
        std::int32_t flag) noexcept {
    const bool inCastDispatch = Operational.load(std::memory_order_acquire)
        && CastDispatchDepth != 0;
    const bool sameLevel = inCastDispatch
        && skillLevel == SameLevelMarker
        && CastDispatchSourceLevel > 0;
    const auto requestedLevel = skillLevel;
    if (sameLevel) skillLevel = CastDispatchSourceLevel;

    const SuspendDispatchScope suspend;
    const ProcExecutionScope procExecution;
    const auto result = OriginalCastItemSkillAtPosition(
        caster,
        skillId,
        skillLevel,
        x,
        y,
        inCastDispatch ? NativeNormalSkillTargetFlag : flag);
    if (inCastDispatch && result != 0) {
        if (sameLevel) {
            SameLevelProcs.fetch_add(1, std::memory_order_relaxed);
        } else {
            FixedLevelProcs.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (inCastDispatch && Settings.diagnostics) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: position proc skill=%d requested-level=%d effective-level=%d result=%d.",
            skillId,
            requestedLevel,
            skillLevel,
            result);
        RecordDiagnostic(message);
    }
    return result;
}

std::int32_t __fastcall HookSkillHandler(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::int32_t skillLevel,
        std::int32_t consumeResources,
        std::int32_t itemCast,
        std::int32_t itemEffect) noexcept {
    const auto unitType = unit ? GetUnitType(unit) : 6;
    const bool preCastCandidate =
        Operational.load(std::memory_order_acquire)
        && CastDispatchDepth == 0
        && ProcExecutionDepth == 0
        && !SourceTargetObservationActive
        && unitType == PlayerUnitType
        && skillLevel > 0
        && consumeResources == 1
        && itemCast == 0
        && itemEffect == 0;
    std::uint64_t sourceFlags{};
    const auto sourceTriggerKind = preCastCandidate
        ? ClassifyEligibleSourceRecord(game, skillId, sourceFlags)
        : SourceTriggerKind::None;
    const bool eligibleSourceRecord =
        sourceTriggerKind != SourceTriggerKind::None;
    NativeSourceTargetDescriptor sourceDescriptor{};
    bool usedPlayerInputDescriptor = false;
    bool reusedChannelTarget = false;
    bool expiredPlayerInput = false;
    std::int32_t nativeResult{};
    if (eligibleSourceRecord) {
        void* const sourcePath = GetDynamicPath(unit);
        SourceTargetObservationScope observation(game, unit, sourcePath);
        nativeResult = OriginalSkillHandler(
            game,
            unit,
            skillId,
            skillLevel,
            consumeResources,
            itemCast,
            itemEffect);
        sourceDescriptor = observation.Snapshot();
    } else {
        nativeResult = OriginalSkillHandler(
            game,
            unit,
            skillId,
            skillLevel,
            consumeResources,
            itemCast,
            itemEffect);
    }
    if (!Operational.load(std::memory_order_acquire)
            || CastDispatchDepth != 0) {
        return nativeResult;
    }

    ManualCastsObserved.fetch_add(1, std::memory_order_relaxed);
    if (!IsManualPlayerCast(
            nativeResult,
            consumeResources,
            itemCast,
            itemEffect,
            unitType)
            || skillLevel <= 0
            || !eligibleSourceRecord) {
        return nativeResult;
    }

    EligibleCasts.fetch_add(1, std::memory_order_relaxed);
    const auto sourceFrame = ReadCurrentGameFrame(game);
    NativeSourceTargetDescriptor inputDescriptor{};
    if (ResolvePlayerInputTarget(
            game,
            unit,
            skillId,
            sourceFrame,
            sourceTriggerKind == SourceTriggerKind::WhileChanneling,
            inputDescriptor,
            reusedChannelTarget,
            expiredPlayerInput)) {
        sourceDescriptor = inputDescriptor;
        usedPlayerInputDescriptor = true;
    } else if (expiredPlayerInput && Settings.diagnostics) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: input expired skill=%d frame=%u; handler target retained.",
            skillId,
            sourceFrame);
        RecordDiagnostic(message);
    }
    std::uint32_t channelingFrame{};
    if (sourceTriggerKind == SourceTriggerKind::WhileChanneling) {
        ChannelingTicksObserved.fetch_add(1, std::memory_order_relaxed);
        channelingFrame = sourceFrame;
        if (!ShouldDispatchChanneling(
                game,
                unit,
                skillId,
                channelingFrame)) {
            ChannelingTicksThrottled.fetch_add(
                1,
                std::memory_order_relaxed);
            if (Settings.diagnostics) {
                char message[256]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "CastTriggers diagnostic: channel source skill=%d frame=%u throttled; interval=%u.",
                    skillId,
                    channelingFrame,
                    Settings.whileChanneling.intervalFrames);
                RecordDiagnostic(message);
            }
            return nativeResult;
        }
        ChannelingDispatches.fetch_add(1, std::memory_order_relaxed);
    }
    if (Settings.diagnostics) {
        const char* triggerKind = sourceTriggerKind
                == SourceTriggerKind::WhileChanneling
            ? "channel"
            : "cast";
        const char* descriptorSource = usedPlayerInputDescriptor
            ? (reusedChannelTarget ? "channel-input" : "input")
            : "handler";
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "CastTriggers diagnostic: eligible %s source skill=%d effective-level=%d flags=0x%llX frame=%u descriptor-source=%s target=%s target-unit=%d target-position=(%d,%d).",
            triggerKind,
            skillId,
            skillLevel,
            static_cast<unsigned long long>(sourceFlags),
            sourceFrame,
            descriptorSource,
            NativeTargetKindName(sourceDescriptor.kind),
            sourceDescriptor.unit ? 1 : 0,
            sourceDescriptor.x,
            sourceDescriptor.y);
        RecordDiagnostic(message);
    }
    const DispatchScope dispatch(
        skillLevel,
        unit,
        sourceDescriptor.kind,
        sourceDescriptor.unit,
        sourceDescriptor.x,
        sourceDescriptor.y,
        sourceTriggerKind,
        skillId);
    OriginalDispatchUnitStatEvent(
        game,
        DoActiveEvent,
        unit,
        unit,
        nullptr);
    EventDispatches.fetch_add(1, std::memory_order_relaxed);
    return nativeResult;
}

bool InstallHooks() noexcept {
    if (!Context->InstallInlineHook(
            DispatchUnitStatEventRva,
            DispatchUnitStatEventExpected.data(),
            static_cast<std::uint32_t>(
                DispatchUnitStatEventExpected.size()),
            HookDispatchUnitStatEvent,
            &OriginalDispatchUnitStatEvent)) {
        Context->LogError(
            "CastTriggers: unit-stat event dispatcher hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            EventFunc20Rva,
            EventFunc20Expected.data(),
            static_cast<std::uint32_t>(EventFunc20Expected.size()),
            HookEventFunc20,
            &OriginalEventFunc20)) {
        Context->LogError(
            "CastTriggers: item-skill callback hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            EventFunc15Rva,
            EventFunc15Expected.data(),
            static_cast<std::uint32_t>(EventFunc15Expected.size()),
            HookEventFunc15,
            &OriginalEventFunc15)) {
        Context->LogError(
            "CastTriggers: Open Wounds callback hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            EventFunc16Rva,
            EventFunc16Expected.data(),
            static_cast<std::uint32_t>(EventFunc16Expected.size()),
            HookEventFunc16,
            &OriginalEventFunc16)) {
        Context->LogError(
            "CastTriggers: Crushing Blow callback hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            FillDamageValuesRva,
            FillDamageValuesExpected.data(),
            static_cast<std::uint32_t>(FillDamageValuesExpected.size()),
            HookFillDamageValues,
            &OriginalFillDamageValues)) {
        Context->LogError(
            "CastTriggers: damage builder hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            CopyDamageRva,
            CopyDamageExpected.data(),
            static_cast<std::uint32_t>(CopyDamageExpected.size()),
            HookCopyDamage,
            &OriginalCopyDamage)) {
        Context->LogError(
            "CastTriggers: damage copy hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            MoveDamageRva,
            MoveDamageExpected.data(),
            static_cast<std::uint32_t>(MoveDamageExpected.size()),
            HookMoveDamage,
            &OriginalMoveDamage)) {
        Context->LogError(
            "CastTriggers: damage move hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            DestroyDamageRva,
            DestroyDamageExpected.data(),
            static_cast<std::uint32_t>(DestroyDamageExpected.size()),
            HookDestroyDamage,
            &OriginalDestroyDamage)) {
        Context->LogError(
            "CastTriggers: damage destructor hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            PlayerSkillPositionInputRva,
            PlayerSkillPositionInputExpected.data(),
            static_cast<std::uint32_t>(
                PlayerSkillPositionInputExpected.size()),
            HookPlayerSkillPositionInput,
            &OriginalPlayerSkillPositionInput)) {
        Context->LogError(
            "CastTriggers: player position input executor hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            PlayerSkillUnitInputRva,
            PlayerSkillUnitInputExpected.data(),
            static_cast<std::uint32_t>(
                PlayerSkillUnitInputExpected.size()),
            HookPlayerSkillUnitInput,
            &OriginalPlayerSkillUnitInput)) {
        Context->LogError(
            "CastTriggers: player unit input executor hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            GetTargetUnitRva,
            GetTargetUnitExpected.data(),
            static_cast<std::uint32_t>(GetTargetUnitExpected.size()),
            HookGetTargetUnit,
            &OriginalGetTargetUnit)) {
        Context->LogError(
            "CastTriggers: native target observer hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            PathGetFirstPointXRva,
            PathGetFirstPointXExpected.data(),
            static_cast<std::uint32_t>(
                PathGetFirstPointXExpected.size()),
            HookPathGetFirstPointX,
            &OriginalPathGetFirstPointX)) {
        Context->LogError(
            "CastTriggers: native target X observer hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            PathGetFirstPointYRva,
            PathGetFirstPointYExpected.data(),
            static_cast<std::uint32_t>(
                PathGetFirstPointYExpected.size()),
            HookPathGetFirstPointY,
            &OriginalPathGetFirstPointY)) {
        Context->LogError(
            "CastTriggers: native target Y observer hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            CastItemSkillOnTargetRva,
            CastItemSkillOnTargetExpected.data(),
            static_cast<std::uint32_t>(
                CastItemSkillOnTargetExpected.size()),
            HookCastItemSkillOnTarget,
            &OriginalCastItemSkillOnTarget)) {
        Context->LogError(
            "CastTriggers: target item-skill caster hook is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            CastItemSkillAtPositionRva,
            CastItemSkillAtPositionExpected.data(),
            static_cast<std::uint32_t>(
                CastItemSkillAtPositionExpected.size()),
            HookCastItemSkillAtPosition,
            &OriginalCastItemSkillAtPosition)) {
        Context->LogError(
            "CastTriggers: position item-skill caster hook is already owned or unavailable.");
        return false;
    }

    if (!Context->InstallInlineHook(
            SkillHandlerRva,
            SkillHandlerExpected.data(),
            static_cast<std::uint32_t>(SkillHandlerExpected.size()),
            HookSkillHandler,
            &OriginalSkillHandler)) {
        Context->LogError(
            "CastTriggers: central skill handler hook is already owned or unavailable.");
        return false;
    }
    return true;
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char summary[768]{};
    std::snprintf(
        summary,
        sizeof(summary),
        "Cast Triggers 1.0.0: %s; observed=%llu; eligible=%llu; cast dispatches=%llu; fixed procs=%llu; same-level procs=%llu; native-position procs=%llu; native-unit-target procs=%llu; config=%s.",
        Operational.load(std::memory_order_acquire) ? "active" : "disabled",
        static_cast<unsigned long long>(
            ManualCastsObserved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            EligibleCasts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            EventDispatches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            FixedLevelProcs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            SameLevelProcs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativePositionProcs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeUnitTargetProcs.load(std::memory_order_relaxed)),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(summary);

    char cadence[768]{};
    std::snprintf(
        cadence,
        sizeof(cadence),
        "Cast Triggers cadence/input: channel ticks=%llu; channel dispatches=%llu; channel throttled=%llu; inputs captured=%llu; inputs consumed=%llu; inputs expired=%llu; channel targets reused=%llu.",
        static_cast<unsigned long long>(
            ChannelingTicksObserved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ChannelingDispatches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ChannelingTicksThrottled.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PlayerInputsCaptured.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PlayerInputsConsumed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PlayerInputsExpired.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ChannelTargetsReused.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(cadence);

    char combat[768]{};
    std::snprintf(
        combat,
        sizeof(combat),
        "Cast Triggers combat: critical=%llu; crushing-blow=%llu; open-wounds=%llu; combat dispatches=%llu; combat chains suppressed=%llu; trigger stats filtered=%llu; critical checks=%llu; passive chance seen=%llu; predicted=%llu; native flags confirmed=%llu; markers created=%llu; markers propagated=%llu; markers consumed=%llu; markers removed=%llu; event flags without marker=%llu; critical marker overflows=%llu.",
        static_cast<unsigned long long>(
            CriticalOutcomes.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CrushingBlowOutcomes.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            OpenWoundsOutcomes.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CombatDispatches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CombatChainsSuppressed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            SyntheticStatsFiltered.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalPredictionChecks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalPassiveChanceSeen.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalPredicted.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalNativeFlagsConfirmed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalMarkersCreated.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalMarkersPropagated.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalMarkersConsumed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalMarkersRemoved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalEventFlagsWithoutMarker.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            CriticalMarkerOverflows.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(combat);
    if (Settings.diagnostics) {
        const auto trace = SnapshotDiagnosticTrace();
        char traceSummary[192]{};
        std::snprintf(
            traceSummary,
            sizeof(traceSummary),
            "Cast Triggers diagnostics: retained=%zu/%zu; total=%llu; combat-hook logging=deferred.",
            trace.count,
            MaximumDiagnosticTraceEntries,
            static_cast<unsigned long long>(trace.total));
        command->plugin->WriteConsoleMessage(traceSummary);
        for (std::size_t index = 0; index < trace.count; ++index) {
            const auto& entry = trace.entries[index];
            char traceMessage[MaximumDiagnosticTraceMessageLength + 48]{};
            std::snprintf(
                traceMessage,
                sizeof(traceMessage),
                "#%llu %s",
                static_cast<unsigned long long>(entry.sequence),
                entry.message.data());
            command->plugin->WriteConsoleMessage(traceMessage);
        }
    }
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    CastDispatchDepth = 0;
    ProcExecutionDepth = 0;
    ActiveSourceTrigger = SourceTriggerKind::None;
    ActiveSourceSkillId = -1;
    ActiveCombatTrigger = CombatTriggerKind::None;
    CastDispatchSourceLevel = 0;
    CastDispatchSourceUnit = nullptr;
    CastDispatchSourceTargetKind = NativeSourceTargetKind::None;
    CastDispatchSourceTarget = nullptr;
    CastDispatchTargetX = 0;
    CastDispatchTargetY = 0;
    SourceTargetObservationActive = false;
    SourceTargetObservationGame = nullptr;
    SourceTargetObservationUnit = nullptr;
    SourceTargetObservationPath = nullptr;
    ObservedSourceTargetKind = NativeSourceTargetKind::None;
    ObservedSourceTarget = nullptr;
    ObservedSourceTargetX = 0;
    ObservedSourceTargetY = 0;
    ObservedSourceTargetXReady = false;
    ObservedSourceTargetYReady = false;
    ActiveCombatObservation = nullptr;
    ManualCastsObserved.store(0, std::memory_order_relaxed);
    EligibleCasts.store(0, std::memory_order_relaxed);
    EventDispatches.store(0, std::memory_order_relaxed);
    FixedLevelProcs.store(0, std::memory_order_relaxed);
    SameLevelProcs.store(0, std::memory_order_relaxed);
    NativePositionProcs.store(0, std::memory_order_relaxed);
    NativeUnitTargetProcs.store(0, std::memory_order_relaxed);
    ChannelingTicksObserved.store(0, std::memory_order_relaxed);
    ChannelingDispatches.store(0, std::memory_order_relaxed);
    ChannelingTicksThrottled.store(0, std::memory_order_relaxed);
    PlayerInputsCaptured.store(0, std::memory_order_relaxed);
    PlayerInputsConsumed.store(0, std::memory_order_relaxed);
    PlayerInputsExpired.store(0, std::memory_order_relaxed);
    ChannelTargetsReused.store(0, std::memory_order_relaxed);
    CriticalOutcomes.store(0, std::memory_order_relaxed);
    CrushingBlowOutcomes.store(0, std::memory_order_relaxed);
    OpenWoundsOutcomes.store(0, std::memory_order_relaxed);
    CombatDispatches.store(0, std::memory_order_relaxed);
    CombatChainsSuppressed.store(0, std::memory_order_relaxed);
    SyntheticStatsFiltered.store(0, std::memory_order_relaxed);
    CriticalMarkerOverflows.store(0, std::memory_order_relaxed);
    CriticalPredictionChecks.store(0, std::memory_order_relaxed);
    CriticalPassiveChanceSeen.store(0, std::memory_order_relaxed);
    CriticalPredicted.store(0, std::memory_order_relaxed);
    CriticalNativeFlagsConfirmed.store(0, std::memory_order_relaxed);
    CriticalMarkersCreated.store(0, std::memory_order_relaxed);
    CriticalMarkersPropagated.store(0, std::memory_order_relaxed);
    CriticalMarkersConsumed.store(0, std::memory_order_relaxed);
    CriticalMarkersRemoved.store(0, std::memory_order_relaxed);
    CriticalEventFlagsWithoutMarker.store(0, std::memory_order_relaxed);
    {
        std::scoped_lock lock(PlayerInputTargetMutex);
        PlayerInputTargetEntries = {};
        PlayerInputTargetSequence = 0;
    }
    {
        std::scoped_lock lock(ChannelingCadenceMutex);
        ChannelingCadenceEntries = {};
        ChannelingObservationSequence = 0;
    }
    {
        std::scoped_lock lock(CriticalDamageMutex);
        CriticalDamageMarkers = {};
    }
    {
        std::scoped_lock lock(DiagnosticTraceMutex);
        DiagnosticTraceEntries = {};
        DiagnosticTraceTotal = 0;
        DiagnosticTraceCount = 0;
        DiagnosticTraceNext = 0;
    }
}

} // namespace
} // namespace RuffnecKk::CastTriggers

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-cast-triggers",
    .name = "Cast Triggers",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description =
        "Triggers item skills from spells, attack attempts, and combat outcomes.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    using namespace RuffnecKk::CastTriggers;
    Context = context;
    Base = context ? reinterpret_cast<std::uint8_t*>(context->exeBase) : nullptr;
    ResetState();
    if (!Context || !Base) return false;

    LogRuntimeIdentity();
    if (!LoadConfig()) return false;
    if (!Settings.enabled) {
        const auto message = std::string(
            "Cast Triggers 1.0.0 by RuffnecKk loaded disabled; config=")
            + LoadedConfigPath + ".";
        context->LogInfo(message.c_str());
        return true;
    }
    if (!ValidateNativeFingerprint()) return false;
    ResolveNativeFunctions();
    if (!InstallHooks()) return false;

    Operational.store(true, std::memory_order_release);
    if (!context->RegisterConsoleCommand(
            "cast-triggers",
            Status,
            "Show Cast Triggers counters and retained diagnostics.")) {
        context->LogWarn(
            "CastTriggers: status command could not be registered.");
    }
    const auto message = std::string(
        "Cast Triggers 1.0.0 by RuffnecKk active; native fingerprint accepted; config=")
        + LoadedConfigPath + "; channeling="
        + (Settings.whileChanneling.enabled
                && HasConfiguredTriggerStat(Settings.whileChanneling.stats)
            ? "enabled"
            : "disabled")
        + "; interval-frames="
        + std::to_string(Settings.whileChanneling.intervalFrames)
        + "; channel-stat-ids=fixed/same="
        + std::to_string(Settings.whileChanneling.stats.fixedStatId)
        + "/"
        + std::to_string(Settings.whileChanneling.stats.sameLevelStatId)
        + "; source-skill-rules="
        + std::to_string(Settings.sourceSkillTriggers.size())
        + "; combat-stat-ids=attack/critical/crushing/open="
        + std::to_string(Settings.combatTriggers.attackAttemptStatId)
        + "/"
        + std::to_string(Settings.combatTriggers.criticalStrikeStatId)
        + "/"
        + std::to_string(Settings.combatTriggers.crushingBlowStatId)
        + "/"
        + std::to_string(Settings.combatTriggers.openWoundsStatId)
        + "; diagnostics="
        + (Settings.diagnostics ? "buffered" : "off")
        + ".";
    context->LogInfo(message.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    RuffnecKk::CastTriggers::Operational.store(
        false,
        std::memory_order_release);
}
