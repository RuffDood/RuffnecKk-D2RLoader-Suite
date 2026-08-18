#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuffnecKk::ProgressiveAffixes {

inline constexpr std::size_t RareSelectionPatchSize = 30;

namespace Detail {

inline auto EncodeRel32(
    std::uint8_t* output,
    std::uint8_t opcode,
    std::uintptr_t instruction,
    std::uintptr_t target
) noexcept -> bool {
    const auto displacement = static_cast<std::int64_t>(target)
        - static_cast<std::int64_t>(instruction + 5);
    if (displacement < std::numeric_limits<std::int32_t>::min()
            || displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    output[0] = opcode;
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(output + 1, &relative, sizeof(relative));
    return true;
}

} // namespace Detail

inline auto BuildRareSelectionPatch(
    std::array<std::uint8_t, RareSelectionPatchSize>& output,
    std::uintptr_t patchSite,
    std::uintptr_t relayTarget,
    std::uintptr_t returnTarget
) noexcept -> bool {
    std::array<std::uint8_t, RareSelectionPatchSize> patch{};
    patch.fill(0x90);
    if (!Detail::EncodeRel32(
            patch.data(),
            0xE8,
            patchSite,
            relayTarget)) {
        return false;
    }

    // The REX prefix must select R12D. 41 8B E0 decodes as mov esp,r8d and
    // corrupts RSP before the Rare generator next reads its stack frame.
    constexpr std::array<std::uint8_t, 3> moveCount{
        0x44, 0x8B, 0xE0, // mov r12d, eax
    };
    constexpr std::array<std::uint8_t, 3> restoreSeed{
        0x4C, 0x8B, 0xCB, // mov r9, rbx
    };
    std::memcpy(patch.data() + 5, moveCount.data(), moveCount.size());
    std::memcpy(patch.data() + 8, restoreSeed.data(), restoreSeed.size());

    constexpr std::size_t jumpOffset = 11;
    if (!Detail::EncodeRel32(
            patch.data() + jumpOffset,
            0xE9,
            patchSite + jumpOffset,
            returnTarget)) {
        return false;
    }
    output = patch;
    return true;
}

} // namespace RuffnecKk::ProgressiveAffixes
