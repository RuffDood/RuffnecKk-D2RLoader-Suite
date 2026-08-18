#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::BulkSkillPointAllocation::NativeContract {

inline constexpr std::uint32_t SupportedBuild = 92777;

inline constexpr std::uintptr_t SendFiveBytePacketRva = 0x000EC700;
inline constexpr std::uintptr_t IsVirtualKeyDownRva = 0x0120A100;
inline constexpr std::uintptr_t GetLocalizedStringByKeyRva = 0x005F4B90;
inline constexpr std::uintptr_t ShowAssignAllConfirmationRva = 0x014EF670;

inline constexpr std::uint8_t AllocateSkillOpcode = 0x3B;
inline constexpr std::int32_t SkillConfirmationSentinel = 0x42534B50;
inline constexpr std::size_t FakeStatWidgetSize = 0xB90;
inline constexpr std::size_t FakeStatIndexOffset = 0xB88;

inline constexpr std::array<std::uint8_t, 29> SendFiveBytePacketExpected{
    0x48, 0x83, 0xEC, 0x28, 0x88, 0x4C, 0x24, 0x48,
    0x48, 0x8D, 0x4C, 0x24, 0x48, 0x66, 0x89, 0x54,
    0x24, 0x49, 0xBA, 0x05, 0x00, 0x00, 0x00, 0x66,
    0x44, 0x89, 0x44, 0x24, 0x4B,
};

inline constexpr std::array<std::uint8_t, 21> IsVirtualKeyDownExpected{
    0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0x86, 0x6E,
    0xAA, 0x00, 0xC1, 0xE8, 0x0F, 0x83, 0xE0, 0x01,
    0x48, 0x83, 0xC4, 0x28, 0xC3,
};

inline constexpr std::array<std::uint8_t, 29>
    GetLocalizedStringByKeyExpected{
        0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x49, 0x8D,
        0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0x05, 0x20, 0x67, 0x3D, 0x02,
        0x48, 0x33, 0xC4, 0x48, 0x89,
    };

inline constexpr std::array<std::uint8_t, 29>
    ShowAssignAllConfirmationExpected{
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
        0x24, 0x20, 0x55, 0x57, 0x41, 0x55, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xFC,
        0xFF, 0xFF, 0x48, 0x81, 0xEC,
    };

struct HookSite {
    std::uintptr_t rva;
    std::size_t preflightSize;
    bool conditional;
};

inline constexpr std::array<HookSite, 2> OwnedHookSites{{
    {SendFiveBytePacketRva, SendFiveBytePacketExpected.size(), false},
    {GetLocalizedStringByKeyRva,
     GetLocalizedStringByKeyExpected.size(), true},
}};

struct ProtectedRange {
    std::uintptr_t rva;
    std::size_t size;
};

constexpr auto Overlaps(
    HookSite owned,
    ProtectedRange other
) noexcept -> bool {
    const auto ownedEnd = owned.rva + owned.preflightSize;
    const auto otherEnd = other.rva + other.size;
    return owned.rva < otherEnd && other.rva < ownedEnd;
}

// Exact external coexistence gate supplied for the maintained yinyin plugins.
inline constexpr std::array<ProtectedRange, 21> ExternalProtectedSites{{
    {0x349860, 32}, {0x483B20, 32}, {0x485350, 32},
    {0x4FF1A0, 32}, {0x501550, 32}, {0x4350E0, 32},
    {0x432B70, 32}, {0x492CF0, 32}, {0x55A5D0, 32},
    {0x533AE0, 32}, {0x531510, 32},
    {0x472590, 32}, {0x36CFE0, 32}, {0x5269C0, 32},
    {0x314110, 32}, {0x34A360, 32}, {0x388C10, 32},
    {0x38ABA0, 32},
    {0x59CC90, 32}, {0x47C570, 32}, {0x12C190, 32},
}};

// Remote Stash owns these entries or callsites. Thirty-two bytes are reserved
// conservatively for inline entries and five bytes for redirected callsites.
inline constexpr std::array<ProtectedRange, 30> RemoteStashProtectedSites{{
    {0x22BA70, 32}, {0x4BA617, 32}, {0x0CE500, 32},
    {0x0C7D30, 32}, {0x4BA580, 32}, {0x43EC10, 32},
    {0x4AA100, 32}, {0x474700, 32}, {0x4C5570, 32},
    {0x4C6480, 32}, {0x4BFF30, 32},
    {0x0CD7C0, 32}, {0x08D540, 32}, {0x08D510, 32},
    {0x8F1069, 5}, {0x259132, 5}, {0x25A11D, 5},
    {0x0FEE36, 5}, {0x0FF1D7, 5}, {0x15E382, 5},
    {0x1F6674, 5}, {0x2A9D68, 5}, {0x2AAB0D, 5},
    {0x2ABB09, 5}, {0x2C6055, 5}, {0x2C6265, 5},
    {0x2C7479, 5}, {0x102590, 5}, {0x14F5895, 5},
    {0x0C1F01, 5},
}};

template<std::size_t Count>
constexpr auto HasNoOverlap(
    const std::array<ProtectedRange, Count>& protectedSites
) noexcept -> bool {
    for (const auto& owned : OwnedHookSites) {
        for (const auto& site : protectedSites) {
            if (Overlaps(owned, site)) return false;
        }
    }
    return true;
}

static_assert(FakeStatIndexOffset + sizeof(std::int32_t)
    <= FakeStatWidgetSize);
static_assert(HasNoOverlap(ExternalProtectedSites));
static_assert(HasNoOverlap(RemoteStashProtectedSites));

} // namespace RuffnecKk::BulkSkillPointAllocation::NativeContract
