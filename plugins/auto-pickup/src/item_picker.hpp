#pragma once
#include "config_edit.hpp"
#include "policy.hpp"
#include <cstring>
#include <optional>
#include <span>
#include <iterator>

namespace RuffnecKk::AutoPickup::Picker {
inline constexpr std::size_t PageSize = 8;
enum class Choice { Default, Include, Exclude };
struct Entry {
    std::uint32_t code{};
    std::string name;
    bool available{true};
};
inline auto CodeText(std::uint32_t code) -> std::string {
    std::string text;
    for (unsigned i = 0; i != 4; ++i) {
        const auto c = static_cast<char>((code >> (8 * i)) & 255);
        if (c == '\0' || c == ' ') break;
        text += c;
    }
    return text;
}
inline auto ValidCode(std::uint32_t raw) -> bool {
    const auto text = CodeText(raw);
    if (text.empty()) return false;
    for (auto c : text) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    // Reject embedded padding followed by other bytes.
    for (auto i = text.size(); i < 4; ++i) {
        const auto c = (raw >> (8 * i)) & 255;
        if (c != 0 && c != ' ') return false;
    }
    return true;
}
inline auto Fold(std::string text) -> std::string {
    // Preserve UTF-8 bytes; ASCII code/name search is case insensitive.
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
inline auto PlainName(std::string_view text) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        // Native item strings can contain UTF-8 D2 color markers (including
        // mods using U+FFFD in place of U+00FF). Search/display plain names.
        const auto marker = text.substr(i, 2) == "\xc3\xbf" ? 2U : text.substr(i, 3) == "\xef\xbf\xbd" ? 3U : 0U;
        if (marker && i + marker + 1 < text.size() && text[i + marker] == 'c') { i += marker + 2; continue; }
        const char c = text[i++];
        out += c == '\r' || c == '\n' || c == '\t' ? ' ' : c;
    }
    return out;
}
inline auto GetChoice(const Config& config, std::uint32_t code) -> Choice {
    if (std::binary_search(config.excludeCodes.begin(), config.excludeCodes.end(), code)) return Choice::Exclude;
    if (std::binary_search(config.includeCodes.begin(), config.includeCodes.end(), code)) return Choice::Include;
    return Choice::Default;
}
inline void SetChoice(Config& config, std::uint32_t code, Choice choice) {
    for (auto* list : {&config.includeCodes, &config.excludeCodes}) std::erase(*list, code);
    auto* list = choice == Choice::Include ? &config.includeCodes : choice == Choice::Exclude ? &config.excludeCodes : nullptr;
    if (list) { list->push_back(code); std::sort(list->begin(), list->end()); }
}
inline void SetChoices(Config& config, std::span<const std::uint32_t> codes, Choice choice) {
    std::vector<std::uint32_t> targets(codes.begin(), codes.end());
    std::erase(targets, PackItemCode("gld"));
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    for (auto* list : {&config.includeCodes, &config.excludeCodes}) {
        std::vector<std::uint32_t> keep;
        std::set_difference(list->begin(), list->end(), targets.begin(), targets.end(), std::back_inserter(keep));
        *list = std::move(keep);
    }
    auto* list = choice == Choice::Include ? &config.includeCodes : choice == Choice::Exclude ? &config.excludeCodes : nullptr;
    if (list) {
        std::vector<std::uint32_t> merged;
        std::set_union(list->begin(), list->end(), targets.begin(), targets.end(), std::back_inserter(merged));
        *list = std::move(merged);
    }
}
inline auto SameSelection(const Config& a, const Config& b) -> bool {
    return a.includeCodes == b.includeCodes && a.excludeCodes == b.excludeCodes;
}
inline constexpr std::array<const char*, 14> ToggleLabels{
    "Enable item pickup", "Vanilla gems", "Runes", "Ordinary keys", "Health potions", "Mana potions", "Rejuvenation potions",
    "Reimagined gems", "Reimagined gem stacks", "Reimagined rune stacks", "Pick up while moving", "Pick up after casting", "Post-cast gold", "Post-cast items"};
inline auto ToggleValue(const Config& config, std::size_t index) -> bool {
    if (!index) return config.enabled;
    if (index <= config.vanilla.size()) return config.vanilla[index - 1];
    if (index < 10) return config.reimagined[index - 1 - config.vanilla.size()];
    return index == 10 ? config.walkingEnabled : index == 11 ? config.teleportEnabled
        : index == 12 ? config.teleportGold : index == 13 && config.teleportItems;
}
inline void SetToggle(Config& config, std::size_t index, bool value) {
    if (!index) config.enabled = value;
    else if (index <= config.vanilla.size()) config.vanilla[index - 1] = value;
    else if (index < 10) config.reimagined[index - 1 - config.vanilla.size()] = value;
    else if (index == 10) config.walkingEnabled = value;
    else if (index == 11) config.teleportEnabled = value;
    else if (index == 12) config.teleportGold = value;
    else if (index == 13) config.teleportItems = value;
}
inline auto SameSettings(const Config& a, const Config& b) -> bool {
    return a.potionRouting == b.potionRouting && SameSelection(a, b) && a.enabled == b.enabled && a.vanilla == b.vanilla && a.reimagined == b.reimagined
        && a.walkingEnabled == b.walkingEnabled && a.teleportEnabled == b.teleportEnabled && a.teleportGold == b.teleportGold
        && a.teleportItems == b.teleportItems && a.walkingRange == b.walkingRange && a.teleportRange == b.teleportRange;
}
inline auto Search(std::vector<Entry> entries, const Config& draft, std::string_view query, bool overridesOnly) -> std::vector<Entry> {
    for (const auto* list : {&draft.includeCodes, &draft.excludeCodes}) for (const auto code : *list) {
        if (std::none_of(entries.begin(), entries.end(), [code](const auto& row) { return row.code == code; }))
            entries.push_back({code, "Not present in loaded tables", false});
    }
    const auto needle = Fold(std::string(query));
    std::erase_if(entries, [&](const auto& row) {
        return row.code == PackItemCode("gld")
            || (overridesOnly && GetChoice(draft, row.code) == Choice::Default)
            || Fold(row.name + " " + CodeText(row.code)).find(needle) == std::string::npos;
    });
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        const auto an = Fold(a.name), bn = Fold(b.name);
        return an == bn ? CodeText(a.code) < CodeText(b.code) : an < bn;
    });
    return entries;
}
inline auto ArrayText(const std::vector<std::uint32_t>& codes) -> std::string {
    std::string out = "[";
    for (const auto code : codes) { if (out.size() > 1) out += ", "; out += "\"" + CodeText(code) + "\""; }
    return out + "]";
}
// Edit only the two arrays. Retain other settings, comments and loader metadata.
// Input has already passed ParseConfig, so no ambiguous/duplicate keys exist.
inline auto SaveSelection(std::string text, const Config& expected, const Config& draft,
        std::string& output, std::string& error) -> bool {
    Config current;
    if (!ParseConfig(text, current, error)) return false;
    if (!SameSelection(current, expected)) { error = "Item lists changed outside this panel. Close and reopen before saving."; return false; }
    struct Edit { std::size_t start, length; std::string replacement; };
    std::vector<Edit> edits;
    bool inItems{}, sectionFound{}, includeFound{}, excludeFound{};
    std::size_t insert = text.size();
    for (std::size_t start = 0; start < text.size();) {
        const auto end = text.find('\n', start);
        const auto next = end == std::string::npos ? text.size() : end + 1;
        auto line = Trim(WithoutComment(std::string_view(text).substr(start, next - start)));
        // Trim also removes carriage returns but deliberately not newlines.
        if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
        line = Trim(line);
        if (!line.empty() && line.front() == '[') {
            if (inItems) insert = start;
            inItems = line == "[item_codes]";
            sectionFound = sectionFound || inItems;
        } else if (inItems) {
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                const auto key = Trim(line.substr(0, eq));
                if (key == "include" || key == "exclude") {
                    const auto value = Trim(line.substr(eq + 1));
                    const auto valueStart = static_cast<std::size_t>(value.data() - text.data());
                    auto valueEnd = valueStart;
                    // Item codes cannot contain ']' and comments can. Search
                    // each comment-stripped line, rather than raw text.
                    for (auto scan = valueStart; scan < text.size();) {
                        const auto eol = text.find('\n', scan);
                        const auto spanEnd = eol == std::string::npos ? text.size() : eol;
                        const auto part = WithoutComment(std::string_view(text).substr(scan, spanEnd - scan));
                        const auto bracket = part.find(']');
                        if (bracket != std::string_view::npos) { valueEnd = scan + bracket + 1; break; }
                        scan = spanEnd + 1;
                    }
                    if (valueEnd == valueStart) { error = "Unterminated item list."; return false; }
                    const bool include = key == "include";
                    edits.push_back({valueStart, valueEnd - valueStart, ArrayText(include ? draft.includeCodes : draft.excludeCodes)});
                    (include ? includeFound : excludeFound) = true;
                    start = valueEnd; // skip any continuation lines
                    const auto after = text.find('\n', start);
                    start = after == std::string::npos ? text.size() : after + 1;
                    continue;
                }
            }
        }
        start = next;
    }
    const auto newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string extra;
    if (!sectionFound) extra += std::string(newline) + "[item_codes]" + newline;
    if (!includeFound) extra += "include = " + ArrayText(draft.includeCodes) + newline;
    if (!excludeFound) extra += "exclude = " + ArrayText(draft.excludeCodes) + newline;
    if (!extra.empty()) {
        if (insert && text[insert - 1] != '\n') extra.insert(0, newline);
        edits.push_back({insert, 0, extra});
    }
    std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
    for (const auto& edit : edits) text.replace(edit.start, edit.length, edit.replacement);
    Config verify;
    if (!ParseConfig(text, verify, error) || !SameSelection(verify, draft)) { error = "Could not preserve the configuration while updating item lists."; return false; }
    output = std::move(text); return true;
}
// Patch editable boolean values without replacing comments, diagnostic mode or
// loader metadata. Missing keys/sections are added in their correct TOML scope.
inline auto SaveSettings(std::string text, const Config& expected, const Config& draft,
        std::string& output, std::string& error) -> bool {
    Config current;
    if (!ParseConfig(text, current, error)) return false;
    if (!SameSettings(current, expected)) { error = "Settings changed outside this panel. Close and reopen before saving."; return false; }
    if (!SaveSelection(std::move(text), expected, draft, text, error)) return false;
    auto patch = [&](std::string_view section, std::string_view key, std::string_view value) { PatchScalar(text, section, key, value); };
    if (current.enabled != draft.enabled) patch({}, "enabled", draft.enabled ? "true" : "false");
    for (std::size_t i = 0; i < draft.vanilla.size(); ++i)
        if (current.vanilla[i] != draft.vanilla[i]) patch("vanilla", VanillaGroups[i], draft.vanilla[i] ? "true" : "false");
    for (std::size_t i = 0; i < draft.reimagined.size(); ++i)
        if (current.reimagined[i] != draft.reimagined[i]) patch("reimagined", ReimaginedGroups[i], draft.reimagined[i] ? "true" : "false");
    if (current.walkingEnabled != draft.walkingEnabled) patch("walking", "enabled", draft.walkingEnabled ? "true" : "false");
    if (current.teleportEnabled != draft.teleportEnabled) patch("teleport", "enabled", draft.teleportEnabled ? "true" : "false");
    if (current.teleportGold != draft.teleportGold) patch("teleport", "gold", draft.teleportGold ? "true" : "false");
    if (current.teleportItems != draft.teleportItems) patch("teleport", "items", draft.teleportItems ? "true" : "false");
    if (current.walkingRange != draft.walkingRange) patch("walking", "pickup_range", std::to_string(draft.walkingRange));
    if (current.teleportRange != draft.teleportRange) patch("teleport", "pickup_range", std::to_string(draft.teleportRange));
    for (std::size_t i=0;i<3;++i) {
        const auto& before=current.potionRouting.families[i];const auto& after=draft.potionRouting.families[i];
        if(before.columns!=after.columns || before.count!=after.count) patch(PotionRouting::Sections[i],"belt_columns",PotionRouting::ColumnsText(after));
        if(before.inventoryFallback!=after.inventoryFallback) patch(PotionRouting::Sections[i],"inventory_fallback",after.inventoryFallback?"true":"false");
    }
    Config verify;
    if (!ParseConfig(text, verify, error) || !SameSettings(verify, draft)) { error = "Could not preserve configuration while saving settings."; return false; }
    output = std::move(text); return true;
}
}
