#pragma once

#include "config.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

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

// The diagnostic line is a check contract, pinned by tests/policy_tests.cpp.
// Checks match CorrectionLogPhrase and the fields, never the version prefix.
inline constexpr std::uint64_t MaximumDiagnosticLogs = 8;
inline constexpr char CorrectionLogPhrase[] = "restored an off-weapon update";
inline constexpr char CorrectionLogFormat[] =
    "Enhanced Damage Min/Max Fix 1.2.4 restored an off-weapon update "
    "(stat=%u, retained=%d, evaluated=%d).";

// Counts a line only while diagnostics are enabled, and allows the first
// MaximumDiagnosticLogs of them.
inline auto ShouldLogCorrection(
    bool diagnosticsEnabled,
    std::atomic<std::uint64_t>& loggedCorrections
) noexcept -> bool {
    return diagnosticsEnabled
        && loggedCorrections.fetch_add(1, std::memory_order_relaxed)
            < MaximumDiagnosticLogs;
}

inline auto FormatCorrectionLog(
    char* output,
    std::size_t size,
    std::int32_t packedStat,
    std::int32_t retainedValue,
    std::int32_t evaluatedValue
) noexcept -> int {
    return std::snprintf(
        output,
        size,
        CorrectionLogFormat,
        static_cast<unsigned>(static_cast<std::uint32_t>(packedStat) >> 16U),
        retainedValue,
        evaluatedValue);
}

} // namespace RuffnecKk::EnhancedDamageMinMaxFix
