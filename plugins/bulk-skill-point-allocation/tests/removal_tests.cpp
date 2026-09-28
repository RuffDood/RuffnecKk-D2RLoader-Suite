#include "removal_settings.hpp"
#include <chrono>
#include <iostream>

namespace R = RuffnecKk::BulkSkillPointAllocation::Removal;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed at line " << __LINE__ << ": " #condition "\n"; return 1; \
} } while (false)

template<class Fn> bool Refuses(Fn fn) {
    try { fn(); } catch (const std::exception&) { return true; }
    return false;
}

struct TemporaryFiles {
    std::filesystem::path path;
    TemporaryFiles() : path(std::filesystem::temp_directory_path()
        / ("bulk-skill-removal-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directory(path);
    }
    ~TemporaryFiles() {
        // Remove only the individually known test files, never a recursive tree.
        std::error_code error;
        for (const auto* name : {"mod.toml", "global.toml", "legacy.json"})
            std::filesystem::remove(path / name, error);
        std::filesystem::remove(path, error);
    }
    auto Write(const char* name, const char* text) {
        const auto file = path / name;
        std::ofstream output(file, std::ios::binary);
        output << text;
        if (!output) throw std::runtime_error("fixture write failed");
        return file;
    }
};

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto defaults = R::LoadConfiguration({argv[1]}, {});
    CHECK(defaults.allocation.enabled);
    CHECK(defaults.allocation.skillPointsPerCtrlClick == 5);
    CHECK(defaults.removal.batchPoints == 5);
    CHECK(R::Match(defaults.removal, R::Button::Right, 0) == R::Action::Single);
    CHECK(R::Match(defaults.removal, R::Button::Right, 1) == R::Action::Batch);
    CHECK(R::Match(defaults.removal, R::Button::Right, 2) == R::Action::All);
    CHECK(!R::Match(defaults.removal, R::Button::Right, 3));
    CHECK(!R::Match(defaults.removal, R::Button::Middle, 0));
    const auto custom = R::ParseToml(R"(
enabled = true
skillPointsPerCtrlClick = 9
confirmShiftAllocation = true
[refund]
single_removal = "Alt+MiddleClick"
batch_removal = "Mouse4"
all_removal = "Shift+Mouse5"
batch_points = 12
)");
    CHECK(custom.allocation.skillPointsPerCtrlClick == 9);
    CHECK(custom.allocation.confirmShiftAllocation);
    CHECK(custom.removal.batchPoints == 12);
    CHECK(R::Match(custom.removal, R::Button::Middle, 4) == R::Action::Single);
    CHECK(R::Match(custom.removal, R::Button::Mouse4, 0) == R::Action::Batch);
    CHECK(R::Match(custom.removal, R::Button::Mouse5, 2) == R::Action::All);
    for (const char* bad : {
        "[refund]\nsingle_control = 'RightClick'",
        "[refund]\nsingle_removal = 'Ctrl+RightClick'",
        "[refund]\nsingle_removal = 'Ctrl+Shift+RightClick'",
        "[refund]\nsingle_removal = 'LeftClick'",
        "[refund]\nsingle_removal = 4",
        "[refund]\nbatch_points = 0",
        "[refund]\nbatch_points = 1001",
        "[refund]\nbatch_points = 5.0",
        "[refund]\nbatch_points = '5'",
        "refund = false",
        "enabled = 'true'",
        "enabled = true\nenabled = false",
        "unknown = 1",
        "[refund]\nall_removal = 'Unknown'"})
        CHECK(Refuses([&] { R::ParseToml(bad); }));

    std::array<R::Skill, 3> skills{{{1, 8}, {2, 0, {1, -1, -1}}, {3, 2}}};
    auto single = R::MakePlan(skills, 1, R::Action::Single, 5);
    CHECK(single && single->remainingRanks == 7 && single->removedRanks == 1);
    auto batch = R::MakePlan(skills, 1, R::Action::Batch, 5);
    CHECK(batch && batch->remainingRanks == 3 && batch->removedRanks == 5);
    auto all = R::MakePlan(skills, 1, R::Action::All, 5);
    CHECK(all && all->remainingRanks == 0 && all->removedRanks == 8);
    const auto shortBatch = R::MakePlan(skills, 3, R::Action::Batch, 5);
    CHECK(shortBatch && shortBatch->removedRanks == 2);
    CHECK(!R::MakePlan(skills, 2, R::Action::Single, 5));
    CHECK(!R::MakePlan(skills, 999, R::Action::All, 5));
    CHECK(R::StillApplicable(*all, skills));
    skills[1].investedRanks = 1;
    CHECK(!R::StillApplicable(*all, skills));
    all = R::MakePlan(skills, 1, R::Action::All, 5);
    CHECK(all && all->remainingRanks == 1 && all->removedRanks == 7);
    skills[0].investedRanks = 1;
    CHECK(!R::StillApplicable(*single, skills));
    CHECK(!R::MakePlan(skills, 1, R::Action::Single, 5));
    skills[1].investedRanks = 0;
    CHECK(R::MakePlan(skills, 1, R::Action::All, 5)->removedRanks == 1);
    skills[2].prerequisites[0] = 999;
    CHECK(!R::MakePlan(skills, 1, R::Action::Single, 5));
    skills[2].prerequisites[0] = -1;
    skills[2].id = 1;
    CHECK(!R::MakePlan(skills, 1, R::Action::Single, 5));

    const R::Plan twoRanks{3, 4, 2, 2};
    const std::array<R::RankCost, 2> modCosts{{{4, 4}, {3, 3}}};
    const auto refund = R::WithCosts(twoRanks, 0, modCosts);
    CHECK(refund && refund->returnedPoints == 7 && refund->resultingPool == 7);
    CHECK(!R::WithCosts(twoRanks, 0, {}));
    CHECK(!R::WithCosts(twoRanks, (std::numeric_limits<int>::max)(), modCosts));
    CHECK(!R::WithCosts(twoRanks, -1, modCosts));
    const std::array<R::RankCost, 2> negative{{{4, -1}, {3, 3}}};
    const std::array<R::RankCost, 2> duplicate{{{4, 4}, {4, 3}}};
    const std::array<R::RankCost, 2> free{{{4, 0}, {3, 0}}};
    CHECK(!R::WithCosts(twoRanks, 0, negative));
    CHECK(!R::WithCosts(twoRanks, 0, duplicate));
    CHECK(R::WithCosts(twoRanks, 2, free)->resultingPool == 2);
    CHECK(!R::WithCosts(R::Plan{3, 4, 2, 100}, 0, modCosts));

    // User regression: batch/all stop at the last required invested rank.
    for (const auto action : {R::Action::Single, R::Action::Batch, R::Action::All}) {
        std::array<R::Skill, 2> tree{{{1, 2}, {2, 1, {1, -1, -1}}}};
        const auto protectedPlan = R::MakePlan(tree, 1, action, 5);
        CHECK(protectedPlan && protectedPlan->removedRanks == 1 && protectedPlan->remainingRanks == 1);
        tree[0].investedRanks = 1;
        CHECK(!R::MakePlan(tree, 1, action, 5));
        tree[1].investedRanks = 0;
        CHECK(R::MakePlan(tree, 1, action, 5)->remainingRanks == 0);
    }

    int scenarios = 0;
    for (int parent = 0; parent <= 20; ++parent) {
        for (int child = 0; child <= 20; ++child) {
            const std::array<R::Skill, 2> tree{{
                {1, parent}, {2, child, {1, -1, -1}}}};
            for (const auto action : {R::Action::Single, R::Action::Batch, R::Action::All}) {
                ++scenarios;
                const auto plan = R::MakePlan(tree, 1, action, 5);
                if (!plan) {
                    CHECK(parent == 0 || (parent == 1 && child > 0));
                    continue;
                }
                CHECK(plan->remainingRanks + plan->removedRanks == parent);
                CHECK(plan->remainingRanks >= (child > 0 ? 1 : 0));
                CHECK(plan->removedRanks > 0 && plan->removedRanks <= parent);
                CHECK(R::StillApplicable(*plan, tree));
                if (action == R::Action::Single) CHECK(plan->removedRanks == 1);
                if (action == R::Action::Batch) CHECK(plan->removedRanks <= 5);
                std::vector<R::RankCost> costs;
                int expectedCredit = 0;
                for (int rank = parent; rank > plan->remainingRanks; --rank) {
                    costs.push_back({rank, rank * 2});
                    expectedCredit += rank * 2;
                }
                const auto priced = R::WithCosts(*plan, 3, costs);
                CHECK(priced && priced->returnedPoints == expectedCredit);
                CHECK(priced->resultingPool == 3 + expectedCredit);
            }
        }
    }

    TemporaryFiles temporary;
    const auto legacy = temporary.Write("legacy.json",
        "{\"enabled\":true,\"skillPointsPerCtrlClick\":17,\"confirmShiftAllocation\":true}");
    auto loaded = R::LoadConfiguration({}, {legacy});
    CHECK(loaded.source == legacy && loaded.allocation.skillPointsPerCtrlClick == 17);
    CHECK(loaded.allocation.confirmShiftAllocation && loaded.removal.batchPoints == 5);
    const auto global = temporary.Write("global.toml", "enabled = true\nskillPointsPerCtrlClick = 11");
    const auto mod = temporary.Write("mod.toml", "enabled = true\nskillPointsPerCtrlClick = 7");
    loaded = R::LoadConfiguration({mod, global}, {legacy});
    CHECK(loaded.source == mod && loaded.allocation.skillPointsPerCtrlClick == 7);
    temporary.Write("mod.toml", "[refund]\nbatch_points = -1");
    CHECK(Refuses([&] { R::LoadConfiguration({mod, global}, {legacy}); }));
    std::filesystem::remove(mod);
    loaded = R::LoadConfiguration({mod, global}, {legacy});
    CHECK(loaded.source == global && loaded.allocation.skillPointsPerCtrlClick == 11);
    CHECK(!R::LoadConfiguration({}, {}).source);
    std::cout << "Bindings, prerequisite limits, supplied mod costs, stale state and migration passed; "
        << scenarios << " prerequisite scenarios.\n";
}
