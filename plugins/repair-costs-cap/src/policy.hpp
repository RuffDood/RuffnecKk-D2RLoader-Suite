#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace RuffnecKk::RepairCostsCap {

inline constexpr std::int32_t RepairTransactionType = 3;
inline constexpr std::int64_t MaximumGoldLimit =
    std::numeric_limits<std::int32_t>::max();
inline constexpr std::uint32_t ChanceBasisPointScale = 10'000;

struct RepairPolicy {
    bool pluginEnabled{true};
    bool enabled{};
    std::int32_t maximumGold{std::numeric_limits<std::int32_t>::max()};
    bool durabilityWearEnabled{};
    double durabilityWearChance{};
    bool diagnosticsEnabled{};
};

inline auto IsValidMaximumGold(std::int64_t value) noexcept -> bool {
    return value >= 0 && value <= MaximumGoldLimit;
}

inline auto IsValidChance(double value) noexcept -> bool {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

inline auto ChanceToBasisPoints(double chance) noexcept -> std::uint32_t {
    if (!IsValidChance(chance)) return 0;
    return static_cast<std::uint32_t>(std::llround(
        chance * static_cast<double>(ChanceBasisPointScale)));
}

inline auto IsValidPolicy(const RepairPolicy& policy) noexcept -> bool {
    return IsValidMaximumGold(policy.maximumGold)
        && IsValidChance(policy.durabilityWearChance);
}

inline auto IsPhysicalRepairCandidate(
    std::int32_t durabilityBeforeRepair,
    std::int32_t maximumDurabilityBeforeRepair
) noexcept -> bool {
    return maximumDurabilityBeforeRepair > 1
        && durabilityBeforeRepair >= 0
        && durabilityBeforeRepair < maximumDurabilityBeforeRepair;
}

inline auto DidPhysicalRepairSucceed(
    std::int32_t durabilityBeforeRepair,
    std::int32_t maximumDurabilityBeforeRepair,
    std::int32_t durabilityAfterRepair
) noexcept -> bool {
    return IsPhysicalRepairCandidate(
            durabilityBeforeRepair, maximumDurabilityBeforeRepair)
        && durabilityAfterRepair >= maximumDurabilityBeforeRepair;
}

inline auto ShouldLoseMaximumDurability(
    bool enabled,
    double chance,
    std::uint32_t roll
) noexcept -> bool {
    if (!enabled || !IsValidChance(chance)
        || roll >= ChanceBasisPointScale) {
        return false;
    }
    return roll < ChanceToBasisPoints(chance);
}

inline auto ReducedMaximumDurability(
    std::int32_t maximumDurability
) noexcept -> std::int32_t {
    return maximumDurability > 1
        ? maximumDurability - 1
        : maximumDurability;
}

inline auto ApplyRepairCostCap(
    std::int32_t vanillaCost,
    std::int32_t transactionType,
    const RepairPolicy& policy
) noexcept -> std::int32_t {
    if (!policy.enabled || transactionType != RepairTransactionType) {
        return vanillaCost;
    }
    if (!IsValidPolicy(policy)) return vanillaCost;
    if (vanillaCost <= 0
        || vanillaCost == std::numeric_limits<std::int32_t>::max()) {
        return vanillaCost;
    }
    return std::min(vanillaCost, policy.maximumGold);
}

inline auto ApplyRepairAllCap(
    std::int32_t adjustedItemTotal,
    const RepairPolicy& policy
) noexcept -> std::int32_t {
    if (!policy.enabled || !IsValidPolicy(policy)) return adjustedItemTotal;
    if (adjustedItemTotal <= 0
        || adjustedItemTotal == std::numeric_limits<std::int32_t>::max()) {
        return adjustedItemTotal;
    }
    return std::min(adjustedItemTotal, policy.maximumGold);
}

inline auto GoldReduction(
    std::int32_t before,
    std::int32_t after
) noexcept -> std::uint64_t {
    if (before <= after || before <= 0 || after < 0) return 0;
    return static_cast<std::uint64_t>(
        static_cast<std::int64_t>(before) - static_cast<std::int64_t>(after));
}

inline auto Trim(std::string_view value) noexcept -> std::string_view {
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

inline auto WithoutComment(std::string_view value) noexcept -> std::string_view {
    const auto comment = value.find('#');
    return comment == std::string_view::npos
        ? value
        : value.substr(0, comment);
}

inline auto ParseBoolean(std::string_view value, bool& output) noexcept -> bool {
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

inline auto ParseMaximumGold(
    std::string_view value,
    std::int32_t& output
) noexcept -> bool {
    if (value.empty()) return false;
    std::int64_t parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{}
        || result.ptr != value.data() + value.size()
        || !IsValidMaximumGold(parsed)) {
        return false;
    }
    output = static_cast<std::int32_t>(parsed);
    return true;
}

inline auto ParseChance(std::string_view value, double& output) noexcept -> bool {
    if (value.empty()) return false;
    double parsed{};
    const auto result = std::from_chars(
        value.data(),
        value.data() + value.size(),
        parsed,
        std::chars_format::general);
    if (result.ec != std::errc{}
        || result.ptr != value.data() + value.size()
        || !IsValidChance(parsed)) {
        return false;
    }
    output = parsed;
    return true;
}

inline auto SetError(
    std::string& error,
    std::size_t line,
    std::string_view message
) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

inline auto ParseConfig(
    std::string_view input,
    RepairPolicy& output,
    std::string& error
) -> bool {
    RepairPolicy parsed{};
    std::string section;
    bool pluginSection{};
    bool repairSection{};
    bool wearSection{};
    bool diagnosticsSection{};
    bool pluginEnabled{};
    bool repairEnabled{};
    bool maximumGold{};
    bool wearEnabled{};
    bool chance{};
    bool diagnosticsEnabled{};

    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = Trim(WithoutComment(input.substr(
            start,
            end == std::string_view::npos ? input.size() - start : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']'
                || line.find('[', 1) != std::string_view::npos
                || line.find(']') != line.size() - 1) {
                return SetError(error, lineNumber, "invalid section header");
            }
            section.assign(Trim(line.substr(1, line.size() - 2)));
            bool* seen{};
            if (section == "plugin") seen = &pluginSection;
            else if (section == "repair_costs") seen = &repairSection;
            else if (section == "durability_wear") seen = &wearSection;
            else if (section == "diagnostics") seen = &diagnosticsSection;
            else return SetError(error, lineNumber, "unknown section");
            if (*seen) return SetError(error, lineNumber, "duplicate section");
            *seen = true;
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string_view::npos
            || line.find('=', equal + 1) != std::string_view::npos) {
            return SetError(error, lineNumber, "expected one key/value assignment");
        }
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty() || section.empty()) {
            return SetError(error, lineNumber, "invalid key/value assignment");
        }

        bool valid{};
        if (section == "plugin" && key == "enabled") {
            if (pluginEnabled) return SetError(error, lineNumber, "duplicate setting");
            pluginEnabled = true;
            valid = ParseBoolean(value, parsed.pluginEnabled);
        } else if (section == "repair_costs" && key == "enabled") {
            if (repairEnabled) return SetError(error, lineNumber, "duplicate setting");
            repairEnabled = true;
            valid = ParseBoolean(value, parsed.enabled);
        } else if (section == "repair_costs" && key == "maximum_gold") {
            if (maximumGold) return SetError(error, lineNumber, "duplicate setting");
            maximumGold = true;
            valid = ParseMaximumGold(value, parsed.maximumGold);
        } else if (section == "durability_wear" && key == "enabled") {
            if (wearEnabled) return SetError(error, lineNumber, "duplicate setting");
            wearEnabled = true;
            valid = ParseBoolean(value, parsed.durabilityWearEnabled);
        } else if (section == "durability_wear" && key == "chance") {
            if (chance) return SetError(error, lineNumber, "duplicate setting");
            chance = true;
            valid = ParseChance(value, parsed.durabilityWearChance);
        } else if (section == "diagnostics" && key == "enabled") {
            if (diagnosticsEnabled) {
                return SetError(error, lineNumber, "duplicate setting");
            }
            diagnosticsEnabled = true;
            valid = ParseBoolean(value, parsed.diagnosticsEnabled);
        } else {
            return SetError(error, lineNumber, "unknown setting");
        }
        if (!valid) return SetError(error, lineNumber, "invalid setting value");
    }

    if (!(repairSection && wearSection && repairEnabled
            && maximumGold && wearEnabled && chance)
        || pluginSection != pluginEnabled
        || diagnosticsSection != diagnosticsEnabled) {
        error = "one or more required settings are missing";
        return false;
    }
    output = parsed;
    error.clear();
    return true;
}

} // namespace RuffnecKk::RepairCostsCap
