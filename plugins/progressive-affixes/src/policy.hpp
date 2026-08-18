#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RuffnecKk::ProgressiveAffixes {

constexpr std::size_t MaxCategories = 32;
constexpr std::size_t MaxTypesPerCategory = 16;
constexpr std::size_t MaxStepsPerCategory = 32;
constexpr std::size_t ItemTypeRecordStride = 0xE8;

struct ItemTypeCode {
    std::array<char, 4> bytes{' ', ' ', ' ', ' '};
    std::array<char, 5> text{};
    std::uint8_t length{};
    bool wildcard{};
};

struct MagicStep {
    std::int32_t minimumItemLevel{};
    std::int32_t minimumAffixes{};
};

struct WeightedStep {
    std::int32_t minimumItemLevel{};
    std::vector<std::uint32_t> weights;
};

struct MagicCategory {
    std::string name;
    std::vector<ItemTypeCode> itemTypes;
    std::vector<MagicStep> steps;
};

struct WeightedCategory {
    std::string name;
    std::vector<ItemTypeCode> itemTypes;
    std::vector<std::int32_t> counts;
    std::vector<WeightedStep> steps;
};

struct Config {
    bool enabled{};
    bool diagnostics{};
    std::vector<MagicCategory> magic;
    std::vector<WeightedCategory> rare;
    std::vector<WeightedCategory> crafted;
};

inline std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

inline std::string StripComment(std::string line) {
    bool quoted{};
    bool escaped{};
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == '"' && !escaped) quoted = !quoted;
        if (character == '#' && !quoted) return line.substr(0, index);
        escaped = character == '\\' && !escaped;
        if (character != '\\') escaped = false;
    }
    return line;
}

inline ItemTypeCode NormalizeItemTypeCode(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
        input.remove_suffix(1);
    }
    if (input == "*") {
        ItemTypeCode result{};
        result.text[0] = '*';
        result.length = 1;
        result.wildcard = true;
        return result;
    }
    if (input.empty() || input.size() > 4) {
        throw std::invalid_argument("item type codes must contain one to four characters");
    }
    ItemTypeCode result{};
    result.bytes.fill(' ');
    result.length = static_cast<std::uint8_t>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto character = static_cast<unsigned char>(input[index]);
        if (!std::isalnum(character) && character != '_') {
            throw std::invalid_argument("item type codes may contain only letters, digits, or underscore");
        }
        const auto normalized = static_cast<char>(std::tolower(character));
        result.bytes[index] = normalized;
        result.text[index] = normalized;
    }
    return result;
}

inline bool SameCode(const ItemTypeCode& left, const ItemTypeCode& right) noexcept {
    return left.wildcard == right.wildcard && left.bytes == right.bytes;
}

inline std::int32_t FindItemTypeId(
        const void* records,
        std::uint64_t count,
        const ItemTypeCode& code) noexcept {
    if (!records || code.wildcard || count == 0 || count > 4096) return -1;
    const auto* bytes = static_cast<const std::uint8_t*>(records);
    for (std::uint64_t index = 0; index < count; ++index) {
        if (std::equal(
                code.bytes.begin(),
                code.bytes.end(),
                reinterpret_cast<const char*>(bytes + index * ItemTypeRecordStride))) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

inline std::string ParseString(std::string_view value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        throw std::invalid_argument("expected a quoted string");
    }
    std::string result;
    result.reserve(value.size() - 2);
    bool escaped{};
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        const char character = value[index];
        if (escaped) {
            switch (character) {
            case '\\': result.push_back('\\'); break;
            case '"': result.push_back('"'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: throw std::invalid_argument("unsupported string escape");
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            throw std::invalid_argument("unescaped quote in string");
        } else {
            result.push_back(character);
        }
    }
    if (escaped) throw std::invalid_argument("unterminated string escape");
    return result;
}

inline bool ParseBoolean(std::string_view value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::invalid_argument("expected true or false");
}

inline std::int32_t ParseInteger(std::string_view value) {
    if (value.empty()) throw std::invalid_argument("expected an integer");
    std::int32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument("expected a base-10 integer");
    }
    return result;
}

inline std::vector<std::string> ParseStringArray(std::string_view value) {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        throw std::invalid_argument("expected a string array");
    }
    value.remove_prefix(1);
    value.remove_suffix(1);
    std::vector<std::string> result;
    std::size_t index{};
    while (index < value.size()) {
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index]))) ++index;
        if (index == value.size()) break;
        if (value[index] != '"') throw std::invalid_argument("string-array values must be quoted");
        std::size_t end = index + 1;
        bool escaped{};
        for (; end < value.size(); ++end) {
            if (value[end] == '"' && !escaped) break;
            escaped = value[end] == '\\' && !escaped;
            if (value[end] != '\\') escaped = false;
        }
        if (end == value.size()) throw std::invalid_argument("unterminated string-array value");
        result.push_back(ParseString(value.substr(index, end - index + 1)));
        index = end + 1;
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index]))) ++index;
        if (index == value.size()) break;
        if (value[index] != ',') throw std::invalid_argument("string-array values must be comma-separated");
        ++index;
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index]))) ++index;
        if (index == value.size()) break;
    }
    return result;
}

inline std::vector<std::int32_t> ParseIntegerArray(std::string_view value) {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        throw std::invalid_argument("expected an integer array");
    }
    value.remove_prefix(1);
    value.remove_suffix(1);
    std::vector<std::int32_t> result;
    std::size_t start{};
    while (start < value.size()) {
        const auto comma = value.find(',', start);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        const auto token = Trim(std::string(value.substr(start, end - start)));
        if (!token.empty()) result.push_back(ParseInteger(token));
        else if (comma != std::string_view::npos) throw std::invalid_argument("empty integer-array value");
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return result;
}

inline std::uint32_t ParsePercentage(std::string_view value) {
    if (value.empty()) throw std::invalid_argument("expected a percentage");
    const auto decimal = value.find('.');
    if (decimal != std::string_view::npos && value.find('.', decimal + 1) != std::string_view::npos) {
        throw std::invalid_argument("percentages may contain only one decimal point");
    }

    const auto wholeText = decimal == std::string_view::npos ? value : value.substr(0, decimal);
    const auto fractionText = decimal == std::string_view::npos ? std::string_view{} : value.substr(decimal + 1);
    if (wholeText.empty() || (decimal != std::string_view::npos && fractionText.empty())) {
        throw std::invalid_argument("percentages require digits before and after the decimal point");
    }
    if (fractionText.size() > 2) {
        throw std::invalid_argument("percentages support at most two decimal places");
    }

    const auto whole = ParseInteger(wholeText);
    if (whole < 0 || whole > 100) throw std::invalid_argument("percentages must be within 0..100");
    std::uint32_t fraction{};
    for (const auto character : fractionText) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            throw std::invalid_argument("percentage decimals must contain only digits");
        }
        fraction = fraction * 10 + static_cast<std::uint32_t>(character - '0');
    }
    if (fractionText.size() == 1) fraction *= 10;
    if (whole == 100 && fraction != 0) throw std::invalid_argument("percentages must be within 0..100");
    return static_cast<std::uint32_t>(whole) * 100 + fraction;
}

inline std::vector<std::uint32_t> ParsePercentageArray(std::string_view value) {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        throw std::invalid_argument("expected a percentage array");
    }
    value.remove_prefix(1);
    value.remove_suffix(1);
    std::vector<std::uint32_t> result;
    std::size_t start{};
    while (start < value.size()) {
        const auto comma = value.find(',', start);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        const auto token = Trim(std::string(value.substr(start, end - start)));
        if (!token.empty()) result.push_back(ParsePercentage(token));
        else if (comma != std::string_view::npos) throw std::invalid_argument("empty percentage-array value");
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return result;
}

inline std::size_t ArrayBalance(std::string_view value) {
    bool quoted{};
    bool escaped{};
    std::int32_t balance{};
    for (const auto character : value) {
        if (character == '"' && !escaped) quoted = !quoted;
        if (!quoted && character == '[') ++balance;
        if (!quoted && character == ']') --balance;
        if (balance < 0) throw std::invalid_argument("unexpected closing array bracket");
        escaped = character == '\\' && !escaped;
        if (character != '\\') escaped = false;
    }
    return static_cast<std::size_t>(balance);
}

inline std::size_t FindAssignment(std::string_view line) {
    bool quoted{};
    bool escaped{};
    std::size_t result = std::string_view::npos;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == '"' && !escaped) quoted = !quoted;
        if (character == '=' && !quoted) {
            if (result != std::string_view::npos) {
                throw std::invalid_argument("an assignment must contain one equals sign");
            }
            result = index;
        }
        escaped = character == '\\' && !escaped;
        if (character != '\\') escaped = false;
    }
    return result;
}

class ConfigParser {
public:
    Config Parse(std::istream& input) {
        std::string pending;
        std::string line;
        std::size_t lineNumber{};
        while (std::getline(input, line)) {
            ++lineNumber;
            line = Trim(StripComment(std::move(line)));
            if (line.empty()) continue;
            try {
                if (!pending.empty()) {
                    pending += ' ';
                    pending += line;
                    if (ArrayBalance(pending) == 0) {
                        ParseLine(pending);
                        pending.clear();
                    }
                    continue;
                }
                if (line.front() != '[') {
                    const auto equal = FindAssignment(line);
                    if (equal == std::string::npos) throw std::invalid_argument("expected a section or assignment");
                    if (ArrayBalance(line.substr(equal + 1)) != 0) {
                        pending = line;
                        continue;
                    }
                }
                ParseLine(line);
            } catch (const std::exception& exception) {
                throw std::invalid_argument(
                    "line " + std::to_string(lineNumber) + ": " + exception.what());
            }
        }
        if (!pending.empty()) throw std::invalid_argument("unterminated multiline array");
        PrepareSimpleConfig();
        Validate();
        return std::move(config_);
    }

private:
    enum class Section {
        None,
        Plugin,
        Diagnostics,
        SimpleMagic,
        SimpleRareJewels,
        SimpleRegularRare,
        SimpleCrafted,
        MagicCategory,
        MagicStep,
        RareCategory,
        RareStep,
        CraftedCategory,
        CraftedStep,
    };

    enum class Format {
        None,
        Simple,
        Advanced,
    };

    struct SimpleMagicSettings {
        std::optional<std::int32_t> weaponsAndArmor;
        std::optional<std::int32_t> jewelsRingsAndAmulets;
        std::optional<std::int32_t> charms;
    };

    Config config_;
    Section section_{Section::None};
    Format format_{Format::None};
    bool pluginSeen_{};
    bool diagnosticsTableSeen_{};
    bool diagnosticsSettingSeen_{};
    std::unordered_set<std::string> seen_;
    std::unordered_set<std::string> simpleTables_;
    SimpleMagicSettings simpleMagic_;
    std::vector<WeightedStep> simpleRareJewels_;
    std::vector<WeightedStep> simpleRegularRare_;
    std::vector<WeightedStep> simpleCrafted_;

    void ParseLine(const std::string& line) {
        if (line.starts_with("[[") && line.ends_with("]]")) {
            StartArrayTable(line.substr(2, line.size() - 4));
            return;
        }
        if (line.front() == '[' && line.back() == ']') {
            const auto name = line.substr(1, line.size() - 2);
            StartTable(name);
            return;
        }
        const auto equal = FindAssignment(line);
        if (equal == std::string::npos) throw std::invalid_argument("expected an assignment");
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty()) throw std::invalid_argument("assignment key and value are required");
        ParseAssignment(key, value);
    }

    void UseFormat(Format format) {
        if (format_ != Format::None && format_ != format) {
            throw std::invalid_argument("simple and advanced configuration formats cannot be mixed");
        }
        format_ = format;
    }

    void StartTable(const std::string& name) {
        if (name == "plugin") {
            if (pluginSeen_) throw std::invalid_argument("unknown or duplicate table: " + name);
            pluginSeen_ = true;
            section_ = Section::Plugin;
            return;
        }
        if (name == "diagnostics") {
            if (diagnosticsTableSeen_) {
                throw std::invalid_argument(
                    "unknown or duplicate table: " + name);
            }
            diagnosticsTableSeen_ = true;
            section_ = Section::Diagnostics;
            return;
        }

        UseFormat(Format::Simple);
        if (!simpleTables_.insert(name).second) throw std::invalid_argument("duplicate table: " + name);
        if (name == "magic") section_ = Section::SimpleMagic;
        else if (name == "rare_jewels") section_ = Section::SimpleRareJewels;
        else if (name == "regular_rare_items") section_ = Section::SimpleRegularRare;
        else if (name == "crafted") section_ = Section::SimpleCrafted;
        else throw std::invalid_argument("unknown table: " + name);
    }

    void StartArrayTable(const std::string& name) {
        UseFormat(Format::Advanced);
        if (name == "magic.categories") {
            if (config_.magic.size() >= MaxCategories) throw std::invalid_argument("too many magic categories");
            config_.magic.emplace_back();
            section_ = Section::MagicCategory;
        } else if (name == "magic.categories.steps") {
            if (config_.magic.empty()) throw std::invalid_argument("magic step declared before its category");
            if (config_.magic.back().steps.size() >= MaxStepsPerCategory) throw std::invalid_argument("too many magic steps");
            config_.magic.back().steps.emplace_back();
            section_ = Section::MagicStep;
        } else if (name == "rare.categories") {
            if (config_.rare.size() >= MaxCategories) throw std::invalid_argument("too many rare categories");
            config_.rare.emplace_back();
            section_ = Section::RareCategory;
        } else if (name == "rare.categories.steps") {
            if (config_.rare.empty()) throw std::invalid_argument("rare step declared before its category");
            if (config_.rare.back().steps.size() >= MaxStepsPerCategory) throw std::invalid_argument("too many rare steps");
            config_.rare.back().steps.emplace_back();
            section_ = Section::RareStep;
        } else if (name == "crafted.categories") {
            if (config_.crafted.size() >= MaxCategories) throw std::invalid_argument("too many crafted categories");
            config_.crafted.emplace_back();
            section_ = Section::CraftedCategory;
        } else if (name == "crafted.categories.steps") {
            if (config_.crafted.empty()) throw std::invalid_argument("crafted step declared before its category");
            if (config_.crafted.back().steps.size() >= MaxStepsPerCategory) throw std::invalid_argument("too many crafted steps");
            config_.crafted.back().steps.emplace_back();
            section_ = Section::CraftedStep;
        } else {
            throw std::invalid_argument("unknown array table: " + name);
        }
    }

    void MarkSeen(const std::string& path) {
        if (!seen_.insert(path).second) throw std::invalid_argument("duplicate setting: " + path);
    }

    std::string CurrentPath(const std::string& key) const {
        switch (section_) {
        case Section::Plugin: return "plugin." + key;
        case Section::Diagnostics: return "diagnostics." + key;
        case Section::SimpleMagic: return "magic." + key;
        case Section::SimpleRareJewels: return "rare_jewels." + key;
        case Section::SimpleRegularRare: return "regular_rare_items." + key;
        case Section::SimpleCrafted: return "crafted." + key;
        case Section::MagicCategory: return "magic." + std::to_string(config_.magic.size() - 1) + "." + key;
        case Section::MagicStep: return "magic." + std::to_string(config_.magic.size() - 1) + ".step." + std::to_string(config_.magic.back().steps.size() - 1) + "." + key;
        case Section::RareCategory: return "rare." + std::to_string(config_.rare.size() - 1) + "." + key;
        case Section::RareStep: return "rare." + std::to_string(config_.rare.size() - 1) + ".step." + std::to_string(config_.rare.back().steps.size() - 1) + "." + key;
        case Section::CraftedCategory: return "crafted." + std::to_string(config_.crafted.size() - 1) + "." + key;
        case Section::CraftedStep: return "crafted." + std::to_string(config_.crafted.size() - 1) + ".step." + std::to_string(config_.crafted.back().steps.size() - 1) + "." + key;
        default: return key;
        }
    }

    static std::vector<ItemTypeCode> ParseTypes(const std::string& value) {
        const auto strings = ParseStringArray(value);
        if (strings.empty() || strings.size() > MaxTypesPerCategory) {
            throw std::invalid_argument("item_types must contain between one and sixteen codes");
        }
        std::vector<ItemTypeCode> result;
        for (const auto& text : strings) {
            const auto code = NormalizeItemTypeCode(text);
            if (std::any_of(result.begin(), result.end(), [&](const auto& existing) {
                    return SameCode(existing, code);
                })) {
                throw std::invalid_argument("duplicate item type code: " + text);
            }
            result.push_back(code);
        }
        if (std::any_of(result.begin(), result.end(), [](const auto& code) { return code.wildcard; })
                && (result.size() != 1 || !result.front().wildcard)) {
            throw std::invalid_argument("wildcard must be the only item type in its category");
        }
        return result;
    }

    void ParseAssignment(const std::string& key, const std::string& value) {
        MarkSeen(CurrentPath(key));
        switch (section_) {
        case Section::Plugin:
            if (key == "enabled") config_.enabled = ParseBoolean(value);
            else if (key == "diagnostics") {
                if (diagnosticsSettingSeen_) {
                    throw std::invalid_argument(
                        "duplicate diagnostics setting");
                }
                diagnosticsSettingSeen_ = true;
                config_.diagnostics = ParseBoolean(value);
            }
            else throw std::invalid_argument("unknown plugin setting: " + key);
            break;
        case Section::Diagnostics:
            if (key != "enabled") {
                throw std::invalid_argument(
                    "unknown diagnostics setting: " + key);
            }
            if (diagnosticsSettingSeen_) {
                throw std::invalid_argument("duplicate diagnostics setting");
            }
            diagnosticsSettingSeen_ = true;
            config_.diagnostics = ParseBoolean(value);
            break;
        case Section::SimpleMagic:
            if (key == "weapons_and_armor") simpleMagic_.weaponsAndArmor = ParseInteger(value);
            else if (key == "jewels_rings_and_amulets") simpleMagic_.jewelsRingsAndAmulets = ParseInteger(value);
            else if (key == "charms") simpleMagic_.charms = ParseInteger(value);
            else throw std::invalid_argument("unknown magic setting: " + key);
            break;
        case Section::SimpleRareJewels:
            ParseSimpleWeightedStep(simpleRareJewels_, "rare_jewels", key, value);
            break;
        case Section::SimpleRegularRare:
            ParseSimpleWeightedStep(simpleRegularRare_, "regular_rare_items", key, value);
            break;
        case Section::SimpleCrafted:
            ParseSimpleWeightedStep(simpleCrafted_, "crafted", key, value);
            break;
        case Section::MagicCategory:
            if (key == "name") config_.magic.back().name = ParseString(value);
            else if (key == "item_types") config_.magic.back().itemTypes = ParseTypes(value);
            else throw std::invalid_argument("unknown magic category setting: " + key);
            break;
        case Section::MagicStep:
            if (key == "minimum_item_level") config_.magic.back().steps.back().minimumItemLevel = ParseInteger(value);
            else if (key == "minimum_affixes") config_.magic.back().steps.back().minimumAffixes = ParseInteger(value);
            else throw std::invalid_argument("unknown magic step setting: " + key);
            break;
        case Section::RareCategory:
            ParseWeightedCategory(config_.rare.back(), "rare", key, value);
            break;
        case Section::RareStep:
            ParseWeightedStep(config_.rare.back().steps.back(), "rare", key, value);
            break;
        case Section::CraftedCategory:
            ParseWeightedCategory(config_.crafted.back(), "crafted", key, value);
            break;
        case Section::CraftedStep:
            ParseWeightedStep(config_.crafted.back().steps.back(), "crafted", key, value);
            break;
        default:
            throw std::invalid_argument("assignments must belong to a supported table");
        }
    }

    static void ParseSimpleWeightedStep(
            std::vector<WeightedStep>& steps,
            const char* table,
            const std::string& key,
            const std::string& value) {
        constexpr std::string_view prefix = "from_level_";
        if (!key.starts_with(prefix) || key.size() == prefix.size()) {
            throw std::invalid_argument(std::string("unknown ") + table + " setting: " + key);
        }
        if (steps.size() >= MaxStepsPerCategory) throw std::invalid_argument(std::string("too many ") + table + " steps");
        WeightedStep step{};
        step.minimumItemLevel = ParseInteger(std::string_view(key).substr(prefix.size()));
        step.weights = ParsePercentageArray(value);
        steps.push_back(std::move(step));
    }

    static void ParseWeightedCategory(
            WeightedCategory& category,
            const char* quality,
            const std::string& key,
            const std::string& value) {
        if (key == "name") category.name = ParseString(value);
        else if (key == "item_types") category.itemTypes = ParseTypes(value);
        else if (key == "counts") category.counts = ParseIntegerArray(value);
        else throw std::invalid_argument(std::string("unknown ") + quality + " category setting: " + key);
    }

    static void ParseWeightedStep(
            WeightedStep& step,
            const char* quality,
            const std::string& key,
            const std::string& value) {
        if (key == "minimum_item_level") step.minimumItemLevel = ParseInteger(value);
        else if (key == "weights") {
            const auto values = ParseIntegerArray(value);
            step.weights.clear();
            for (const auto entry : values) {
                if (entry < 0) throw std::invalid_argument("weights cannot be negative");
                step.weights.push_back(static_cast<std::uint32_t>(entry));
            }
        } else {
            throw std::invalid_argument(std::string("unknown ") + quality + " step setting: " + key);
        }
    }

    static bool IsWildcard(const auto& category) {
        return category.itemTypes.size() == 1 && category.itemTypes.front().wildcard;
    }

    static std::vector<ItemTypeCode> MakeTypes(std::initializer_list<std::string_view> types) {
        std::vector<ItemTypeCode> result;
        result.reserve(types.size());
        for (const auto type : types) result.push_back(NormalizeItemTypeCode(type));
        return result;
    }

    static WeightedCategory MakeSimpleWeightedCategory(
            std::string name,
            std::vector<ItemTypeCode> itemTypes,
            std::vector<std::int32_t> counts,
            std::vector<WeightedStep> steps) {
        if (steps.empty()) throw std::invalid_argument(name + " requires at least one from_level setting");
        std::sort(steps.begin(), steps.end(), [](const auto& left, const auto& right) {
            return left.minimumItemLevel < right.minimumItemLevel;
        });
        for (const auto& step : steps) {
            if (step.weights.size() != counts.size()) {
                throw std::invalid_argument(name + " percentages do not match the documented affix counts");
            }
            std::uint32_t total{};
            for (const auto percentage : step.weights) total += percentage;
            if (total != 10000) throw std::invalid_argument(name + " percentages must total 100");
        }
        return WeightedCategory{
            .name = std::move(name),
            .itemTypes = std::move(itemTypes),
            .counts = std::move(counts),
            .steps = std::move(steps),
        };
    }

    void PrepareSimpleConfig() {
        if (format_ != Format::Simple) return;
        for (const auto* table : {"magic", "rare_jewels", "regular_rare_items", "crafted"}) {
            if (!simpleTables_.contains(table)) throw std::invalid_argument(std::string("missing required table: ") + table);
        }
        if (!simpleMagic_.weaponsAndArmor
                || !simpleMagic_.jewelsRingsAndAmulets
                || !simpleMagic_.charms) {
            throw std::invalid_argument("magic requires weapons_and_armor, jewels_rings_and_amulets, and charms");
        }

        config_.magic = {
            MagicCategory{"weapons_and_armor", MakeTypes({"weap", "armo"}), {{*simpleMagic_.weaponsAndArmor, 2}}},
            MagicCategory{"jewels_rings_and_amulets", MakeTypes({"jewl", "ring", "amul"}), {{*simpleMagic_.jewelsRingsAndAmulets, 2}}},
            MagicCategory{"charms", MakeTypes({"char"}), {{*simpleMagic_.charms, 2}}},
            MagicCategory{"all_other_items", MakeTypes({"*"}), {{1, 1}}},
        };
        config_.rare.push_back(MakeSimpleWeightedCategory(
            "rare_jewels", MakeTypes({"jewl"}), {3, 4}, std::move(simpleRareJewels_)));
        config_.rare.push_back(MakeSimpleWeightedCategory(
            "all_other_items", MakeTypes({"*"}), {3, 4, 5, 6}, std::move(simpleRegularRare_)));
        config_.crafted.push_back(MakeSimpleWeightedCategory(
            "all_items", MakeTypes({"*"}), {1, 2, 3, 4}, std::move(simpleCrafted_)));
    }

    template <typename Category>
    static void ValidateCommonCategories(
            const std::vector<Category>& categories,
            const char* quality) {
        std::unordered_set<std::string> names;
        bool sawWildcard{};
        for (std::size_t index = 0; index < categories.size(); ++index) {
            const auto& category = categories[index];
            if (category.name.empty()) throw std::invalid_argument(std::string(quality) + " category name is required");
            if (!names.insert(category.name).second) throw std::invalid_argument(std::string(quality) + " category names must be unique");
            if (category.itemTypes.empty()) throw std::invalid_argument(std::string(quality) + " category item_types is required");
            if (IsWildcard(category)) {
                if (index + 1 != categories.size()) throw std::invalid_argument(std::string(quality) + " wildcard category must be last");
                sawWildcard = true;
            }
        }
        if (!categories.empty() && !sawWildcard) {
            throw std::invalid_argument(std::string(quality) + " categories require a final wildcard fallback");
        }
    }

    static void ValidateMagic(const std::vector<MagicCategory>& categories) {
        ValidateCommonCategories(categories, "magic");
        for (const auto& category : categories) {
            if (category.steps.empty()) throw std::invalid_argument("each magic category requires at least one step");
            std::int32_t previous{};
            for (const auto& step : category.steps) {
                if (step.minimumItemLevel < 1 || step.minimumItemLevel > 255 || step.minimumItemLevel <= previous) {
                    throw std::invalid_argument("magic minimum_item_level values must increase within 1..255");
                }
                if (step.minimumAffixes < 1 || step.minimumAffixes > 2) {
                    throw std::invalid_argument("magic minimum_affixes must be 1 or 2");
                }
                previous = step.minimumItemLevel;
            }
        }
    }

    static void ValidateWeighted(
            const std::vector<WeightedCategory>& categories,
            const char* quality,
            std::int32_t minimumCount,
            std::int32_t maximumCount) {
        ValidateCommonCategories(categories, quality);
        for (const auto& category : categories) {
            if (category.counts.empty()) throw std::invalid_argument(std::string(quality) + " counts is required");
            std::unordered_set<std::int32_t> counts;
            for (const auto count : category.counts) {
                if (count < minimumCount || count > maximumCount) {
                    throw std::invalid_argument(std::string(quality) + " counts are outside the native range");
                }
                if (!counts.insert(count).second) throw std::invalid_argument(std::string(quality) + " counts must be unique");
            }
            if (category.steps.empty() || category.steps.front().minimumItemLevel != 1) {
                throw std::invalid_argument(std::string(quality) + " categories must start at item level 1");
            }
            std::int32_t previous{};
            for (const auto& step : category.steps) {
                if (step.minimumItemLevel < 1 || step.minimumItemLevel > 255 || step.minimumItemLevel <= previous) {
                    throw std::invalid_argument(std::string(quality) + " minimum_item_level values must increase within 1..255");
                }
                if (step.weights.size() != category.counts.size()) {
                    throw std::invalid_argument(std::string(quality) + " weights must match counts");
                }
                std::uint64_t total{};
                for (const auto weight : step.weights) total += weight;
                if (total == 0 || total > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                    throw std::invalid_argument(std::string(quality) + " weights require a positive 32-bit total");
                }
                previous = step.minimumItemLevel;
            }
        }
    }

    void Validate() const {
        ValidateMagic(config_.magic);
        ValidateWeighted(config_.rare, "rare", 3, 6);
        ValidateWeighted(config_.crafted, "crafted", 1, 4);
        if (config_.enabled && config_.magic.empty() && config_.rare.empty() && config_.crafted.empty()) {
            throw std::invalid_argument("an enabled configuration must define at least one quality category");
        }
    }
};

inline Config ParseConfig(std::istream& input) {
    return ConfigParser{}.Parse(input);
}

template <typename Step>
inline const Step* FindStep(const std::vector<Step>& steps, std::int32_t itemLevel) noexcept {
    const Step* selected{};
    for (const auto& step : steps) {
        if (step.minimumItemLevel > itemLevel) break;
        selected = &step;
    }
    return selected;
}

inline std::uint32_t TotalWeight(const WeightedStep& step) noexcept {
    std::uint32_t total{};
    for (const auto weight : step.weights) total += weight;
    return total;
}

inline std::int32_t PickWeightedCount(
        const WeightedCategory& category,
        const WeightedStep& step,
        std::uint32_t roll) noexcept {
    std::uint32_t cursor{};
    for (std::size_t index = 0; index < step.weights.size(); ++index) {
        cursor += step.weights[index];
        if (roll < cursor) return category.counts[index];
    }
    return category.counts.back();
}

} // namespace RuffnecKk::ProgressiveAffixes
