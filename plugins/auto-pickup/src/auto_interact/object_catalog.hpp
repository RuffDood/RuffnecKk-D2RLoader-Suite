#pragma once
#include "policy.hpp"
#include <vector>
#include <unordered_set>
#include <span>

namespace AutoInteract {
// Objects' Class column can be compiled to a linker ID, not an ASCII name.
// Preserve source keys once, then require the current row count and runtime
// semantic tuple to agree. No numeric object IDs are authored into the plugin.
class ObjectCatalog {
public:
    auto Parse(std::string_view text, std::string& error) -> bool {
        rows_.clear();
        if (text.size() > 8 * 1024 * 1024) { error = "Objects TXT exceeds 8 MiB"; return false; }
        if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
        std::vector<std::string_view> header, cells;
        auto line = [&]() { auto end = text.find('\n'); auto s = text.substr(0, end); text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1); if (!s.empty() && s.back() == '\r') s.remove_suffix(1); return s; };
        auto split = [&](std::string_view s, std::vector<std::string_view>& out) {
            out.clear(); std::size_t begin{}; bool quote{};
            if (s.size() > 65536) return false;
            for (std::size_t i = 0; i <= s.size(); ++i) {
                if (i < s.size() && s[i] == '"') { if (quote && i + 1 < s.size() && s[i + 1] == '"') ++i; else quote = !quote; }
                if (i == s.size() || (s[i] == '\t' && !quote)) {
                    auto v = s.substr(begin, i - begin); if (v.size() >= 2 && v.front() == '"' && v.back() == '"') { v.remove_prefix(1); v.remove_suffix(1); }
                    out.push_back(v); begin = i + 1; if (out.size() > 512) return false;
                }
            }
            return !quote;
        };
        if (!split(line(), header)) { error = "Invalid Objects header"; return false; }
        constexpr std::array<std::string_view, 6> names{"Class", "InitFn", "OperateFn", "SubClass", "Selectable0", "IsDoor"};
        std::array<std::size_t, names.size()> columns;
        for (std::size_t i = 0; i < names.size(); ++i) {
            std::size_t found = header.size();
            for (std::size_t j = 0; j < header.size(); ++j) if (header[j] == names[i]) { if (found != header.size()) { error = "Duplicate Objects column"; return false; } found = j; }
            if (found == header.size()) { error = "Missing Objects column"; return false; } columns[i] = found;
        }
        std::vector<Row> parsed; std::unordered_set<std::string> keys;
        while (!text.empty()) {
            const auto s = line(); if (s.empty()) continue;
            if (!split(s, cells) || cells.size() != header.size()) { error = "Invalid Objects row"; return false; }
            const auto key = cells[columns[0]];
            if (key == "Expansion") continue;
            if (!SafeKey(key) || !keys.emplace(key).second || parsed.size() >= 16384) { error = "Invalid or duplicate Objects Class"; return false; }
            Row row; row.key = key;
            for (std::size_t i = 1; i < names.size(); ++i) {
                const auto v = cells[columns[i]]; std::uint32_t n{};
                if (!v.empty()) { const auto [end, code] = std::from_chars(v.data(), v.data() + v.size(), n); if (code != std::errc{} || end != v.data() + v.size()) { error = "Invalid Objects number"; return false; } }
                if (n > 255 || (i >= 4 && n > 1)) { error = "Objects field out of range"; return false; }
                row.fields[i - 1] = static_cast<std::uint8_t>(n);
            }
            parsed.push_back(std::move(row));
        }
        if (parsed.empty()) { error = "Empty Objects catalog"; return false; }
        rows_ = std::move(parsed); return true;
    }
    auto Key(std::uint32_t id, std::uint32_t count, std::span<const std::uint8_t> record) const noexcept -> std::string_view {
        if (rows_.size() != count || id >= rows_.size() || record.size() < 0x168) return {};
        const auto& row = rows_[id];
        if (row.fields != std::array<std::uint8_t, 5>{record[0x15C], record[0x15E], record[0x127], record[0x85], record[0xFA]}) return {};
        return row.key;
    }
    auto Size() const noexcept -> std::size_t { return rows_.size(); }
    static auto SafeKey(std::string_view key) noexcept -> bool {
        if (key.empty() || key.size() > 63) return false;
        for (const unsigned char c : key) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        return true;
    }
private:
    struct Row { std::string key; std::array<std::uint8_t, 5> fields{}; };
    std::vector<Row> rows_;
};
}
