#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ruffneckk::doll_explosion {

inline constexpr std::int32_t MaximumFixedDamage =
    std::numeric_limits<std::int32_t>::max() >> 8;
inline constexpr std::int32_t MaximumDelayFrames =
    std::numeric_limits<std::int16_t>::max();
inline constexpr std::int32_t MaximumRadius = 64;
inline constexpr std::size_t MaximumTargets = 64;

// The original controls the native lifetime. A removal is not necessarily a
// timeout (collision/invalid room can also remove a missile): only a last-frame
// countdown may blast, but every removal must retire its tracked sidecar.
template<class Original, class Removed>
auto RunTrackedServerDo(std::int32_t framesBefore, Original&& original,
        Removed&& removed) noexcept -> std::int32_t {
    const auto result = original();
    if (result == 2) removed(framesBefore == 1);
    return result;
}

enum class DamageFormula : std::uint8_t {
    Fixed,
    SourceMaxLifePercent,
};

struct InclusiveRange {
    std::int32_t minimum{};
    std::int32_t maximum{};

    friend constexpr auto operator==(
            const InclusiveRange&,
            const InclusiveRange&) noexcept -> bool = default;
};

struct DifficultyRanges {
    InclusiveRange normal{};
    InclusiveRange nightmare{};
    InclusiveRange hell{};
};

struct Config {
    std::uint32_t schemaVersion{1};
    std::vector<std::int32_t> targetMonsterIds{
        212, 213, 214, 215, 216, 690, 691,
    };
    std::int32_t delayFrames{25};
    std::int32_t radius{4};
    DamageFormula formula{DamageFormula::Fixed};
    DifficultyRanges fixed{
        .normal = {18, 30},
        .nightmare = {54, 96},
        .hell = {318, 540},
    };
    DifficultyRanges sourceMaxLifePercent{
        .normal = {30, 50},
        .nightmare = {21, 35},
        .hell = {12, 20},
    };
    bool diagnostics{};
};

inline auto BuildConfigCandidates(
        const std::filesystem::path& activeModConfigDirectory,
        const std::filesystem::path& scopeConfigDirectory,
        const std::filesystem::path& globalConfigDirectory,
        const std::filesystem::path& fileName)
        -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> result;
    const auto append = [&](const std::filesystem::path& directory) {
        if (directory.empty()) return;
        const auto candidate = (directory / fileName).lexically_normal();
        if (std::find(result.begin(), result.end(), candidate) == result.end()) {
            result.emplace_back(candidate);
        }
    };
    append(activeModConfigDirectory);
    append(scopeConfigDirectory);
    append(globalConfigDirectory);
    return result;
}

constexpr auto SelectRange(
        const DifficultyRanges& ranges,
        std::uint8_t difficulty) noexcept -> const InclusiveRange* {
    switch (difficulty) {
    case 0:
        return &ranges.normal;
    case 1:
        return &ranges.nightmare;
    case 2:
        return &ranges.hell;
    default:
        return nullptr;
    }
}

inline auto IsTargetMonster(
        const Config& config,
        std::int32_t monsterId) noexcept -> bool {
    return std::find(
        config.targetMonsterIds.begin(),
        config.targetMonsterIds.end(),
        monsterId) != config.targetMonsterIds.end();
}

constexpr auto InclusiveSpan(const InclusiveRange& range) noexcept
        -> std::uint32_t {
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(range.maximum)
        - static_cast<std::uint64_t>(range.minimum) + 1ULL);
}

constexpr auto ApplyInclusiveRoll(
        const InclusiveRange& range,
        std::uint32_t zeroBasedRoll) noexcept -> std::int32_t {
    return range.minimum + static_cast<std::int32_t>(zeroBasedRoll);
}

constexpr auto ScaleFixedDamage(std::int32_t points) noexcept
        -> std::optional<std::int32_t> {
    if (points < 0 || points > MaximumFixedDamage) return std::nullopt;
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(points) << 8U);
}

constexpr auto ScaleMaxLifePercent(
        std::int32_t maximumLifeFixed,
        std::int32_t percent) noexcept -> std::optional<std::int32_t> {
    if (maximumLifeFixed <= 0 || percent < 0 || percent > 100) {
        return std::nullopt;
    }
    const auto scaled =
        static_cast<std::int64_t>(maximumLifeFixed) * percent / 100;
    if (scaled < 0 || scaled > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(scaled);
}

inline auto IsKnownKey(
        std::string_view key,
        std::initializer_list<std::string_view> allowed) noexcept -> bool {
    return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

inline auto ValidateKeys(
        const toml::table& table,
        std::string_view prefix,
        std::initializer_list<std::string_view> allowed,
        std::string& error) -> bool {
    for (const auto& [key, value] : table) {
        (void)value;
        if (!IsKnownKey(key.str(), allowed)) {
            error = "unknown setting: " + std::string(prefix)
                + std::string(key.str());
            return false;
        }
    }
    return true;
}

inline auto RequireTable(
        const toml::table& parent,
        std::string_view key,
        std::string& error) -> const toml::table* {
    const auto* node = parent.get(key);
    if (!node) {
        error = "missing [" + std::string(key) + "] section";
        return nullptr;
    }
    const auto* table = node->as_table();
    if (!table) {
        error = std::string(key) + " must be a TOML table";
        return nullptr;
    }
    return table;
}

inline auto ReadRequiredInt(
        const toml::table& table,
        std::string_view key,
        std::string_view qualifiedName,
        std::int64_t minimum,
        std::int64_t maximum,
        std::int32_t& destination,
        std::string& error) -> bool {
    const auto* node = table.get(key);
    const auto* value = node ? node->as_integer() : nullptr;
    if (!value || value->get() < minimum || value->get() > maximum) {
        error = std::string(qualifiedName) + " must be an integer from "
            + std::to_string(minimum) + " to " + std::to_string(maximum);
        return false;
    }
    destination = static_cast<std::int32_t>(value->get());
    return true;
}

inline auto ReadRange(
        const toml::table& table,
        std::string_view key,
        std::string_view qualifiedName,
        std::int32_t maximum,
        InclusiveRange& destination,
        std::string& error) -> bool {
    const auto* node = table.get(key);
    const auto* values = node ? node->as_array() : nullptr;
    if (!values || values->size() != 2) {
        error = std::string(qualifiedName)
            + " must be a two-integer array [minimum, maximum]";
        return false;
    }
    const auto* minimumNode = values->get(0);
    const auto* maximumNode = values->get(1);
    const auto* minimumValue = minimumNode ? minimumNode->as_integer() : nullptr;
    const auto* maximumValue = maximumNode ? maximumNode->as_integer() : nullptr;
    if (!minimumValue || !maximumValue
            || minimumValue->get() < 0 || maximumValue->get() < 0
            || minimumValue->get() > maximum
            || maximumValue->get() > maximum) {
        error = std::string(qualifiedName) + " values must be integers from 0 to "
            + std::to_string(maximum);
        return false;
    }
    if (minimumValue->get() > maximumValue->get()) {
        error = std::string(qualifiedName)
            + " minimum must not exceed maximum";
        return false;
    }
    destination = {
        static_cast<std::int32_t>(minimumValue->get()),
        static_cast<std::int32_t>(maximumValue->get()),
    };
    return true;
}

inline auto ReadDifficultyRanges(
        const toml::table& table,
        std::string_view prefix,
        std::int32_t maximum,
        DifficultyRanges& destination,
        std::string& error) -> bool {
    if (!ValidateKeys(
            table, std::string(prefix) + ".",
            {"normal", "nightmare", "hell"}, error)) {
        return false;
    }
    return ReadRange(
               table, "normal", std::string(prefix) + ".normal",
               maximum, destination.normal, error)
        && ReadRange(
               table, "nightmare", std::string(prefix) + ".nightmare",
               maximum, destination.nightmare, error)
        && ReadRange(
               table, "hell", std::string(prefix) + ".hell",
               maximum, destination.hell, error);
}

inline auto ReadTargets(
        const toml::table& table,
        Config& destination,
        std::string& error) -> bool {
    if (!ValidateKeys(table, "targets.", {"monstats_ids"}, error)) {
        return false;
    }
    const auto* node = table.get("monstats_ids");
    const auto* values = node ? node->as_array() : nullptr;
    if (!values || values->empty() || values->size() > MaximumTargets) {
        error = "targets.monstats_ids must contain from 1 to "
            + std::to_string(MaximumTargets) + " integers";
        return false;
    }
    std::vector<std::int32_t> ids;
    ids.reserve(values->size());
    for (const auto& item : *values) {
        const auto* value = item.as_integer();
        if (!value || value->get() < 0 || value->get() > 65535) {
            error = "targets.monstats_ids values must be integers from 0 to 65535";
            return false;
        }
        const auto id = static_cast<std::int32_t>(value->get());
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            error = "targets.monstats_ids must not contain duplicates";
            return false;
        }
        ids.emplace_back(id);
    }
    destination.targetMonsterIds = std::move(ids);
    return true;
}

inline auto ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) -> bool {
    try {
        const auto root = toml::parse(input);
        if (!ValidateKeys(
                root, {},
                {"config_version", "targets", "explosion", "damage",
                 "diagnostics"}, error)) {
            return false;
        }
        const auto* versionNode = root.get("config_version");
        const auto* version = versionNode ? versionNode->as_integer() : nullptr;
        if (!version || version->get() != 1) {
            error = "config_version must be integer 1";
            return false;
        }

        Config parsed{};
        const auto* targets = RequireTable(root, "targets", error);
        const auto* explosion = RequireTable(root, "explosion", error);
        const auto* damage = RequireTable(root, "damage", error);
        const auto* diagnostics = RequireTable(root, "diagnostics", error);
        if (!targets || !explosion || !damage || !diagnostics
                || !ReadTargets(*targets, parsed, error)) {
            return false;
        }

        if (!ValidateKeys(
                *explosion, "explosion.", {"delay_frames", "radius"}, error)
                || !ReadRequiredInt(
                    *explosion, "delay_frames", "explosion.delay_frames",
                    0, MaximumDelayFrames, parsed.delayFrames, error)
                || !ReadRequiredInt(
                    *explosion, "radius", "explosion.radius",
                    1, MaximumRadius, parsed.radius, error)) {
            return false;
        }

        if (!ValidateKeys(
                *damage, "damage.",
                {"formula", "fixed", "source_max_life_percent"}, error)) {
            return false;
        }
        const auto* formulaNode = damage->get("formula");
        const auto* formula = formulaNode ? formulaNode->as_string() : nullptr;
        if (!formula) {
            error = "damage.formula must be a string";
            return false;
        }
        if (formula->get() == "fixed") {
            parsed.formula = DamageFormula::Fixed;
        } else if (formula->get() == "source_max_life_percent") {
            parsed.formula = DamageFormula::SourceMaxLifePercent;
        } else {
            error = "damage.formula must be \"fixed\" or \"source_max_life_percent\"";
            return false;
        }

        const auto* fixed = RequireTable(*damage, "fixed", error);
        const auto* percent = RequireTable(
            *damage, "source_max_life_percent", error);
        if (!fixed || !percent
                || !ReadDifficultyRanges(
                    *fixed, "damage.fixed", MaximumFixedDamage,
                    parsed.fixed, error)
                || !ReadDifficultyRanges(
                    *percent, "damage.source_max_life_percent", 100,
                    parsed.sourceMaxLifePercent, error)) {
            return false;
        }

        if (!ValidateKeys(
                *diagnostics, "diagnostics.", {"show_usage_counters"}, error)) {
            return false;
        }
        const auto* diagnosticNode = diagnostics->get("show_usage_counters");
        const auto* diagnostic = diagnosticNode
            ? diagnosticNode->as_boolean() : nullptr;
        if (!diagnostic) {
            error = "diagnostics.show_usage_counters must be true or false";
            return false;
        }
        parsed.diagnostics = diagnostic->get();

        result = std::move(parsed);
        error.clear();
        return true;
    } catch (const toml::parse_error& exception) {
        error = exception.description();
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace ruffneckk::doll_explosion
