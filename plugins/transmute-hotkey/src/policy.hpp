#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace RuffnecKk::TransmuteHotkey {

enum class Modifier : std::uint32_t {
    None = 0,
    Shift = 1,
    Control = 2,
    Alt = 3,
};

struct Hotkey {
    std::uint32_t virtualKey{'T'};
    Modifier modifier{Modifier::Shift};
};

struct Config {
    bool enabled{};
    bool diagnostics{};
    Hotkey hotkey{};
    std::string hotkeyText{"SHIFT+T"};
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

inline auto UpperTrim(std::string_view value) -> std::string {
    value = Trim(value);
    std::string output(value);
    std::transform(output.begin(), output.end(), output.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return output;
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

inline auto ParseQuotedString(
    std::string_view value,
    std::string& output
) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }
    const auto content = value.substr(1, value.size() - 2);
    if (content.find('"') != std::string_view::npos
        || content.find('\\') != std::string_view::npos) {
        return false;
    }
    output.assign(content);
    return true;
}

inline auto IsUnsupportedMouseToken(std::string_view token) noexcept -> bool {
    return token == "MOUSE3" || token == "MOUSE 3" || token == "MIDDLE"
        || token == "MBUTTON" || token == "MOUSE4" || token == "MOUSE 4"
        || token == "XBUTTON1" || token == "MOUSE5" || token == "MOUSE 5"
        || token == "XBUTTON2";
}

inline auto ParseMainKey(
    const std::string& token,
    std::uint32_t& virtualKey
) noexcept -> bool {
    if (token.size() == 1) {
        const auto character = static_cast<unsigned char>(token.front());
        if ((character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')) {
            virtualKey = character;
            return true;
        }
    }
    if (token.size() >= 2 && token.front() == 'F') {
        unsigned value{};
        for (std::size_t index = 1; index < token.size(); ++index) {
            if (token[index] < '0' || token[index] > '9') return false;
            const auto digit = static_cast<unsigned>(token[index] - '0');
            if (value > (24U - digit) / 10U) return false;
            value = value * 10U + digit;
        }
        if (value >= 1 && value <= 24) {
            virtualKey = 0x70U + value - 1U;
            return true;
        }
    }

    struct NamedKey {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedKey NamedKeys[]{
        {"SPACE", 0x20},
        {"TAB", 0x09},
        {"INSERT", 0x2D},
        {"DELETE", 0x2E},
        {"HOME", 0x24},
        {"END", 0x23},
        {"PAGEUP", 0x21},
        {"PAGEDOWN", 0x22},
    };
    for (const auto& key : NamedKeys) {
        if (token == key.name) {
            virtualKey = key.virtualKey;
            return true;
        }
    }
    return false;
}

inline auto ParseHotkey(
    std::string_view text,
    Hotkey& output,
    std::string& error
) -> bool {
    Hotkey parsed{};
    parsed.virtualKey = 0;
    parsed.modifier = Modifier::None;
    bool hasMainKey{};
    bool hasModifier{};
    std::size_t begin{};
    while (begin <= text.size()) {
        const auto separator = text.find('+', begin);
        const auto token = UpperTrim(text.substr(
            begin,
            separator == std::string_view::npos
                ? text.size() - begin : separator - begin));
        if (token.empty()) {
            error = "hotkey contains an empty token";
            return false;
        }

        Modifier modifier{Modifier::None};
        if (token == "CTRL" || token == "CONTROL") {
            modifier = Modifier::Control;
        } else if (token == "SHIFT") {
            modifier = Modifier::Shift;
        } else if (token == "ALT") {
            modifier = Modifier::Alt;
        }
        if (modifier != Modifier::None) {
            if (hasModifier) {
                error = "Transmute Hotkey supports exactly zero or one modifier";
                return false;
            }
            hasModifier = true;
            parsed.modifier = modifier;
        } else if (IsUnsupportedMouseToken(token)) {
            error = "Transmute Hotkey currently supports keyboard bindings only";
            return false;
        } else {
            if (hasMainKey || !ParseMainKey(token, parsed.virtualKey)) {
                error = "hotkey contains an unsupported keyboard key";
                return false;
            }
            hasMainKey = true;
        }

        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    if (!hasMainKey || parsed.virtualKey == 0) {
        error = "hotkey requires one keyboard key";
        return false;
    }
    output = parsed;
    error.clear();
    return true;
}

inline auto ParseConfig(
    std::string_view input,
    Config& output,
    std::string& error
) -> bool {
    Config parsed{};
    bool enabled{};
    bool hotkey{};
    bool diagnostics{};
    bool diagnosticsTable{};
    bool inDiagnosticsTable{};

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
            if (line != "[diagnostics]" || diagnosticsTable) {
                return SetError(
                    error, lineNumber, "unknown or duplicate section");
            }
            diagnosticsTable = true;
            inDiagnosticsTable = true;
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
        if (inDiagnosticsTable) {
            if (key != "enabled") {
                return SetError(
                    error, lineNumber, "unknown diagnostics setting");
            }
            duplicate = &diagnostics;
            valid = ParseBoolean(value, parsed.diagnostics);
        } else if (key == "enabled") {
            duplicate = &enabled;
            valid = ParseBoolean(value, parsed.enabled);
        } else if (key == "hotkey") {
            duplicate = &hotkey;
            valid = ParseQuotedString(value, parsed.hotkeyText);
            if (valid) {
                std::string hotkeyError;
                valid = ParseHotkey(
                    parsed.hotkeyText, parsed.hotkey, hotkeyError);
                if (!valid) {
                    return SetError(error, lineNumber, hotkeyError);
                }
            }
        } else {
            return SetError(error, lineNumber, "unknown setting");
        }
        if (duplicate && *duplicate) {
            return SetError(error, lineNumber, "duplicate setting");
        }
        if (duplicate) *duplicate = true;
        if (!valid) return SetError(error, lineNumber, "invalid setting value");
    }

    if (!(enabled && hotkey)) {
        error = "one or more required settings are missing";
        return false;
    }
    output = parsed;
    error.clear();
    return true;
}

} // namespace RuffnecKk::TransmuteHotkey
