#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ruffneckk::extended_act_level_ids {

inline constexpr std::size_t LevelsIdOffset = 0x00;
inline constexpr std::size_t LevelsActOffset = 0x0D;
inline constexpr std::uint32_t LevelsRowSize = 0x18C;
inline constexpr std::uint8_t MinimumDataContext = 1;
inline constexpr std::uint8_t MaximumDataContext = 3;
inline constexpr std::uint8_t MaximumAct = 4;

struct ActEntry {
    std::int32_t levelId{};
    std::uint8_t act{};

    auto operator<=>(const ActEntry&) const noexcept = default;
};

constexpr bool IsSupportedDataContext(std::uint8_t dataContext) noexcept {
    return dataContext >= MinimumDataContext
        && dataContext <= MaximumDataContext;
}

inline std::optional<std::uint8_t> FindAct(
        std::span<const ActEntry> entries,
        std::int32_t levelId) noexcept {
    const auto found = std::lower_bound(
        entries.begin(),
        entries.end(),
        levelId,
        [](const ActEntry& entry, std::int32_t value) {
            return entry.levelId < value;
        });
    if (found == entries.end()
            || found->levelId != levelId
            || found->act > MaximumAct) {
        return std::nullopt;
    }
    return found->act;
}

inline bool HasValidAnchorActs(std::span<const ActEntry> entries) noexcept {
    constexpr std::array<ActEntry, 5> anchors{{
        {1, 0},
        {40, 1},
        {75, 2},
        {103, 3},
        {109, 4},
    }};
    return std::all_of(
        anchors.begin(),
        anchors.end(),
        [&](const ActEntry& anchor) {
            return FindAct(entries, anchor.levelId) == anchor.act;
        });
}

} // namespace ruffneckk::extended_act_level_ids
