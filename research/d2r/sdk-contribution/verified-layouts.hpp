#pragma once

#include <cstddef>
#include <cstdint>

// Small, independently verified layout fragments for D2R.exe 3.2.92777.
// These definitions are incomplete on purpose. Unknown fields stay unnamed.
namespace ruffneckk::d2r_3_2_92777 {

inline constexpr std::uintptr_t GetDataTablesForContextRva = 0x300A90;
inline constexpr std::uintptr_t SkillsGetRuntimeMaxLevelRva = 0x214220;
inline constexpr std::uintptr_t SkillTreeCanAllocateSkillRva = 0x14C3DA0;
inline constexpr std::uintptr_t ItemsGetMaxSocketsRva = 0x36EAD0;
inline constexpr std::uintptr_t ItemsGetInvPageRva = 0x36CFE0;
inline constexpr std::uintptr_t UnitsGetItemDataRva = 0x34A500;
inline constexpr std::uintptr_t UnitGetClassIdRva = 0x349860;
inline constexpr std::uintptr_t UnitGetTypeRva = 0x34B9D0;

using GetDataTablesForContextFn = std::uint8_t* (__fastcall*)(std::uint8_t context);
using SkillsGetRuntimeMaxLevelFn = std::int32_t (__fastcall*)(std::uint8_t context, std::int32_t skillId);
using SkillTreeCanAllocateSkillFn = bool (__fastcall*)(std::int32_t skillId);
using ItemsGetMaxSocketsFn = std::uint8_t (__fastcall*)(void* item);
using ItemsGetInvPageFn = std::uint8_t (__fastcall*)(void* item);
using UnitsGetItemDataFn = std::uint8_t* (__fastcall*)(void* item);

inline constexpr std::size_t SkillsRecordsOffset = 0x11B0;
inline constexpr std::size_t SkillsCountOffset = 0x11B8;
inline constexpr std::size_t SkillsRecordSize = 0x2EC;

inline constexpr std::size_t ItemTypesRecordsOffset = 0x1348;
inline constexpr std::size_t ItemTypesCountOffset = 0x1350;
inline constexpr std::size_t ItemTypesRecordSize = 0xE8;

inline constexpr std::size_t ItemsRecordsOffset = 0x15A0;
inline constexpr std::size_t ItemsCountOffset = 0x15A8;
inline constexpr std::size_t ItemsRecordSize = 0x1C0;

#pragma pack(push, 1)

struct D2UnitHeader {
    std::uint32_t unitType; // +0x00
    std::uint32_t classId;  // +0x04: class/TXT record ID, not unit flags
};

struct D2ItemDataPageView {
    std::uint8_t unknown00[0x55]; // +0x00..+0x54
    std::uint8_t inventoryPage;   // +0x55
};

struct D2ItemTypesTxt {
    std::uint32_t code;              // +0x00
    std::uint16_t equivalentTypeOne; // +0x04
    std::uint16_t equivalentTypeTwo; // +0x06
    std::uint8_t repair;             // +0x08
    std::uint8_t unknown09[0xDF];     // +0x09..+0xE7
};

struct D2ItemsTxtVerifiedView {
    std::uint8_t unknown000[0x121]; // +0x000..+0x120
    std::uint8_t durability;       // +0x121
    std::uint8_t noDurability;     // +0x122
    std::uint8_t unknown123[0x0B]; // +0x123..+0x12D
    std::uint16_t primaryType;     // +0x12E
    std::uint8_t unknown130[0x90]; // +0x130..+0x1BF
};

#pragma pack(pop)

static_assert(offsetof(D2UnitHeader, classId) == 0x04);
static_assert(sizeof(D2UnitHeader) == 0x08);

static_assert(offsetof(D2ItemDataPageView, inventoryPage) == 0x55);

static_assert(offsetof(D2ItemTypesTxt, equivalentTypeOne) == 0x04);
static_assert(offsetof(D2ItemTypesTxt, equivalentTypeTwo) == 0x06);
static_assert(offsetof(D2ItemTypesTxt, repair) == 0x08);
static_assert(sizeof(D2ItemTypesTxt) == ItemTypesRecordSize);

static_assert(offsetof(D2ItemsTxtVerifiedView, durability) == 0x121);
static_assert(offsetof(D2ItemsTxtVerifiedView, noDurability) == 0x122);
static_assert(offsetof(D2ItemsTxtVerifiedView, primaryType) == 0x12E);
static_assert(sizeof(D2ItemsTxtVerifiedView) == ItemsRecordSize);

} // namespace ruffneckk::d2r_3_2_92777
