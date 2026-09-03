#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneckk::resistance_floor {

inline constexpr std::int32_t VanillaFloor = -100;
inline constexpr std::int32_t MinimumFloor = -1000;
inline constexpr std::int32_t PhysicalResistanceStat = 36;
inline constexpr std::int32_t MagicResistanceStat = 37;
inline constexpr std::int32_t FireResistanceStat = 39;
inline constexpr std::int32_t LightningResistanceStat = 41;
inline constexpr std::int32_t ColdResistanceStat = 43;
inline constexpr std::int32_t PoisonResistanceStat = 45;

enum class UnitClass : std::uint8_t {
    Unknown,
    Player,
    PlayerOwned,
    Monster,
};

struct TargetConfig {
    bool enabled{};
    std::int32_t floor{VanillaFloor};
};

struct DisplayConfig {
    bool syncCharacterScreen{true};
};

struct Config {
    std::uint32_t schemaVersion{3};
    bool enabled{true};
    TargetConfig players{true, MinimumFloor};
    TargetConfig playerOwnedUnits{true, MinimumFloor};
    TargetConfig monsters{false, MinimumFloor};
    DisplayConfig display{};
    bool diagnostics{};
};

constexpr auto IsSupportedResistanceStat(std::int32_t statId) noexcept -> bool {
    return statId == PhysicalResistanceStat
        || statId == MagicResistanceStat
        || statId == FireResistanceStat
        || statId == LightningResistanceStat
        || statId == ColdResistanceStat
        || statId == PoisonResistanceStat;
}

constexpr auto SelectConfiguredFloor(
        const Config& config,
        UnitClass unitClass,
        std::int32_t statId) noexcept -> std::int32_t {
    if (!config.enabled || !IsSupportedResistanceStat(statId)) {
        return VanillaFloor;
    }
    const TargetConfig* target{};
    switch (unitClass) {
    case UnitClass::Player:
        target = &config.players;
        break;
    case UnitClass::PlayerOwned:
        target = &config.playerOwnedUnits;
        break;
    case UnitClass::Monster:
        target = &config.monsters;
        break;
    default:
        return VanillaFloor;
    }
    return target->enabled ? target->floor : VanillaFloor;
}

constexpr auto CanEncodeRel32(
        std::uintptr_t instructionAddress,
        std::uintptr_t targetAddress) noexcept -> bool {
    if (instructionAddress > std::numeric_limits<std::uintptr_t>::max() - 5U) {
        return false;
    }
    const auto next = instructionAddress + 5U;
    if (targetAddress >= next) {
        return targetAddress - next
            <= static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max());
    }
    return next - targetAddress
        <= static_cast<std::uintptr_t>(
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
            + 1ULL);
}

inline auto BuildConfigCandidates(
        const std::filesystem::path& activeModConfigDirectory,
        const std::filesystem::path& scopeConfigDirectory,
        const std::filesystem::path& globalConfigDirectory,
        const std::filesystem::path& fileName)
        -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> candidates;
    const auto append = [&](const std::filesystem::path& directory) {
        if (directory.empty()) return;
        const auto candidate = (directory / fileName).lexically_normal();
        if (std::find(candidates.begin(), candidates.end(), candidate)
                == candidates.end()) {
            candidates.emplace_back(candidate);
        }
    };
    append(activeModConfigDirectory);
    append(scopeConfigDirectory);
    append(globalConfigDirectory);
    return candidates;
}

inline auto IsKnownKey(
        std::string_view key,
        std::initializer_list<std::string_view> allowed) noexcept -> bool {
    return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

inline auto RequireTable(
        const toml::table& parent,
        std::string_view key,
        std::string& error) -> const toml::table* {
    const auto* node = parent.get(key);
    if (!node) {
        error = "missing [" + std::string(key) + "] section";
        return nullptr;
    }
    const auto* table = node->as_table();
    if (!table) {
        error = std::string(key) + " must be a TOML table";
        return nullptr;
    }
    return table;
}

inline auto ValidateKeys(
        const toml::table& table,
        std::string_view prefix,
        std::initializer_list<std::string_view> allowed,
        std::string& error) -> bool {
    for (const auto& [key, value] : table) {
        (void)value;
        if (!IsKnownKey(key.str(), allowed)) {
            error = "unknown setting: " + std::string(prefix)
                + std::string(key.str());
            return false;
        }
    }
    return true;
}

inline auto ReadRequiredBool(
        const toml::table& table,
        std::string_view key,
        std::string_view qualifiedName,
        bool& destination,
        std::string& error) -> bool {
    const auto* node = table.get(key);
    if (!node) {
        error = "missing setting: " + std::string(qualifiedName);
        return false;
    }
    const auto* value = node->as_boolean();
    if (!value) {
        error = std::string(qualifiedName) + " must be true or false";
        return false;
    }
    destination = value->get();
    return true;
}

inline auto ReadRequiredInt(
        const toml::table& table,
        std::string_view key,
        std::string_view qualifiedName,
        std::int64_t minimum,
        std::int64_t maximum,
        std::int32_t& destination,
        std::string& error) -> bool {
    const auto* node = table.get(key);
    if (!node) {
        error = "missing setting: " + std::string(qualifiedName);
        return false;
    }
    const auto* value = node->as_integer();
    if (!value || value->get() < minimum || value->get() > maximum) {
        error = std::string(qualifiedName) + " must be an integer from "
            + std::to_string(minimum) + " to " + std::to_string(maximum);
        return false;
    }
    destination = static_cast<std::int32_t>(value->get());
    return true;
}

inline auto ParseTarget(
        const toml::table& table,
        std::string_view name,
        TargetConfig& destination,
        std::string& error) -> bool {
    if (!ValidateKeys(
            table,
            std::string(name) + ".",
            {"enabled", "minimum_resistance"},
            error)) {
        return false;
    }
    return ReadRequiredBool(
               table, "enabled", std::string(name) + ".enabled",
               destination.enabled, error)
        && ReadRequiredInt(
               table,
               "minimum_resistance",
               std::string(name) + ".minimum_resistance",
               MinimumFloor, VanillaFloor, destination.floor, error);
}

inline auto ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) -> bool {
    try {
        const auto root = toml::parse(input);
        if (!ValidateKeys(
                root, {},
                {"config_version", "enabled", "players", "companions",
                 "monsters", "character_screen", "troubleshooting"},
                error)) {
            return false;
        }

        Config parsed{};
        const auto* schemaNode = root.get("config_version");
        const auto* schema = schemaNode ? schemaNode->as_integer() : nullptr;
        if (!schema || schema->get() != 3) {
            error = "config_version must be integer 3";
            return false;
        }
        parsed.schemaVersion = 3;
        if (!ReadRequiredBool(root, "enabled", "enabled", parsed.enabled, error)) {
            return false;
        }

        const auto* players = RequireTable(root, "players", error);
        const auto* playerOwned = RequireTable(root, "companions", error);
        const auto* monsters = RequireTable(root, "monsters", error);
        if (!players || !playerOwned || !monsters
                || !ParseTarget(
                    *players, "players", parsed.players, error)
                || !ParseTarget(
                    *playerOwned, "companions",
                    parsed.playerOwnedUnits, error)
                || !ParseTarget(
                    *monsters, "monsters", parsed.monsters, error)) {
            return false;
        }

        const auto* display = RequireTable(root, "character_screen", error);
        if (!display
                || !ValidateKeys(
                    *display, "character_screen.",
                    {"show_resistances_below_minus_100"}, error)
                || !ReadRequiredBool(
                    *display, "show_resistances_below_minus_100",
                    "character_screen.show_resistances_below_minus_100",
                    parsed.display.syncCharacterScreen, error)) {
            return false;
        }

        const auto* diagnostics = RequireTable(
            root, "troubleshooting", error);
        if (!diagnostics
                || !ValidateKeys(
                    *diagnostics, "troubleshooting.",
                    {"show_usage_counters"}, error)
                || !ReadRequiredBool(
                    *diagnostics, "show_usage_counters",
                    "troubleshooting.show_usage_counters",
                    parsed.diagnostics, error)) {
            return false;
        }

        result = parsed;
        error.clear();
        return true;
    } catch (const toml::parse_error& exception) {
        error = exception.description();
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace ruffneckk::resistance_floor
