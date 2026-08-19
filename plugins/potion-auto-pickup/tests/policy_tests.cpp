#include "policy.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main(int argc, char** argv) {
    using namespace RuffnecKk::PotionAutoPickup;

    REQUIRE(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();

    Config config{};
    std::string error;
    REQUIRE(ParseConfig(stream.str(), config, error));
    REQUIRE(config.schema == ConfigSchema::PlayerFriendly);
    REQUIRE(config.enabled);
    REQUIRE(config.distance == 4);
    REQUIRE(config.interval == 1);
    REQUIRE(config.familyPriorityCount == 3);
    REQUIRE(config.familyPriority[0] == Family::Rejuvenation);
    REQUIRE(config.familyPriority[1] == Family::Healing);
    REQUIRE(config.familyPriority[2] == Family::Mana);
    REQUIRE(config.healing.policy.enabled);
    REQUIRE(config.mana.policy.enabled);
    REQUIRE(config.rejuvenation.policy.enabled);
    REQUIRE(config.healing.policy.columnCount == 2);
    REQUIRE(config.healing.policy.columns[0] == 1);
    REQUIRE(config.healing.policy.columns[1] == 2);
    REQUIRE(config.mana.policy.columnCount == 1);
    REQUIRE(config.mana.policy.columns[0] == 3);
    REQUIRE(config.rejuvenation.policy.columnCount == 1);
    REQUIRE(config.rejuvenation.policy.columns[0] == 4);
    REQUIRE(config.healing.policy.Accepts(Classify("hp2")));
    REQUIRE(!config.healing.policy.Accepts(Classify("hp1")));
    REQUIRE(config.mana.policy.Accepts(Classify("mp3")));
    REQUIRE(!config.mana.policy.Accepts(Classify("mp2")));
    REQUIRE(config.rejuvenation.policy.Accepts(Classify("rvl")));
    REQUIRE(config.healing.policy.AllowsOverflow(Classify("hp5")));
    REQUIRE(config.mana.policy.AllowsOverflow(Classify("mp5")));
    REQUIRE(config.rejuvenation.policy.AllowsOverflow(Classify("rvs")));
    REQUIRE(!config.diagnosticsEnabled);
    REQUIRE(!config.logScans);

    Policy enabledHealing{};
    enabledHealing.enabled = true;
    enabledHealing.tiers[5] = true;
    enabledHealing.columns[0] = 1;
    enabledHealing.columnCount = 1;
    std::array<BeltSlot, 16> belt{};
    belt[0] = {true, Family::Healing};
    REQUIRE(ChooseBeltSlot(
        enabledHealing, Classify("hp5"), belt, 16) == 4);
    for (auto& slot : belt) slot.occupied = true;
    REQUIRE(ChooseBeltSlot(
        enabledHealing, Classify("hp5"), belt, 16) == -1);

    auto invalid = stream.str();
    invalid += "\nunknown = true\n";
    REQUIRE(!ParseConfig(invalid, config, error));
    REQUIRE(!ParseConfig(
        "enabled = true\npickup_distance = 5\n", config, error));

    auto duplicateColumn = stream.str();
    const auto columns = duplicateColumn.find("belt_columns = [1, 2]");
    REQUIRE(columns != std::string::npos);
    duplicateColumn.replace(columns, std::string("belt_columns = [1, 2]").size(),
        "belt_columns = [1, 1]");
    REQUIRE(!ParseConfig(duplicateColumn, config, error));

    auto wrongCode = stream.str();
    const auto code = wrongCode.find("potion_codes = [\"hp5\", \"hp4\", \"hp3\", \"hp2\"]");
    REQUIRE(code != std::string::npos);
    wrongCode.replace(code, std::string("potion_codes = [\"hp5\", \"hp4\", \"hp3\", \"hp2\"]").size(),
        "potion_codes = [\"mp2\"]");
    REQUIRE(!ParseConfig(wrongCode, config, error));

    auto missing = stream.str();
    const auto diagnostics = missing.find("log_scans = false");
    REQUIRE(diagnostics != std::string::npos);
    missing.erase(diagnostics, std::string("log_scans = false").size());
    REQUIRE(!ParseConfig(missing, config, error));

    constexpr std::string_view desired = R"toml(
enabled = true
pickup_range = 4
pickup_family_order = ["rejuvenation", "health", "mana"]

[health_potions]
enabled = true
potion_codes = ["hp5", "hp4", "hp3", "hp2"]
belt_columns = [1, 2]
inventory_fallback_potion_codes = ["hp5"]

[mana_potions]
enabled = true
potion_codes = ["mp5", "mp4", "mp3"]
belt_columns = [3]
inventory_fallback_potion_codes = ["mp5"]

[rejuvenation_potions]
enabled = true
potion_codes = ["rvl", "rvs"]
belt_columns = [4]
inventory_fallback_potion_codes = ["rvl", "rvs"]

[advanced]
scan_every_player_actions = 1

[diagnostics]
enabled = true
log_scans = false
)toml";
    REQUIRE(ParseConfig(desired, config, error));
    REQUIRE(config.schema == ConfigSchema::PlayerFriendly);
    REQUIRE(config.familyPriorityCount == 3);
    REQUIRE(config.familyPriority[0] == Family::Rejuvenation);
    REQUIRE(config.familyPriority[1] == Family::Healing);
    REQUIRE(config.familyPriority[2] == Family::Mana);
    REQUIRE(config.healing.policy.Accepts(Classify("hp2")));
    REQUIRE(!config.healing.policy.Accepts(Classify("hp1")));
    REQUIRE(config.healing.policy.columnCount == 2);
    REQUIRE(config.healing.policy.columns[0] == 1);
    REQUIRE(config.healing.policy.columns[1] == 2);
    REQUIRE(config.healing.tierPriority[0] == 5);
    REQUIRE(config.healing.tierPriority[3] == 2);
    REQUIRE(config.healing.policy.AllowsOverflow(Classify("hp5")));
    REQUIRE(!config.healing.policy.AllowsOverflow(Classify("hp4")));
    REQUIRE(config.mana.policy.Accepts(Classify("mp3")));
    REQUIRE(!config.mana.policy.Accepts(Classify("mp2")));
    REQUIRE(config.mana.policy.columns[0] == 3);
    REQUIRE(config.mana.policy.AllowsOverflow(Classify("mp5")));
    REQUIRE(!config.mana.policy.AllowsOverflow(Classify("mp4")));
    REQUIRE(config.rejuvenation.policy.columns[0] == 4);
    REQUIRE(config.rejuvenation.policy.AllowsOverflow(Classify("rvs")));
    REQUIRE(config.rejuvenation.policy.AllowsOverflow(Classify("rvl")));

    auto fallbackNotPicked = std::string(desired);
    const auto healthFallback = fallbackNotPicked.find(
        "inventory_fallback_potion_codes = [\"hp5\"]");
    REQUIRE(healthFallback != std::string::npos);
    fallbackNotPicked.replace(
        healthFallback,
        std::string("inventory_fallback_potion_codes = [\"hp5\"]").size(),
        "inventory_fallback_potion_codes = [\"hp1\"]");
    REQUIRE(!ParseConfig(fallbackNotPicked, config, error));

    constexpr std::string_view legacy = R"toml(
enabled = true
pickup_distance = 4
minimum_interval_actions = 1
family_priority = ["rejuvenation", "healing", "mana"]
[healing]
enabled = true
tiers = ["hp5"]
columns = [1]
overflow_to_inventory = false
overflow_tiers = ["hp5"]
tier_priority = ["hp5"]
[mana]
enabled = false
tiers = []
columns = []
overflow_to_inventory = false
overflow_tiers = []
tier_priority = []
[rejuvenation]
enabled = false
tiers = []
columns = []
overflow_to_inventory = false
overflow_tiers = []
tier_priority = []
[diagnostics]
enabled = false
log_scans = false
)toml";
    REQUIRE(ParseConfig(legacy, config, error));
    REQUIRE(config.schema == ConfigSchema::Legacy);
    REQUIRE(config.healing.policy.AllowsOverflow(Classify("hp5")));

    auto mixed = std::string(desired);
    const auto pickupRange = mixed.find("pickup_range = 4");
    REQUIRE(pickupRange != std::string::npos);
    mixed.replace(pickupRange, std::string("pickup_range = 4").size(),
        "pickup_distance = 4");
    REQUIRE(!ParseConfig(mixed, config, error));
    REQUIRE(error.find("cannot be mixed") != std::string::npos);
    return 0;
}
