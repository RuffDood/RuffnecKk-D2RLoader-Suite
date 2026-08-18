#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuffnecKk::ProgressiveAffixes {

inline constexpr std::size_t RelayStride = 32;

inline auto BuildTailRelay(
    std::array<std::uint8_t, RelayStride>& relay,
    const std::uint8_t* setup,
    std::size_t setupSize,
    std::uintptr_t target
) noexcept -> bool {
    if (setupSize + 12 > relay.size()) return false;
    relay.fill(0xCC);
    if (setupSize != 0) {
        std::memcpy(relay.data(), setup, setupSize);
    }
    relay[setupSize] = 0x48;
    relay[setupSize + 1] = 0xB8; // mov rax, target
    std::memcpy(relay.data() + setupSize + 2, &target, sizeof(target));
    relay[setupSize + 10] = 0xFF;
    relay[setupSize + 11] = 0xE0; // jmp rax
    return true;
}

inline void BuildPreservingFirstTwoArgumentsRelay(
    std::array<std::uint8_t, RelayStride>& relay,
    std::uintptr_t target
) noexcept {
    relay.fill(0xCC);
    constexpr std::array<std::uint8_t, 8> prefix{
        0x51,                         // push rcx
        0x52,                         // push rdx
        0x48, 0x83, 0xEC, 0x28,       // shadow space and alignment
        0x48, 0xB8,                   // mov rax, target
    };
    constexpr std::array<std::uint8_t, 7> suffix{
        0xFF, 0xD0,                   // call rax
        0x48, 0x83, 0xC4, 0x28,       // release shadow space
        0x5A,                         // pop rdx
    };
    constexpr std::array<std::uint8_t, 2> epilogue{
        0x59,                         // pop rcx
        0xC3,                         // ret
    };
    std::size_t offset{};
    std::memcpy(relay.data() + offset, prefix.data(), prefix.size());
    offset += prefix.size();
    std::memcpy(relay.data() + offset, &target, sizeof(target));
    offset += sizeof(target);
    std::memcpy(relay.data() + offset, suffix.data(), suffix.size());
    offset += suffix.size();
    std::memcpy(relay.data() + offset, epilogue.data(), epilogue.size());
}

} // namespace RuffnecKk::ProgressiveAffixes
