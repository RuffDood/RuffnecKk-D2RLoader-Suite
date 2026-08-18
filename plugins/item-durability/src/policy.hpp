#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace RuffnecKk::ItemDurability {

struct Config {
    bool enabled{true};
    bool durabilityLossEnabled{};
    std::uint32_t normalResistancePercent{};
    std::uint32_t etherealResistancePercent{};
    std::uint32_t etherealMaximumPercent{50};
    bool forceMaximumDurability{};
    bool bowsAndCrossbowsHaveDurability{};
    bool diagnosticsEnabled{};
};

inline auto Trim(std::string_view value) noexcept -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'
            || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'
            || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

inline auto WithoutComment(std::string_view value) noexcept -> std::string_view {
    bool quoted{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '"') quoted = !quoted;
        if (value[index] == '#' && !quoted) return value.substr(0, index);
    }
    return value;
}

inline auto ParseBoolean(std::string_view value, bool& output) noexcept -> bool {
    if (value == "true") {
        output = true;
        return true;
    }
    if (value == "false") {
        output = false;
        return true;
    }
    return false;
}

inline auto ParseUnsigned(std::string_view value, std::uint32_t& output) noexcept -> bool {
    if (value.empty()) return false;
    std::uint32_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return false;
    output = parsed;
    return true;
}

inline auto SetError(
    std::string& error,
    std::size_t line,
    std::string_view message
) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

inline auto ParseConfig(
    std::string_view input,
    Config& output,
    std::string& error
) -> bool {
    Config parsed{};
    std::string section;
    bool pluginSection{};
    bool durabilitySection{};
    bool etherealSection{};
    bool rangedSection{};
    bool diagnosticsSection{};
    bool pluginEnabled{};
    bool lossEnabled{};
    bool normalResistance{};
    bool etherealResistance{};
    bool maximumPercent{};
    bool forceMaximum{};
    bool rangedDurability{};
    bool diagnosticsEnabled{};

    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = Trim(WithoutComment(input.substr(
            start,
            end == std::string_view::npos ? input.size() - start : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']'
                || line.find('[', 1) != std::string_view::npos
                || line.find(']') != line.size() - 1) {
                return SetError(error, lineNumber, "invalid section header");
            }
            section.assign(Trim(line.substr(1, line.size() - 2)));
            bool* seen{};
            if (section == "plugin") seen = &pluginSection;
            else if (section == "durability_loss") seen = &durabilitySection;
            else if (section == "ethereal") seen = &etherealSection;
            else if (section == "ranged_weapons") seen = &rangedSection;
            else if (section == "diagnostics") seen = &diagnosticsSection;
            else return SetError(error, lineNumber, "unknown section");
            if (*seen) return SetError(error, lineNumber, "duplicate section");
            *seen = true;
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string_view::npos
            || line.find('=', equal + 1) != std::string_view::npos) {
            return SetError(error, lineNumber, "expected one key/value assignment");
        }
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty() || section.empty()) {
            return SetError(error, lineNumber, "invalid key/value assignment");
        }

        bool valid{};
        if (section == "plugin" && key == "enabled") {
            if (pluginEnabled) return SetError(error, lineNumber, "duplicate setting");
            pluginEnabled = true;
            valid = ParseBoolean(value, parsed.enabled);
        } else if (section == "durability_loss" && key == "enabled") {
            if (lossEnabled) return SetError(error, lineNumber, "duplicate setting");
            lossEnabled = true;
            valid = ParseBoolean(value, parsed.durabilityLossEnabled);
        } else if (section == "durability_loss"
            && key == "normal_resistance_percent") {
            if (normalResistance) return SetError(error, lineNumber, "duplicate setting");
            normalResistance = true;
            valid = ParseUnsigned(value, parsed.normalResistancePercent)
                && parsed.normalResistancePercent <= 100;
        } else if (section == "durability_loss"
            && key == "ethereal_resistance_percent") {
            if (etherealResistance) return SetError(error, lineNumber, "duplicate setting");
            etherealResistance = true;
            valid = ParseUnsigned(value, parsed.etherealResistancePercent)
                && parsed.etherealResistancePercent <= 100;
        } else if (section == "ethereal" && key == "max_durability_percent") {
            if (maximumPercent) return SetError(error, lineNumber, "duplicate setting");
            maximumPercent = true;
            valid = ParseUnsigned(value, parsed.etherealMaximumPercent)
                && parsed.etherealMaximumPercent >= 1
                && parsed.etherealMaximumPercent <= 200;
        } else if (section == "ethereal" && key == "force_maximum_durability") {
            if (forceMaximum) return SetError(error, lineNumber, "duplicate setting");
            forceMaximum = true;
            valid = ParseBoolean(value, parsed.forceMaximumDurability);
        } else if (section == "ranged_weapons"
            && key == "bows_and_crossbows_have_durability") {
            if (rangedDurability) return SetError(error, lineNumber, "duplicate setting");
            rangedDurability = true;
            valid = ParseBoolean(value, parsed.bowsAndCrossbowsHaveDurability);
        } else if (section == "diagnostics" && key == "enabled") {
            if (diagnosticsEnabled) return SetError(error, lineNumber, "duplicate setting");
            diagnosticsEnabled = true;
            valid = ParseBoolean(value, parsed.diagnosticsEnabled);
        } else {
            return SetError(error, lineNumber, "unknown setting");
        }
        if (!valid) return SetError(error, lineNumber, "invalid setting value");
    }

    if (!(durabilitySection && etherealSection && rangedSection && diagnosticsSection
            && lossEnabled && normalResistance && etherealResistance
            && maximumPercent && forceMaximum && rangedDurability
            && diagnosticsEnabled)
        || pluginSection != pluginEnabled) {
        error = "one or more required settings are missing";
        return false;
    }
    output = parsed;
    error.clear();
    return true;
}

constexpr auto PackItemTypeCode(
    char first,
    char second,
    char third,
    char fourth = ' '
) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(first))
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(second)) << 8U)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(third)) << 16U)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourth)) << 24U);
}

constexpr auto IsBowOrCrossbowItemTypeCode(std::uint32_t code) noexcept -> bool {
    return code == PackItemTypeCode('b', 'o', 'w')
        || code == PackItemTypeCode('x', 'b', 'o', 'w');
}

inline constexpr std::size_t CompiledItemRecordStride = 0x1C0;
inline constexpr std::size_t CompiledItemNoDurabilityOffset = 0x122;
inline constexpr std::size_t CompiledItemPrimaryTypeOffset = 0x12E;
inline constexpr std::size_t CompiledItemTypeRecordStride = 0xE8;
inline constexpr std::size_t CompiledItemTypeCodeOffset = 0x00;
inline constexpr std::size_t CompiledItemTypeEquivalentOneOffset = 0x04;
inline constexpr std::size_t CompiledItemTypeEquivalentTwoOffset = 0x06;
inline constexpr std::size_t CompiledItemTypeRepairOffset = 0x08;
inline constexpr std::uint64_t MaximumCompiledItemRecords = 65'536;
inline constexpr std::uint64_t MaximumCompiledItemTypeRecords = 4'096;

struct RangedTableMutationResult {
    bool valid{};
    std::uint64_t itemRecordsUpdated{};
    std::uint64_t itemTypesUpdated{};
};

inline auto ReadItemTypeIndex(const std::uint8_t* bytes) noexcept -> std::uint16_t {
    std::uint16_t value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

inline auto IsBowOrCrossbowItemType(
    const std::uint8_t* records,
    std::uint64_t count,
    std::uint16_t itemType
) noexcept -> bool {
    if (!records || count == 0 || count > MaximumCompiledItemTypeRecords
        || itemType >= count) {
        return false;
    }

    std::array<std::uint16_t, 16> pending{};
    std::size_t pendingCount{1};
    pending[0] = itemType;
    for (std::size_t visited = 0;
        pendingCount != 0 && visited < pending.size();
        ++visited) {
        const auto current = pending[--pendingCount];
        if (current >= count) continue;
        const auto* record = records
            + static_cast<std::size_t>(current) * CompiledItemTypeRecordStride;
        std::uint32_t code{};
        std::memcpy(&code, record + CompiledItemTypeCodeOffset, sizeof(code));
        if (IsBowOrCrossbowItemTypeCode(code)) return true;

        const auto equivalentOne = ReadItemTypeIndex(
            record + CompiledItemTypeEquivalentOneOffset);
        const auto equivalentTwo = ReadItemTypeIndex(
            record + CompiledItemTypeEquivalentTwoOffset);
        if (equivalentOne < count && equivalentOne != current
            && pendingCount < pending.size()) {
            pending[pendingCount++] = equivalentOne;
        }
        if (equivalentTwo < count && equivalentTwo != current
            && pendingCount < pending.size()) {
            pending[pendingCount++] = equivalentTwo;
        }
    }
    return false;
}

inline auto ApplyRangedDurabilityToCompiledTables(
    std::uint8_t* itemRecords,
    std::uint64_t itemRecordCount,
    std::uint8_t* itemTypeRecords,
    std::uint64_t itemTypeCount
) noexcept -> RangedTableMutationResult {
    RangedTableMutationResult result{};
    if (!itemRecords || !itemTypeRecords || itemRecordCount == 0
        || itemRecordCount > MaximumCompiledItemRecords || itemTypeCount == 0
        || itemTypeCount > MaximumCompiledItemTypeRecords) {
        return result;
    }
    result.valid = true;

    for (std::uint64_t index = 0; index < itemRecordCount; ++index) {
        auto* itemRecord = itemRecords
            + static_cast<std::size_t>(index) * CompiledItemRecordStride;
        const auto itemType = ReadItemTypeIndex(
            itemRecord + CompiledItemPrimaryTypeOffset);
        if (!IsBowOrCrossbowItemType(itemTypeRecords, itemTypeCount, itemType)) {
            continue;
        }

        auto& noDurability = itemRecord[CompiledItemNoDurabilityOffset];
        if (noDurability != 0) {
            noDurability = 0;
            ++result.itemRecordsUpdated;
        }
        auto& repair = itemTypeRecords[
            static_cast<std::size_t>(itemType) * CompiledItemTypeRecordStride
            + CompiledItemTypeRepairOffset];
        if (repair == 0) {
            repair = 1;
            ++result.itemTypesUpdated;
        }
    }
    return result;
}

constexpr auto PreventsLoss(
    std::uint32_t resistancePercent,
    std::uint32_t roll
) noexcept -> bool {
    return roll < std::min(resistancePercent, 100U);
}

constexpr auto EffectiveChanceBasisPoints(
    std::uint32_t vanillaChancePercent,
    std::uint32_t resistancePercent
) noexcept -> std::uint32_t {
    return vanillaChancePercent * (100U - std::min(resistancePercent, 100U));
}

constexpr auto TargetEtherealMaxDurability(
    std::int32_t normalMaximum,
    std::uint32_t percent
) noexcept -> std::int32_t {
    if (normalMaximum <= 0) return normalMaximum;
    const auto clampedPercent = std::clamp(percent, 1U, 200U);
    const auto numerator = static_cast<std::int64_t>(normalMaximum) * clampedPercent;
    const auto scaled = clampedPercent < 100U
        ? numerator / 100 + 1
        : (numerator + 50) / 100;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled, 1, 255));
}

constexpr auto EncodeEtherealMaximumTarget(std::int32_t target) noexcept
    -> std::int32_t {
    return 2 * (std::clamp(target, 1, 255) - 1);
}

constexpr auto EncodeForVanillaEtherealHalving(
    std::int32_t normalMaximum,
    std::uint32_t percent
) noexcept -> std::int32_t {
    if (normalMaximum <= 0) return normalMaximum;
    return EncodeEtherealMaximumTarget(
        TargetEtherealMaxDurability(normalMaximum, percent));
}

constexpr auto ApplyVanillaEtherealHalving(std::int32_t value) noexcept
    -> std::int32_t {
    return value / 2 + 1;
}

} // namespace RuffnecKk::ItemDurability
