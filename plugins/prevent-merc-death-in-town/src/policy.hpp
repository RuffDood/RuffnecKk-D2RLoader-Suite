#pragma once

#include "config.hpp"

#include <cstdint>

namespace RuffnecKk::PreventMercDeathInTown {

constexpr auto IsHirelingClass(std::uint32_t classId) noexcept -> bool {
    return classId == 271 || classId == 338 || classId == 359
        || classId == 560 || classId == 561;
}

constexpr auto IsProjectedLethal(
    std::int32_t hitpoints,
    std::int32_t regeneration
) noexcept -> bool {
    return regeneration < 0
        && static_cast<std::int64_t>(hitpoints) + regeneration <= 0;
}

} // namespace RuffnecKk::PreventMercDeathInTown
