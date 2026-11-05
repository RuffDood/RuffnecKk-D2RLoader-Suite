#pragma once
#include <D2RLPlugin/api.h>
#include <RuffnecKk/native_stat_compat.hpp>
#include <cstdint>
#include <span>

namespace RuffnecKk::AutoPickup::Compat {
inline constexpr std::uintptr_t MapsStatReaderRva = 0x13340;
inline constexpr std::uintptr_t MapsStatOwnerRva = 0xE730;
inline constexpr std::uintptr_t MapsOriginalStatSlotRva = 0xFFBF0;
inline constexpr std::uintptr_t MapsRedirectSlotAddressRva = 0x1021E0;
inline constexpr std::uintptr_t MapsRedirectPreviousValueRva = 0x1021E8;
inline constexpr std::uintptr_t MapsContextSlotRva = 0x112230;
inline constexpr std::uintptr_t MapsStatHookActiveRva = 0xFFE11;
inline constexpr std::uintptr_t MapsNativeStatRvaSlotRva = 0xFFE18;
inline constexpr std::uint32_t NativeUnitStatRva = 0x2F5020;

struct MapsStatProfile final {
    const char* version;
    std::uintptr_t readerRva;
    std::uintptr_t ownerRva;
    std::uintptr_t originalStatSlotRva;
    std::uintptr_t redirectSlotAddressRva;
    std::uintptr_t redirectPreviousValueRva;
    std::uintptr_t contextSlotRva;
    std::uintptr_t statHookActiveRva;
    std::uintptr_t nativeStatRvaSlotRva;
    std::span<const std::uint8_t> readerWitness;
    std::span<const std::uint8_t> ownerWitness;
};

auto ValidateReimaginedStatRedirect(void* userData, std::uintptr_t mainImageBase,
    std::uint32_t nativeRva, std::uintptr_t slotAddress,
    std::uintptr_t actualTarget, std::uintptr_t expectedCoreTarget) noexcept -> bool;
inline auto StatRedirectProof() noexcept -> RuffnecKk::NativeStatCompat::TransformDiagnostics {
    return {nullptr, nullptr, ValidateReimaginedStatRedirect};
}
}
