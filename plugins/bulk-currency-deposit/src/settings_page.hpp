#pragma once

#include "bulk_currency_deposit_policy.hpp"
#include <RuffnecKk/live_settings.hpp>
#include <stdexcept>

namespace ruffneckk::bulk_currency_deposit::LivePage {

inline constexpr const char Schema[] = R"toml(title = "Automatic Materials Deposit"
[[setting]]
type = "text"
label = "Cube inclusion and item filters apply on the next deposit. Other settings apply after restart."
[[setting]]
key = "deposit.include_cube_materials"
type = "bool"
label = "Include materials from the cube"
hint = "Only when the Horadric Cube is in your main inventory. Uses the same Advanced Stash routing and item filters."
[[setting]]
key = "deposit.include_item_codes"
type = "string"
label = "Include item codes"
hint = 'Enter a TOML array, for example ["r01", "r33"]. Empty [] includes all supported materials.'
[[setting]]
key = "deposit.exclude_item_codes"
type = "string"
label = "Exclude item codes"
hint = 'Enter a TOML array, for example ["r33"]. Exclusions take priority.'
[[setting]]
key = "deposit.enabled"
type = "bool"
label = "Enable at next start"
restart = true
[[setting]]
key = "deposit.inventory_button_enabled"
type = "bool"
label = "Inject Inventory button"
restart = true
[[setting]]
key = "button.x"
type = "int"
label = "Inventory button X"
min = -32768
max = 32767
restart = true
[[setting]]
key = "button.y"
type = "int"
label = "Inventory button Y"
min = -32768
max = 32767
restart = true
[[setting]]
key = "button.tooltip"
type = "string"
label = "Inventory button tooltip"
restart = true
)toml";

inline bool IsCodeList(std::string_view key) noexcept {
    return key == "deposit.include_item_codes" || key == "deposit.exclude_item_codes";
}

inline bool IsLive(std::string_view key) noexcept {
    return key == "deposit.include_cube_materials" || IsCodeList(key);
}

inline bool Known(std::string_view key) noexcept {
    return IsLive(key) || key == "deposit.enabled"
        || key == "deposit.inventory_button_enabled" || key == "button.x"
        || key == "button.y" || key == "button.tooltip";
}

inline Config Parse(std::string_view text) {
    Config config;
    std::string error;
    if (!ParseToml(text, config, error)) throw std::invalid_argument(error);
    return config;
}

inline std::string Value(const Config& config, std::string_view key) {
    using RuffnecKk::LiveSettings::Quote;
    if (key == "deposit.include_cube_materials") return config.includeCubeMaterials ? "true" : "false";
    if (key == "deposit.enabled") return config.enabled ? "true" : "false";
    if (key == "deposit.inventory_button_enabled") return config.inventoryButtonEnabled ? "true" : "false";
    if (key == "button.x") return std::to_string(config.button.x);
    if (key == "button.y") return std::to_string(config.button.y);
    if (key == "button.tooltip") return Quote(config.button.tooltip);
    if (IsCodeList(key)) {
        const auto& codes = key == "deposit.include_item_codes"
            ? config.includeItemCodes : config.excludeItemCodes;
        std::string array = "[";
        for (const auto code : codes) {
            if (array.size() > 1) array += ", ";
            array += Quote(UnpackItemCode(code));
        }
        return Quote(array + ']');
    }
    throw std::invalid_argument("Unknown setting.");
}

// The menu edits a string containing a TOML array; the file keeps a real array.
inline std::string FileValue(std::string_view key, std::string_view value) {
    if (!IsCodeList(key)) return std::string(value);
    const auto root = toml::parse("value = " + std::string(value));
    if (root.size() != 1) throw std::invalid_argument("Enter one item-code array.");
    const auto array = root["value"].value<std::string>();
    if (!array) throw std::invalid_argument("Enter an item-code array as text.");
    const auto codes = toml::parse("codes = " + *array);
    if (codes.size() != 1 || !codes["codes"].is_array())
        throw std::invalid_argument("Enter one item-code array.");
    const auto parsed = Parse("[deposit]\ninclude_item_codes = " + *array);
    std::string result = "[";
    for (const auto code : parsed.includeItemCodes) {
        if (result.size() > 1) result += ", ";
        result += RuffnecKk::LiveSettings::Quote(UnpackItemCode(code));
    }
    return result + ']';
}

inline std::string Edit(std::string_view document, std::string_view key, std::string_view value) {
    // Scalar edits already preserve comments and line endings. A code array
    // can span several lines; replace its parsed value as one source region.
    if (IsCodeList(key)) {
        const auto root = toml::parse(document);
        const auto name = key.substr(key.find('.') + 1);
        if (const auto* node = root["deposit"][name].node()) {
            const auto region = node->source();
            const auto offset = [&](toml::source_position position) {
                std::size_t at{};
                for (std::uint32_t line = 1; line < position.line; ++line) {
                    at = document.find('\n', at);
                    if (at == document.npos) throw std::invalid_argument("Invalid array source region.");
                    ++at;
                }
                // TOML columns count Unicode code points, not UTF-8 bytes.
                for (std::uint32_t column = 1; column < position.column; ++at) {
                    if (at >= document.size()) throw std::invalid_argument("Invalid array source column.");
                    if ((static_cast<unsigned char>(document[at]) & 0xC0) != 0x80) ++column;
                }
                while (at < document.size()
                        && (static_cast<unsigned char>(document[at]) & 0xC0) == 0x80) ++at;
                return at;
            };
            std::string edited(document);
            const auto first = offset(region.begin);
            edited.replace(first, offset(region.end) - first, value);
            return edited;
        }
    }
    return RuffnecKk::LiveSettings::Edit(document, key, value);
}

inline Config CopyLive(const Config& running, const Config& saved, std::string_view key) {
    auto next = running;
    if (key == "deposit.include_cube_materials") next.includeCubeMaterials = saved.includeCubeMaterials;
    else if (key == "deposit.include_item_codes") next.includeItemCodes = saved.includeItemCodes;
    else if (key == "deposit.exclude_item_codes") next.excludeItemCodes = saved.excludeItemCodes;
    else if (!Known(key)) throw std::invalid_argument("Unknown setting.");
    return next;
}

} // namespace ruffneckk::bulk_currency_deposit::LivePage
