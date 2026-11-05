#pragma once
#include "item_picker.hpp"
#include <D2RLPlugin/api.h>

namespace RuffnecKk::AutoPickup::Picker {
// The public table API supplies raw compiled rows, not a stable Items struct.
// Discover the code column using the SDK's semantic code lookup as a witness.
// Never guess a name-string offset or reuse a numeric row id between mods.
template<class Lookup>
auto DiscoverCodeColumn(const D2RL::DataTables::TableView& table, Lookup lookup) -> std::optional<std::size_t> {
    if (!table.rows || table.rowCount < 2 || table.rowCount > 65'536 || table.rowSize < 4 || table.rowSize > 4096) return {};
    const auto* bytes = static_cast<const std::uint8_t*>(table.rows);
    const auto samples = (std::min)(table.rowCount, 32U);
    for (std::size_t offset = 0; offset + 4 <= table.rowSize; offset += 4) {
        std::size_t witnessed{};
        bool rejected{};
        for (std::uint32_t sample = 0; sample < samples; ++sample) {
            const auto index = static_cast<std::uint32_t>((std::uint64_t{sample} * (table.rowCount - 1)) / (samples - 1));
            std::uint32_t raw{};
            std::memcpy(&raw, bytes + std::size_t{index} * table.rowSize + offset, 4);
            if (!ValidCode(raw)) continue;
            const auto code = PackItemCode(CodeText(raw));
            if (!lookup(code, index)) { rejected = true; break; }
            ++witnessed;
        }
        if (!rejected && witnessed >= (std::min)(std::size_t{8}, std::size_t{samples})) return offset;
    }
    return {};
}
// Optional name-id fallback for mods whose namestr points to a different
// existing game key (e.g. a new charm code using cm1's base name). Locate the
// field only when numeric localization and code-key localization independently
// agree across diverse rows. Custom numeric string IDs remain unsupported by
// localization ABI 2; code-key lookup is still the primary name source.
template<class KeyName, class IdName>
auto DiscoverNameColumn(const D2RL::DataTables::TableView& table, std::size_t codeColumn, KeyName keyName, IdName idName) -> std::optional<std::size_t> {
    if (!table.rows || table.rowCount < 8 || table.rowCount > 65'536 || table.rowSize > 4096 || codeColumn + 4 > table.rowSize) return {};
    const auto* bytes = static_cast<const std::uint8_t*>(table.rows);
    struct Witness { const std::uint8_t* row; std::string name; };
    std::vector<Witness> witnesses;
    const auto samples = (std::min)(table.rowCount, 32U);
    for (std::uint32_t i = 0; i < samples; ++i) {
        const auto index = static_cast<std::uint32_t>((std::uint64_t{i} * (table.rowCount - 1)) / (samples - 1));
        const auto* row = bytes + std::size_t{index} * table.rowSize;
        std::uint32_t raw{}; std::memcpy(&raw, row + codeColumn, 4);
        if (!ValidCode(raw)) continue;
        auto name = keyName(PackItemCode(CodeText(raw)));
        if (!name.empty() && std::none_of(witnesses.begin(), witnesses.end(), [&](const auto& w) { return w.name == name; }))
            witnesses.push_back({row, std::move(name)});
    }
    if (witnesses.size() < 8) return {};
    for (std::size_t offset = 0; offset + 2 <= table.rowSize; offset += 2) {
        const bool agrees = std::all_of(witnesses.begin(), witnesses.end(), [&](const auto& w) {
            std::uint16_t id{}; std::memcpy(&id, w.row + offset, 2);
            return idName(id) == w.name;
        });
        if (agrees) return offset;
    }
    return {};
}
}
