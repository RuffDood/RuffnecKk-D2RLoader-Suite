#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ruffneckk::isc12 {

inline constexpr std::uint32_t SerializedBitWidth = 12;
inline constexpr std::uint16_t SerializedSentinel = 0x0FFF;
inline constexpr std::uint16_t MaximumStatId = SerializedSentinel - 1;
inline constexpr std::size_t MaximumRecordCount = 4095;
inline constexpr std::uint16_t InternalWordSentinel = 0xFFFF;
inline constexpr std::uint32_t LegacySerializedBitWidth = 9;
inline constexpr std::uint16_t LegacySerializedSentinel = 0x01FF;

static_assert(MaximumStatId == 4094);
static_assert(MaximumRecordCount == 4095);
static_assert(InternalWordSentinel != SerializedSentinel);

inline auto IsValidStatId(std::uint32_t id) noexcept -> bool {
    return id <= MaximumStatId;
}

inline auto IsValidRecordCount(std::size_t count) noexcept -> bool {
    return count <= MaximumRecordCount;
}

inline auto AddedSerializedBits(
        std::size_t identifierCount,
        std::size_t terminatorCount) noexcept -> std::size_t {
    constexpr auto AddedBitsPerField =
        SerializedBitWidth - LegacySerializedBitWidth;
    const auto fieldCount = identifierCount + terminatorCount;
    if (fieldCount > static_cast<std::size_t>(-1) / AddedBitsPerField) {
        return static_cast<std::size_t>(-1);
    }
    return fieldCount * AddedBitsPerField;
}

struct DescriptionEntry {
    std::uint16_t statId{};
    std::int16_t priority{};
};

struct DescriptionSource {
    std::uint8_t function{};
    std::int16_t priority{};
};

inline auto SortDescriptionEntries(
        std::span<DescriptionEntry> entries) noexcept -> bool {
    for (const auto& entry : entries) {
        if (!IsValidStatId(entry.statId)) return false;
    }
    std::sort(entries.begin(), entries.end(), [](
            const DescriptionEntry& left,
            const DescriptionEntry& right) noexcept {
        return left.priority < right.priority;
    });
    return true;
}

inline auto BuildDescriptionIndex(
        std::span<const DescriptionSource> rows,
        std::vector<std::uint16_t>& destination) -> bool {
    if (!IsValidRecordCount(rows.size())) return false;

    std::vector<DescriptionEntry> staged;
    staged.reserve(rows.size());
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        if (row.function == 0) continue;
        staged.push_back({
            static_cast<std::uint16_t>(rowIndex),
            row.priority,
        });
    }
    if (!SortDescriptionEntries(staged)) return false;

    std::vector<std::uint16_t> committed;
    committed.reserve(staged.size());
    for (const auto& entry : staged) committed.push_back(entry.statId);
    destination.swap(committed);
    return true;
}

} // namespace ruffneckk::isc12
