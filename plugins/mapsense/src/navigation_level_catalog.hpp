#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace RuffnecKk::MapSense {

// Resolves canonical English level names from the D2R 3.3 Levels.txt source.
// Ambiguous names fail closed; callers can always use level_id instead.
[[nodiscard]] auto ResolveCanonicalLevelName(
    std::string_view name) noexcept -> std::optional<std::int32_t>;

} // namespace RuffnecKk::MapSense
