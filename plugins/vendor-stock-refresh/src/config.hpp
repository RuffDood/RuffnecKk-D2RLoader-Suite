#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace RuffnecKk::VendorStockRefresh {

struct Config {
    bool enabled{true};
    bool diagnosticsEnabled{};
};

inline auto TrimConfigValue(std::string_view value) noexcept
    -> std::string_view {
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

inline auto WithoutConfigComment(std::string_view value) noexcept
    -> std::string_view {
    const auto comment = value.find('#');
    return comment == std::string_view::npos
        ? value
        : value.substr(0, comment);
}

inline auto SetConfigError(
    std::string& error,
    std::size_t line,
    std::string_view message
) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

inline auto ParseConfigBoolean(
    std::string_view value,
    bool& output
) noexcept -> bool {
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

inline auto ParseConfig(
    std::string_view input,
    Config& output,
    std::string& error
) -> bool {
    enum class Section {
        None,
        Plugin,
        Diagnostics,
    };

    Config parsed{};
    Section section{Section::None};
    bool pluginSectionSeen{};
    bool diagnosticsSectionSeen{};
    bool pluginEnabledSeen{};
    bool diagnosticsEnabledSeen{};

    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = TrimConfigValue(WithoutConfigComment(input.substr(
            start,
            end == std::string_view::npos
                ? input.size() - start
                : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']'
                || line.find('[', 1) != std::string_view::npos
                || line.find(']') != line.size() - 1) {
                return SetConfigError(
                    error, lineNumber, "invalid section header");
            }
            const auto name = TrimConfigValue(
                line.substr(1, line.size() - 2));
            if (name == "plugin") {
                if (pluginSectionSeen) {
                    return SetConfigError(
                        error, lineNumber, "duplicate section");
                }
                pluginSectionSeen = true;
                section = Section::Plugin;
            } else if (name == "diagnostics") {
                if (diagnosticsSectionSeen) {
                    return SetConfigError(
                        error, lineNumber, "duplicate section");
                }
                diagnosticsSectionSeen = true;
                section = Section::Diagnostics;
            } else {
                return SetConfigError(error, lineNumber, "unknown section");
            }
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string_view::npos
            || line.find('=', equal + 1) != std::string_view::npos) {
            return SetConfigError(
                error, lineNumber, "expected one key/value assignment");
        }
        const auto key = TrimConfigValue(line.substr(0, equal));
        const auto value = TrimConfigValue(line.substr(equal + 1));
        if (section == Section::None || key.empty() || value.empty()) {
            return SetConfigError(
                error, lineNumber, "invalid key/value assignment");
        }
        if (key != "enabled") {
            return SetConfigError(error, lineNumber, "unknown setting");
        }

        bool* seen{};
        bool* destination{};
        if (section == Section::Plugin) {
            seen = &pluginEnabledSeen;
            destination = &parsed.enabled;
        } else {
            seen = &diagnosticsEnabledSeen;
            destination = &parsed.diagnosticsEnabled;
        }
        if (*seen) {
            return SetConfigError(error, lineNumber, "duplicate setting");
        }
        *seen = true;
        if (!ParseConfigBoolean(value, *destination)) {
            return SetConfigError(
                error, lineNumber, "expected true or false");
        }
    }

    if (!(pluginSectionSeen && diagnosticsSectionSeen
            && pluginEnabledSeen && diagnosticsEnabledSeen)) {
        error = "one or more required settings are missing";
        return false;
    }

    output = parsed;
    error.clear();
    return true;
}

} // namespace RuffnecKk::VendorStockRefresh
