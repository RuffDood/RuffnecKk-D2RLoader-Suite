#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::LiveSettings {
inline std::string Trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == s.npos ? std::string{} : std::string(s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1));
}
inline std::size_t Comment(std::string_view s) {
    char quote{}; bool escape{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (escape) { escape = false; continue; }
        if (quote == '"' && c == '\\') { escape = true; continue; }
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '#') return i;
    }
    return s.npos;
}
inline std::string Quote(std::string_view s) {
    std::string out = "\"";
    for (const auto c : s) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 32) throw std::invalid_argument("Unsupported control character.");
        else out += c;
    }
    return out + '"';
}
inline std::optional<std::string> Scalar(std::string_view document, std::string_view qualified) {
    const auto dot = qualified.find('.');
    const auto table = dot == qualified.npos ? std::string{} : std::string(qualified.substr(0, dot));
    const auto key = qualified.substr(dot == qualified.npos ? 0 : dot + 1);
    std::string current;
    for (std::size_t start=0; start<document.size();) {
        auto end=document.find('\n',start); if(end==document.npos) end=document.size();
        const auto line=document.substr(start,end-start); const auto code=Trim(line.substr(0,Comment(line)));
        if(!code.empty() && code.front()=='[') current=Trim(std::string_view(code).substr(1,code.size()-2));
        else if(current==table) {
            const auto equal=code.find('=');
            if(equal!=code.npos && Trim(std::string_view(code).substr(0,equal))==key)
                return Trim(std::string_view(code).substr(equal+1));
        }
        start=end==document.size()?end:end+1;
    }
    return std::nullopt;
}
// One scalar edit, preserving unrelated bytes/comments/order and CRLF. The
// consumer's existing full-document parser validates before and after editing.
inline std::string Edit(std::string_view document, std::string_view qualified, std::string_view value) {
    if (value.empty() || value.find_first_of("\r\n") != value.npos || Comment(value) != value.npos)
        throw std::invalid_argument("Enter one TOML value, without a comment or newline.");
    const auto dot = qualified.find('.');
    const auto table = dot == qualified.npos ? std::string{} : std::string(qualified.substr(0, dot));
    const auto key = std::string(qualified.substr(dot == qualified.npos ? 0 : dot + 1));
    const std::string newline = document.find("\r\n") == document.npos ? "\n" : "\r\n";
    std::string current, result(document); std::size_t insertion = document.size(); bool found = table.empty();
    for (std::size_t start = 0; start < document.size();) {
        auto end = document.find('\n', start); if (end == document.npos) end = document.size();
        const auto line = document.substr(start, end - start); const auto comment = Comment(line);
        const auto code = Trim(line.substr(0, comment));
        if (!code.empty() && code.front() == '[') {
            if (current == table) insertion = start;
            current = Trim(std::string_view(code).substr(1, code.size() - 2));
            if (current == table) found = true;
        } else if (current == table) {
            const auto equal = line.find('=');
            if (equal != line.npos && Trim(line.substr(0, equal)) == key) {
                auto begin = equal + 1; while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
                auto finish = comment == line.npos ? line.size() : comment;
                while (finish > begin && (line[finish - 1] == ' ' || line[finish - 1] == '\t' || line[finish - 1] == '\r')) --finish;
                result.replace(start + begin, finish - begin, value); return result;
            }
        }
        start = end == document.size() ? end : end + 1;
    }
    std::string row = key + " = " + std::string(value) + newline;
    if (!found) {
        if (!result.empty() && result.back() != '\n') result += newline;
        return result + '[' + table + ']' + newline + row;
    }
    if (insertion == result.size() && !result.empty() && result.back() != '\n') row.insert(0, newline);
    result.insert(insertion, row); return result;
}
template<class T> class Snapshot {
    std::atomic<std::shared_ptr<const T>> value_{std::make_shared<const T>()};
public:
    auto Read() const noexcept { return value_.load(std::memory_order_acquire); }
    void Publish(std::shared_ptr<const T> value) noexcept { value_.store(std::move(value), std::memory_order_release); }
    void Set(const T& value) { Publish(std::make_shared<const T>(value)); }
};
// All validation and allocation precede saving; publication cannot throw.
template<class T, class Parse, class Save>
void Change(Snapshot<T>& running, std::string_view document, std::string_view key,
        std::string_view value, Parse parse, Save save) {
    (void)parse(document);
    const auto edited = Edit(document, key, value);
    auto parsed = parse(edited);
    auto next = std::make_shared<const T>(std::move(parsed));
    if (!save(edited)) throw std::runtime_error("Cannot save; the running settings were kept.");
    running.Publish(std::move(next));
}
}
