#include <Windows.h>
#include <D2RLPlugin/api.h>

#include "burn_damage_fix_policy.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <vector>

extern "C" void BurnDamageFixProductionMidHook();
extern "C" void* gBurnDamageFixProductionContinuation{};

namespace {
using namespace ruffneckk::burn_damage_fix;

constexpr wchar_t ConfigFileName[] = L"ruffneckk-burn-damage-fix.toml";
constexpr std::uintptr_t GenericBurnProductionRva = 0x44CB32;
constexpr std::uintptr_t GenericBurnProductionContinuationRva = 0x44CB38;
constexpr std::uintptr_t ApplyBurnDamageRva = 0x451380;
constexpr std::uintptr_t ApplyResistancesAndAbsorbRva = 0x4523E0;
constexpr std::uintptr_t ResistanceRecordCaptureRva = 0x4523F2;
constexpr std::uintptr_t ResistanceStatReadRva = 0x452412;
constexpr std::uintptr_t MaximumResistanceStatReadRva = 0x4524A1;
constexpr std::uintptr_t ResistanceBypassGateRva = 0x45251F;
constexpr std::uintptr_t ResistanceReductionAndLogRva = 0x452614;
constexpr std::uintptr_t AbsorbGateRva = 0x452658;
constexpr std::uintptr_t PierceStatReadRva = 0x44F8CF;
constexpr std::uintptr_t ImmunityPierceStatReadRva = 0x44F6F5;
constexpr std::uintptr_t AbsorbPercentStatReadRva = 0x450685;
constexpr std::uintptr_t AbsorbFlatStatReadRva = 0x45076D;
constexpr std::uintptr_t GetDifficultyRecordRva = 0x300830;
constexpr std::uintptr_t GetDataTablesForContextRva = 0x300A90;
constexpr std::uintptr_t StateOverlayFieldDescriptorRva = 0x307EB3;
constexpr std::uintptr_t StateRecordCompileWitnessRva = 0x3083D7;
constexpr std::uintptr_t StateVectorLayoutWitnessRva = 0x30843C;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t CheckStateRva = 0x3351B0;
constexpr std::uintptr_t StateToggleContextLayoutWitnessRva = 0x3354E0;
constexpr std::uintptr_t SetOverlayRva = 0x349020;
constexpr std::uintptr_t OverlayStatWriteWitnessRva = 0x34916C;
constexpr std::uintptr_t IsDeadRva = 0x34C2C0;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t GetItemDataContextRva = 0x34A0E0;
constexpr std::uintptr_t GetHirelingTypeIdRva = 0x3AF240;
constexpr std::uintptr_t EmptyStateRecordInitializerRva = 0x394640;
constexpr std::uintptr_t GetGameFromUnitRva = 0x48FF00;
constexpr std::uintptr_t PlayerEventDispatcherRva = 0x42CE30;
constexpr std::uintptr_t MonsterEventDispatcherRva = 0x447420;
constexpr std::uintptr_t GameDataLayoutWitnessRva = 0x44DF40;
constexpr std::uintptr_t GameFrameLayoutWitnessRva = 0x42E615;
constexpr std::size_t GameDifficultyOffset = 0x104;
constexpr std::size_t GameDataSetOffset = 0x106;
constexpr std::size_t GameFrameOffset = 0x170;
constexpr std::size_t StatesVectorOffset = 0x290;
constexpr std::size_t StatesCountOffset = 0x298;
constexpr std::size_t StateRecordStride = 0x44;
constexpr std::size_t MaximumDataContexts = 4;
constexpr std::int32_t PlayerUnitType = 0;
constexpr std::int32_t MonsterUnitType = 1;
constexpr std::int32_t FireLengthStat = 315;
constexpr std::int32_t BurningMinStat = 316;
constexpr std::int32_t BurningMaxStat = 317;
constexpr std::int32_t PassiveFireMasteryStat = 329;
constexpr std::uint32_t GenericBurnPatchSize = 6;
constexpr std::size_t RelayBytes = 16;
constexpr char FireTypeName[] = "Fire";

constexpr std::array<std::uint8_t, 10> GenericBurnProductionExpected{
    0x81, 0xC3, 0x3C, 0x01, 0x00, 0x00, 0x41, 0x0F, 0x48, 0xDE,
};
constexpr std::array<std::uint8_t, 23> ApplyBurnDamageExpected{
    0x45, 0x85, 0xC9, 0x0F, 0x8E, 0xE6, 0x01, 0x00, 0x00,
    0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x40,
};
constexpr auto ApplyResistancesAndAbsorbExpected =
    std::to_array<std::uint8_t>({
        0x48,0x89,0x6C,0x24,0x18,0x56,0x41,0x54,0x41,0x55,0x41,0x56,0x41,
        0x57,0x48,0x83,0xEC,0x50,0x4C,0x8B,0x3A,0x33,0xED,0x45,0x8B,0xE8,
        0x4C,0x8B,0xF2,0x48,0x8B,0xF1,
    });
constexpr auto ResistanceRecordCaptureExpected =
    std::to_array<std::uint8_t>({
        0x4C,0x8B,0x3A,0x33,0xED,0x45,0x8B,0xE8,0x4C,0x8B,0xF2,0x48,0x8B,
        0xF1,0x45,0x8B,0x27,
    });
constexpr auto ResistanceStatReadExpected = std::to_array<std::uint8_t>({
    0x8B,0x52,0x08,0x8B,0xC5,0x48,0x89,0x9C,0x24,0x80,0x00,0x00,0x00,0x48,
    0x89,0xBC,0x24,0x88,0x00,0x00,0x00,
});
constexpr auto MaximumResistanceStatReadExpected =
    std::to_array<std::uint8_t>({
        0x41,0x8B,0x56,0x0C,0xB9,0x4B,0x00,0x00,0x00,0x83,0xFA,0xFF,0x74,
        0x21,
    });
constexpr auto PierceStatReadExpected = std::to_array<std::uint8_t>({
    0x83,0x7A,0x10,0xFF,0x41,0x8B,0xF0,0x48,0x8B,0xEA,0x48,0x8B,0xD9,
});
constexpr auto ImmunityPierceStatReadExpected =
    std::to_array<std::uint8_t>({
        0x8B,0x52,0x14,0x41,0x8B,0xE8,0x48,0x8B,0xD9,0x83,0xFA,0xFF,
    });
constexpr auto AbsorbPercentStatReadExpected =
    std::to_array<std::uint8_t>({
        0x8B,0x52,0x18,0x48,0x8B,0xF9,0x83,0xFA,0xFF,
    });
constexpr auto AbsorbFlatStatReadExpected = std::to_array<std::uint8_t>({
    0x8B,0x56,0x1C,0x45,0x33,0xC0,0x48,0x8B,0x4F,0x18,
});
constexpr auto ResistanceBypassGateExpected =
    std::to_array<std::uint8_t>({
        0x8B,0xCB,0x45,0x85,0xED,0x74,0x07,0x85,0xC9,0x0F,0x4E,0xE9,0xEB,
        0x12,0x49,0x63,0x46,0x20,
    });
constexpr auto ResistanceReductionAndLogExpected =
    std::to_array<std::uint8_t>({
        0x41,0x0F,0xB6,0x46,0x38,0x45,0x8B,0xCC,0x49,0x63,0x4E,0x20,0x4C,
        0x8B,0x46,0x18,0x48,0x8B,0x56,0x10,
    });
constexpr auto AbsorbGateExpected = std::to_array<std::uint8_t>({
    0x45,0x85,0xED,0x75,0x2E,0x44,0x8B,0xC7,0x49,0x8B,0xD6,0x48,0x8B,0xCE,
    0xE8,0x05,0xE0,0xFF,0xFF,
});
constexpr std::array<std::uint8_t, 32> GetDifficultyRecordExpected{
    0x40,0x53,0x56,0x57,0x48,0x83,0xEC,0x30,0x0F,0xB6,0xC1,0x8B,0xF2,0x48,
    0x89,0x44,0x24,0x60,0x48,0x83,0xF8,0x04,0x72,0x19,0x48,0x8D,0x44,0x24,
    0x60,0x48,0x8D,0x4C,
};
constexpr auto GetDataTablesForContextExpected =
    std::to_array<std::uint8_t>({
        0x48,0x83,0xEC,0x28,0x0F,0xB6,0xC1,0x48,0x89,0x44,0x24,0x38,0x48,
        0x83,0xF8,0x04,0x72,0x19,0x48,0x8D,0x44,0x24,0x38,0x48,0x8D,0x4C,
        0x24,0x40,
    });
constexpr auto StateOverlayFieldDescriptorExpected =
    std::to_array<std::uint8_t>({
        0x48,0xC7,0x85,0x58,0x05,0x00,0x00,0x16,0x00,0x00,0x00,0x48,0xC7,
        0x85,0x60,0x05,0x00,0x00,0x02,0x00,0x00,0x00,0x48,0xC7,0x85,0x78,
        0x05,0x00,0x00,0x16,0x00,0x00,0x00,
    });
constexpr auto StateRecordCompileWitnessExpected =
    std::to_array<std::uint8_t>({
        0x40,0x0F,0xB6,0xCE,0x48,0x8D,0x45,0xF0,0x48,0xC7,0x44,0x24,0x28,
        0x44,0x00,0x00,0x00,0x48,0x89,0x44,0x24,0x20,
    });
// The following CALL is a D2RLoader compiler integration surface and may be
// redirected before plugins load. This unique prefix alone proves stride 0x44.
static_assert(StateRecordCompileWitnessExpected.size() == 22);
constexpr auto StateVectorLayoutWitnessExpected =
    std::to_array<std::uint8_t>({
        0x48,0x8D,0xB7,0x90,0x02,0x00,0x00,0x48,0x8D,0x44,0x24,0x48,0x48,
        0x3B,0xF0,0x74,0x2B,0x4C,0x39,0x7E,0x10,0x7C,0x18,0x48,0x8B,0x1E,
    });
constexpr std::array<std::uint8_t, 32> GetUnitStatExpected{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,
    0x20,0x57,0x48,0x83,0xEC,0x20,0x41,0x0F,0xB7,0xE8,0x8B,0xFA,0x48,0x8B,
    0xD9,0x48,0x85,0xC9,
};
constexpr std::array<std::uint8_t, 32> CheckStateExpected{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,
    0x20,0x8B,0xDA,0x48,0x8B,0xF1,0xE8,0x07,0x68,0x01,0x00,0x85,0xC0,0x74,
    0x0E,0x83,0xE8,0x01,
};
constexpr auto StateToggleContextLayoutWitnessExpected =
    std::to_array<std::uint8_t>({
        0xE8,0xFB,0x4B,0x01,0x00,0x0F,0xB6,0xC8,0xE8,0xA3,0xB5,0xFC,0xFF,
        0x48,0x8B,0xB8,0x98,0x02,0x00,0x00,0x48,0x63,0xC7,0x48,0x3B,0xC7,
        0x75,0x04,0x85,0xFF,0x79,0x14,
    });
constexpr auto SetOverlayExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x83,0xEC,0x30,0x45,
    0x8B,0xF0,0x8B,0xFA,0x48,0x8B,0xD9,0x48,0x85,0xC9,
});
constexpr auto OverlayStatWriteWitnessExpected =
    std::to_array<std::uint8_t>({
        0x0F,0xB6,0x8B,0xBD,0x01,0x00,0x00,0x44,0x0F,0xB7,0xCF,0x41,0xB8,
        0xB2,0x00,0x00,0x00,0x48,0x8B,0xD6,0x44,0x89,0x74,0x24,0x20,0xE8,
        0x96,0x9E,0xFA,0xFF,
    });
constexpr auto IsDeadExpected = std::to_array<std::uint8_t>({
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,
    0x8D,0x4C,0x24,0x30,0xE8,0x59,0x94,0xFF,0xFF,0x84,0xC0,0x74,0x4D,0xCC,
    0xB8,0x01,0x00,0x00,
});
constexpr auto PlayerEventDispatcherExpected =
    std::to_array<std::uint8_t>({
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,
        0x24,0x20,0x57,0x48,0x83,0xEC,0x30,0x49,0x63,0xD8,0x41,0x8B,0xF9,
        0x48,0x8B,0xF2,0x48,0x8B,0xE9,
    });
constexpr auto MonsterEventDispatcherExpected =
    std::to_array<std::uint8_t>({
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,
        0x24,0x20,0x57,0x48,0x83,0xEC,0x50,0x80,0x3D,0x61,0x76,0x66,0x02,
        0x00,0x41,0x8B,0xE9,0x49,0x63,0xF8,0x48,0x8B,0xDA,0x48,0x8B,0xF1,
    });
constexpr auto GameFrameLayoutWitnessExpected =
    std::to_array<std::uint8_t>({
        0x44,0x8B,0x89,0x70,0x01,0x00,0x00,0x48,0x8B,0xDA,0x89,0x44,0x24,
        0x28,0x41,0xFF,0xC1,
    });
constexpr auto GameDataLayoutWitnessExpected =
    std::to_array<std::uint8_t>({
        0x48,0x89,0x4C,0x24,0x30,0x0F,0xB6,0x91,0x04,0x01,0x00,0x00,0x48,
        0x8B,0xF9,0x48,0x89,0x4C,0x24,0x40,0x4D,0x8B,0xF9,0x0F,0xB6,0x89,
        0x06,0x01,0x00,0x00,0x49,0x8B,0xF0,0xE8,0xCA,0x28,0xEB,0xFF,
    });
constexpr std::array<std::uint8_t, 28> GetUnitTypeExpected{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,
    0x8D,0x4C,0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,
};
constexpr auto GetItemDataContextExpected =
    std::to_array<std::uint8_t>({
        0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1A,0x88,0x4C,0x24,0x30,
        0x48,0x8D,0x4C,0x24,0x30,0xE8,0x49,0xC7,0xFF,0xFF,0x84,0xC0,0x74,
        0x01,0xCC,0x32,0xC0,0x48,0x83,0xC4,0x28,0xC3,0x0F,0xB6,0x81,0xBD,
        0x01,0x00,0x00,0x48,0x83,0xC4,0x28,0xC3,
    });
constexpr std::array<std::uint8_t, 24> GetHirelingTypeIdExpected{
    0x40,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1,0xE8,0x82,0xC7,0xF9,0xFF,
    0x83,0xF8,0x01,0x75,0x5D,0x48,0x89,0x5C,0x24,0x30,
};
constexpr std::array<std::uint8_t, 32> GetGameFromUnitExpected{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x20,
    0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x44,0xD9,0xFF,0xFF,
    0x84,0xC0,0x74,0x01,
};
constexpr auto EmptyStateRecordInitializerExpected =
    std::to_array<std::uint8_t>({
        0x48,0x89,0x48,0x04,0x48,0x89,0x48,0x0C,0x48,0x89,0x48,0x14,0x48,
        0x89,0x48,0x1C,0x48,0x89,0x48,0x24,0x48,0x89,0x48,0x2C,0x48,0x89,
        0x48,0x34,0x48,0x89,0x48,0x3C,0xC7,0x00,0x00,0x00,0xFF,0xFF,0x48,
        0x83,0xC0,0x44,0x48,0x3B,0xC2,0x75,0xD1,
    });

struct DamagePayloadView {
    std::array<std::byte, 0x20> reserved00{};
    std::int32_t fireDamage{};
    std::int32_t burnDamage{};
    std::array<std::byte, 0x118> reserved28{};
};

struct DamageInfoView {
    void* game{};
    void* difficultyRecord{};
    void* attacker{};
    void* defender{};
    std::int32_t attackerIsMonster{};
    std::int32_t defenderIsMonster{};
    DamagePayloadView* damage{};
    std::array<std::int32_t, 32> reductions{};
};

struct ResistanceRecordView {
    std::int32_t* value{};
    std::int32_t resistanceStat{};
    std::int32_t maximumResistanceStat{};
    std::int32_t pierceStat{};
    std::int32_t immunityPierceStat{};
    std::int32_t absorbPercentStat{};
    std::int32_t absorbFlatStat{};
    std::int32_t damageReductionIndex{};
    std::int32_t attackerGate{};
    std::int32_t flags28{};
    std::int32_t reserved2C{};
    const char* typeName{};
    std::uint8_t logFlag{};
    std::array<std::byte, 7> reserved39{};
};

struct StateRecordPrefix {
    std::int16_t state{};
    std::uint16_t overlay{};
};

enum class NativeOverlayStatus : std::uint8_t {
    Unseen,
    Removed,
    AlreadyNone,
    CustomPreserved,
    Failed,
};

struct NativeOverlayContextSlot {
    SRWLOCK lock{};
    StateRecordPrefix* observedRecord{};
    StateRecordPrefix* ownedRecord{};
    NativeOverlayStatus status{NativeOverlayStatus::Unseen};
    std::uint16_t observedOverlay{EmptyStateOverlay};
};

static_assert(offsetof(DamagePayloadView, fireDamage) == 0x20);
static_assert(offsetof(DamagePayloadView, burnDamage) == 0x24);
static_assert(sizeof(DamagePayloadView) == 0x140);
static_assert(offsetof(DamageInfoView, game) == 0x00);
static_assert(offsetof(DamageInfoView, difficultyRecord) == 0x08);
static_assert(offsetof(DamageInfoView, attacker) == 0x10);
static_assert(offsetof(DamageInfoView, defender) == 0x18);
static_assert(offsetof(DamageInfoView, attackerIsMonster) == 0x20);
static_assert(offsetof(DamageInfoView, defenderIsMonster) == 0x24);
static_assert(offsetof(DamageInfoView, damage) == 0x28);
static_assert(offsetof(DamageInfoView, reductions) == 0x30);
static_assert(sizeof(DamageInfoView) == 0xB0);
static_assert(offsetof(ResistanceRecordView, value) == 0x00);
static_assert(offsetof(ResistanceRecordView, resistanceStat) == 0x08);
static_assert(offsetof(ResistanceRecordView, maximumResistanceStat) == 0x0C);
static_assert(offsetof(ResistanceRecordView, pierceStat) == 0x10);
static_assert(offsetof(ResistanceRecordView, immunityPierceStat) == 0x14);
static_assert(offsetof(ResistanceRecordView, absorbPercentStat) == 0x18);
static_assert(offsetof(ResistanceRecordView, absorbFlatStat) == 0x1C);
static_assert(offsetof(ResistanceRecordView, damageReductionIndex) == 0x20);
static_assert(offsetof(ResistanceRecordView, attackerGate) == 0x24);
static_assert(offsetof(ResistanceRecordView, flags28) == 0x28);
static_assert(offsetof(ResistanceRecordView, reserved2C) == 0x2C);
static_assert(offsetof(ResistanceRecordView, typeName) == 0x30);
static_assert(offsetof(ResistanceRecordView, logFlag) == 0x38);
static_assert(sizeof(ResistanceRecordView) == 0x40);
static_assert(std::is_standard_layout_v<ResistanceRecordView>);
static_assert(offsetof(StateRecordPrefix, state) == 0x00);
static_assert(offsetof(StateRecordPrefix, overlay) == 0x02);
static_assert(sizeof(StateRecordPrefix) == 0x04);

using ApplyBurnDamageFn = void(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t) noexcept;
using ApplyResistancesAndAbsorbFn = std::int32_t(__fastcall*)(
    DamageInfoView*, ResistanceRecordView*, std::int32_t) noexcept;
using GetDifficultyRecordFn = void*(__fastcall*)(
    std::uint32_t, std::int32_t) noexcept;
using GetDataTablesForContextFn = void*(__fastcall*)(
    std::uint8_t) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using CheckStateFn = std::int32_t(__fastcall*)(
    void*, std::int32_t) noexcept;
using SetOverlayFn = void(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using IsDeadFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetItemDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetHirelingTypeIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetGameFromUnitFn = void*(__fastcall*)(void*) noexcept;
using EventDispatcherFn = void(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t, std::int32_t,
    std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
std::size_t ImageSize{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};
std::string RuntimeBuild{"unknown"};
void* RelayPage{};
ApplyBurnDamageFn OriginalApplyBurnDamage{};
ApplyResistancesAndAbsorbFn ApplyResistancesAndAbsorb{};
GetDifficultyRecordFn GetDifficultyRecord{};
GetDataTablesForContextFn GetDataTablesForContext{};
GetUnitStatFn GetUnitStat{};
CheckStateFn CheckState{};
SetOverlayFn SetOverlay{};
IsDeadFn IsDead{};
GetUnitTypeFn GetUnitType{};
GetItemDataContextFn GetItemDataContext{};
GetHirelingTypeIdFn GetHirelingTypeId{};
GetGameFromUnitFn GetGameFromUnit{};
EventDispatcherFn OriginalPlayerEventDispatcher{};
EventDispatcherFn OriginalMonsterEventDispatcher{};
const D2RL::DiagnosticsServiceV1* DiagnosticsService{};
std::atomic_bool Operational{};
std::atomic<std::uint64_t> GenericProductionCalls{};
std::atomic<std::uint64_t> GenericProductionRolls{};
std::atomic<std::uint64_t> ResolvedBurnApplications{};
std::atomic<std::uint64_t> ResolvedBurnCancellations{};
std::atomic<std::uint64_t> ResistanceResolutionFailures{};
std::atomic<std::uint64_t> BurningStateActiveWitnesses{};
std::atomic<std::uint64_t> BurningStateMissingWitnesses{};
std::atomic<std::uint64_t> OverlayCadenceChecks{};
std::atomic<std::uint64_t> OverlayReplays{};
std::atomic<std::uint64_t> OverlayForeignReplacements{};
std::atomic<std::uint64_t> NativeOverlaySuppressions{};
std::atomic<std::uint64_t> NativeOverlayAlreadyNone{};
std::atomic<std::uint64_t> NativeOverlayCustomPreserved{};
std::atomic<std::uint64_t> NativeOverlayFailures{};
std::atomic<std::uint64_t> NativeOverlayRestores{};
std::atomic_bool NativeOverlayUnscopedFailureReported{};
std::array<NativeOverlayContextSlot, MaximumDataContexts> NativeOverlaySlots{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-burn-damage-fix",
    .name = "Burn Damage Fix",
    .version = "1.0.1",
    .author = "RuffnecKk",
    .description = "Restores Burn damage and Fire defenses with a moving periodic flame.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template <typename T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

auto IsExecutableRange(const void* address, std::size_t size) noexcept -> bool;

auto IsRangeWithinImage(
        std::uintptr_t rva,
        std::size_t size) noexcept -> bool {
    return Base != nullptr && ImageSize != 0
        && rva <= ImageSize
        && size <= ImageSize - rva;
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

template <std::size_t Size>
auto Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    if (!Base || !IsRangeWithinImage(rva, expected.size())
            || !IsExecutableRange(Base + rva, expected.size())) {
        return false;
    }
    __try {
        return std::memcmp(
            Base + rva, expected.data(), expected.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto IsExecutableProtection(DWORD value) noexcept -> bool {
    const auto protection = value & 0xFFU;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

auto IsWritableProtection(DWORD value) noexcept -> bool {
    const auto protection = value & 0xFFU;
    return protection == PAGE_READWRITE
        || protection == PAGE_WRITECOPY
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

auto IsExecutableRange(const void* address, std::size_t size) noexcept -> bool {
    if (!address || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) return false;
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
                || !IsExecutableProtection(info.Protect)) {
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

auto IsWritableRange(const void* address, std::size_t size) noexcept -> bool {
    if (!address || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) return false;
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
                || !IsWritableProtection(info.Protect)) {
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

auto QueryDiagnosticsService() noexcept -> bool {
    const auto result = Context->QueryService(
        D2RL::ServiceId::Diagnostics,
        D2RL::DiagnosticsServiceV1Version,
        &DiagnosticsService);
    if (result != D2RL::ServiceQueryResult::Success) {
        DiagnosticsService = nullptr;
        Context->LogWarn(
            "BurnDamageFix: DiagnosticsService v1 is unavailable; strict vanilla entry signatures remain mandatory.");
        return true;
    }
    if (!D2RL::HasDiagnosticsServiceV1Field(
            DiagnosticsService,
            D2RL::DiagnosticsServiceV1RequiredSize)
            || DiagnosticsService->queryHookStatus == nullptr) {
        Context->LogError(
            "BurnDamageFix: DiagnosticsService v1 returned an invalid contract.");
        DiagnosticsService = nullptr;
        return false;
    }
    return true;
}

template <std::size_t Size>
auto ValidateExclusiveSurface(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected,
        const char* label) noexcept -> bool {
    if (!Matches(rva, expected)) {
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "BurnDamageFix: %s signature mismatch; plugin refused.", label);
        Context->LogError(message);
        return false;
    }
    if (!DiagnosticsService) return true;

    const D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .flags = 0,
        .rva = rva,
        .expected = expected.data(),
        .expectedSize = static_cast<std::uint32_t>(expected.size()),
        .reserved = 0,
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
        "BurnDamageFix: %s is not an unowned vanilla surface; plugin refused.",
        label);
    Context->LogError(message);
    return false;
}

auto ValidateResistanceResolverEntry() noexcept -> bool {
    if (!IsRangeWithinImage(
            ApplyResistancesAndAbsorbRva,
            ApplyResistancesAndAbsorbExpected.size())) {
        Context->LogError(
            "BurnDamageFix: Fire Resistance resolver lies outside the executable image; plugin refused.");
        return false;
    }
    const auto vanillaMatches = Matches(
        ApplyResistancesAndAbsorbRva,
        ApplyResistancesAndAbsorbExpected);
    const auto executable = IsExecutableRange(
        Base + ApplyResistancesAndAbsorbRva,
        ApplyResistancesAndAbsorbExpected.size());
    if (!DiagnosticsService) {
        if (AcceptResistanceResolver(
                ResistanceResolverStatus::Unchanged,
                vanillaMatches,
                executable,
                0,
                {})) {
            return true;
        }
        Context->LogError(
            "BurnDamageFix: the live Fire Resistance resolver differs from vanilla and no tracked owner proof is available.");
        return false;
    }

    const D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .flags = 0,
        .rva = ApplyResistancesAndAbsorbRva,
        .expected = ApplyResistancesAndAbsorbExpected.data(),
        .expectedSize = static_cast<std::uint32_t>(
            ApplyResistancesAndAbsorbExpected.size()),
        .reserved = 0,
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    const auto result = DiagnosticsService->queryHookStatus(
        Context, &query, &status);
    if (result != D2RL::Diagnostics::Result::Success
            || status.structSize
                < D2RL::Diagnostics::HookStatusRequiredSize) {
        Context->LogError(
            "BurnDamageFix: DiagnosticsService could not validate the live Fire Resistance resolver.");
        return false;
    }

    const auto ownerLength = std::find(
        std::begin(status.ownerPluginId),
        std::end(status.ownerPluginId),
        '\0') - std::begin(status.ownerPluginId);
    const std::string_view owner{
        status.ownerPluginId,
        static_cast<std::size_t>(ownerLength)};
    const auto resolverStatus =
        status.state == D2RL::Diagnostics::ModificationState::Unchanged
            ? ResistanceResolverStatus::Unchanged
        : status.state == D2RL::Diagnostics::ModificationState::Tracked
            && status.kind
                == D2RL::Diagnostics::ModificationKind::InlineHook
            ? ResistanceResolverStatus::TrackedInlineHook
            : ResistanceResolverStatus::Other;
    if (AcceptResistanceResolver(
            resolverStatus,
            vanillaMatches,
            executable,
            status.ownerCount,
            owner)) {
        return true;
    }

    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "BurnDamageFix: Fire Resistance resolver ownership refused (state=%u, kind=%u, owners=%u, owner=%.*s).",
        static_cast<unsigned>(status.state),
        static_cast<unsigned>(status.kind),
        status.ownerCount,
        static_cast<int>(owner.size()),
        owner.data());
    Context->LogError(message);
    return false;
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

auto LoadConfig() noexcept -> bool {
    Settings = {};
    LoadedConfigPath = "embedded defaults";
    for (const auto& path : ConfigCandidates()) {
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
            Settings = parsed;
            LoadedConfigPath = path.string();
            return true;
        } catch (const std::exception& exception) {
            const auto message = std::string("BurnDamageFix: invalid ")
                + path.string() + " (" + exception.what() + ").";
            Context->LogError(message.c_str());
            return false;
        }
    }
    Context->LogWarn(
        "BurnDamageFix: no TOML was found; embedded defaults are active.");
    return true;
}

auto ValidateRuntime() noexcept -> bool {
    const auto requireSignature = [](
            std::uintptr_t rva,
            const auto& expected,
            const char* label) noexcept -> bool {
        if (Matches(rva, expected)) return true;
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "BurnDamageFix: %s signature mismatch; plugin refused.", label);
        Context->LogError(message);
        return false;
    };

    if (Settings.normalizeGenericBurn) {
        if (!ValidateExclusiveSurface(
                GenericBurnProductionRva,
                GenericBurnProductionExpected,
                "generic Burn production seam")
                || !requireSignature(
                    GetUnitStatRva,
                    GetUnitStatExpected,
                    "unit stat resolver")) {
            return false;
        }
    }

    if (Settings.applyFireResistance) {
        if (!ValidateExclusiveSurface(
                ApplyBurnDamageRva,
                ApplyBurnDamageExpected,
                "Burn application entry")
                || !requireSignature(
                    GetDifficultyRecordRva,
                    GetDifficultyRecordExpected,
                    "difficulty record resolver")
                || !requireSignature(
                    GetUnitStatRva,
                    GetUnitStatExpected,
                    "unit stat resolver")
                || !requireSignature(
                    GetUnitTypeRva,
                    GetUnitTypeExpected,
                    "unit type resolver")
                || !requireSignature(
                    GetHirelingTypeIdRva,
                    GetHirelingTypeIdExpected,
                    "hireling type resolver")
                || !requireSignature(
                    GetGameFromUnitRva,
                    GetGameFromUnitExpected,
                    "unit game resolver")
                || !requireSignature(
                    GameDataLayoutWitnessRva,
                    GameDataLayoutWitnessExpected,
                    "game difficulty/data-set layout witness")
                || !requireSignature(
                    ResistanceRecordCaptureRva,
                    ResistanceRecordCaptureExpected,
                    "Fire record/value ABI witness")
                || !requireSignature(
                    ResistanceStatReadRva,
                    ResistanceStatReadExpected,
                    "Fire Resistance stat layout witness")
                || !requireSignature(
                    MaximumResistanceStatReadRva,
                    MaximumResistanceStatReadExpected,
                    "maximum Fire Resistance layout witness")
                || !requireSignature(
                    PierceStatReadRva,
                    PierceStatReadExpected,
                    "Fire pierce layout witness")
                || !requireSignature(
                    ImmunityPierceStatReadRva,
                    ImmunityPierceStatReadExpected,
                    "Fire immunity-pierce layout witness")
                || !requireSignature(
                    AbsorbPercentStatReadRva,
                    AbsorbPercentStatReadExpected,
                    "Fire percentage-absorb layout witness")
                || !requireSignature(
                    AbsorbFlatStatReadRva,
                    AbsorbFlatStatReadExpected,
                    "Fire flat-absorb layout witness")
                || !requireSignature(
                    ResistanceBypassGateRva,
                    ResistanceBypassGateExpected,
                    "positive-resistance bypass witness")
                || !requireSignature(
                    ResistanceReductionAndLogRva,
                    ResistanceReductionAndLogExpected,
                    "damage-reduction/log layout witness")
                || !requireSignature(
                    AbsorbGateRva,
                    AbsorbGateExpected,
                    "absorb gate witness")
                || !ValidateResistanceResolverEntry()) {
            return false;
        }
    }

    if (ShouldSuppressNativeBurning(Settings)) {
        if (!requireSignature(
                GetItemDataContextRva,
                GetItemDataContextExpected,
                "unit data-context resolver")
                || !requireSignature(
                    GetDataTablesForContextRva,
                    GetDataTablesForContextExpected,
                    "data-table context resolver")
                || !requireSignature(
                    StateToggleContextLayoutWitnessRva,
                    StateToggleContextLayoutWitnessExpected,
                    "states count/context layout witness")
                || !requireSignature(
                    StateRecordCompileWitnessRva,
                    StateRecordCompileWitnessExpected,
                    "states record-stride compiler witness")
                || !requireSignature(
                    StateVectorLayoutWitnessRva,
                    StateVectorLayoutWitnessExpected,
                    "states vector layout witness")
                || !requireSignature(
                    StateOverlayFieldDescriptorRva,
                    StateOverlayFieldDescriptorExpected,
                    "states overlay-field layout witness")
                || !requireSignature(
                    EmptyStateRecordInitializerRva,
                    EmptyStateRecordInitializerExpected,
                    "empty state-overlay sentinel witness")) {
            return false;
        }
    }

    if (Settings.replayFireHit) {
        if (!ValidateExclusiveSurface(
                PlayerEventDispatcherRva,
                PlayerEventDispatcherExpected,
                "player event dispatcher")
                || !ValidateExclusiveSurface(
                    MonsterEventDispatcherRva,
                    MonsterEventDispatcherExpected,
                    "monster event dispatcher")
                || !requireSignature(
                    CheckStateRva,
                    CheckStateExpected,
                    "burning-state predicate")
                || !requireSignature(
                    GetUnitStatRva,
                    GetUnitStatExpected,
                    "direct-overlay stat resolver")
                || !requireSignature(
                    GetUnitTypeRva,
                    GetUnitTypeExpected,
                    "dispatcher unit-type guard")
                || !requireSignature(
                    SetOverlayRva,
                    SetOverlayExpected,
                    "unit overlay setter")
                || !requireSignature(
                    OverlayStatWriteWitnessRva,
                    OverlayStatWriteWitnessExpected,
                    "unit_dooverlay stat-write witness")
                || !requireSignature(
                    IsDeadRva,
                    IsDeadExpected,
                    "unit death predicate")
                || !requireSignature(
                    GameFrameLayoutWitnessRva,
                    GameFrameLayoutWitnessExpected,
                    "game-frame layout witness")) {
            return false;
        }
    } else if (Settings.diagnostics && Settings.applyFireResistance
            && !requireSignature(
                CheckStateRva,
                CheckStateExpected,
                "burning-state diagnostic predicate")) {
        return false;
    }
    return true;
}

auto AllocateNear(void* hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto aligned = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < 0x70000000ULL; delta += granularity) {
        if (aligned > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(
                reinterpret_cast<std::uintptr_t>(hint), candidate)) {
            break;
        }
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

auto WriteAbsoluteJump(std::uint8_t* destination, const void* target) noexcept
        -> bool {
    if (!destination || !target) return false;
    destination[0] = 0xFF;
    destination[1] = 0x25;
    destination[2] = destination[3] = destination[4] = destination[5] = 0;
    const auto address = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(destination + 6, &address, sizeof(address));
    return true;
}

auto InstallProductionRelay() noexcept -> bool {
    if (!Settings.normalizeGenericBurn) return true;
    RelayPage = AllocateNear(Base + GenericBurnProductionRva, RelayBytes);
    if (!RelayPage) {
        Context->LogError(
            "BurnDamageFix: no relay page was available within rel32 reach.");
        return false;
    }
    auto* relay = static_cast<std::uint8_t*>(RelayPage);
    if (!WriteAbsoluteJump(
            relay,
            reinterpret_cast<const void*>(&BurnDamageFixProductionMidHook))) {
        return false;
    }
    DWORD previousProtection{};
    if (!VirtualProtect(
            relay, RelayBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError(
            "BurnDamageFix: relay page protection could not be finalized.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, RelayBytes);
    const auto relayAddress = reinterpret_cast<std::uintptr_t>(relay);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress
            || !CanEncodeRel32(
                baseAddress + GenericBurnProductionRva, relayAddress)) {
        Context->LogError(
            "BurnDamageFix: relay displacement validation failed.");
        return false;
    }
    gBurnDamageFixProductionContinuation =
        Base + GenericBurnProductionContinuationRva;
    if (!Context->PatchJmpRel32(
            GenericBurnProductionRva,
            GenericBurnProductionExpected.data(),
            GenericBurnPatchSize,
            relayAddress - baseAddress,
            GenericBurnPatchSize)) {
        Context->LogError(
            "BurnDamageFix: generic Burn production seam is already owned; plugin refused.");
        return false;
    }
    return true;
}

auto IsNonHirelingMonster(void* unit) noexcept -> std::int32_t {
    if (!unit || GetUnitType(unit) != MonsterUnitType) return 0;
    return GetHirelingTypeId(unit) == 0 ? 1 : 0;
}

auto MakeBurnFireRecord(std::int32_t* value) noexcept
        -> ResistanceRecordView {
    ResistanceRecordView record{};
    record.value = value;
    record.resistanceStat = BurnFireResistance.resistanceStat;
    record.maximumResistanceStat =
        BurnFireResistance.maximumResistanceStat;
    record.pierceStat = BurnFireResistance.pierceStat;
    record.immunityPierceStat = BurnFireResistance.immunityPierceStat;
    record.absorbPercentStat = BurnFireResistance.absorbPercentStat;
    record.absorbFlatStat = BurnFireResistance.absorbFlatStat;
    record.damageReductionIndex =
        BurnFireResistance.damageReductionIndex;
    record.attackerGate = BurnFireResistance.attackerGate;
    record.flags28 = BurnFireResistance.flags28;
    record.reserved2C = BurnFireResistance.reserved2C;
    record.typeName = FireTypeName;
    record.logFlag = BurnFireResistance.logFlag;
    return record;
}

auto RecordNativeOverlayStatus(
        NativeOverlayContextSlot& slot,
        StateRecordPrefix* record,
        std::uint16_t overlay,
        NativeOverlayStatus status) noexcept -> bool {
    const auto changed = slot.observedRecord != record
        || slot.status != status
        || slot.observedOverlay != overlay;
    if (!changed) return false;

    slot.observedRecord = record;
    slot.status = status;
    slot.observedOverlay = overlay;
    switch (status) {
    case NativeOverlayStatus::Removed:
        NativeOverlaySuppressions.fetch_add(1, std::memory_order_relaxed);
        break;
    case NativeOverlayStatus::AlreadyNone:
        NativeOverlayAlreadyNone.fetch_add(1, std::memory_order_relaxed);
        break;
    case NativeOverlayStatus::CustomPreserved:
        NativeOverlayCustomPreserved.fetch_add(
            1, std::memory_order_relaxed);
        break;
    case NativeOverlayStatus::Failed:
        NativeOverlayFailures.fetch_add(1, std::memory_order_relaxed);
        break;
    case NativeOverlayStatus::Unseen:
        break;
    }
    return true;
}

auto ResolveNativeBurningRecord(
        void* unit,
        std::uint8_t& dataContext,
        StateRecordPrefix*& record) noexcept -> bool {
    dataContext = std::numeric_limits<std::uint8_t>::max();
    record = nullptr;
    if (!unit || !GetItemDataContext || !GetDataTablesForContext) {
        return false;
    }

    bool resolved{};
    __try {
        dataContext = GetItemDataContext(unit);
        if (dataContext >= MaximumDataContexts) return false;
        auto* dataTables = static_cast<std::uint8_t*>(
            GetDataTablesForContext(dataContext));
        if (!dataTables) return false;
        const auto stateCount = *reinterpret_cast<const std::uint64_t*>(
            dataTables + StatesCountOffset);
        auto* stateRecords = *reinterpret_cast<std::uint8_t**>(
            dataTables + StatesVectorOffset);
        if (stateCount <= static_cast<std::uint64_t>(BurningState)
                || !stateRecords) {
            return false;
        }
        constexpr auto burningRecordOffset = StateRecordStride
            * static_cast<std::size_t>(BurningState);
        const auto recordsAddress = reinterpret_cast<std::uintptr_t>(
            stateRecords);
        if (recordsAddress > std::numeric_limits<std::uintptr_t>::max()
                - burningRecordOffset) {
            return false;
        }
        record = reinterpret_cast<StateRecordPrefix*>(
            recordsAddress + burningRecordOffset);
        resolved = IsWritableRange(record, sizeof(*record))
            && (reinterpret_cast<std::uintptr_t>(&record->overlay)
                & (alignof(SHORT) - 1U)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        record = nullptr;
        resolved = false;
    }
    return resolved;
}

auto SuppressNativeBurningOverlay(void* unit) noexcept -> bool {
    std::uint8_t dataContext{};
    StateRecordPrefix* record{};
    if (!ResolveNativeBurningRecord(unit, dataContext, record)) {
        if (dataContext < MaximumDataContexts) {
            auto& slot = NativeOverlaySlots[dataContext];
            AcquireSRWLockExclusive(&slot.lock);
            const auto changed = RecordNativeOverlayStatus(
                slot,
                record,
                EmptyStateOverlay,
                NativeOverlayStatus::Failed);
            ReleaseSRWLockExclusive(&slot.lock);
            if (changed && Context) {
                Context->LogWarn(
                    "BurnDamageFix: native burning overlay suppression failed closed for one data context; Burn mechanics remain active.");
            }
        } else if (!NativeOverlayUnscopedFailureReported.exchange(
                true, std::memory_order_relaxed)) {
            NativeOverlayFailures.fetch_add(1, std::memory_order_relaxed);
            if (Context) {
                Context->LogWarn(
                    "BurnDamageFix: native burning overlay suppression could not resolve a valid data context; Burn mechanics remain active.");
            }
        }
        return false;
    }

    auto& slot = NativeOverlaySlots[dataContext];
    auto status = NativeOverlayStatus::Failed;
    auto observedOverlay = EmptyStateOverlay;
    AcquireSRWLockExclusive(&slot.lock);
    __try {
        if (record->state == BurningState
                && (!slot.ownedRecord || slot.ownedRecord == record)) {
            observedOverlay = static_cast<std::uint16_t>(
                InterlockedCompareExchange16(
                    reinterpret_cast<volatile SHORT*>(&record->overlay),
                    static_cast<SHORT>(EmptyStateOverlay),
                    static_cast<SHORT>(NativeBurningOverlay)));
            switch (ClassifyNativeBurningOverlay(observedOverlay)) {
            case NativeBurningOverlayAction::Suppress:
                slot.ownedRecord = record;
                status = NativeOverlayStatus::Removed;
                break;
            case NativeBurningOverlayAction::AlreadySuppressed:
                status = slot.ownedRecord == record
                    ? NativeOverlayStatus::Removed
                    : NativeOverlayStatus::AlreadyNone;
                break;
            case NativeBurningOverlayAction::PreserveCustom:
                status = NativeOverlayStatus::CustomPreserved;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = NativeOverlayStatus::Failed;
    }
    if (status == NativeOverlayStatus::Removed) {
        observedOverlay = EmptyStateOverlay;
    }
    const auto changed = RecordNativeOverlayStatus(
        slot, record, observedOverlay, status);
    ReleaseSRWLockExclusive(&slot.lock);

    if (changed && Context) {
        char message[256]{};
        if (status == NativeOverlayStatus::Removed) {
            std::snprintf(
                message,
                sizeof(message),
                "BurnDamageFix: suppressed native burning overlay 224 in data context %u.",
                static_cast<unsigned>(dataContext));
            Context->LogInfo(message);
        } else if (status == NativeOverlayStatus::CustomPreserved) {
            std::snprintf(
                message,
                sizeof(message),
                "BurnDamageFix: preserved custom burning overlay %u in data context %u.",
                static_cast<unsigned>(observedOverlay),
                static_cast<unsigned>(dataContext));
            Context->LogWarn(message);
        } else if (status == NativeOverlayStatus::Failed) {
            Context->LogWarn(
                "BurnDamageFix: native burning row validation failed closed; Burn mechanics remain active.");
        }
    }
    return status != NativeOverlayStatus::Failed;
}

void RestoreNativeBurningOverlays() noexcept {
    for (auto& slot : NativeOverlaySlots) {
        AcquireSRWLockExclusive(&slot.lock);
        auto* record = slot.ownedRecord;
        auto restored = false;
        if (record && IsWritableRange(record, sizeof(*record))) {
            __try {
                if (record->state == BurningState) {
                    const auto observed = static_cast<std::uint16_t>(
                        InterlockedCompareExchange16(
                            reinterpret_cast<volatile SHORT*>(
                                &record->overlay),
                            static_cast<SHORT>(NativeBurningOverlay),
                            static_cast<SHORT>(EmptyStateOverlay)));
                    restored = observed == EmptyStateOverlay;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                restored = false;
            }
        }
        if (restored) {
            NativeOverlayRestores.fetch_add(1, std::memory_order_relaxed);
        }
        slot.ownedRecord = nullptr;
        slot.observedRecord = nullptr;
        slot.status = NativeOverlayStatus::Unseen;
        slot.observedOverlay = EmptyStateOverlay;
        ReleaseSRWLockExclusive(&slot.lock);
    }
}

auto TryResolveBurn(
        void* attacker,
        void* defender,
        std::int32_t burnDamage,
        std::int32_t& resolvedBurn) noexcept -> bool {
    resolvedBurn = 0;
    if (!defender || burnDamage <= 0) return false;

    bool resolved{};
    __try {
        auto* game = GetGameFromUnit(attacker ? attacker : defender);
        if (!game) return false;
        const auto* gameBytes = static_cast<const std::uint8_t*>(game);
        auto* difficultyRecord = GetDifficultyRecord(
            gameBytes[GameDataSetOffset], gameBytes[GameDifficultyOffset]);
        if (!difficultyRecord) return false;

        DamagePayloadView damage{};
        damage.burnDamage = burnDamage;

        DamageInfoView damageInfo{};
        damageInfo.game = game;
        damageInfo.difficultyRecord = difficultyRecord;
        damageInfo.attacker = attacker;
        damageInfo.defender = defender;
        damageInfo.attackerIsMonster = IsNonHirelingMonster(attacker);
        damageInfo.defenderIsMonster = IsNonHirelingMonster(defender);
        damageInfo.damage = &damage;
        damageInfo.reductions[BurnFireResistance.damageReductionIndex] = 0;

        auto fireResistance = MakeBurnFireRecord(&damage.burnDamage);
        if (!attacker) {
            fireResistance.pierceStat = -1;
            fireResistance.immunityPierceStat = -1;
        }

        // Zero keeps positive resistance and immunity active. Fire Absorb is
        // excluded by the two -1 stat sentinels, and MDR stays zero in the
        // synthetic DamageInfo reduction slot.
        ApplyResistancesAndAbsorb(
            &damageInfo,
            &fireResistance,
            PreservePositiveResistanceAndImmunity);
        resolvedBurn = std::max(damage.burnDamage, 0);
        resolved = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        resolvedBurn = 0;
        resolved = false;
    }
    return resolved;
}

auto ObserveBurningState(
        void* unit,
        bool replayOverlay) noexcept -> bool {
    if (!unit || !CheckState) return false;
    bool active{};
    __try {
        active = CheckState(unit, BurningState) != 0;
        if (active && replayOverlay && SetOverlay && IsDead && GetUnitStat
                && IsDead(unit) == 0) {
            const auto activeDirectOverlay = GetUnitStat(
                unit, UnitDoOverlayStat, 0);
            if (activeDirectOverlay != 0
                    && activeDirectOverlay != FireHitOverlay) {
                // unit_dooverlay stores the last direct overlay write, not a
                // reliable animation-active flag. Count native last-write-wins
                // arbitration, but do not let a stale value suppress Burn.
                OverlayForeignReplacements.fetch_add(
                    1, std::memory_order_relaxed);
            }
            SetOverlay(unit, FireHitOverlay, 0);
            OverlayReplays.fetch_add(1, std::memory_order_relaxed);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        active = false;
    }
    return active;
}

void ReplayPeriodicOverlay(
        void* game,
        void* unit,
        std::int32_t eventType,
        std::int32_t expectedUnitType) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !Settings.replayFireHit || !game || !unit
            || eventType != StatRegenerationEvent) {
        return;
    }

    std::uint32_t gameFrame{};
    __try {
        if (!GetUnitType || GetUnitType(unit) != expectedUnitType) return;
        const auto* gameBytes = static_cast<const std::uint8_t*>(game);
        gameFrame = *reinterpret_cast<const std::uint32_t*>(
            gameBytes + GameFrameOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!ShouldReplayFireHit(Settings, eventType, gameFrame)) return;

    OverlayCadenceChecks.fetch_add(1, std::memory_order_relaxed);
    (void)ObserveBurningState(unit, true);
}

void HookPlayerEventDispatcher(
        void* game,
        void* unit,
        std::int32_t eventType,
        std::int32_t callbackArg0,
        std::int32_t callbackArg1,
        std::int32_t callbackArg2) noexcept {
    ReplayPeriodicOverlay(game, unit, eventType, PlayerUnitType);
    OriginalPlayerEventDispatcher(
        game, unit, eventType,
        callbackArg0, callbackArg1, callbackArg2);
}

void HookMonsterEventDispatcher(
        void* game,
        void* unit,
        std::int32_t eventType,
        std::int32_t callbackArg0,
        std::int32_t callbackArg1,
        std::int32_t callbackArg2) noexcept {
    ReplayPeriodicOverlay(game, unit, eventType, MonsterUnitType);
    OriginalMonsterEventDispatcher(
        game, unit, eventType,
        callbackArg0, callbackArg1, callbackArg2);
}

void HookApplyBurnDamage(
        void* attacker,
        void* defender,
        std::int32_t burnDamage,
        std::int32_t burnLength) noexcept {
    if (!Operational.load(std::memory_order_acquire)) {
        OriginalApplyBurnDamage(
            attacker, defender, burnDamage, burnLength);
        return;
    }

    auto resolvedBurn = burnDamage;
    if (ShouldResolveBurn(Settings, burnDamage, burnLength)) {
        if (!TryResolveBurn(
                attacker, defender, burnDamage, resolvedBurn)) {
            ResistanceResolutionFailures.fetch_add(
                1, std::memory_order_relaxed);
            return;
        }
        if (resolvedBurn <= 0) {
            ResolvedBurnCancellations.fetch_add(
                1, std::memory_order_relaxed);
            return;
        }
        ResolvedBurnApplications.fetch_add(1, std::memory_order_relaxed);
    }
    if (ShouldSuppressNativeBurning(Settings) && defender) {
        (void)SuppressNativeBurningOverlay(defender);
    }
    OriginalApplyBurnDamage(attacker, defender, resolvedBurn, burnLength);

    const auto shouldWitness = ShouldWitnessBurningState(
        Settings, resolvedBurn, burnLength);
    const auto shouldReplay = Settings.enabled && Settings.replayFireHit
        && resolvedBurn > 0 && burnLength > 0;
    if ((!shouldWitness && !shouldReplay) || !defender || !CheckState) {
        return;
    }
    const auto stateActive = ObserveBurningState(defender, shouldReplay);
    if (shouldWitness) {
        (stateActive
            ? BurningStateActiveWitnesses
            : BurningStateMissingWitnesses).fetch_add(
                1, std::memory_order_relaxed);
    }
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[1280]{};
    std::snprintf(
        message,
        sizeof(message),
        "Burn Damage Fix 1.0.1: active=%s; build=%s; generic=%s; resistance=%s; overlay=%s/fire_hit/%df; native-burning=%s/%llu/%llu/%llu/%llu/%llu removed/already-none/custom/fail/restored; diagnostics=%s; production=%llu/%llu; resolved=%llu/%llu/%llu applied/cancelled/fail; burning-state=%llu/%llu active/missing; overlay-replay=%llu/%llu/%llu replayed/cadence/foreign-replaced; config=%s.",
        Operational.load(std::memory_order_acquire) ? "true" : "false",
        RuntimeBuild.c_str(),
        Settings.normalizeGenericBurn ? "on" : "off",
        Settings.applyFireResistance ? "on" : "off",
        Settings.replayFireHit ? "on" : "off",
        Settings.overlayRepeatFrames,
        ShouldSuppressNativeBurning(Settings) ? "on" : "off",
        static_cast<unsigned long long>(
            NativeOverlaySuppressions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeOverlayAlreadyNone.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeOverlayCustomPreserved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeOverlayFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            NativeOverlayRestores.load(std::memory_order_relaxed)),
        Settings.diagnostics ? "on" : "off",
        static_cast<unsigned long long>(
            GenericProductionCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            GenericProductionRolls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ResolvedBurnApplications.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ResolvedBurnCancellations.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ResistanceResolutionFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            BurningStateActiveWitnesses.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            BurningStateMissingWitnesses.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            OverlayReplays.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            OverlayCadenceChecks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            OverlayForeignReplacements.load(std::memory_order_relaxed)),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    GenericProductionCalls.store(0, std::memory_order_relaxed);
    GenericProductionRolls.store(0, std::memory_order_relaxed);
    ResolvedBurnApplications.store(0, std::memory_order_relaxed);
    ResolvedBurnCancellations.store(0, std::memory_order_relaxed);
    ResistanceResolutionFailures.store(0, std::memory_order_relaxed);
    BurningStateActiveWitnesses.store(0, std::memory_order_relaxed);
    BurningStateMissingWitnesses.store(0, std::memory_order_relaxed);
    OverlayCadenceChecks.store(0, std::memory_order_relaxed);
    OverlayReplays.store(0, std::memory_order_relaxed);
    OverlayForeignReplacements.store(0, std::memory_order_relaxed);
    NativeOverlaySuppressions.store(0, std::memory_order_relaxed);
    NativeOverlayAlreadyNone.store(0, std::memory_order_relaxed);
    NativeOverlayCustomPreserved.store(0, std::memory_order_relaxed);
    NativeOverlayFailures.store(0, std::memory_order_relaxed);
    NativeOverlayRestores.store(0, std::memory_order_relaxed);
    NativeOverlayUnscopedFailureReported.store(
        false, std::memory_order_relaxed);
    for (auto& slot : NativeOverlaySlots) {
        InitializeSRWLock(&slot.lock);
        slot.observedRecord = nullptr;
        slot.ownedRecord = nullptr;
        slot.status = NativeOverlayStatus::Unseen;
        slot.observedOverlay = EmptyStateOverlay;
    }
    RuntimeBuild = "unknown";
    LoadedConfigPath = "embedded defaults";
    RelayPage = nullptr;
    ImageSize = 0;
    ApplyResistancesAndAbsorb = nullptr;
    GetDifficultyRecord = nullptr;
    GetDataTablesForContext = nullptr;
    GetUnitStat = nullptr;
    CheckState = nullptr;
    SetOverlay = nullptr;
    IsDead = nullptr;
    GetUnitType = nullptr;
    GetItemDataContext = nullptr;
    GetHirelingTypeId = nullptr;
    GetGameFromUnit = nullptr;
    OriginalApplyBurnDamage = nullptr;
    OriginalPlayerEventDispatcher = nullptr;
    OriginalMonsterEventDispatcher = nullptr;
    DiagnosticsService = nullptr;
}

} // namespace

extern "C" std::int32_t __fastcall BurnDamageFixNormalizeGeneric(
        void* attacker,
        std::int32_t scaledExistingBurn,
        std::uint32_t advancedRandom) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !Settings.normalizeGenericBurn || !GetUnitStat || !attacker) {
        return NormalizeGenericNumerator(
            scaledExistingBurn, 0, 0, 0, advancedRandom);
    }

    std::int32_t burningMin{};
    std::int32_t burningMax{};
    std::int32_t fireMastery{};
    __try {
        burningMax = GetUnitStat(attacker, BurningMaxStat, 0);
        if (burningMax > 0) {
            burningMin = GetUnitStat(attacker, BurningMinStat, 0);
            if (burningMin > 0) {
                fireMastery = GetUnitStat(attacker, PassiveFireMasteryStat, 0);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        burningMin = burningMax = fireMastery = 0;
    }

    if (Settings.diagnostics) {
        GenericProductionCalls.fetch_add(1, std::memory_order_relaxed);
        if (burningMin > 0 && burningMax > 0) {
            GenericProductionRolls.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return NormalizeGenericNumerator(
        scaledExistingBurn,
        burningMin,
        burningMax,
        fireMastery,
        advancedRandom);
}

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
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetState();
    if (!Base || !LoadConfig()) return false;

    const auto* runtimeBuild = D2RL::GetBuildName(context);
    RuntimeBuild = runtimeBuild && runtimeBuild[0] != '\0'
        ? runtimeBuild : "<unavailable>";

    if (!context->RegisterConsoleCommand(
            "burn-damage-fix",
            Status,
            "Show Burn Damage Fix settings and diagnostic counters.")) {
        context->LogWarn(
            "BurnDamageFix: optional status command was not registered.");
    }
    if (!Settings.enabled) {
        context->LogInfo(
            "Burn Damage Fix 1.0.1 by RuffnecKk loaded disabled; no hook was installed.");
        return true;
    }

    char fingerprintMessage[256]{};
    std::snprintf(
        fingerprintMessage,
        sizeof(fingerprintMessage),
        "BurnDamageFix: observed D2R build-name=%s; validating the complete native fingerprint.",
        RuntimeBuild.c_str());
    context->LogInfo(fingerprintMessage);
    if (!InitializeImageBounds()) {
        context->LogError(
            "BurnDamageFix: invalid PE64/AMD64 image metadata; plugin refused.");
        return false;
    }
    if (!QueryDiagnosticsService() || !ValidateRuntime()) return false;

    ApplyResistancesAndAbsorb = At<ApplyResistancesAndAbsorbFn>(
        ApplyResistancesAndAbsorbRva);
    GetDifficultyRecord = At<GetDifficultyRecordFn>(GetDifficultyRecordRva);
    GetDataTablesForContext = At<GetDataTablesForContextFn>(
        GetDataTablesForContextRva);
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    CheckState = At<CheckStateFn>(CheckStateRva);
    SetOverlay = At<SetOverlayFn>(SetOverlayRva);
    IsDead = At<IsDeadFn>(IsDeadRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetItemDataContext = At<GetItemDataContextFn>(GetItemDataContextRva);
    GetHirelingTypeId = At<GetHirelingTypeIdFn>(GetHirelingTypeIdRva);
    GetGameFromUnit = At<GetGameFromUnitFn>(GetGameFromUnitRva);

    if (Settings.applyFireResistance
            && !context->InstallInlineHook(
                ApplyBurnDamageRva,
                ApplyBurnDamageExpected.data(),
                static_cast<std::uint32_t>(ApplyBurnDamageExpected.size()),
                HookApplyBurnDamage,
                &OriginalApplyBurnDamage)) {
        context->LogError(
            "BurnDamageFix: Burn application entry is already owned; plugin refused.");
        return false;
    }
    if (Settings.replayFireHit
            && (!context->InstallInlineHook(
                    PlayerEventDispatcherRva,
                    PlayerEventDispatcherExpected.data(),
                    static_cast<std::uint32_t>(
                        PlayerEventDispatcherExpected.size()),
                    HookPlayerEventDispatcher,
                    &OriginalPlayerEventDispatcher)
                || !context->InstallInlineHook(
                    MonsterEventDispatcherRva,
                    MonsterEventDispatcherExpected.data(),
                    static_cast<std::uint32_t>(
                        MonsterEventDispatcherExpected.size()),
                    HookMonsterEventDispatcher,
                    &OriginalMonsterEventDispatcher))) {
        context->LogError(
            "BurnDamageFix: a unit-event dispatcher is already owned; plugin refused.");
        return false;
    }
    if (!InstallProductionRelay()) return false;
    Operational.store(true, std::memory_order_release);

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "Burn Damage Fix 1.0.1 by RuffnecKk active for observed D2R %s; generic=%s; resistance=%s; overlay=%s/fire_hit/%df; native-burning=%s; installation=%s; TOML=%s.",
        RuntimeBuild.c_str(),
        Settings.normalizeGenericBurn ? "enabled" : "disabled",
        Settings.applyFireResistance ? "enabled" : "disabled",
        Settings.replayFireHit ? "enabled" : "disabled",
        Settings.overlayRepeatFrames,
        ShouldSuppressNativeBurning(Settings) ? "suppress" : "preserve",
        context->loadScope == D2RL::LoadScope::Mod ? "mod-local" : "global",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    RestoreNativeBurningOverlays();
}
