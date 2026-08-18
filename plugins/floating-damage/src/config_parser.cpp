#include "config_parser.hpp"

#include "d3d12_renderer.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

namespace FloatingDamage {
namespace {

std::string Trim(std::string_view value) {
    std::size_t first{};
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t'
                || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t'
                || value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool StripComment(std::string& line) {
    char quote{};
    for (std::size_t index{}; index < line.size(); ++index) {
        const char character = line[index];
        if (quote != 0) {
            if (character == quote) quote = 0;
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
        } else if (character == '#') {
            line.resize(index);
            break;
        }
    }
    return quote == 0;
}

bool ParseBool(std::string_view value, bool& output) {
    const std::string text = Trim(value);
    if (text == "true") {
        output = true;
        return true;
    }
    if (text == "false") {
        output = false;
        return true;
    }
    return false;
}

bool ParseInt(std::string_view value, int minimum, int maximum, int& output) {
    const std::string text = Trim(value);
    int parsed{};
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{}
            || result.ptr != text.data() + text.size()
            || parsed < minimum || parsed > maximum) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseFloat(
    std::string_view value,
    float minimum,
    float maximum,
    float& output) {
    const std::string text = Trim(value);
    if (text.empty()) return false;
    char* end{};
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size()
            || !std::isfinite(parsed)
            || parsed < minimum || parsed > maximum) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseColor(std::string_view value, ImVec4& output) {
    std::string text = Trim(value);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']')
        return false;
    text = text.substr(1, text.size() - 2);
    std::array<float, 4> components{};
    std::size_t cursor{};
    for (std::size_t index{}; index < components.size(); ++index) {
        const std::size_t comma = text.find(',', cursor);
        const bool last = index + 1 == components.size();
        if ((!last && comma == std::string::npos)
                || (last && comma != std::string::npos)) {
            return false;
        }
        const std::size_t end = last ? text.size() : comma;
        if (!ParseFloat(
                std::string_view(text).substr(cursor, end - cursor),
                0.0f,
                1.0f,
                components[index])) {
            return false;
        }
        cursor = end + 1;
    }
    output = ImVec4(
        components[0], components[1], components[2], components[3]);
    return true;
}

bool ParseTomlString(std::string_view value, std::string& output) {
    const std::string text = Trim(value);
    if (text.size() < 2
            || (text.front() != '"' && text.front() != '\'')
            || text.back() != text.front()) {
        return false;
    }
    output = text.substr(1, text.size() - 2);
    return !output.empty() && output.size() <= 64
        && output.find(text.front()) == std::string::npos;
}

bool ParseMainHotkey(const std::string& token, std::uint32_t& virtualKey) {
    if (token.size() == 1) {
        const auto character = static_cast<unsigned char>(token.front());
        if ((character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')) {
            virtualKey = character;
            return true;
        }
        struct PunctuationKey {
            char character;
            std::uint32_t virtualKey;
        };
        constexpr PunctuationKey punctuationKeys[] = {
            {';', VK_OEM_1}, {'=', VK_OEM_PLUS}, {',', VK_OEM_COMMA},
            {'-', VK_OEM_MINUS}, {'.', VK_OEM_PERIOD}, {'/', VK_OEM_2},
            {'`', VK_OEM_3}, {'[', VK_OEM_4}, {'\\', VK_OEM_5},
            {']', VK_OEM_6}, {'\'', VK_OEM_7},
        };
        for (const auto& key : punctuationKeys) {
            if (character == static_cast<unsigned char>(key.character)) {
                virtualKey = key.virtualKey;
                return true;
            }
        }
    }

    if (token.size() >= 2 && token.front() == 'F') {
        unsigned functionKey{};
        for (std::size_t index = 1; index < token.size(); ++index) {
            if (token[index] < '0' || token[index] > '9') return false;
            functionKey = functionKey * 10U
                + static_cast<unsigned>(token[index] - '0');
        }
        if (functionKey >= 1 && functionKey <= 24) {
            virtualKey = VK_F1 + functionKey - 1;
            return true;
        }
    }

    struct NamedKey {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedKey namedKeys[] = {
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT},
        {"DELETE", VK_DELETE}, {"HOME", VK_HOME}, {"END", VK_END},
        {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"ENTER", VK_RETURN}, {"BACKSPACE", VK_BACK},
        {"ESCAPE", VK_ESCAPE}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
        {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT},
        {"SEMICOLON", VK_OEM_1}, {"EQUALS", VK_OEM_PLUS},
        {"COMMA", VK_OEM_COMMA}, {"MINUS", VK_OEM_MINUS},
        {"PERIOD", VK_OEM_PERIOD}, {"SLASH", VK_OEM_2},
        {"BACKTICK", VK_OEM_3}, {"LBRACKET", VK_OEM_4},
        {"BACKSLASH", VK_OEM_5}, {"RBRACKET", VK_OEM_6},
        {"APOSTROPHE", VK_OEM_7}, {"MOUSE3", VK_MBUTTON},
        {"MIDDLE", VK_MBUTTON}, {"MOUSE4", VK_XBUTTON1},
        {"MOUSE5", VK_XBUTTON2},
    };
    for (const auto& key : namedKeys) {
        if (token == key.name) {
            virtualKey = key.virtualKey;
            return true;
        }
    }
    return false;
}

bool ParseHotkey(std::string_view text, HotkeyBinding& output) {
    HotkeyBinding parsed{};
    parsed.virtualKey = 0;
    parsed.control = false;
    parsed.shift = false;
    parsed.alt = false;
    bool hasMainKey{};

    std::size_t begin{};
    while (begin <= text.size()) {
        const std::size_t separator = text.find('+', begin);
        const std::string token = Upper(Trim(text.substr(
            begin,
            separator == std::string_view::npos
                ? text.size() - begin
                : separator - begin)));
        if (token.empty()) return false;
        if (token == "CTRL" || token == "CONTROL") {
            if (parsed.control) return false;
            parsed.control = true;
        } else if (token == "SHIFT") {
            if (parsed.shift) return false;
            parsed.shift = true;
        } else if (token == "ALT") {
            if (parsed.alt) return false;
            parsed.alt = true;
        } else {
            if (hasMainKey || !ParseMainHotkey(token, parsed.virtualKey))
                return false;
            hasMainKey = true;
        }
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    if (!hasMainKey) return false;
    output = parsed;
    return true;
}

bool IsKnownTable(std::string_view table) {
    constexpr std::array<std::string_view, 10> tables{
        "general", "hotkey", "appearance", "animation", "combining",
        "layout", "dps", "preview", "diagnostics", "colors",
    };
    return std::find(tables.begin(), tables.end(), table) != tables.end();
}

bool ApplySetting(
    std::string_view table,
    std::string_view key,
    std::string_view value,
    Config& config) {
    const auto boolean = [&](bool& destination) {
        return ParseBool(value, destination);
    };
    const auto integer = [&](int minimum, int maximum, int& destination) {
        return ParseInt(value, minimum, maximum, destination);
    };
    const auto number = [&](float minimum, float maximum, float& destination) {
        return ParseFloat(value, minimum, maximum, destination);
    };

    if (table == "general") {
        if (key == "enabled") return boolean(config.enabled);
        if (key == "max_numbers_on_screen")
            return integer(1, 4096, config.maxNumbersOnScreen);
        if (key == "font_index")
            return integer(0, D3D12::kFloatingDamageFontCount - 1,
                config.fontIndex);
        if (key == "color_by_damage_type")
            return boolean(config.colorByDamageType);
    } else if (table == "hotkey") {
        if (key == "toggle_hotkey_enabled")
            return boolean(config.toggleHotkeyEnabled);
        if (key == "toggle_hotkey") {
            std::string text;
            HotkeyBinding binding{};
            if (!ParseTomlString(value, text) || !ParseHotkey(text, binding))
                return false;
            config.toggleHotkeyText = text;
            config.toggleHotkey = binding;
            return true;
        }
    } else if (table == "appearance") {
        if (key == "text_size") return number(1.0f, 512.0f, config.textSize);
        if (key == "critical_hit_size")
            return number(1.0f, 512.0f, config.criticalHitSize);
        if (key == "text_outline_width")
            return integer(0, 16, config.textOutlineWidth);
        if (key == "shadow_left_right_offset")
            return number(-512.0f, 512.0f, config.shadowLeftRightOffset);
        if (key == "shadow_up_down_offset")
            return number(-512.0f, 512.0f, config.shadowUpDownOffset);
    } else if (table == "animation") {
        if (key == "display_time_seconds")
            return number(0.01f, 60.0f, config.displayTimeSeconds);
        if (key == "critical_display_time_seconds")
            return number(0.01f, 60.0f, config.criticalDisplayTimeSeconds);
        if (key == "fade_out_start")
            return number(0.0f, 1.0f, config.fadeOutStart);
        if (key == "spawn_size") return number(0.001f, 16.0f, config.spawnSize);
        if (key == "pop_bounce_size")
            return number(0.001f, 16.0f, config.popBounceSize);
        if (key == "pop_in_time_seconds")
            return number(0.0f, 10.0f, config.popInTimeSeconds);
        if (key == "settle_time_seconds")
            return number(0.0f, 10.0f, config.settleTimeSeconds);
        if (key == "upward_drift_speed")
            return number(-4096.0f, 4096.0f, config.upwardDriftSpeed);
        if (key == "sideways_spread")
            return number(-4096.0f, 4096.0f, config.sidewaysSpread);
        if (key == "spawn_height_offset")
            return number(-4096.0f, 4096.0f, config.spawnHeightOffset);
    } else if (table == "combining") {
        if (key == "enable_hit_combining")
            return boolean(config.enableHitCombining);
        if (key == "max_combined_hit_size")
            return integer(1, (std::numeric_limits<int>::max)(),
                config.maxCombinedHitSize);
        if (key == "combine_window_ms")
            return integer(0, 60000, config.combineWindowMs);
        if (key == "extend_display_on_hit_seconds")
            return number(0.0f, 60.0f, config.extendDisplayOnHitSeconds);
        if (key == "hit_pulse_size")
            return number(0.001f, 16.0f, config.hitPulseSize);
        if (key == "hit_pulse_time_seconds")
            return number(0.0f, 10.0f, config.hitPulseTimeSeconds);
        if (key == "show_tick_popups") return boolean(config.showTickPopups);
        if (key == "tick_popup_time_seconds")
            return number(0.01f, 60.0f, config.tickPopupTimeSeconds);
        if (key == "tick_popup_size")
            return number(0.001f, 16.0f, config.tickPopupSize);
        if (key == "tick_popup_travel")
            return number(-4096.0f, 4096.0f, config.tickPopupTravel);
        if (key == "tick_popup_height_offset")
            return number(-4096.0f, 4096.0f,
                config.tickPopupHeightOffset);
    } else if (table == "layout") {
        if (key == "spread_numbers_horizontally")
            return boolean(config.spreadNumbersHorizontally);
        if (key == "number_of_columns")
            return integer(1, 64, config.numberOfColumns);
        if (key == "column_spacing")
            return number(-4096.0f, 4096.0f, config.columnSpacing);
        if (key == "stack_height_step")
            return number(-4096.0f, 4096.0f, config.stackHeightStep);
        if (key == "column_reuse_time_seconds")
            return number(0.0f, 60.0f, config.columnReuseTimeSeconds);
        if (key == "max_stack_height")
            return number(0.0f, 4096.0f, config.maxStackHeight);
    } else if (table == "dps") {
        if (key == "show_dps_counter") return boolean(config.showDpsCounter);
        if (key == "horizontal_position_percent")
            return number(0.0f, 100.0f, config.horizontalPositionPercent);
        if (key == "vertical_position_percent")
            return number(0.0f, 100.0f, config.verticalPositionPercent);
        if (key == "dps_sample_time_seconds")
            return number(0.01f, 600.0f, config.dpsSampleTimeSeconds);
    } else if (table == "preview") {
        if (key == "preview_number_count")
            return integer(1, 1024, config.previewNumberCount);
        if (key == "preview_spread")
            return number(0.0f, 4096.0f, config.previewSpread);
    } else if (table == "diagnostics") {
        if (key == "enabled") return boolean(config.diagnosticsEnabled);
    } else if (table == "colors") {
        if (key == "normal") return ParseColor(value, config.normalColor);
        if (key == "critical") return ParseColor(value, config.criticalColor);
        if (key == "physical") return ParseColor(value, config.physicalColor);
        if (key == "fire") return ParseColor(value, config.fireColor);
        if (key == "lightning") return ParseColor(value, config.lightningColor);
        if (key == "cold") return ParseColor(value, config.coldColor);
        if (key == "poison") return ParseColor(value, config.poisonColor);
        if (key == "magic") return ParseColor(value, config.magicColor);
        if (key == "outline") return ParseColor(value, config.outlineColor);
        if (key == "shadow") return ParseColor(value, config.shadowColor);
    }
    return false;
}

bool Fail(std::size_t line, std::string_view message, std::string& error) {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

} // namespace

bool ParseConfigToml(
    std::string_view text,
    Config& output,
    std::string& error) {
    Config parsed{};
    std::string currentTable;
    std::unordered_set<std::string> seenTables;
    std::unordered_set<std::string> seenKeys;
    std::size_t cursor{};
    std::size_t lineNumber{};

    if (text.size() >= 3
            && static_cast<unsigned char>(text[0]) == 0xEF
            && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF) {
        return Fail(1, "UTF-8 BOM is not supported", error);
    }

    while (cursor < text.size()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n', cursor);
        std::string line = Trim(text.substr(
            cursor,
            newline == std::string_view::npos
                ? text.size() - cursor
                : newline - cursor));
        cursor = newline == std::string_view::npos
            ? text.size()
            : newline + 1;
        if (!StripComment(line))
            return Fail(lineNumber, "unterminated quoted string", error);
        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']'
                    || line.find('[', 1) != std::string::npos
                    || line.find(']') != line.size() - 1) {
                return Fail(lineNumber, "malformed table header", error);
            }
            currentTable = Trim(
                std::string_view(line).substr(1, line.size() - 2));
            if (!IsKnownTable(currentTable))
                return Fail(lineNumber, "unknown table", error);
            if (!seenTables.insert(currentTable).second)
                return Fail(lineNumber, "duplicate table", error);
            continue;
        }

        if (currentTable.empty())
            return Fail(lineNumber, "setting appears before a table", error);
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            return Fail(lineNumber, "missing '='", error);
        const std::string key = Trim(
            std::string_view(line).substr(0, equals));
        const std::string value = Trim(
            std::string_view(line).substr(equals + 1));
        if (key.empty() || value.empty())
            return Fail(lineNumber, "empty key or value", error);
        const std::string qualifiedKey = currentTable + "." + key;
        if (!seenKeys.insert(qualifiedKey).second)
            return Fail(lineNumber, "duplicate key", error);
        if (!ApplySetting(currentTable, key, value, parsed))
            return Fail(lineNumber, "unknown key or invalid value", error);
    }

    output = std::move(parsed);
    error.clear();
    return true;
}

} // namespace FloatingDamage
