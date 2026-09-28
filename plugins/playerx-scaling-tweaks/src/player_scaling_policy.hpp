#pragma once

#include <toml++/toml.hpp>
#include "player_hotkey.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneckk::player_scaling {

inline constexpr std::int32_t MinimumPlayerCount = 1;
inline constexpr std::int32_t MaximumPlayerCount = 65535;

struct ScalingChannel {
    bool enabled{true};
    std::int32_t maximumPlayers{};
};

struct NoDropConfig {
    bool playersCommandSimulatesNearbyParty{};
    std::int32_t minimumEffectivePlayers{1};
};

struct Config {
    std::uint32_t schemaVersion{1};
    bool enabled{true};
    // This is a master mode: when enabled, the runtime must use connected
    // players as its only dynamic source and ignore command-based simulation.
    bool battleNetSimulationEnabled{};
    std::int32_t minimumScalingPlayers{1};
    std::int32_t maximumCommandPlayers{8};
    std::int32_t startGamePlayers{};
    std::vector<PlayerHotkey> hotkeys{};
    ScalingChannel monsterLife{true, 0};
    ScalingChannel monsterExperience{true, 0};
    ScalingChannel monsterOffense{true, 0};
    NoDropConfig noDrop{};
    bool showUsageCounters{};
};

// Real-player scaling takes precedence over the optional /players NoDrop
// simulation. Keeping this decision in one pure helper prevents callsites
// from accidentally re-enabling the subordinate setting.
constexpr auto IsNoDropPartySimulationEnabled(const Config& config) noexcept
        -> bool {
    return config.noDrop.playersCommandSimulatesNearbyParty
        && !config.battleNetSimulationEnabled;
}

constexpr auto EffectiveNoDropConfig(const Config& config) noexcept -> NoDropConfig {
    auto result = config.noDrop;
    result.playersCommandSimulatesNearbyParty = IsNoDropPartySimulationEnabled(config);
    return result;
}

constexpr auto IsNoDropAdjustmentEnabled(const Config& config) noexcept -> bool {
    return config.noDrop.minimumEffectivePlayers > 1
        || IsNoDropPartySimulationEnabled(config);
}

struct NoDropCounts {
    std::int32_t playersCommand{1};
    std::int32_t nearbyPartyMembers{1};
    std::int32_t effectivePlayers{1};
};

constexpr auto NormalizePlayerCount(std::int32_t count) noexcept
        -> std::int32_t {
    return std::clamp(count, MinimumPlayerCount, MaximumPlayerCount);
}

constexpr auto HasNativePlayerCountScaling(std::int32_t count) noexcept
        -> bool {
    return count > 0;
}

constexpr auto ClampPlayersCommand(
        std::int32_t requested,
        const Config& config) noexcept -> std::int32_t {
    return std::clamp(
        NormalizePlayerCount(requested),
        config.minimumScalingPlayers,
        config.maximumCommandPlayers);
}

constexpr auto ResolveScalingCount(
        std::int32_t observed,
        std::int32_t baseline,
        const ScalingChannel& channel) noexcept -> std::int32_t {
    const auto normalized = NormalizePlayerCount(observed);
    auto result = (std::max)(normalized, baseline);
    if (!channel.enabled) return baseline;
    if (channel.maximumPlayers > 0) {
        result = (std::min)(result, channel.maximumPlayers);
    }
    return result;
}

constexpr auto MonsterLifeBonusPercent(std::int32_t count) noexcept
        -> std::int32_t {
    const auto normalized = NormalizePlayerCount(count);
    return normalized <= 8
        ? (normalized - 1) * 50
        : (normalized - 2) * 50;
}

constexpr auto MonsterExperienceBonusPercent(std::int32_t count) noexcept
        -> std::int32_t {
    const auto normalized = NormalizePlayerCount(count);
    return normalized <= 8
        ? (normalized - 1) * 50
        : (normalized + 26) * 10;
}

constexpr auto NativeNoDropEffectiveCount(
        std::int32_t playersCommand,
        std::int32_t nearbyPartyMembers) noexcept -> std::int32_t {
    // Zero nearby living party members is a valid native input. Promoting it
    // to one would grant extra half-credit at odd player counts.
    const auto nearby = std::clamp(nearbyPartyMembers, 0, MaximumPlayerCount);
    const auto players = (std::max)(NormalizePlayerCount(playersCommand), nearby);
    return nearby + (players - nearby) / 2;
}

constexpr auto ResolveNoDropCounts(
        std::int32_t observedPlayersCommand,
        std::int32_t observedNearbyPartyMembers,
        const NoDropConfig& config) noexcept -> NoDropCounts {
    auto players = NormalizePlayerCount(observedPlayersCommand);
    auto nearby = std::clamp(observedNearbyPartyMembers, 0, MaximumPlayerCount);
    if (config.playersCommandSimulatesNearbyParty) {
        players = (std::max)(players, nearby);
        nearby = players;
    }
    const auto minimum = NormalizePlayerCount(config.minimumEffectivePlayers);
    if (NativeNoDropEffectiveCount(players, nearby) < minimum) {
        // These are temporary inputs to this drop calculation only. Equal
        // inputs encode the exact effective minimum without half-credit loss.
        players = minimum;
        nearby = minimum;
    }
    return {players, nearby, NativeNoDropEffectiveCount(players, nearby)};
}

constexpr auto ResolveNoDropMonsterCap(
        std::int32_t observedMonsterCount,
        std::int32_t effectiveNoDropCount,
        bool simulateNearbyParty,
        std::int32_t minimumEffectivePlayers = 1) noexcept -> std::int32_t {
    if (!simulateNearbyParty) {
        return minimumEffectivePlayers > 1
            ? (std::max)(observedMonsterCount, NormalizePlayerCount(minimumEffectivePlayers))
            : observedMonsterCount;
    }
    return (std::max)(
        observedMonsterCount,
        NormalizePlayerCount(effectiveNoDropCount));
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

inline auto RequireTable(
        const toml::table& parent,
        std::string_view key,
        std::string& error) -> const toml::table* {
    const auto* node = parent.get(key);
    const auto* table = node ? node->as_table() : nullptr;
    if (!table) {
        error = "missing or invalid [" + std::string(key) + "] section";
    }
    return table;
}

inline auto ReadBool(
        const toml::table& table,
        std::string_view key,
        std::string_view name,
        bool& destination,
        std::string& error) -> bool {
    const auto* node = table.get(key);
    const auto* value = node ? node->as_boolean() : nullptr;
    if (!value) {
        error = std::string(name) + " must be true or false";
        return false;
    }
    destination = value->get();
    return true;
}

inline auto ReadInt(
        const toml::table& table,
        std::string_view key,
        std::string_view name,
        std::int32_t minimum,
        std::int32_t maximum,
        std::int32_t& destination,
        std::string& error) -> bool {
    const auto* node = table.get(key);
    const auto* value = node ? node->as_integer() : nullptr;
    if (!value || value->get() < minimum || value->get() > maximum) {
        error = std::string(name) + " must be an integer from "
            + std::to_string(minimum) + " to " + std::to_string(maximum);
        return false;
    }
    destination = static_cast<std::int32_t>(value->get());
    return true;
}

inline auto ParseChannel(
        const toml::table& table,
        std::string_view name,
        ScalingChannel& destination,
        std::int32_t baseline,
        std::string& error) -> bool {
    if (!ValidateKeys(table, std::string(name) + ".",
            {"enabled", "maximum-players"}, error)
            || !ReadBool(table, "enabled", std::string(name) + ".enabled",
                destination.enabled, error)
            || !ReadInt(table, "maximum-players",
                std::string(name) + ".maximum-players", 0,
                MaximumPlayerCount, destination.maximumPlayers, error)) {
        return false;
    }
    if (destination.maximumPlayers > 0
            && destination.maximumPlayers < baseline) {
        error = std::string(name)
            + ".maximum-players must be 0 or at least "
              "player-count.minimum-scaling-players";
        return false;
    }
    return true;
}

inline auto ParsePlayerControls(const toml::table& table, Config& config,
        std::string& error) -> bool {
    if (table.contains("start-game-players")) {
        if (!ReadInt(table, "start-game-players", "player-count.start-game-players",
                0, MaximumPlayerCount, config.startGamePlayers, error)) return false;
        if (config.startGamePlayers != 0
                && (config.startGamePlayers < config.minimumScalingPlayers
                    || config.startGamePlayers > config.maximumCommandPlayers)) {
            error = "player-count.start-game-players must be 0 or within the configured player-count bounds";
            return false;
        }
    }
    const auto* node = table.get("hotkeys");
    if (!node) return true;
    const auto* hotkeys = node->as_array();
    if (!hotkeys) {
        error = "player-count.hotkeys must contain [[player-count.hotkeys]] tables";
        return false;
    }
    for (const auto& entry : *hotkeys) {
        const auto* hotkey = entry.as_table();
        if (!hotkey) {
            error = "each player-count.hotkeys entry must be a table";
            return false;
        }
        PlayerHotkey parsed{};
        if (!ValidateKeys(*hotkey, "player-count.hotkeys.", {"keys", "players"}, error)
                || !ReadInt(*hotkey, "players", "player-count.hotkeys.players",
                    config.minimumScalingPlayers, config.maximumCommandPlayers,
                    parsed.players, error)) return false;
        const auto* keys = hotkey->get_as<std::string>("keys");
        if (!keys || !ParsePlayerHotkey(keys->get(), parsed)) {
            error = "player-count.hotkeys.keys must be A-Z, 0-9 or F1-F24, optionally prefixed by Shift+, Ctrl+ or Alt+";
            return false;
        }
        for (const auto& previous : config.hotkeys) {
            if (previous.key == parsed.key && previous.modifier == parsed.modifier) {
                error = "duplicate player-count.hotkeys key combination";
                return false;
            }
        }
        config.hotkeys.push_back(parsed);
    }
    return true;
}

inline auto ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) -> bool {
    try {
        const auto root = toml::parse(input);
        if (!ValidateKeys(root, {},
                {"config-version", "enabled", "battle-net-simulation",
                 "player-count",
                 "monster-life-scaling", "monster-experience-scaling",
                 "monster-offense-scaling", "no-drop", "troubleshooting",
                 "d2rl"}, error)) {
            return false;
        }

        Config parsed{};
        std::int32_t schema{};
        if (!ReadInt(root, "config-version", "config-version", 1, 1,
                schema, error)
                || !ReadBool(root, "enabled", "enabled", parsed.enabled,
                    error)) {
            return false;
        }
        parsed.schemaVersion = static_cast<std::uint32_t>(schema);

        if (const auto* node = root.get("battle-net-simulation")) {
            const auto* battleNetSimulation = node->as_table();
            if (!battleNetSimulation) {
                error = "missing or invalid [battle-net-simulation] section";
                return false;
            }
            if (!ValidateKeys(*battleNetSimulation,
                    "battle-net-simulation.", {"enabled"}, error)
                    || !ReadBool(*battleNetSimulation, "enabled",
                        "battle-net-simulation.enabled",
                        parsed.battleNetSimulationEnabled, error)) {
                return false;
            }
        }

        const auto* playerCount = RequireTable(root, "player-count", error);
        if (!playerCount
                || !ValidateKeys(*playerCount, "player-count.",
                    {"minimum-scaling-players", "maximum-command-players",
                     "start-game-players", "hotkeys"},
                    error)
                || !ReadInt(*playerCount, "minimum-scaling-players",
                    "player-count.minimum-scaling-players",
                    MinimumPlayerCount, MaximumPlayerCount,
                    parsed.minimumScalingPlayers, error)
                || !ReadInt(*playerCount, "maximum-command-players",
                    "player-count.maximum-command-players",
                    MinimumPlayerCount, MaximumPlayerCount,
                    parsed.maximumCommandPlayers, error)) {
            return false;
        }
        if (parsed.maximumCommandPlayers < parsed.minimumScalingPlayers) {
            error = "player-count.maximum-command-players must be at least "
                "player-count.minimum-scaling-players";
            return false;
        }
        if (!ParsePlayerControls(*playerCount, parsed, error)) return false;

        const auto* life = RequireTable(root, "monster-life-scaling", error);
        const auto* experience = RequireTable(
            root, "monster-experience-scaling", error);
        const auto* offense = RequireTable(
            root, "monster-offense-scaling", error);
        if (!life || !experience || !offense
                || !ParseChannel(*life, "monster-life-scaling",
                    parsed.monsterLife, parsed.minimumScalingPlayers, error)
                || !ParseChannel(*experience, "monster-experience-scaling",
                    parsed.monsterExperience, parsed.minimumScalingPlayers,
                    error)
                || !ParseChannel(*offense, "monster-offense-scaling",
                    parsed.monsterOffense, parsed.minimumScalingPlayers,
                    error)) {
            return false;
        }

        const auto* noDrop = RequireTable(root, "no-drop", error);
        if (!noDrop
                || !ValidateKeys(*noDrop, "no-drop.",
                    {"players-command-simulates-nearby-party", "minimum-effective-players"}, error)
                || !ReadBool(*noDrop,
                    "players-command-simulates-nearby-party",
                    "no-drop.players-command-simulates-nearby-party",
                    parsed.noDrop.playersCommandSimulatesNearbyParty,
                    error)) {
            return false;
        }

        if (noDrop->contains("minimum-effective-players")
                && !ReadInt(*noDrop, "minimum-effective-players",
                    "no-drop.minimum-effective-players", MinimumPlayerCount,
                    MaximumPlayerCount, parsed.noDrop.minimumEffectivePlayers, error)) {
            return false;
        }

        const auto* troubleshooting = RequireTable(
            root, "troubleshooting", error);
        if (!troubleshooting
                || !ValidateKeys(*troubleshooting, "troubleshooting.",
                    {"show-usage-counters"}, error)
                || !ReadBool(*troubleshooting, "show-usage-counters",
                    "troubleshooting.show-usage-counters",
                    parsed.showUsageCounters, error)) {
            return false;
        }

        const auto* d2rl = RequireTable(root, "d2rl", error);
        bool match{};
        if (!d2rl
                || !ValidateKeys(*d2rl, "d2rl.", {"match"}, error)
                || !ReadBool(*d2rl, "match", "d2rl.match", match, error)) {
            return false;
        }
        if (!match) {
            error = "d2rl.match must be true";
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

} // namespace ruffneckk::player_scaling
