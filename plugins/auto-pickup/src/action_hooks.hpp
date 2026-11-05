#pragma once
#include <cstdint>
namespace Reimagined::ActionHooks {
inline constexpr std::uint8_t MovementFirst = 0x01, MovementLast = 0x04;
inline constexpr std::uint8_t CastFirst = 0x05, CastLast = 0x12;
inline constexpr auto IsMovement(std::uint8_t opcode) noexcept -> bool {
    return opcode >= MovementFirst && opcode <= MovementLast;
}
inline constexpr auto IsCast(std::uint8_t opcode) noexcept -> bool {
    return opcode >= CastFirst && opcode <= CastLast;
}
static_assert(MovementLast < CastFirst);
}
