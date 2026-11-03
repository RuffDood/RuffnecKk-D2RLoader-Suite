#pragma once
#include "player_scaling_policy.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>

namespace ruffneckk::player_scaling::menu {
struct Setting {
    const char* key;
    const char* label;
    const char* type;
    bool restart;
    int minimum{};
    int maximum{65535};
};
inline constexpr Setting Settings[] {
    {"hud-player-slider.enabled", "Show player-count HUD", "bool", false},
    {"hud-player-slider.x", "HUD horizontal position", "int", true, 0, 3200},
    {"hud-player-slider.y", "HUD vertical position", "int", true, 0, 1801},
    {"enabled", "Enable plugin", "bool", true},
    {"battle-net-simulation.enabled", "Battle.net simulation", "bool", true},
    {"player-count.minimum-scaling-players", "Minimum scaling players", "int", true, 1},
    {"player-count.maximum-command-players", "Maximum command players", "int", true, 1},
    {"player-count.start-game-players", "Starting players (0 keeps Loader value)", "int", true},
    {"player-count.hotkeys", "Hotkeys (TOML array)", "string", true},
    {"monster-life-scaling.enabled", "Scale monster life", "bool", false},
    {"monster-life-scaling.maximum-players", "Life cap (0 is unlimited)", "int", false},
    {"monster-experience-scaling.enabled", "Scale monster experience", "bool", false},
    {"monster-experience-scaling.maximum-players", "Experience cap (0 is unlimited)", "int", false},
    {"monster-offense-scaling.enabled", "Scale physical damage and Attack Rating", "bool", false},
    {"monster-offense-scaling.maximum-players", "Offense cap (0 is unlimited)", "int", false},
    {"no-drop.minimum-effective-players", "Minimum effective NoDrop players", "int", false, 1},
    {"no-drop.players-command-simulates-nearby-party", "Simulate nearby party for NoDrop", "bool", false},
    {"troubleshooting.show-usage-counters", "Show usage counters", "bool", false},
};
inline const Setting* Find(std::string_view key) {
    for (const auto& setting : Settings) if (key == setting.key) return &setting;
    return nullptr;
}
inline std::string Format(const toml::node& node) {
    std::ostringstream out;
    out << toml::toml_formatter(node);
    return out.str();
}
inline std::string Quote(std::string_view value) { return Format(toml::value<std::string>(value)); }
inline std::string Trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == value.npos) return {};
    return std::string(value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1));
}
inline std::size_t Offset(std::string_view text, toml::source_position position) {
    std::size_t offset = 0;
    for (std::uint32_t line = 1; line < position.line; ++line) {
        const auto end = text.find('\n', offset);
        if (end == text.npos) throw std::runtime_error("Invalid source position");
        offset = end + 1;
    }
    // TOML columns count Unicode code points, not UTF-8 bytes.
    for (std::uint32_t column = 1; column < position.column; ++column) {
        if (offset >= text.size()) throw std::runtime_error("Invalid source column");
        ++offset;
        while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) ++offset;
    }
    return offset;
}
inline std::string HotkeysValue(const toml::table& root) {
    const auto* array = root.at_path("player-count.hotkeys").as_array();
    std::string result = "[";
    if (array) for (const auto& entry : *array) {
        if (result.size() > 1) result += ", ";
        const auto& table = *entry.as_table();
        result += "{keys = " + Format(*table.get("keys")) + ", players = " + Format(*table.get("players")) + "}";
    }
    return result + "]";
}
inline std::string Value(const Config& config, const Setting& setting) {
    const std::string_view key(setting.key);
    if (key == "hud-player-slider.enabled") return config.hudEnabled ? "true" : "false";
    if (key == "hud-player-slider.x") return std::to_string(config.hudX);
    if (key == "hud-player-slider.y") return std::to_string(config.hudY);
    if (key == "enabled") return config.enabled ? "true" : "false";
    if (key == "battle-net-simulation.enabled") return config.battleNetSimulationEnabled ? "true" : "false";
    if (key == "player-count.minimum-scaling-players") return std::to_string(config.minimumScalingPlayers);
    if (key == "player-count.maximum-command-players") return std::to_string(config.maximumCommandPlayers);
    if (key == "player-count.start-game-players") return std::to_string(config.startGamePlayers);
    if (key == "no-drop.minimum-effective-players") return std::to_string(config.noDrop.minimumEffectivePlayers);
    if (key == "no-drop.players-command-simulates-nearby-party") return config.noDrop.playersCommandSimulatesNearbyParty ? "true" : "false";
    if (key == "troubleshooting.show-usage-counters") return config.showUsageCounters ? "true" : "false";
    const auto& channel = key.starts_with("monster-life-") ? config.monsterLife
        : key.starts_with("monster-experience-") ? config.monsterExperience : config.monsterOffense;
    return key.ends_with(".enabled") ? (channel.enabled ? "true" : "false") : std::to_string(channel.maximumPlayers);
}
inline void CopyLiveField(Config& current, const Config& saved, std::string_view key) {
    if (key == "hud-player-slider.enabled") current.hudEnabled = saved.hudEnabled;
    else if (key == "monster-life-scaling.enabled") current.monsterLife.enabled = saved.monsterLife.enabled;
    else if (key == "monster-life-scaling.maximum-players") current.monsterLife.maximumPlayers = saved.monsterLife.maximumPlayers;
    else if (key == "monster-experience-scaling.enabled") current.monsterExperience.enabled = saved.monsterExperience.enabled;
    else if (key == "monster-experience-scaling.maximum-players") current.monsterExperience.maximumPlayers = saved.monsterExperience.maximumPlayers;
    else if (key == "monster-offense-scaling.enabled") current.monsterOffense.enabled = saved.monsterOffense.enabled;
    else if (key == "monster-offense-scaling.maximum-players") current.monsterOffense.maximumPlayers = saved.monsterOffense.maximumPlayers;
    else if (key == "no-drop.minimum-effective-players") current.noDrop.minimumEffectivePlayers = saved.noDrop.minimumEffectivePlayers;
    else if (key == "no-drop.players-command-simulates-nearby-party") current.noDrop.playersCommandSimulatesNearbyParty = saved.noDrop.playersCommandSimulatesNearbyParty;
    else if (key == "troubleshooting.show-usage-counters") current.showUsageCounters = saved.showUsageCounters;
}
inline bool EditDocument(std::string_view document, const Setting& setting, std::string_view input,
        std::string& updated, Config& parsed, std::string& error) {
    Config original;
    if (!ParseToml(document, original, error)) return false;
    const auto root = toml::parse(document);
    auto valueRoot = toml::parse("value = " + std::string(input));
    if (valueRoot.size() != 1) { error = "Enter one value only."; return false; }
    const auto& value = *valueRoot.get("value");
    const std::string_view type(setting.type), key(setting.key);
    if ((type == "bool" && !value.is_boolean()) || (type == "int" && !value.is_integer())
            || (type == "string" && !value.is_string())) { error = "Incorrect value type."; return false; }
    std::string canonical = Format(value);
    if (type == "string") {
        canonical = value.as_string()->get();
        const auto hotkeyInput = toml::parse("value = " + canonical);
        if (hotkeyInput.size() != 1 || !hotkeyInput["value"].is_array()) { error = "Enter a TOML array of hotkeys."; return false; }
        canonical = Format(*hotkeyInput.get("value"));
    }
    updated = document;
    const auto* node = root.at_path(key).node();
    const bool arrayTables = node && node->is_array() && !node->as_array()->empty()
        && node->as_array()->front().is_table() && !node->as_array()->front().as_table()->is_inline();
    if (node && !arrayTables) {
        const auto begin = Offset(document, node->source().begin);
        const auto end = Offset(document, node->source().end);
        updated.replace(begin, end - begin, canonical);
    } else {
        const std::string newline = document.find("\r\n") == document.npos ? "\n" : "\r\n";
        if (arrayTables) {
            // Keep all comments while replacing the existing hotkey declarations.
            std::string retained;
            bool inHotkey = false;
            for (std::size_t start = 0; start < document.size();) {
                auto end = document.find('\n', start);
                end = end == document.npos ? document.size() : end + 1;
                const auto line = document.substr(start, end - start);
                const auto code = Trim(line.substr(0, line.find('#')));
                if (code.starts_with("[")) inHotkey = code == "[[player-count.hotkeys]]";
                if (inHotkey && !code.empty()) {
                    const auto comment = line.find('#');
                    if (comment != line.npos) retained += std::string(line.substr(comment));
                    else retained += newline;
                } else retained += line;
                start = end;
            }
            updated = std::move(retained);
        }
        const auto dot = key.rfind('.');
        const auto section = dot == key.npos ? std::string_view{} : key.substr(0, dot);
        const auto leaf = dot == key.npos ? key : key.substr(dot + 1);
        const auto row = std::string(leaf) + " = " + canonical + newline;
        bool inserted = section.empty();
        if (inserted) updated.insert(0, row);
        else for (std::size_t start = 0; start < updated.size();) {
            auto end = updated.find('\n', start);
            end = end == updated.npos ? updated.size() : end + 1;
            const auto line = std::string_view(updated).substr(start, end - start);
            if (Trim(line.substr(0, line.find('#'))) == "[" + std::string(section) + "]") {
                if (end && updated[end - 1] != '\n') { updated += newline; end = updated.size(); }
                updated.insert(end, row); inserted = true; break;
            }
            start = end;
        }
        if (!inserted) {
            if (!updated.empty() && updated.back() != '\n') updated += newline;
            updated += "[" + std::string(section) + "]" + newline + row;
        }
    }
    // Refuse unsupported layouts rather than saving a malformed or lossy rewrite.
    return ParseToml(updated, parsed, error);
}

// Startup Settings and PlayerControls remain immutable. Hooks hold one coherent
// snapshot; edits allocate before saving and publication cannot fail afterwards.
class State {
    std::atomic<std::shared_ptr<const Config>> active_;
public:
    std::mutex editMutex;
    State() : active_(std::make_shared<const Config>()) {}
    auto Read() const noexcept { return active_.load(std::memory_order_acquire); }
    void Initialize(const Config& config) { active_.store(std::make_shared<const Config>(config)); }
    template<class Write>
    bool Change(std::string_view document, const Setting& setting, std::string_view input, Write write, std::string& error) {
        std::string updated;
        Config saved;
        if (!EditDocument(document, setting, input, updated, saved, error)) return false;
        auto next = std::make_shared<Config>(*Read());
        if (!setting.restart) CopyLiveField(*next, saved, setting.key);
        for (const auto* channel : {&next->monsterLife, &next->monsterExperience, &next->monsterOffense}) {
            if (channel->maximumPlayers && channel->maximumPlayers < next->minimumScalingPlayers) {
                error = "This cap is below the running baseline. Restart to apply the saved baseline first.";
                return false;
            }
        }
        if (!write(updated)) { error = "Cannot save the configuration; the running settings were kept."; return false; }
        active_.store(std::move(next), std::memory_order_release);
        return true;
    }
};
inline const std::string& Schema() {
    static const std::string schema = [] {
        std::string out = "title = \"PlayerX Scaling Tweaks\"\n[[setting]]\ntype = \"text\"\nlabel = \"Scaling and NoDrop edits affect subsequent game calculations. Existing monsters may keep their original stats.\"\n";
        for (const auto& setting : Settings) {
            out += "[[setting]]\nkey = " + Quote(setting.key) + "\nlabel = " + Quote(setting.label) + "\ntype = " + Quote(setting.type) + "\n";
            if (setting.restart) out += "restart = true\n";
            if (std::string_view(setting.type) == "int") out += "min = " + std::to_string(setting.minimum) + "\nmax = " + std::to_string(setting.maximum) + "\nstep = 1\n";
            if (std::string_view(setting.key) == "player-count.hotkeys") out += "hint = 'Example: [{keys = \"Shift+1\", players = 1}]. Use [] to remove shortcuts. Loader Controls can override bindings.'\n";
            if (std::string_view(setting.key).starts_with("no-drop.")) out += "note = \"Party simulation is ignored in Battle.net simulation. The NoDrop minimum still applies.\"\n";
        }
        return out;
    }();
    return schema;
}
} // namespace ruffneckk::player_scaling::menu
