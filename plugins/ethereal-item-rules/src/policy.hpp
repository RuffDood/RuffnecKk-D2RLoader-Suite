#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace RuffnecKk::EtherealItemRules {

inline constexpr std::size_t MaximumExcludedItemTypes = 64;
inline constexpr std::size_t ItemTypeRecordStride = 0xE8;
inline constexpr std::uint8_t VanillaChancePercent = 5;

struct ItemTypeCode {
    std::array<char, 4> bytes{' ', ' ', ' ', ' '};
    std::array<char, 5> text{};
    std::uint8_t length{};
};

struct ExclusionsConfig {
    bool enabled{};
    std::array<ItemTypeCode, MaximumExcludedItemTypes> itemTypes{};
    std::size_t itemTypeCount{};
};

struct GenerationConfig {
    bool enabled{};
    std::uint8_t chancePercent{VanillaChancePercent};
    bool allowSetItems{};
    bool allowIndestructibleItems{};
};

struct Config {
    bool enabled{true};
    ExclusionsConfig exclusions{};
    GenerationConfig generation{};
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
        if (value[index] == '"' && (index == 0 || value[index - 1] != '\\')) {
            quoted = !quoted;
        }
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
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{}
        || result.ptr != value.data() + value.size()) {
        return false;
    }
    output = parsed;
    return true;
}

inline auto NormalizeItemTypeCode(
    std::string_view input,
    ItemTypeCode& output
) noexcept -> bool {
    input = Trim(input);
    if (input.empty() || input.size() > 4) return false;

    output = {};
    output.bytes.fill(' ');
    output.length = static_cast<std::uint8_t>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto character = static_cast<unsigned char>(input[index]);
        if (!std::isalnum(character) && character != '_') return false;
        const auto normalized = static_cast<char>(std::tolower(character));
        output.bytes[index] = normalized;
        output.text[index] = normalized;
    }
    return true;
}

inline auto SameCode(
    const ItemTypeCode& left,
    const ItemTypeCode& right
) noexcept -> bool {
    return left.bytes == right.bytes;
}

inline auto FindItemTypeId(
    const void* records,
    std::uint64_t count,
    std::size_t stride,
    const ItemTypeCode& code
) noexcept -> std::int32_t {
    if (!records || stride < code.bytes.size() || count > 4096) return -1;
    const auto* bytes = static_cast<const std::uint8_t*>(records);
    for (std::uint64_t index = 0; index < count; ++index) {
        if (std::memcmp(
                bytes + index * stride,
                code.bytes.data(),
                code.bytes.size()) == 0) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

inline auto SetError(
    std::string& error,
    std::size_t line,
    std::string_view message
) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

inline auto AddItemType(
    std::string_view value,
    Config& parsed,
    std::string& error
) -> bool {
    ItemTypeCode code{};
    if (!NormalizeItemTypeCode(value, code)) {
        error = "item_types entries must be 1-4 character itemtypes.txt codes";
        return false;
    }
    for (std::size_t index = 0;
        index < parsed.exclusions.itemTypeCount;
        ++index) {
        if (SameCode(parsed.exclusions.itemTypes[index], code)) return true;
    }
    if (parsed.exclusions.itemTypeCount >= MaximumExcludedItemTypes) {
        error = "item_types supports at most 64 unique codes";
        return false;
    }
    parsed.exclusions.itemTypes[parsed.exclusions.itemTypeCount++] = code;
    return true;
}

inline auto ParseItemTypeArray(
    std::string_view value,
    Config& parsed,
    std::string& error
) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        error = "item_types must be an array of quoted strings";
        return false;
    }
    value = Trim(value.substr(1, value.size() - 2));
    while (!value.empty()) {
        if (value.front() != '"') {
            error = "item_types entries must be quoted strings";
            return false;
        }
        value.remove_prefix(1);
        const auto quote = value.find('"');
        if (quote == std::string_view::npos
            || value.substr(0, quote).find('\\') != std::string_view::npos) {
            error = "item_types entries cannot contain escapes";
            return false;
        }
        if (!AddItemType(value.substr(0, quote), parsed, error)) return false;
        value = Trim(value.substr(quote + 1));
        if (value.empty()) break;
        if (value.front() != ',') {
            error = "item_types entries must be comma-separated";
            return false;
        }
        value = Trim(value.substr(1));
    }
    return true;
}

inline auto ParseConfig(
    std::string_view input,
    Config& output,
    std::string& error
) -> bool {
    Config parsed{};
    std::string section;
    bool pluginSection{};
    bool exclusionsSection{};
    bool generationSection{};
    bool diagnosticsSection{};
    bool pluginEnabled{};
    bool exclusionsEnabled{};
    bool itemTypes{};
    bool generationEnabled{};
    bool chancePercent{};
    bool allowSetItems{};
    bool allowIndestructibleItems{};
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
            else if (section == "exclusions") seen = &exclusionsSection;
            else if (section == "generation") seen = &generationSection;
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

        std::string continuedValue;
        auto settingValue = value;
        if (section == "exclusions" && key == "item_types"
            && value.front() == '['
            && value.find(']') == std::string_view::npos) {
            continuedValue.assign(value);
            while (start <= input.size()
                && continuedValue.find(']') == std::string::npos) {
                ++lineNumber;
                const auto continuationEnd = input.find('\n', start);
                const auto continuation = Trim(WithoutComment(input.substr(
                    start,
                    continuationEnd == std::string_view::npos
                        ? input.size() - start
                        : continuationEnd - start)));
                start = continuationEnd == std::string_view::npos
                    ? input.size() + 1
                    : continuationEnd + 1;
                if (!continuation.empty()) {
                    continuedValue.push_back(' ');
                    continuedValue.append(continuation);
                }
            }
            settingValue = continuedValue;
        }

        bool valid{};
        std::string valueError;
        if (section == "plugin" && key == "enabled") {
            if (pluginEnabled) {
                return SetError(error, lineNumber, "duplicate setting");
            }
            pluginEnabled = true;
            valid = ParseBoolean(settingValue, parsed.enabled);
        } else if (section == "exclusions" && key == "enabled") {
            if (exclusionsEnabled) {
                return SetError(error, lineNumber, "duplicate setting");
            }
            exclusionsEnabled = true;
            valid = ParseBoolean(settingValue, parsed.exclusions.enabled);
        } else if (section == "exclusions" && key == "item_types") {
            if (itemTypes) return SetError(error, lineNumber, "duplicate setting");
            itemTypes = true;
            valid = ParseItemTypeArray(settingValue, parsed, valueError);
        } else if (section == "generation" && key == "enabled") {
            if (generationEnabled) {
                return SetError(error, lineNumber, "duplicate setting");
            }
            generationEnabled = true;
            valid = ParseBoolean(settingValue, parsed.generation.enabled);
        } else if (section == "generation" && key == "chance_percent") {
            if (chancePercent) return SetError(error, lineNumber, "duplicate setting");
            chancePercent = true;
            std::uint32_t chance{};
            valid = ParseUnsigned(settingValue, chance) && chance <= 100;
            if (valid) {
                parsed.generation.chancePercent = static_cast<std::uint8_t>(chance);
            }
        } else if (section == "generation" && key == "allow_set_items") {
            if (allowSetItems) return SetError(error, lineNumber, "duplicate setting");
            allowSetItems = true;
            valid = ParseBoolean(settingValue, parsed.generation.allowSetItems);
        } else if (section == "generation"
            && key == "allow_indestructible_items") {
            if (allowIndestructibleItems) {
                return SetError(error, lineNumber, "duplicate setting");
            }
            allowIndestructibleItems = true;
            valid = ParseBoolean(
                settingValue, parsed.generation.allowIndestructibleItems);
        } else if (section == "diagnostics" && key == "enabled") {
            if (diagnosticsEnabled) {
                return SetError(error, lineNumber, "duplicate setting");
            }
            diagnosticsEnabled = true;
            valid = ParseBoolean(settingValue, parsed.diagnosticsEnabled);
        } else {
            return SetError(error, lineNumber, "unknown setting");
        }
        if (!valid) {
            return SetError(
                error,
                lineNumber,
                valueError.empty() ? "invalid setting value" : valueError);
        }
    }

    if (!(exclusionsSection && generationSection && exclusionsEnabled
            && itemTypes && generationEnabled && chancePercent
            && allowSetItems && allowIndestructibleItems)
        || pluginSection != pluginEnabled
        || diagnosticsSection != diagnosticsEnabled) {
        error = "one or more required settings are missing";
        return false;
    }
    output = parsed;
    error.clear();
    return true;
}

inline auto HasExcludedItemTypes(const Config& config) noexcept -> bool {
    return config.enabled
        && config.exclusions.enabled
        && config.exclusions.itemTypeCount != 0;
}

inline auto PatchChance(const Config& config) noexcept -> bool {
    return config.enabled
        && config.generation.enabled
        && config.generation.chancePercent != VanillaChancePercent;
}

inline auto PatchSetItems(const Config& config) noexcept -> bool {
    return config.enabled
        && config.generation.enabled
        && config.generation.allowSetItems;
}

inline auto PatchIndestructibleItems(const Config& config) noexcept -> bool {
    return config.enabled
        && config.generation.enabled
        && config.generation.allowIndestructibleItems;
}

inline auto HasDirectRulePatches(const Config& config) noexcept -> bool {
    return PatchChance(config)
        || PatchSetItems(config)
        || PatchIndestructibleItems(config);
}

} // namespace RuffnecKk::EtherealItemRules
