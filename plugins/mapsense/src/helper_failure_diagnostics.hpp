#pragma once
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace RuffnecKk::MapSense::Detail {
// Keep the beginning and end: a helper may emit atlas records before its error.
inline auto BoundedHelperText(std::string_view text, std::size_t limit) -> std::string {
    std::string selected;
    if (text.size() <= limit) selected.assign(text);
    else {
        const auto head = limit / 4U;
        selected.assign(text.substr(0, head));
        selected += " ...[truncated]... ";
        selected.append(text.substr(text.size() - (limit - head)));
    }
    for (auto& ch : selected) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte == 0x7FU) ch = ' ';
    }
    return selected;
}

struct HelperFailureDetails {
    std::string_view stage;
    std::uint32_t operation{};
    std::uint32_t seed{};
    unsigned difficulty{};
    int act{};
    int level{};
    std::uint32_t windowsError{};
    bool exitCodeKnown{};
    std::uint32_t exitCode{};
    bool timedOut{};
    std::string_view command;
    std::string_view workingDirectory;
    std::string_view output;
};

inline auto FormatHelperFailure(const HelperFailureDetails& value) -> std::string {
    std::ostringstream out;
    out << "MapSense helper failure: stage=" << value.stage
        << " operation=" << value.operation << " seed=" << value.seed
        << " difficulty=" << value.difficulty << " act=" << value.act
        << " level=" << value.level << " win32=" << value.windowsError
        << " exit=";
    if (value.exitCodeKnown) out << "0x" << std::hex << value.exitCode << std::dec;
    else out << "unavailable";
    out << " timeout=" << (value.timedOut ? "yes" : "no")
        << " cwd=[" << BoundedHelperText(value.workingDirectory, 240) << ']'
        << " command=[" << BoundedHelperText(value.command, 900) << ']'
        << " output=[" << BoundedHelperText(value.output, 1800) << ']';
    return out.str();
}
}
