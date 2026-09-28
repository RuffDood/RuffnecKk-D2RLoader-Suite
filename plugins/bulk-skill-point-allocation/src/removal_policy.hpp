#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace RuffnecKk::BulkSkillPointAllocation::Removal {

enum class Button { Right, Middle, Mouse4, Mouse5 };
enum class Modifier : unsigned { None = 0, Control = 1, Shift = 2, Alt = 4 };
enum class Action { Single, Batch, All };

struct Binding {
    Button button;
    Modifier modifier;
    bool operator==(const Binding&) const = default;
};

inline Binding ParseBinding(std::string_view text) {
    // Exact chords prevent Ctrl+Shift from accidentally selecting "all".
    Modifier modifier = Modifier::None;
    const auto separator = text.find('+');
    if (separator != std::string_view::npos) {
        const auto prefix = text.substr(0, separator);
        if (prefix == "Ctrl") modifier = Modifier::Control;
        else if (prefix == "Shift") modifier = Modifier::Shift;
        else if (prefix == "Alt") modifier = Modifier::Alt;
        else throw std::invalid_argument("removal modifier must be Ctrl, Shift or Alt");
        text.remove_prefix(separator + 1);
    }
    Button button;
    if (text == "RightClick") button = Button::Right;
    else if (text == "MiddleClick") button = Button::Middle;
    else if (text == "Mouse4") button = Button::Mouse4;
    else if (text == "Mouse5") button = Button::Mouse5;
    else throw std::invalid_argument(
        "removal button must be RightClick, MiddleClick, Mouse4 or Mouse5");
    return {button, modifier};
}

struct Settings {
    Binding single{Button::Right, Modifier::None};
    Binding batch{Button::Right, Modifier::Control};
    Binding all{Button::Right, Modifier::Shift};
    int batchPoints = 5;
};

inline void Validate(const Settings& settings) {
    if (settings.batchPoints < 1 || settings.batchPoints > 1000)
        throw std::invalid_argument("refund.batch_points must be 1 through 1000");
    if (settings.single == settings.batch || settings.single == settings.all
        || settings.batch == settings.all)
        throw std::invalid_argument("refund removal controls must be different");
}

inline std::optional<Action> Match(
    const Settings& settings, Button button, unsigned modifiers) {
    const Binding input{button, static_cast<Modifier>(modifiers)};
    if (input == settings.single) return Action::Single;
    if (input == settings.batch) return Action::Batch;
    if (input == settings.all) return Action::All;
    return std::nullopt;
}

struct Skill {
    int id;
    int investedRanks;
    // Native adapters must supply the complete class skill list and base ranks.
    // Item bonuses, charged skills and general skills do not belong here.
    std::array<int, 3> prerequisites{-1, -1, -1};
};

struct Plan {
    int skillId;
    int expectedRanks;
    int remainingRanks;
    int removedRanks;
};

inline std::optional<Plan> MakePlan(
    std::span<const Skill> skills, int skillId, Action action, int batchPoints) {
    if (batchPoints < 1 || batchPoints > 1000) return std::nullopt;
    std::unordered_set<int> ids;
    const Skill* selected = nullptr;
    for (const auto& skill : skills) {
        if (skill.id < 0 || skill.investedRanks < 0
            || !ids.insert(skill.id).second) return std::nullopt;
        if (skill.id == skillId) selected = &skill;
    }
    if (!selected || selected->investedRanks == 0) return std::nullopt;
    int minimum = 0;
    for (const auto& skill : skills) {
        for (const int prerequisite : skill.prerequisites) {
            if (prerequisite < -1 || prerequisite == skill.id
                || (prerequisite >= 0 && !ids.contains(prerequisite)))
                return std::nullopt;
            if (skill.investedRanks > 0 && prerequisite == skillId) minimum = 1;
        }
    }
    int requested;
    switch (action) {
    case Action::Single: requested = 1; break;
    case Action::Batch: requested = batchPoints; break;
    case Action::All: requested = selected->investedRanks; break;
    default: return std::nullopt;
    }
    const int removed = std::min(requested, selected->investedRanks - minimum);
    if (removed <= 0) return std::nullopt;
    return Plan{skillId, selected->investedRanks,
        selected->investedRanks - removed, removed};
}

// Called again against authoritative current state immediately before mutation.
inline bool StillApplicable(const Plan& plan, std::span<const Skill> current) {
    const auto now = MakePlan(current, plan.skillId, Action::All, 1);
    return now && now->expectedRanks == plan.expectedRanks
        && plan.removedRanks > 0 && plan.removedRanks <= now->removedRanks
        && plan.remainingRanks == plan.expectedRanks - plan.removedRanks;
}

inline std::optional<int> CreditPool(int available, int returnedPoints) {
    if (available < 0 || returnedPoints < 0
        || available > (std::numeric_limits<int>::max)() - returnedPoints)
        return std::nullopt;
    return available + returnedPoints;
}

struct RankCost {
    int rank;
    int points;
};

struct RefundPlan {
    Plan ranks;
    int returnedPoints;
    int expectedPool;
    int resultingPool;
};

// Costs must come from the mod's authoritative rules or a proved spending
// record. Never infer a cost of one when that evidence is unavailable.
inline std::optional<RefundPlan> WithCosts(
    const Plan& plan, int available, std::span<const RankCost> costs) {
    if (plan.skillId < 0 || plan.expectedRanks <= 0 || plan.remainingRanks < 0
        || plan.remainingRanks >= plan.expectedRanks || plan.removedRanks <= 0
        || plan.removedRanks != plan.expectedRanks - plan.remainingRanks
        || costs.size() != static_cast<std::size_t>(plan.removedRanks))
        return std::nullopt;
    int points = 0;
    int rank = plan.expectedRanks;
    for (const auto& cost : costs) {
        if (cost.rank != rank--) return std::nullopt;
        const auto sum = CreditPool(points, cost.points);
        if (!sum) return std::nullopt;
        points = *sum;
    }
    const auto pool = CreditPool(available, points);
    if (!pool) return std::nullopt;
    return RefundPlan{plan, points, available, *pool};
}

} // namespace RuffnecKk::BulkSkillPointAllocation::Removal
