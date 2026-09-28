#pragma once

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace ruffneckk::player_scaling {

struct PlayerHotkey {
    std::uint32_t key{};
    std::uint32_t modifier{};
    std::int32_t players{1};
};

// Stable virtual-key and modifier values from Input v1. Limit the grammar
// to portable named keys and the single modifier supported by that service.
inline auto ParsePlayerHotkey(std::string_view text, PlayerHotkey& result)
        -> bool {
    std::string chord;
    for (const auto c : text) {
        if (c == ' ' || c == '\t') continue;
        chord += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
    std::string_view key = chord;
    result.modifier = 0;
    const auto separator = key.find('+');
    if (separator != std::string_view::npos) {
        const auto modifier = key.substr(0, separator);
        if (modifier == "SHIFT") result.modifier = 1;
        else if (modifier == "CTRL" || modifier == "CONTROL") result.modifier = 2;
        else if (modifier == "ALT") result.modifier = 3;
        else return false;
        key.remove_prefix(separator + 1);
    }
    if (key.size() == 1 && ((key[0] >= '0' && key[0] <= '9')
            || (key[0] >= 'A' && key[0] <= 'Z'))) {
        result.key = static_cast<std::uint32_t>(key[0]);
        return true;
    }
    if (key.size() >= 2 && key[0] == 'F') {
        int number{};
        const auto parsed = std::from_chars(key.data() + 1,
            key.data() + key.size(), number);
        if (parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size()
                && number >= 1 && number <= 24) {
            result.key = static_cast<std::uint32_t>(0x6F + number);
            return true;
        }
    }
    return false;
}

} // namespace ruffneckk::player_scaling
