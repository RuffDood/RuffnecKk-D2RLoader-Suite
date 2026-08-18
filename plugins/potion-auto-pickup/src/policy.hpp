#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace RuffnecKk::PotionAutoPickup {

enum class Family : std::uint8_t {
    Healing,
    Mana,
    Rejuvenation,
    Unknown,
};

struct Item {
    std::string_view code;
    Family family;
    std::uint8_t tier;
};

inline constexpr std::array Items{
    Item{"hp1", Family::Healing, 1},
    Item{"hp2", Family::Healing, 2},
    Item{"hp3", Family::Healing, 3},
    Item{"hp4", Family::Healing, 4},
    Item{"hp5", Family::Healing, 5},
    Item{"mp1", Family::Mana, 1},
    Item{"mp2", Family::Mana, 2},
    Item{"mp3", Family::Mana, 3},
    Item{"mp4", Family::Mana, 4},
    Item{"mp5", Family::Mana, 5},
    Item{"rvs", Family::Rejuvenation, 1},
    Item{"rvl", Family::Rejuvenation, 2},
};

constexpr auto PackItemCode(std::string_view code) noexcept -> std::uint32_t {
    std::uint32_t packed = 0x20202020U;
    for (std::size_t index = 0; index < 4 && index < code.size(); ++index) {
        const auto shift = static_cast<std::uint32_t>(index * 8U);
        packed = (packed & ~(0xFFU << shift))
            | (static_cast<std::uint32_t>(
                static_cast<std::uint8_t>(code[index])) << shift);
    }
    return packed;
}

constexpr auto Classify(std::string_view code) noexcept -> Item {
    for (const auto& item : Items) {
        if (item.code == code) return item;
    }
    return {code, Family::Unknown, 0};
}

struct Policy {
    bool enabled{};
    std::array<bool, 6> tiers{};
    std::array<std::uint8_t, 4> columns{};
    std::uint8_t columnCount{};
    std::array<bool, 6> overflowTiers{};

    constexpr auto Accepts(Item item) const noexcept -> bool {
        return enabled && item.family != Family::Unknown
            && item.tier < tiers.size() && tiers[item.tier];
    }

    constexpr auto AllowsOverflow(Item item) const noexcept -> bool {
        return Accepts(item) && item.tier < overflowTiers.size()
            && overflowTiers[item.tier];
    }
};

struct BeltSlot {
    bool occupied{};
    Family family{Family::Unknown};
};

struct RoutingToken {
    static constexpr std::uint32_t InvalidGuid =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t itemGuid{InvalidGuid};

    constexpr auto Matches(std::uint32_t actualGuid) const noexcept -> bool {
        return itemGuid != InvalidGuid && itemGuid == actualGuid;
    }

    constexpr void Reset() noexcept {
        itemGuid = InvalidGuid;
    }
};

constexpr auto ChooseBeltSlot(
    const Policy& policy,
    Item item,
    const std::array<BeltSlot, 16>& slots,
    std::uint8_t capacity
) noexcept -> std::int8_t {
    if (!policy.Accepts(item) || capacity < 4 || capacity > slots.size()
        || capacity % 4 != 0) {
        return -1;
    }
    const auto rows = static_cast<std::uint8_t>(capacity / 4);
    for (std::uint8_t index = 0; index < policy.columnCount; ++index) {
        const auto column = policy.columns[index];
        if (column < 1 || column > 4) continue;
        const auto bottom = static_cast<std::uint8_t>(column - 1);
        if (!slots[bottom].occupied || slots[bottom].family != item.family) {
            continue;
        }
        for (std::uint8_t row = 1; row < rows; ++row) {
            const auto slot = static_cast<std::uint8_t>(bottom + row * 4);
            if (!slots[slot].occupied) return static_cast<std::int8_t>(slot);
        }
    }
    for (std::uint8_t index = 0; index < policy.columnCount; ++index) {
        const auto column = policy.columns[index];
        if (column < 1 || column > 4) continue;
        const auto bottom = static_cast<std::uint8_t>(column - 1);
        if (!slots[bottom].occupied) return static_cast<std::int8_t>(bottom);
    }
    return -1;
}

struct FamilyConfig {
    Policy policy{};
    bool legacyOverflow{};
    bool explicitOverflowTiers{};
    std::array<std::uint8_t, 5> tierPriority{};
    std::uint8_t tierPriorityCount{};
};

enum class ConfigSchema : std::uint8_t {
    PlayerFriendly,
    Legacy,
};

struct Config {
    ConfigSchema schema{ConfigSchema::PlayerFriendly};
    bool enabled{true};
    std::uint32_t distance{4};
    std::uint32_t interval{3};
    FamilyConfig healing{};
    FamilyConfig mana{};
    FamilyConfig rejuvenation{};
    std::array<Family, 3> familyPriority{
        Family::Rejuvenation, Family::Healing, Family::Mana};
    std::uint8_t familyPriorityCount{3};
    bool diagnosticsEnabled{};
    bool logScans{};
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

inline auto SetError(
    std::string& error,
    std::size_t line,
    std::string_view message
) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
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

inline auto ParseUnsigned(
    std::string_view value,
    std::uint32_t& output
) noexcept -> bool {
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

inline auto ParseStringArray(
    std::string_view value,
    std::vector<std::string>& output
) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    output.clear();
    std::size_t position = 1;
    const auto last = value.size() - 1;
    while (position < last) {
        while (position < last && (value[position] == ' '
                || value[position] == '\t')) {
            ++position;
        }
        if (position == last) break;
        if (value[position] != '"') return false;
        const auto close = value.find('"', position + 1);
        if (close == std::string_view::npos || close >= last) return false;
        output.emplace_back(value.substr(position + 1, close - position - 1));
        position = close + 1;
        while (position < last && (value[position] == ' '
                || value[position] == '\t')) {
            ++position;
        }
        if (position == last) break;
        if (value[position] != ',') return false;
        ++position;
        while (position < last && (value[position] == ' '
                || value[position] == '\t')) {
            ++position;
        }
        if (position == last) return false;
    }
    return true;
}

inline auto ParseIntegerArray(
    std::string_view value,
    std::vector<std::uint32_t>& output
) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    output.clear();
    std::size_t position = 1;
    const auto last = value.size() - 1;
    while (position < last) {
        while (position < last && (value[position] == ' '
                || value[position] == '\t')) {
            ++position;
        }
        if (position == last) break;
        const auto comma = value.find(',', position);
        const auto end = comma == std::string_view::npos || comma > last
            ? last : comma;
        const auto token = Trim(value.substr(position, end - position));
        std::uint32_t parsed{};
        if (!ParseUnsigned(token, parsed)) return false;
        output.push_back(parsed);
        position = end;
        if (position == last) break;
        ++position;
        if (position == last) return false;
    }
    return true;
}

inline auto FindItem(std::string_view code, Family family) noexcept
    -> const Item* {
    for (const auto& item : Items) {
        if (item.family == family && item.code == code) return &item;
    }
    return nullptr;
}

inline auto ParseTierSet(
    std::string_view value,
    Family family,
    std::array<bool, 6>& output
) -> bool {
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

inline auto ParseColumns(
    std::string_view value,
    FamilyConfig& output
) -> bool {
    std::vector<std::uint32_t> values;
    if (!ParseIntegerArray(value, values) || values.size() > 4) return false;
    output.policy.columns.fill(0);
    output.policy.columnCount = 0;
    std::array<bool, 4> seen{};
    for (const auto column : values) {
        if (column < 1 || column > 4 || seen[column - 1]) return false;
        seen[column - 1] = true;
        output.policy.columns[output.policy.columnCount++] =
            static_cast<std::uint8_t>(column);
    }
    return true;
}

inline auto ParseTierPriority(
    std::string_view value,
    Family family,
    FamilyConfig& output
) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values)
        || values.size() > output.tierPriority.size()) {
        return false;
    }
    output.tierPriority.fill(0);
    output.tierPriorityCount = 0;
    std::array<bool, 6> seen{};
    for (const auto& code : values) {
        const auto* item = FindItem(code, family);
        if (!item || seen[item->tier]) return false;
        seen[item->tier] = true;
        output.tierPriority[output.tierPriorityCount++] = item->tier;
    }
    return true;
}

inline auto ParsePotionCodes(
    std::string_view value,
    Family family,
    FamilyConfig& output
) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values)
        || values.size() > output.tierPriority.size()) {
        return false;
    }
    output.policy.tiers.fill(false);
    output.tierPriority.fill(0);
    output.tierPriorityCount = 0;
    for (const auto& code : values) {
        const auto* item = FindItem(code, family);
        if (!item || output.policy.tiers[item->tier]) return false;
        output.policy.tiers[item->tier] = true;
        output.tierPriority[output.tierPriorityCount++] = item->tier;
    }
    return true;
}

inline auto ParseFamilyPriority(
    std::string_view value,
    Config& output
) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values)
        || values.size() > output.familyPriority.size()) {
        return false;
    }
    output.familyPriority.fill(Family::Unknown);
    output.familyPriorityCount = 0;
    std::array<bool, 3> seen{};
    for (const auto& name : values) {
        Family family{Family::Unknown};
        std::size_t index{};
        if (name == "healing") {
            family = Family::Healing;
        } else if (name == "mana") {
            family = Family::Mana;
            index = 1;
        } else if (name == "rejuvenation") {
            family = Family::Rejuvenation;
            index = 2;
        } else {
            return false;
        }
        if (seen[index]) return false;
        seen[index] = true;
        output.familyPriority[output.familyPriorityCount++] = family;
    }
    return true;
}

inline auto ParsePlayerFamilyOrder(
    std::string_view value,
    Config& output
) -> bool {
    std::vector<std::string> values;
    if (!ParseStringArray(value, values)
        || values.size() > output.familyPriority.size()) {
        return false;
    }
    output.familyPriority.fill(Family::Unknown);
    output.familyPriorityCount = 0;
    std::array<bool, 3> seen{};
    for (const auto& name : values) {
        Family family{Family::Unknown};
        std::size_t index{};
        if (name == "health") {
            family = Family::Healing;
        } else if (name == "mana") {
            family = Family::Mana;
            index = 1;
        } else if (name == "rejuvenation") {
            family = Family::Rejuvenation;
            index = 2;
        } else {
            return false;
        }
        if (seen[index]) return false;
        seen[index] = true;
        output.familyPriority[output.familyPriorityCount++] = family;
    }
    return true;
}

inline auto FamilyIndex(Family family) noexcept -> std::size_t {
    return static_cast<std::size_t>(family);
}

inline void SetDefaults(Config& output) noexcept {
    output = {};
    output.enabled = true;
    output.distance = 4;
    output.interval = 3;
    output.familyPriority = {
        Family::Rejuvenation, Family::Healing, Family::Mana};
    output.familyPriorityCount = 3;

    output.healing.policy.enabled = true;
    output.healing.policy.tiers[4] = true;
    output.healing.policy.tiers[5] = true;
    output.healing.policy.columns[0] = 1;
    output.healing.policy.columnCount = 1;
    output.healing.tierPriority[0] = 5;
    output.healing.tierPriority[1] = 4;
    output.healing.tierPriorityCount = 2;

    output.mana.policy.enabled = true;
    output.mana.policy.tiers[4] = true;
    output.mana.policy.tiers[5] = true;
    output.mana.policy.columns[0] = 2;
    output.mana.policy.columnCount = 1;
    output.mana.tierPriority[0] = 5;
    output.mana.tierPriority[1] = 4;
    output.mana.tierPriorityCount = 2;

    output.rejuvenation.policy.enabled = true;
    output.rejuvenation.policy.tiers[1] = true;
    output.rejuvenation.policy.tiers[2] = true;
    output.rejuvenation.policy.columns[0] = 3;
    output.rejuvenation.policy.columns[1] = 4;
    output.rejuvenation.policy.columnCount = 2;
    output.rejuvenation.legacyOverflow = true;
    output.rejuvenation.tierPriority[0] = 2;
    output.rejuvenation.tierPriority[1] = 1;
    output.rejuvenation.tierPriorityCount = 2;
}

struct FamilySeen {
    bool section{};
    bool enabled{};
    bool tiers{};
    bool columns{};
    bool legacyOverflow{};
    bool overflowTiers{};
    bool tierPriority{};

    constexpr auto Complete() const noexcept -> bool {
        return section && enabled && tiers && columns && legacyOverflow
            && overflowTiers && tierPriority;
    }
};

struct PlayerFamilySeen {
    bool section{};
    bool enabled{};
    bool potionCodes{};
    bool beltColumns{};
    bool inventoryFallbackPotionCodes{};

    constexpr auto Complete() const noexcept -> bool {
        return section && enabled && potionCodes && beltColumns
            && inventoryFallbackPotionCodes;
    }
};

inline auto ParseConfig(
    std::string_view input,
    Config& output,
    std::string& error
) -> bool {
    Config parsed{};
    SetDefaults(parsed);
    enum class Section : std::uint8_t {
        Root,
        LegacyHealing,
        LegacyMana,
        LegacyRejuvenation,
        HealthPotions,
        ManaPotions,
        RejuvenationPotions,
        Advanced,
        Diagnostics,
    } section{Section::Root};

    bool rootEnabled{};
    bool legacyRootDistance{};
    bool legacyRootInterval{};
    bool legacyRootPriority{};
    bool playerRootRange{};
    bool playerRootFamilyOrder{};
    bool advancedSection{};
    bool advancedInterval{};
    bool diagnosticsSection{};
    bool diagnosticsEnabled{};
    bool diagnosticsScans{};
    std::array<FamilySeen, 3> familySeen{};
    std::array<PlayerFamilySeen, 3> playerFamilySeen{};
    bool sawLegacySchema{};
    bool sawPlayerSchema{};

    const auto markSchema = [&](bool player, std::size_t line) -> bool {
        if ((player && sawLegacySchema) || (!player && sawPlayerSchema)) {
            return SetError(error, line,
                "legacy and player-friendly settings cannot be mixed");
        }
        if (player) {
            sawPlayerSchema = true;
        } else {
            sawLegacySchema = true;
        }
        return true;
    };

    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = Trim(WithoutComment(input.substr(
            start,
            end == std::string_view::npos
                ? input.size() - start : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']'
                || line.find('[', 1) != std::string_view::npos
                || line.find(']') != line.size() - 1) {
                return SetError(error, lineNumber, "invalid section header");
            }
            const auto name = Trim(line.substr(1, line.size() - 2));
            Family family{Family::Unknown};
            if (name == "healing") {
                if (!markSchema(false, lineNumber)) return false;
                section = Section::LegacyHealing;
                family = Family::Healing;
            } else if (name == "mana") {
                if (!markSchema(false, lineNumber)) return false;
                section = Section::LegacyMana;
                family = Family::Mana;
            } else if (name == "rejuvenation") {
                if (!markSchema(false, lineNumber)) return false;
                section = Section::LegacyRejuvenation;
                family = Family::Rejuvenation;
            } else if (name == "health_potions") {
                if (!markSchema(true, lineNumber)) return false;
                section = Section::HealthPotions;
                family = Family::Healing;
            } else if (name == "mana_potions") {
                if (!markSchema(true, lineNumber)) return false;
                section = Section::ManaPotions;
                family = Family::Mana;
            } else if (name == "rejuvenation_potions") {
                if (!markSchema(true, lineNumber)) return false;
                section = Section::RejuvenationPotions;
                family = Family::Rejuvenation;
            } else if (name == "advanced") {
                if (!markSchema(true, lineNumber)) return false;
                if (advancedSection) {
                    return SetError(error, lineNumber, "duplicate section");
                }
                advancedSection = true;
                section = Section::Advanced;
                continue;
            } else if (name == "diagnostics") {
                if (diagnosticsSection) {
                    return SetError(error, lineNumber, "duplicate section");
                }
                diagnosticsSection = true;
                section = Section::Diagnostics;
                continue;
            } else {
                return SetError(error, lineNumber, "unknown section");
            }
            if (sawPlayerSchema) {
                auto& seen = playerFamilySeen[FamilyIndex(family)];
                if (seen.section) {
                    return SetError(error, lineNumber, "duplicate section");
                }
                seen.section = true;
            } else {
                auto& seen = familySeen[FamilyIndex(family)];
                if (seen.section) {
                    return SetError(error, lineNumber, "duplicate section");
                }
                seen.section = true;
            }
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string_view::npos
            || line.find('=', equal + 1) != std::string_view::npos) {
            return SetError(error, lineNumber,
                "expected one key/value assignment");
        }
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty()) {
            return SetError(error, lineNumber, "invalid key/value assignment");
        }

        bool valid{};
        bool* duplicate{};
        if (section == Section::Root) {
            if (key == "enabled") {
                duplicate = &rootEnabled;
                valid = ParseBoolean(value, parsed.enabled);
            } else if (key == "pickup_distance") {
                if (!markSchema(false, lineNumber)) return false;
                duplicate = &legacyRootDistance;
                valid = ParseUnsigned(value, parsed.distance)
                    && parsed.distance >= 1 && parsed.distance <= 4;
            } else if (key == "minimum_interval_actions") {
                if (!markSchema(false, lineNumber)) return false;
                duplicate = &legacyRootInterval;
                valid = ParseUnsigned(value, parsed.interval)
                    && parsed.interval >= 1 && parsed.interval <= 25;
            } else if (key == "family_priority") {
                if (!markSchema(false, lineNumber)) return false;
                duplicate = &legacyRootPriority;
                valid = ParseFamilyPriority(value, parsed);
            } else if (key == "pickup_range") {
                if (!markSchema(true, lineNumber)) return false;
                duplicate = &playerRootRange;
                valid = ParseUnsigned(value, parsed.distance)
                    && parsed.distance >= 1 && parsed.distance <= 4;
            } else if (key == "pickup_family_order") {
                if (!markSchema(true, lineNumber)) return false;
                duplicate = &playerRootFamilyOrder;
                valid = ParsePlayerFamilyOrder(value, parsed);
            } else {
                return SetError(error, lineNumber, "unknown root setting");
            }
        } else if (section == Section::Advanced) {
            if (key == "scan_every_player_actions") {
                duplicate = &advancedInterval;
                valid = ParseUnsigned(value, parsed.interval)
                    && parsed.interval >= 1 && parsed.interval <= 25;
            } else {
                return SetError(error, lineNumber,
                    "unknown advanced setting");
            }
        } else if (section == Section::Diagnostics) {
            if (key == "enabled") {
                duplicate = &diagnosticsEnabled;
                valid = ParseBoolean(value, parsed.diagnosticsEnabled);
            } else if (key == "log_scans") {
                duplicate = &diagnosticsScans;
                valid = ParseBoolean(value, parsed.logScans);
            } else {
                return SetError(error, lineNumber,
                    "unknown diagnostics setting");
            }
        } else {
            const bool player = section == Section::HealthPotions
                || section == Section::ManaPotions
                || section == Section::RejuvenationPotions;
            const auto family = section == Section::LegacyHealing
                || section == Section::HealthPotions
                ? Family::Healing
                : section == Section::LegacyMana
                    || section == Section::ManaPotions ? Family::Mana
                : Family::Rejuvenation;
            auto& config = family == Family::Healing
                ? parsed.healing
                : family == Family::Mana ? parsed.mana
                : parsed.rejuvenation;
            if (player) {
                auto& seen = playerFamilySeen[FamilyIndex(family)];
                if (key == "enabled") {
                    duplicate = &seen.enabled;
                    valid = ParseBoolean(value, config.policy.enabled);
                } else if (key == "potion_codes") {
                    duplicate = &seen.potionCodes;
                    valid = ParsePotionCodes(value, family, config);
                } else if (key == "belt_columns") {
                    duplicate = &seen.beltColumns;
                    valid = ParseColumns(value, config);
                } else if (key == "inventory_fallback_potion_codes") {
                    duplicate = &seen.inventoryFallbackPotionCodes;
                    valid = ParseTierSet(
                        value, family, config.policy.overflowTiers);
                    config.explicitOverflowTiers = valid;
                } else {
                    return SetError(error, lineNumber,
                        "unknown potion-family setting");
                }
            } else {
                auto& seen = familySeen[FamilyIndex(family)];
                if (key == "enabled") {
                    duplicate = &seen.enabled;
                    valid = ParseBoolean(value, config.policy.enabled);
                } else if (key == "tiers") {
                    duplicate = &seen.tiers;
                    valid = ParseTierSet(value, family, config.policy.tiers);
                } else if (key == "columns") {
                    duplicate = &seen.columns;
                    valid = ParseColumns(value, config);
                } else if (key == "overflow_to_inventory") {
                    duplicate = &seen.legacyOverflow;
                    valid = ParseBoolean(value, config.legacyOverflow);
                } else if (key == "overflow_tiers") {
                    duplicate = &seen.overflowTiers;
                    valid = ParseTierSet(
                        value, family, config.policy.overflowTiers);
                    config.explicitOverflowTiers = valid;
                } else if (key == "tier_priority") {
                    duplicate = &seen.tierPriority;
                    valid = ParseTierPriority(value, family, config);
                } else {
                    return SetError(error, lineNumber,
                        "unknown family setting");
                }
            }
        }
        if (duplicate && *duplicate) {
            return SetError(error, lineNumber, "duplicate setting");
        }
        if (duplicate) *duplicate = true;
        if (!valid) return SetError(error, lineNumber, "invalid setting value");
    }

    const bool commonComplete = rootEnabled && diagnosticsSection
        && diagnosticsEnabled && diagnosticsScans;
    const bool legacyComplete = sawLegacySchema && commonComplete
        && legacyRootDistance && legacyRootInterval && legacyRootPriority
        && familySeen[0].Complete() && familySeen[1].Complete()
        && familySeen[2].Complete();
    const bool playerComplete = sawPlayerSchema && commonComplete
        && playerRootRange && playerRootFamilyOrder
        && advancedSection && advancedInterval
        && playerFamilySeen[0].Complete()
        && playerFamilySeen[1].Complete()
        && playerFamilySeen[2].Complete();
    if (!(legacyComplete || playerComplete)) {
        error = "one or more required settings are missing";
        return false;
    }
    if (playerComplete) {
        const std::array<const FamilyConfig*, 3> families{
            &parsed.healing, &parsed.mana, &parsed.rejuvenation};
        for (const auto* family : families) {
            for (std::size_t tier = 1;
                tier < family->policy.overflowTiers.size(); ++tier) {
                if (family->policy.overflowTiers[tier]
                    && !family->policy.tiers[tier]) {
                    error = "inventory fallback potion codes must also be listed in potion_codes";
                    return false;
                }
            }
        }
    }
    parsed.schema = legacyComplete
        ? ConfigSchema::Legacy : ConfigSchema::PlayerFriendly;
    output = parsed;
    error.clear();
    return true;
}

} // namespace RuffnecKk::PotionAutoPickup
