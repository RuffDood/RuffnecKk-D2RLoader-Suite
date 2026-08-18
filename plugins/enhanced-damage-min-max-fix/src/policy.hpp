#pragma once

#include "config.hpp"

#include <cstdint>

namespace RuffnecKk::EnhancedDamageMinMaxFix {

inline constexpr std::int32_t ItemMaxDamagePercentStat = 17;
inline constexpr std::int32_t ItemMinDamagePercentStat = 18;
inline constexpr std::int32_t ItemUnitType = 4;
inline constexpr std::int32_t WeaponItemTypeId = 45;
inline constexpr std::uint8_t AddItemStatPercentOperation = 13;

constexpr auto PackStat(
    std::int32_t stat,
    std::uint16_t layer = 0
) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(stat) << 16U) | layer);
}

constexpr auto IsEnhancedDamageStat(
    std::int32_t stat,
    std::uint16_t layer
) noexcept -> bool {
    return layer == 0
        && (stat == ItemMaxDamagePercentStat || stat == ItemMinDamagePercentStat);
}

constexpr auto IsEnhancedDamagePackedStat(
    std::int32_t packedStat
) noexcept -> bool {
    const auto value = static_cast<std::uint32_t>(packedStat);
    return IsEnhancedDamageStat(
        static_cast<std::int32_t>(value >> 16U),
        static_cast<std::uint16_t>(value & 0xFFFFU));
}

constexpr auto ShouldRestoreSuppressedUpdate(
    std::int32_t ownerType,
    std::uint8_t operation,
    std::int32_t packedStat,
    bool effectiveItemIsWeapon,
    std::int32_t evaluatedValue,
    std::int32_t retainedValue
) noexcept -> bool {
    return ownerType == ItemUnitType
        && operation == AddItemStatPercentOperation
        && IsEnhancedDamagePackedStat(packedStat)
        && !effectiveItemIsWeapon
        && evaluatedValue != retainedValue;
}

} // namespace RuffnecKk::EnhancedDamageMinMaxFix
