#pragma once

#include "config.hpp"

#include <cstddef>
#include <cstdint>

namespace RuffnecKk::CharmAuraTriggerFix {

inline constexpr std::int32_t CharmItemTypeId = 0x0D;
inline constexpr std::uint8_t InventoryNodePosition = 3;
inline constexpr std::uint32_t IdentifiedItemFlag = 0x10;
inline constexpr std::size_t MaximumRefreshedCharms = 32;
inline constexpr std::uint16_t ItemAuraStatId = 151;

struct PackedStatRecord {
    std::uint32_t packed{};
    std::int32_t value{};
};

constexpr auto StatId(std::uint32_t packed) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(packed >> 16U);
}

constexpr auto HasNonzeroStat(
    const PackedStatRecord* records,
    std::size_t count,
    std::uint16_t wantedStat
) noexcept -> bool {
    if (!records) return false;
    for (std::size_t index = 0; index < count; ++index) {
        const auto stat = StatId(records[index].packed);
        if (stat > wantedStat) return false;
        if (stat == wantedStat && records[index].value != 0) return true;
    }
    return false;
}

constexpr auto IsEligible(
    bool matchesCharmType,
    std::uint8_t nodePosition,
    std::uint32_t itemFlags
) noexcept -> bool {
    return matchesCharmType
        && nodePosition == InventoryNodePosition
        && (itemFlags & IdentifiedItemFlag) != 0;
}

} // namespace RuffnecKk::CharmAuraTriggerFix
