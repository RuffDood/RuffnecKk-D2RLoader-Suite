#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneckk::cast_triggers {

inline constexpr std::uint8_t PlayerUnitType = 0;
inline constexpr std::uint8_t PlayerModeAttack1 = 7;
inline constexpr std::uint8_t PlayerModeAttack2 = 8;
inline constexpr std::uint8_t PlayerModeCast = 10;
inline constexpr std::uint8_t PlayerModeThrow = 11;
inline constexpr std::uint8_t PlayerModeKick = 12;
inline constexpr std::uint8_t PlayerModeSpecial1 = 13;
inline constexpr std::uint8_t PlayerModeSpecial2 = 14;
inline constexpr std::uint8_t PlayerModeSpecial3 = 15;
inline constexpr std::uint8_t PlayerModeSpecial4 = 16;
inline constexpr std::uint8_t PlayerModeSequence = 18;
inline constexpr std::uint64_t SkillFlagRepeat = 1ULL << 11;
inline constexpr std::int32_t SameLevelMarker = 63;
inline constexpr std::int32_t NativeEventEffectTargetFlag = 1;
inline constexpr std::int32_t NativeNormalSkillTargetFlag = 0;
inline constexpr std::int32_t MinimumSkillId = 0;
inline constexpr std::int32_t MaximumSkillId = 65535;
inline constexpr std::int32_t DisabledCombatStatId = 0;
inline constexpr std::int32_t MinimumCombatStatId = 1;
inline constexpr std::int32_t MaximumCombatStatId = 65535;
inline constexpr std::uint32_t DefaultChannelingIntervalFrames = 50;
inline constexpr std::uint32_t MinimumChannelingIntervalFrames = 1;
inline constexpr std::uint32_t MaximumChannelingIntervalFrames = 65535;
inline constexpr std::uint32_t MaximumPendingInputAgeFrames = 250;
inline constexpr std::uint64_t NativeSeedMultiplier = UINT64_C(0x6AC690C5);

enum class SourceTriggerKind : std::uint8_t {
    None,
    OnCast,
    WhileChanneling,
};

enum class NativeSourceTargetKind : std::uint8_t {
    None,
    Unit,
    Position,
};

enum class CombatTriggerKind : std::uint8_t {
    None,
    AttackAttempt,
    CriticalStrike,
    CrushingBlow,
    OpenWounds,
};

struct SourceSkillFilter {
    std::vector<std::int32_t> includeSkillIds;
    std::vector<std::int32_t> excludeSkillIds;
};

struct TriggerStatPair {
    std::int32_t fixedStatId{};
    std::int32_t sameLevelStatId{};
};

struct ChannelingConfig : SourceSkillFilter {
    bool enabled{true};
    std::uint32_t intervalFrames{DefaultChannelingIntervalFrames};
    TriggerStatPair stats;
};

struct SourceSkillTriggerConfig {
    std::string name;
    std::vector<std::int32_t> sourceSkillIds;
    TriggerStatPair stats;
};

struct CombatTriggerConfig {
    std::int32_t attackAttemptStatId{};
    std::int32_t criticalStrikeStatId{};
    std::int32_t crushingBlowStatId{};
    std::int32_t openWoundsStatId{};
};

struct Config {
    bool enabled{true};
    SourceSkillFilter onCast;
    ChannelingConfig whileChanneling;
    std::vector<SourceSkillTriggerConfig> sourceSkillTriggers;
    CombatTriggerConfig combatTriggers;
    bool diagnostics{};
};

constexpr CombatTriggerKind CombatTriggerForStatId(
        const CombatTriggerConfig& config,
        std::int32_t statId) noexcept {
    if (statId <= DisabledCombatStatId) return CombatTriggerKind::None;
    if (statId == config.attackAttemptStatId) {
        return CombatTriggerKind::AttackAttempt;
    }
    if (statId == config.criticalStrikeStatId) {
        return CombatTriggerKind::CriticalStrike;
    }
    if (statId == config.crushingBlowStatId) {
        return CombatTriggerKind::CrushingBlow;
    }
    if (statId == config.openWoundsStatId) {
        return CombatTriggerKind::OpenWounds;
    }
    return CombatTriggerKind::None;
}

constexpr bool IsCombatTriggerEnabled(
        const CombatTriggerConfig& config,
        CombatTriggerKind kind) noexcept {
    switch (kind) {
    case CombatTriggerKind::AttackAttempt:
        return config.attackAttemptStatId != DisabledCombatStatId;
    case CombatTriggerKind::CriticalStrike:
        return config.criticalStrikeStatId != DisabledCombatStatId;
    case CombatTriggerKind::CrushingBlow:
        return config.crushingBlowStatId != DisabledCombatStatId;
    case CombatTriggerKind::OpenWounds:
        return config.openWoundsStatId != DisabledCombatStatId;
    case CombatTriggerKind::None:
        return false;
    }
    return false;
}

constexpr bool IsConfiguredTriggerStat(
        const TriggerStatPair& stats,
        std::int32_t statId) noexcept {
    return statId > DisabledCombatStatId
        && (statId == stats.fixedStatId
            || statId == stats.sameLevelStatId);
}

constexpr bool HasConfiguredTriggerStat(
        const TriggerStatPair& stats) noexcept {
    return stats.fixedStatId != DisabledCombatStatId
        || stats.sameLevelStatId != DisabledCombatStatId;
}

inline bool IsSourceRuleStat(
        const Config& config,
        std::int32_t statId) noexcept {
    return std::any_of(
        config.sourceSkillTriggers.begin(),
        config.sourceSkillTriggers.end(),
        [statId](const SourceSkillTriggerConfig& rule) {
            return IsConfiguredTriggerStat(rule.stats, statId);
        });
}

inline bool IsReservedSyntheticStat(
        const Config& config,
        std::int32_t statId) noexcept {
    return CombatTriggerForStatId(config.combatTriggers, statId)
            != CombatTriggerKind::None
        || IsConfiguredTriggerStat(config.whileChanneling.stats, statId)
        || IsSourceRuleStat(config, statId);
}

inline bool ShouldExposeSyntheticStat(
        const Config& config,
        SourceTriggerKind activeSourceTrigger,
        std::int32_t activeSourceSkillId,
        CombatTriggerKind activeCombatTrigger,
        std::int32_t statId) noexcept {
    if (activeCombatTrigger != CombatTriggerKind::None) {
        return CombatTriggerForStatId(config.combatTriggers, statId)
            == activeCombatTrigger;
    }
    if (activeSourceTrigger == SourceTriggerKind::WhileChanneling) {
        return IsConfiguredTriggerStat(config.whileChanneling.stats, statId);
    }
    if (activeSourceTrigger == SourceTriggerKind::OnCast) {
        if (CombatTriggerForStatId(config.combatTriggers, statId)
                != CombatTriggerKind::None
                || IsConfiguredTriggerStat(
                    config.whileChanneling.stats,
                    statId)) {
            return false;
        }
        for (const auto& rule : config.sourceSkillTriggers) {
            if (!IsConfiguredTriggerStat(rule.stats, statId)) continue;
            return std::binary_search(
                rule.sourceSkillIds.begin(),
                rule.sourceSkillIds.end(),
                activeSourceSkillId);
        }
        return true;
    }
    return !IsReservedSyntheticStat(config, statId);
}

constexpr std::int32_t PackedEventStatId(std::int32_t packedStat) noexcept {
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(packedStat) >> 16) & 0xFFFFU);
}

constexpr bool RollNativePercent(
        std::uint32_t& seedLow,
        std::uint32_t& seedHigh,
        std::int32_t chance) noexcept {
    if (chance <= 0) return false;
    const auto next = static_cast<std::uint64_t>(seedLow)
        * NativeSeedMultiplier + seedHigh;
    seedLow = static_cast<std::uint32_t>(next);
    seedHigh = static_cast<std::uint32_t>(next >> 32);
    return static_cast<std::int32_t>(seedLow % 100U) < chance;
}

constexpr bool IsCastAnimation(
        std::uint8_t animation,
        std::uint8_t sequenceTransition) noexcept {
    return animation == PlayerModeCast
        || (animation == PlayerModeSequence
            && sequenceTransition == PlayerModeCast);
}

constexpr bool IsAttackAnimation(
        std::uint8_t animation,
        std::uint8_t sequenceTransition) noexcept {
    const auto isAttackMode = [](std::uint8_t mode) constexpr {
        return mode == PlayerModeAttack1
            || mode == PlayerModeAttack2
            || mode == PlayerModeThrow
            || mode == PlayerModeKick
            || mode == PlayerModeSpecial1
            || mode == PlayerModeSpecial2
            || mode == PlayerModeSpecial3
            || mode == PlayerModeSpecial4;
    };
    return isAttackMode(animation)
        || (animation == PlayerModeSequence
            && isAttackMode(sequenceTransition));
}

// Both governed player-skill input executors return zero after accepting and
// finalizing an input. Nonzero values are early rejection/error results.
constexpr bool IsAcceptedPlayerSkillInput(std::int32_t result) noexcept {
    return result == 0;
}

constexpr bool IsChannelingCastAnimation(
        std::uint8_t animation,
        std::uint8_t sequenceTransition) noexcept {
    return animation == PlayerModeCast
        || (animation == PlayerModeSequence
            && (sequenceTransition == PlayerModeCast
                || sequenceTransition == PlayerModeSequence));
}

constexpr bool IsEligibleSkillRecord(
        std::uint8_t animation,
        std::uint8_t sequenceTransition,
        std::uint64_t flags) noexcept {
    return IsCastAnimation(animation, sequenceTransition)
        && (flags & SkillFlagRepeat) == 0;
}

constexpr bool IsEligibleChannelingSkillRecord(
        std::uint8_t animation,
        std::uint8_t sequenceTransition,
        std::uint64_t flags) noexcept {
    return IsChannelingCastAnimation(animation, sequenceTransition)
        && (flags & SkillFlagRepeat) != 0;
}

constexpr SourceTriggerKind ClassifySourceSkillRecord(
        std::uint8_t animation,
        std::uint8_t sequenceTransition,
        std::uint64_t flags) noexcept {
    if ((flags & SkillFlagRepeat) != 0) {
        return IsChannelingCastAnimation(animation, sequenceTransition)
            ? SourceTriggerKind::WhileChanneling
            : SourceTriggerKind::None;
    }
    return IsCastAnimation(animation, sequenceTransition)
        ? SourceTriggerKind::OnCast
        : SourceTriggerKind::None;
}

constexpr bool HasChannelingIntervalElapsed(
        std::uint32_t currentFrame,
        std::uint32_t previousDispatchFrame,
        std::uint32_t intervalFrames) noexcept {
    return intervalFrames >= MinimumChannelingIntervalFrames
        && static_cast<std::uint32_t>(
            currentFrame - previousDispatchFrame) >= intervalFrames;
}

constexpr bool IsPendingInputFresh(
        std::uint32_t currentFrame,
        std::uint32_t capturedFrame,
        bool retainedForChannel) noexcept {
    return retainedForChannel
        || static_cast<std::uint32_t>(currentFrame - capturedFrame)
            <= MaximumPendingInputAgeFrames;
}

inline bool Contains(
        const std::vector<std::int32_t>& values,
        std::int32_t value) noexcept {
    return std::binary_search(values.begin(), values.end(), value);
}

inline bool IsConfiguredSourceSkill(
        const Config& config,
        std::int32_t skillId) noexcept {
    if (!config.onCast.includeSkillIds.empty()
            && !Contains(config.onCast.includeSkillIds, skillId)) {
        return false;
    }
    return !Contains(config.onCast.excludeSkillIds, skillId);
}

inline bool IsConfiguredChannelingSourceSkill(
        const Config& config,
        std::int32_t skillId) noexcept {
    if (!config.whileChanneling.enabled
            || !HasConfiguredTriggerStat(config.whileChanneling.stats)) {
        return false;
    }
    if (!config.whileChanneling.includeSkillIds.empty()
            && !Contains(
                config.whileChanneling.includeSkillIds,
                skillId)) {
        return false;
    }
    return !Contains(config.whileChanneling.excludeSkillIds, skillId);
}

constexpr bool IsManualPlayerCast(
        std::int32_t nativeResult,
        std::int32_t consumeResources,
        std::int32_t itemCast,
        std::int32_t itemEffect,
        std::int32_t unitType) noexcept {
    return nativeResult != 0
        && consumeResources == 1
        && itemCast == 0
        && itemEffect == 0
        && unitType == PlayerUnitType;
}

constexpr bool ShouldResolveTriggeredSkillTarget(
        bool casterIsSource,
        bool targetIsSource,
        std::int32_t itemTargetFlag) noexcept {
    return casterIsSource
        && targetIsSource
        && itemTargetFlag == NativeEventEffectTargetFlag;
}

constexpr bool ShouldUseNativeUnitTarget(
        bool resolvingTarget,
        NativeSourceTargetKind targetKind) noexcept {
    return resolvingTarget && targetKind == NativeSourceTargetKind::Unit;
}

constexpr bool ShouldUseNativePositionTarget(
        bool resolvingTarget,
        NativeSourceTargetKind targetKind) noexcept {
    return resolvingTarget && targetKind == NativeSourceTargetKind::Position;
}

constexpr bool ShouldPreferPlayerInputTarget(
        bool descriptorMatchesSource,
        NativeSourceTargetKind inputTargetKind) noexcept {
    return descriptorMatchesSource
        && inputTargetKind != NativeSourceTargetKind::None;
}

inline std::vector<std::filesystem::path> BuildConfigCandidates(
        const std::vector<std::filesystem::path>& directories,
        const std::filesystem::path& fileName) {
    std::vector<std::filesystem::path> result;
    for (const auto& directory : directories) {
        if (directory.empty()) continue;
        const auto candidate = (directory / fileName).lexically_normal();
        if (std::find(result.begin(), result.end(), candidate) == result.end()) {
            result.emplace_back(candidate);
        }
    }
    return result;
}

inline bool ReadSkillIdArray(
        const toml::table& table,
        std::string_view section,
        const char* key,
        std::vector<std::int32_t>& destination,
        std::string& error) {
    const auto* node = table.get(key);
    if (!node) return true;
    const auto* array = node->as_array();
    if (!array) {
        error = std::string(section) + "." + key
            + " must be an integer array";
        return false;
    }
    destination.clear();
    for (const auto& entry : *array) {
        const auto value = entry.value<std::int64_t>();
        if (!value || *value < MinimumSkillId
                || *value > MaximumSkillId) {
            error = std::string(section) + "." + key
                + " entries must be integers from 0 to 65535";
            return false;
        }
        destination.emplace_back(static_cast<std::int32_t>(*value));
    }
    std::sort(destination.begin(), destination.end());
    destination.erase(
        std::unique(destination.begin(), destination.end()),
        destination.end());
    return true;
}

inline bool ReadTriggerStatId(
        const toml::table& table,
        std::string_view section,
        const char* key,
        std::int32_t& destination,
        std::string& error) {
    const auto* node = table.get(key);
    if (!node) return true;
    const auto value = node->value<std::int64_t>();
    if (!value || *value < DisabledCombatStatId
            || *value > MaximumCombatStatId) {
        error = std::string(section) + "." + key
            + " must be an integer from 0 to 65535";
        return false;
    }
    destination = static_cast<std::int32_t>(*value);
    return true;
}

constexpr bool HasDistinctCombatStatIds(
        const CombatTriggerConfig& config) noexcept {
    const std::array ids{
        config.attackAttemptStatId,
        config.criticalStrikeStatId,
        config.crushingBlowStatId,
        config.openWoundsStatId,
    };
    for (std::size_t left = 0; left < ids.size(); ++left) {
        if (ids[left] == DisabledCombatStatId) continue;
        for (std::size_t right = left + 1; right < ids.size(); ++right) {
            if (ids[left] == ids[right]) return false;
        }
    }
    return true;
}

inline bool HasDistinctSyntheticStatIds(const Config& config) noexcept {
    std::vector<std::int32_t> ids{
        config.whileChanneling.stats.fixedStatId,
        config.whileChanneling.stats.sameLevelStatId,
        config.combatTriggers.attackAttemptStatId,
        config.combatTriggers.criticalStrikeStatId,
        config.combatTriggers.crushingBlowStatId,
        config.combatTriggers.openWoundsStatId,
    };
    for (const auto& rule : config.sourceSkillTriggers) {
        ids.emplace_back(rule.stats.fixedStatId);
        ids.emplace_back(rule.stats.sameLevelStatId);
    }
    std::erase(ids, DisabledCombatStatId);
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

inline bool ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) {
    try {
        const auto root = toml::parse(input);
        for (const auto& [key, value] : root) {
            (void)value;
            if (key != "enabled" && key != "on_cast"
                    && key != "while_channeling"
                    && key != "source_skill_triggers"
                    && key != "combat_triggers"
                    && key != "diagnostics") {
                error = "unknown top-level setting or section: "
                    + std::string(key.str());
                return false;
            }
        }

        Config parsed{};
        if (const auto* enabledNode = root.get("enabled")) {
            if (!enabledNode->is_boolean()) {
                error = "enabled must be true or false";
                return false;
            }
            const auto enabled = enabledNode->value<bool>();
            if (!enabled) {
                error = "enabled must be true or false";
                return false;
            }
            parsed.enabled = *enabled;
        }

        if (const auto* onCastNode = root.get("on_cast")) {
            const auto* onCast = onCastNode->as_table();
            if (!onCast) {
                error = "on_cast must be a TOML table";
                return false;
            }
            for (const auto& [key, value] : *onCast) {
                (void)value;
                if (key != "include_skill_ids"
                        && key != "exclude_skill_ids") {
                    error = "unknown setting: on_cast."
                        + std::string(key.str());
                    return false;
                }
            }
            if (!ReadSkillIdArray(
                    *onCast,
                    "on_cast",
                    "include_skill_ids",
                    parsed.onCast.includeSkillIds,
                    error)
                    || !ReadSkillIdArray(
                        *onCast,
                        "on_cast",
                        "exclude_skill_ids",
                        parsed.onCast.excludeSkillIds,
                        error)) {
                return false;
            }
        }

        if (const auto* channelingNode = root.get("while_channeling")) {
            const auto* channeling = channelingNode->as_table();
            if (!channeling) {
                error = "while_channeling must be a TOML table";
                return false;
            }
            for (const auto& [key, value] : *channeling) {
                (void)value;
                if (key != "enabled" && key != "interval_frames"
                        && key != "include_skill_ids"
                        && key != "exclude_skill_ids"
                        && key != "fixed_stat_id"
                        && key != "same_level_stat_id") {
                    error = "unknown setting: while_channeling."
                        + std::string(key.str());
                    return false;
                }
            }
            if (const auto* node = channeling->get("enabled")) {
                if (!node->is_boolean()) {
                    error = "while_channeling.enabled must be true or false";
                    return false;
                }
                const auto enabled = node->value<bool>();
                if (!enabled) {
                    error = "while_channeling.enabled must be true or false";
                    return false;
                }
                parsed.whileChanneling.enabled = *enabled;
            }
            if (const auto* node = channeling->get("interval_frames")) {
                const auto interval = node->value<std::int64_t>();
                if (!interval
                        || *interval < MinimumChannelingIntervalFrames
                        || *interval > MaximumChannelingIntervalFrames) {
                    error = "while_channeling.interval_frames must be an "
                        "integer from 1 to 65535";
                    return false;
                }
                parsed.whileChanneling.intervalFrames =
                    static_cast<std::uint32_t>(*interval);
            }
            if (!ReadSkillIdArray(
                    *channeling,
                    "while_channeling",
                    "include_skill_ids",
                    parsed.whileChanneling.includeSkillIds,
                    error)
                    || !ReadSkillIdArray(
                        *channeling,
                        "while_channeling",
                        "exclude_skill_ids",
                        parsed.whileChanneling.excludeSkillIds,
                        error)) {
                return false;
            }
            if (!ReadTriggerStatId(
                    *channeling,
                    "while_channeling",
                    "fixed_stat_id",
                    parsed.whileChanneling.stats.fixedStatId,
                    error)
                    || !ReadTriggerStatId(
                        *channeling,
                        "while_channeling",
                        "same_level_stat_id",
                        parsed.whileChanneling.stats.sameLevelStatId,
                        error)) {
                return false;
            }
        }

        if (const auto* rulesNode = root.get("source_skill_triggers")) {
            const auto* rules = rulesNode->as_array();
            if (!rules) {
                error = "source_skill_triggers must be an array of TOML tables";
                return false;
            }
            for (std::size_t index = 0; index < rules->size(); ++index) {
                const auto* ruleTable = rules->get(index)->as_table();
                const auto section = std::string("source_skill_triggers[")
                    + std::to_string(index) + "]";
                if (!ruleTable) {
                    error = section + " must be a TOML table";
                    return false;
                }
                for (const auto& [key, value] : *ruleTable) {
                    (void)value;
                    if (key != "name" && key != "source_skill_ids"
                            && key != "fixed_stat_id"
                            && key != "same_level_stat_id") {
                        error = "unknown setting: " + section + "."
                            + std::string(key.str());
                        return false;
                    }
                }

                SourceSkillTriggerConfig rule{};
                const auto* nameNode = ruleTable->get("name");
                const auto name = nameNode
                    ? nameNode->value<std::string>()
                    : std::optional<std::string>{};
                if (!name || name->empty()) {
                    error = section + ".name must be a non-empty string";
                    return false;
                }
                if (std::any_of(
                        parsed.sourceSkillTriggers.begin(),
                        parsed.sourceSkillTriggers.end(),
                        [&name](const SourceSkillTriggerConfig& existing) {
                            return existing.name == *name;
                        })) {
                    error = section + ".name must be unique";
                    return false;
                }
                rule.name = *name;
                if (!ReadSkillIdArray(
                        *ruleTable,
                        section,
                        "source_skill_ids",
                        rule.sourceSkillIds,
                        error)) {
                    return false;
                }
                if (rule.sourceSkillIds.empty()) {
                    error = section
                        + ".source_skill_ids must contain at least one skill ID";
                    return false;
                }
                if (!ReadTriggerStatId(
                        *ruleTable,
                        section,
                        "fixed_stat_id",
                        rule.stats.fixedStatId,
                        error)
                        || !ReadTriggerStatId(
                            *ruleTable,
                            section,
                            "same_level_stat_id",
                            rule.stats.sameLevelStatId,
                            error)) {
                    return false;
                }
                if (rule.stats.fixedStatId == DisabledCombatStatId
                        && rule.stats.sameLevelStatId
                            == DisabledCombatStatId) {
                    error = section
                        + " must configure at least one nonzero stat ID";
                    return false;
                }
                parsed.sourceSkillTriggers.emplace_back(std::move(rule));
            }
        }

        if (const auto* combatNode = root.get("combat_triggers")) {
            const auto* combat = combatNode->as_table();
            if (!combat) {
                error = "combat_triggers must be a TOML table";
                return false;
            }
            for (const auto& [key, value] : *combat) {
                (void)value;
                if (key != "critical_strike_stat_id"
                        && key != "attack_attempt_stat_id"
                        && key != "crushing_blow_stat_id"
                        && key != "open_wounds_stat_id") {
                    error = "unknown setting: combat_triggers."
                        + std::string(key.str());
                    return false;
                }
            }
            if (!ReadTriggerStatId(
                    *combat,
                    "combat_triggers",
                    "attack_attempt_stat_id",
                    parsed.combatTriggers.attackAttemptStatId,
                    error)
                    || !ReadTriggerStatId(
                    *combat,
                    "combat_triggers",
                    "critical_strike_stat_id",
                    parsed.combatTriggers.criticalStrikeStatId,
                    error)
                    || !ReadTriggerStatId(
                        *combat,
                        "combat_triggers",
                        "crushing_blow_stat_id",
                        parsed.combatTriggers.crushingBlowStatId,
                        error)
                    || !ReadTriggerStatId(
                        *combat,
                        "combat_triggers",
                        "open_wounds_stat_id",
                        parsed.combatTriggers.openWoundsStatId,
                        error)) {
                return false;
            }
            if (!HasDistinctCombatStatIds(parsed.combatTriggers)) {
                error = "combat_triggers stat IDs must be distinct when nonzero";
                return false;
            }
        }

        if (!HasDistinctSyntheticStatIds(parsed)) {
            error = "all nonzero synthetic trigger stat IDs must be distinct";
            return false;
        }

        if (const auto* diagnosticsNode = root.get("diagnostics")) {
            const auto* diagnostics = diagnosticsNode->as_table();
            if (!diagnostics) {
                error = "diagnostics must be a TOML table";
                return false;
            }
            for (const auto& [key, value] : *diagnostics) {
                (void)value;
                if (key != "enabled") {
                    error = "unknown setting: diagnostics."
                        + std::string(key.str());
                    return false;
                }
            }
            if (const auto* node = diagnostics->get("enabled")) {
                if (!node->is_boolean()) {
                    error = "diagnostics.enabled must be true or false";
                    return false;
                }
                const auto enabled = node->value<bool>();
                if (!enabled) {
                    error = "diagnostics.enabled must be true or false";
                    return false;
                }
                parsed.diagnostics = *enabled;
            }
        }

        result = std::move(parsed);
        error.clear();
        return true;
    } catch (const toml::parse_error& exception) {
        error = exception.description();
        return false;
    }
}

} // namespace ruffneckk::cast_triggers
