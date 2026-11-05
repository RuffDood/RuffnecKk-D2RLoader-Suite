#pragma once
#include "policy.hpp"

namespace RuffnecKk::AutoPickup {
inline void PatchScalar(std::string& text, std::string_view section, std::string_view key, std::string_view replacement) {
    bool inSection = section.empty(), foundSection = inSection;
    std::size_t insert = text.size();
    for (std::size_t start = 0; start < text.size();) {
        const auto end = text.find('\n', start);
        const auto next = end == std::string::npos ? text.size() : end + 1;
        auto line = Trim(WithoutComment(std::string_view(text).substr(start, next - start)));
        if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
        line = Trim(line);
        if (!line.empty() && line.front() == '[') {
            if (inSection) insert = start;
            inSection = line == "[" + std::string(section) + "]";
            foundSection = foundSection || inSection;
        } else if (inSection) {
            const auto eq = line.find('=');
            if (eq != std::string::npos && Trim(line.substr(0, eq)) == key) {
                const auto value = Trim(line.substr(eq + 1));
                const auto offset = static_cast<std::size_t>(value.data() - text.data());
                text.replace(offset, value.size(), replacement); return;
            }
        }
        start = next;
    }
    const auto newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string extra;
    if (insert && text[insert - 1] != '\n') extra += newline;
    if (!foundSection) extra += std::string(newline) + "[" + std::string(section) + "]" + newline;
    extra += std::string(key) + " = " + std::string(replacement) + newline;
    text.insert(insert, extra);
}
}
