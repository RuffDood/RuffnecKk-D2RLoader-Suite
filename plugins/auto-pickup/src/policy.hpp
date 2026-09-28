#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace RuffnecKk::AutoPickup {

inline constexpr std::size_t MaximumInventoryCodes = 16;

enum class Family : std::uint8_t { Health, Mana, Rejuvenation, Unknown };

struct Item {
    std::string_view code;
    Family family;
    std::uint8_t tier;
};

inline constexpr std::array Items{
    Item{"hp1", Family::Health, 1}, Item{"hp2", Family::Health, 2},
    Item{"hp3", Family::Health, 3}, Item{"hp4", Family::Health, 4},
    Item{"hp5", Family::Health, 5}, Item{"mp1", Family::Mana, 1},
    Item{"mp2", Family::Mana, 2}, Item{"mp3", Family::Mana, 3},
    Item{"mp4", Family::Mana, 4}, Item{"mp5", Family::Mana, 5},
    Item{"rvs", Family::Rejuvenation, 1}, Item{"rvl", Family::Rejuvenation, 2},
};

constexpr auto PackItemCode(std::string_view code) noexcept -> std::uint32_t {
    std::uint32_t packed = 0x20202020U;
    for (std::size_t index = 0; index < 4 && index < code.size(); ++index) {
        packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[index]))
            << static_cast<std::uint32_t>(index * 8U);
    }
    return packed;
}

constexpr auto Classify(std::string_view code) noexcept -> Item {
    for (const auto& item : Items) if (item.code == code) return item;
    return {code, Family::Unknown, 0};
}

struct Policy {
    bool enabled{};
    std::array<bool, 6> tiers{};
    std::array<bool, 6> inventoryFallback{};
    std::array<std::uint8_t, 4> columns{};
    std::uint8_t columnCount{};

    constexpr auto Accepts(Item item) const noexcept -> bool {
        return enabled && item.family != Family::Unknown
            && item.tier < tiers.size() && tiers[item.tier];
    }
    constexpr auto AllowsInventory(Item item) const noexcept -> bool {
        return Accepts(item) && item.tier < inventoryFallback.size()
            && inventoryFallback[item.tier];
    }
};

struct BeltSlot { bool occupied{}; Family family{Family::Unknown}; };

struct RoutingToken {
    static constexpr std::uint32_t InvalidGuid = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t itemGuid{InvalidGuid};
    constexpr auto Matches(std::uint32_t actual) const noexcept -> bool {
        return itemGuid != InvalidGuid && itemGuid == actual;
    }
    constexpr void Reset() noexcept { itemGuid = InvalidGuid; }
};

constexpr auto ChooseBeltSlot(const Policy& policy, Item item,
    const std::array<BeltSlot, 16>& slots, std::uint8_t capacity) noexcept -> std::int8_t {
    if (!policy.Accepts(item) || capacity < 4 || capacity > slots.size() || capacity % 4 != 0) return -1;
    const auto rows = static_cast<std::uint8_t>(capacity / 4);
    for (std::uint8_t index = 0; index < policy.columnCount; ++index) {
        const auto column = policy.columns[index];
        if (column < 1 || column > 4) continue;
        const auto bottom = static_cast<std::uint8_t>(column - 1);
        if (!slots[bottom].occupied || slots[bottom].family != item.family) continue;
        for (std::uint8_t row = 1; row < rows; ++row) {
            const auto slot = static_cast<std::uint8_t>(bottom + row * 4);
            if (!slots[slot].occupied) return static_cast<std::int8_t>(slot);
        }
    }
    for (std::uint8_t index = 0; index < policy.columnCount; ++index) {
        const auto column = policy.columns[index];
        if (column >= 1 && column <= 4 && !slots[column - 1].occupied) {
            return static_cast<std::int8_t>(column - 1);
        }
    }
    return -1;
}

struct FamilyConfig {
    Policy policy{};
    std::array<std::uint8_t, 5> priority{};
    std::uint8_t priorityCount{};
};

struct Config {
    bool enabled{true};
    std::uint32_t distance{4};
    std::uint32_t interval{1};
    FamilyConfig health{}, mana{}, rejuvenation{};
    std::array<Family, 3> familyPriority{Family::Rejuvenation, Family::Health, Family::Mana};
    std::uint8_t familyPriorityCount{3};
    std::array<std::uint32_t, MaximumInventoryCodes> inventoryCodes{};
    std::uint8_t inventoryCodeCount{};
    bool diagnosticsEnabled{};
    bool logScans{};

    constexpr auto InventoryRank(std::uint32_t code) const noexcept -> std::uint8_t {
        for (std::uint8_t index = 0; index < inventoryCodeCount; ++index) {
            if (inventoryCodes[index] == code) return index;
        }
        return UINT8_MAX;
    }
};

inline auto Trim(std::string_view value) noexcept -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
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
inline auto SetError(std::string& error, std::size_t line, std::string_view message) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}
inline auto ParseBoolean(std::string_view value, bool& output) noexcept -> bool {
    if (value == "true") { output = true; return true; }
    if (value == "false") { output = false; return true; }
    return false;
}
inline auto ParseUnsigned(std::string_view value, std::uint32_t& output) noexcept -> bool {
    if (value.empty()) return false;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}
inline auto ParseStringArray(std::string_view value, std::vector<std::string>& output) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    output.clear();
    for (std::size_t position = 1, last = value.size() - 1; position < last;) {
        while (position < last && (value[position] == ' ' || value[position] == '\t')) ++position;
        if (position == last) break;
        if (value[position] != '"') return false;
        const auto close = value.find('"', position + 1);
        if (close == std::string_view::npos || close >= last) return false;
        output.emplace_back(value.substr(position + 1, close - position - 1));
        position = close + 1;
        while (position < last && (value[position] == ' ' || value[position] == '\t')) ++position;
        if (position == last) break;
        if (value[position] != ',') return false;
        ++position;
    }
    return true;
}
inline auto ParseIntegerArray(std::string_view value, std::vector<std::uint32_t>& output) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    output.clear();
    for (std::size_t position = 1, last = value.size() - 1; position < last;) {
        while (position < last && (value[position] == ' ' || value[position] == '\t')) ++position;
        if (position == last) break;
        const auto comma = value.find(',', position);
        const auto end = comma == std::string_view::npos || comma > last ? last : comma;
        std::uint32_t parsed{};
        if (!ParseUnsigned(Trim(value.substr(position, end - position)), parsed)) return false;
        output.push_back(parsed);
        position = end;
        if (position == last) break;
        ++position;
        if (position == last) return false;
    }
    return true;
}
inline auto FindItem(std::string_view code, Family family) noexcept -> const Item* {
    for (const auto& item : Items) if (item.code == code && item.family == family) return &item;
    return nullptr;
}
inline auto ParsePotionCodes(std::string_view value, Family family, FamilyConfig& output) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values) || values.size() > output.priority.size()) return false;
    output.policy.tiers.fill(false); output.priority.fill(0); output.priorityCount = 0;
    for (const auto& code : values) {
        const auto* item = FindItem(code, family);
        if (!item || output.policy.tiers[item->tier]) return false;
        output.policy.tiers[item->tier] = true;
        output.priority[output.priorityCount++] = item->tier;
    }
    return true;
}
inline auto ParseTierSet(std::string_view value, Family family, std::array<bool, 6>& output) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values)) return false;
    output.fill(false);
    for (const auto& code : values) {
        const auto* item = FindItem(code, family);
        if (!item || output[item->tier]) return false;
        output[item->tier] = true;
    }
    return true;
}
inline auto ParseColumns(std::string_view value, FamilyConfig& output) -> bool {
    std::vector<std::uint32_t> values;
    if (!ParseIntegerArray(value, values) || values.size() > 4) return false;
    output.policy.columns.fill(0); output.policy.columnCount = 0;
    std::array<bool, 4> seen{};
    for (const auto column : values) {
        if (column < 1 || column > 4 || seen[column - 1]) return false;
        seen[column - 1] = true;
        output.policy.columns[output.policy.columnCount++] = static_cast<std::uint8_t>(column);
    }
    return true;
}
inline auto ParseFamilyOrder(std::string_view value, Config& output) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values) || values.size() > output.familyPriority.size()) return false;
    output.familyPriority.fill(Family::Unknown); output.familyPriorityCount = 0;
    std::array<bool, 3> seen{};
    for (const auto& name : values) {
        Family family{Family::Unknown}; std::size_t index{};
        if (name == "health") family = Family::Health;
        else if (name == "mana") { family = Family::Mana; index = 1; }
        else if (name == "rejuvenation") { family = Family::Rejuvenation; index = 2; }
        else return false;
        if (seen[index]) return false;
        seen[index] = true; output.familyPriority[output.familyPriorityCount++] = family;
    }
    return true;
}
inline auto IsExactCode(std::string_view code) noexcept -> bool {
    if (code.empty() || code.size() > 4) return false;
    for (const char character : code) {
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))) return false;
    }
    return true;
}
inline auto ParseInventoryCodes(std::string_view value, Config& output) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values) || values.size() > MaximumInventoryCodes) return false;
    output.inventoryCodes.fill(0); output.inventoryCodeCount = 0;
    for (const auto& code : values) {
        if (!IsExactCode(code) || Classify(code).family != Family::Unknown) return false;
        const auto packed = PackItemCode(code);
        for (std::uint8_t index = 0; index < output.inventoryCodeCount; ++index) {
            if (output.inventoryCodes[index] == packed) return false;
        }
        output.inventoryCodes[output.inventoryCodeCount++] = packed;
    }
    return true;
}
inline auto FamilyIndex(Family family) noexcept -> std::size_t { return static_cast<std::size_t>(family); }

inline auto ParseConfig(std::string_view input, Config& output, std::string& error) -> bool {
    Config parsed{};
    enum class Section : std::uint8_t { Root, Health, Mana, Rejuvenation, Inventory, Advanced, Diagnostics, D2rl } section{Section::Root};
    bool rootEnabled{}, rootRange{}, rootOrder{}, inventorySection{}, inventoryCodes{}, advancedSection{}, interval{}, diagnosticsSection{}, diagnosticsEnabled{}, diagnosticsScans{}, d2rlSection{};
    struct FamilySeen { bool section{}, enabled{}, codes{}, columns{}, fallback{}; constexpr auto Complete() const noexcept -> bool { return section && enabled && codes && columns && fallback; } };
    std::array<FamilySeen, 3> seen{};
    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = Trim(WithoutComment(input.substr(start, end == std::string_view::npos ? input.size() - start : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']') return SetError(error, lineNumber, "invalid section header");
            const auto name = Trim(line.substr(1, line.size() - 2));
            if (name == "health_potions") section = Section::Health;
            else if (name == "mana_potions") section = Section::Mana;
            else if (name == "rejuvenation_potions") section = Section::Rejuvenation;
            else if (name == "inventory_items") { if (inventorySection) return SetError(error, lineNumber, "duplicate section"); inventorySection = true; section = Section::Inventory; continue; }
            else if (name == "advanced") { if (advancedSection) return SetError(error, lineNumber, "duplicate section"); advancedSection = true; section = Section::Advanced; continue; }
            else if (name == "diagnostics") { if (diagnosticsSection) return SetError(error, lineNumber, "duplicate section"); diagnosticsSection = true; section = Section::Diagnostics; continue; }
            else if (name == "d2rl") { if (d2rlSection) return SetError(error, lineNumber, "duplicate section"); d2rlSection = true; section = Section::D2rl; continue; }
            else return SetError(error, lineNumber, "unknown section");
            const auto family = section == Section::Health ? Family::Health : section == Section::Mana ? Family::Mana : Family::Rejuvenation;
            auto& familySeen = seen[FamilyIndex(family)];
            if (familySeen.section) return SetError(error, lineNumber, "duplicate section");
            familySeen.section = true; continue;
        }
        if (section == Section::D2rl) continue;
        const auto equal = line.find('=');
        if (equal == std::string_view::npos || line.find('=', equal + 1) != std::string_view::npos) return SetError(error, lineNumber, "expected one key/value assignment");
        const auto key = Trim(line.substr(0, equal)); const auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty()) return SetError(error, lineNumber, "invalid key/value assignment");
        bool valid{}; bool* duplicate{};
        if (section == Section::Root) {
            if (key == "enabled") { duplicate = &rootEnabled; valid = ParseBoolean(value, parsed.enabled); }
            else if (key == "pickup_range") { duplicate = &rootRange; valid = ParseUnsigned(value, parsed.distance) && parsed.distance >= 1 && parsed.distance <= 4; }
            else if (key == "pickup_family_order") { duplicate = &rootOrder; valid = ParseFamilyOrder(value, parsed); }
            else return SetError(error, lineNumber, "unknown root setting");
        } else if (section == Section::Inventory) {
            if (key == "codes") { duplicate = &inventoryCodes; valid = ParseInventoryCodes(value, parsed); }
            else return SetError(error, lineNumber, "unknown inventory setting");
        } else if (section == Section::Advanced) {
            if (key == "scan_every_player_actions") { duplicate = &interval; valid = ParseUnsigned(value, parsed.interval) && parsed.interval >= 1 && parsed.interval <= 25; }
            else return SetError(error, lineNumber, "unknown advanced setting");
        } else if (section == Section::Diagnostics) {
            if (key == "enabled") { duplicate = &diagnosticsEnabled; valid = ParseBoolean(value, parsed.diagnosticsEnabled); }
            else if (key == "log_scans") { duplicate = &diagnosticsScans; valid = ParseBoolean(value, parsed.logScans); }
            else return SetError(error, lineNumber, "unknown diagnostics setting");
        } else {
            const auto family = section == Section::Health ? Family::Health : section == Section::Mana ? Family::Mana : Family::Rejuvenation;
            auto& familyConfig = family == Family::Health ? parsed.health : family == Family::Mana ? parsed.mana : parsed.rejuvenation;
            auto& familySeen = seen[FamilyIndex(family)];
            if (key == "enabled") { duplicate = &familySeen.enabled; valid = ParseBoolean(value, familyConfig.policy.enabled); }
            else if (key == "potion_codes") { duplicate = &familySeen.codes; valid = ParsePotionCodes(value, family, familyConfig); }
            else if (key == "belt_columns") { duplicate = &familySeen.columns; valid = ParseColumns(value, familyConfig); }
            else if (key == "inventory_fallback_potion_codes") { duplicate = &familySeen.fallback; valid = ParseTierSet(value, family, familyConfig.policy.inventoryFallback); }
            else return SetError(error, lineNumber, "unknown potion setting");
        }
        if (duplicate && *duplicate) return SetError(error, lineNumber, "duplicate setting");
        if (duplicate) *duplicate = true;
        if (!valid) return SetError(error, lineNumber, "invalid setting value");
    }
    if (!(rootEnabled && rootRange && rootOrder && inventorySection && inventoryCodes && advancedSection && interval && diagnosticsSection && diagnosticsEnabled && diagnosticsScans && d2rlSection && seen[0].Complete() && seen[1].Complete() && seen[2].Complete())) { error = "one or more required settings are missing"; return false; }
    const std::array<const FamilyConfig*, 3> families{&parsed.health, &parsed.mana, &parsed.rejuvenation};
    for (const auto* family : families) for (std::size_t tier = 1; tier < family->policy.inventoryFallback.size(); ++tier) {
        if (family->policy.inventoryFallback[tier] && !family->policy.tiers[tier]) { error = "inventory fallback codes must also be listed in potion_codes"; return false; }
    }
    output = parsed; error.clear(); return true;
}

} // namespace RuffnecKk::AutoPickup
