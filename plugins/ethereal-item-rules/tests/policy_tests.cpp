#include "policy.hpp"

#include <array>
#include <cstring>
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
    using namespace RuffnecKk::EtherealItemRules;

    ItemTypeCode belt{};
    REQUIRE(NormalizeItemTypeCode(" BeLt ", belt));
    REQUIRE(belt.text[0] == 'b' && belt.text[3] == 't');

    ItemTypeCode gem{};
    REQUIRE(NormalizeItemTypeCode("gem", gem));
    REQUIRE(gem.bytes[3] == ' ');

    ItemTypeCode invalid{};
    REQUIRE(!NormalizeItemTypeCode("too-long", invalid));
    REQUIRE(!NormalizeItemTypeCode("a-b", invalid));

    struct Record {
        std::array<char, 4> code{};
        std::array<std::uint8_t, ItemTypeRecordStride - 4> padding{};
    };
    static_assert(sizeof(Record) == ItemTypeRecordStride);
    std::array<Record, 3> records{};
    std::memcpy(records[0].code.data(), "armo", 4);
    std::memcpy(records[1].code.data(), "belt", 4);
    std::memcpy(records[2].code.data(), "gem ", 4);
    REQUIRE(FindItemTypeId(records.data(), records.size(), sizeof(Record), belt) == 1);
    REQUIRE(FindItemTypeId(records.data(), records.size(), sizeof(Record), gem) == 2);
    REQUIRE(FindItemTypeId(nullptr, records.size(), sizeof(Record), belt) == -1);
    REQUIRE(FindItemTypeId(records.data(), 4097, sizeof(Record), belt) == -1);

    REQUIRE(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    Config config{};
    std::string error;
    REQUIRE(ParseConfig(stream.str(), config, error));
    REQUIRE(config.enabled);
    REQUIRE(!config.exclusions.enabled);
    REQUIRE(config.exclusions.itemTypeCount == 0);
    REQUIRE(!config.generation.enabled);
    REQUIRE(config.generation.chancePercent == VanillaChancePercent);
    REQUIRE(!config.generation.allowSetItems);
    REQUIRE(!config.generation.allowIndestructibleItems);
    REQUIRE(!config.diagnosticsEnabled);
    REQUIRE(!HasExcludedItemTypes(config));
    REQUIRE(!HasDirectRulePatches(config));

    const auto configured = R"toml(
[exclusions]
enabled = true
item_types = [
    "belt",
    "BELT",
    "armo",
]

[generation]
enabled = true
chance_percent = 6
allow_set_items = true
allow_indestructible_items = true
)toml";
    REQUIRE(ParseConfig(configured, config, error));
    REQUIRE(config.enabled);
    REQUIRE(config.exclusions.enabled);
    REQUIRE(config.exclusions.itemTypeCount == 2);
    REQUIRE(config.generation.enabled);
    REQUIRE(config.generation.chancePercent == 6);
    REQUIRE(HasExcludedItemTypes(config));
    REQUIRE(PatchChance(config));
    REQUIRE(PatchSetItems(config));
    REQUIRE(PatchIndestructibleItems(config));

    const auto disabled = R"toml(
[plugin]
enabled = false
[exclusions]
enabled = true
item_types = ["armo"]
[generation]
enabled = true
chance_percent = 100
allow_set_items = true
allow_indestructible_items = true
[diagnostics]
enabled = true
)toml";
    REQUIRE(ParseConfig(disabled, config, error));
    REQUIRE(!config.enabled);
    REQUIRE(config.diagnosticsEnabled);
    REQUIRE(!HasExcludedItemTypes(config));
    REQUIRE(!HasDirectRulePatches(config));

    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=true\nitem_types=[]\n"
        "[generation]\nenabled=false\nchance_percent=5\n"
        "allow_set_items=false\nallow_indestructible_items=false\nextra=1\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=true\nitem_types=[\"too-long\"]\n"
        "[generation]\nenabled=false\nchance_percent=5\n"
        "allow_set_items=false\nallow_indestructible_items=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=false\nitem_types=[]\n"
        "[generation]\nenabled=true\nchance_percent=101\n"
        "allow_set_items=false\nallow_indestructible_items=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[exclusions]\nenabled=false\nitem_types=[]\n",
        config,
        error));
    return 0;
}
