#include "policy.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
int Failures{};
#define REQUIRE(expression) do { if (!(expression)) { std::cerr << "failed: " #expression " at line " << __LINE__ << '\n'; ++Failures; } } while (false)

auto ReadFile(const char* path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
auto ReplaceOnce(std::string text, std::string_view before, std::string_view after) -> std::string {
    const auto position = text.find(before);
    if (position != std::string::npos) text.replace(position, before.size(), after);
    return text;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    using namespace RuffnecKk::AutoPickup;
    const auto desired = ReadFile(argv[1]);
    Config config{}; std::string error;
    REQUIRE(!desired.empty());
    REQUIRE(ParseConfig(desired, config, error));
    REQUIRE(config.inventoryCodeCount == 2);
    REQUIRE(config.InventoryRank(PackItemCode("tsc")) == 0);
    REQUIRE(config.InventoryRank(PackItemCode("isc")) == 1);
    REQUIRE(config.InventoryRank(PackItemCode("abc")) == UINT8_MAX);
    REQUIRE(config.familyPriority[0] == Family::Rejuvenation);
    REQUIRE(config.health.priority[0] == 5);
    REQUIRE(config.health.priority[1] == 4);
    REQUIRE(config.health.policy.AllowsInventory(Classify("hp5")));
    REQUIRE(!config.health.policy.AllowsInventory(Classify("hp4")));

    auto duplicate = ReplaceOnce(desired, "[\"tsc\", \"isc\"]", "[\"tsc\", \"tsc\"]");
    REQUIRE(!ParseConfig(duplicate, config, error));
    auto invalid = ReplaceOnce(desired, "[\"tsc\", \"isc\"]", "[\"TSC\"]");
    REQUIRE(!ParseConfig(invalid, config, error));
    auto invalidPotion = ReplaceOnce(desired, "[\"tsc\", \"isc\"]", "[\"hp5\"]");
    REQUIRE(!ParseConfig(invalidPotion, config, error));
    auto tooMany = ReplaceOnce(desired, "[\"tsc\", \"isc\"]", "[\"a01\", \"a02\", \"a03\", \"a04\", \"a05\", \"a06\", \"a07\", \"a08\", \"a09\", \"a10\", \"a11\", \"a12\", \"a13\", \"a14\", \"a15\", \"a16\", \"a17\"]");
    REQUIRE(!ParseConfig(tooMany, config, error));
    REQUIRE(IsExactCode("r01"));
    REQUIRE(!IsExactCode(""));
    REQUIRE(!IsExactCode("long5"));
    REQUIRE(!IsExactCode("R01"));

    std::array<BeltSlot, 16> slots{};
    slots[0] = {true, Family::Health};
    REQUIRE(ChooseBeltSlot(config.health.policy, Classify("hp5"), slots, 8) == 4);
    REQUIRE(ChooseBeltSlot(config.health.policy, Classify("hp1"), slots, 8) == -1);
    return Failures == 0 ? 0 : 1;
}
