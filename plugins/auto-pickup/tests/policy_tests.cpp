#include "policy.hpp"
#include "compatibility.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
}
int main(int argc, char** argv) {
    using namespace RuffnecKk::AutoPickup;
    if (argc != 2) return 2;
    std::ifstream input(argv[1]);
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    Config c; std::string error;
    CHECK(ParseConfig(text, c, error) && !c.enabled && !c.validateOnly);
    CHECK(c.includeCodes.empty() && c.excludeCodes.empty());
    CHECK(!c.interact.enabled && !c.interact.chests && !c.interact.corpses && !c.interact.rocks && !c.interact.weaponRacks && !c.interact.armorRacks);
    for(const auto& route:c.potionRouting.families) CHECK(route.count==0 && !route.inventoryFallback);
    for(const auto& row:Catalog) CHECK(!c.Selects(PackItemCode(row.code)));
    CHECK(c.walkingRange == 4 && c.teleportRange == 4 && !c.walkingEnabled && !c.teleportEnabled && !c.teleportGold && !c.teleportItems);
    CHECK(ParseConfig("[walking]\nenabled=false\npickup_range=20\n[teleport]\npickup_range=1\ngold=false\nitems=false\n", c, error));
    CHECK(!c.walkingEnabled && c.walkingRange == 20 && c.teleportRange == 1 && !c.teleportGold && !c.teleportItems);
    for (const auto bad : {"[walking]\npickup_range=0", "[walking]\npickup_range=21", "[teleport]\npickup_range=3.5", "[teleport]\ninterval_ms=49", "[teleport]\nwindow_ms=500\ninterval_ms=1000"})
        CHECK(!ParseConfig(bad, c, error));
    CHECK(ParseConfig(text, c, error));
    CHECK(VanillaGroups.size() == 6 && ReimaginedGroups.size() == 3);
    for (const auto& row : Catalog) {
        const bool expected = false;
        CHECK(c.Selects(PackItemCode(row.code)) == expected);
    }
    for (const auto code : {"ka3", "1ka", "pk1", "dhn", "tes", "toa", "std", "jew", "cm1", "cap", "xa1", "ua1", "rup", "jwp", "1gc", "tsc", "aqv", "gld"}) CHECK(!c.Selects(PackItemCode(code)));
    CHECK(ParseConfig("", c, error) && !c.enabled && !c.validateOnly);
    for(const auto& route:c.potionRouting.families) CHECK(route.count==0 && !route.inventoryFallback);
    CHECK(!c.walkingEnabled && !c.teleportEnabled && !c.teleportGold && !c.teleportItems && !c.interact.enabled);
    for (const auto code : {"gmm", "gmo", "1am", "1so", "s01", "s33"}) CHECK(!c.Selects(PackItemCode(code)));
    CHECK(ParseConfig("[reimagined]\ngems = false", c, error));
    CHECK(!c.Selects(PackItemCode("gmm")) && !c.Selects(PackItemCode("gmo")));
    CHECK(!c.Selects(PackItemCode("gpv")) && !c.Selects(PackItemCode("1so")));
    CHECK(ParseConfig("[reimagined]\ngem_stacks = false", c, error));
    CHECK(!c.Selects(PackItemCode("1so")) && !c.Selects(PackItemCode("1am")));
    CHECK(!c.Selects(PackItemCode("gmo")));
    CHECK(ParseConfig("[reimagined]\nrune_stacks = false", c, error));
    CHECK(!c.Selects(PackItemCode("s01")) && !c.Selects(PackItemCode("r01")));
    CHECK(ParseConfig("[vanilla]\ngems = false", c, error));
    CHECK(!c.Selects(PackItemCode("gpv")) && !c.Selects(PackItemCode("gmm")));
    for (const auto group : {"keys", "health_potions", "mana_potions", "rejuvenation_potions"}) {
        CHECK(ParseConfig(std::string("[vanilla]\n") + group + " = true", c, error));
        for (const auto& row : Catalog) if (!row.reimagined && VanillaGroups[row.group] == group) CHECK(c.Selects(PackItemCode(row.code)));
    }
    CHECK(ParseConfig("[item_codes]\ninclude = [\"jew\", \"cm1\", \"rup\", \"abcd\", \"a\"]\nexclude = [\"r01\"]", c, error));
    for (const auto code : {"jew", "cm1", "rup", "abcd", "a"}) CHECK(c.Selects(PackItemCode(code)));
    CHECK(!c.Selects(PackItemCode("r01")) && !c.Selects(PackItemCode("cap")));
    // Explicit inclusion overrides a disabled category; exclusion wins over both.
    CHECK(ParseConfig("[vanilla]\ngems = false\n[item_codes]\ninclude = [\"gpv\", \"jew\", \"r01\"]\nexclude = [\"jew\", \"r01\", \"gmm\"]", c, error));
    CHECK(c.Selects(PackItemCode("gpv")) && !c.Selects(PackItemCode("gcv")));
    for (const auto code : {"jew", "r01", "gmm"}) CHECK(!c.Selects(PackItemCode(code)));
    CHECK(!c.Selects(PackItemCode("r02")));
    // Ordinary TOML quoting, comments, multiline arrays and trailing commas.
    CHECK(ParseConfig("[item_codes] # custom choices\ninclude = [\n 'cm1', # small charm\n \"jew\", \"cm1\",\n]\nexclude = [ 'r01', 'r01', ] # exclude El\n", c, error));
    CHECK(c.includeCodes.size() == 2 && c.excludeCodes.size() == 1);
    CHECK(c.Selects(PackItemCode("cm1")) && c.Selects(PackItemCode("jew")) && !c.Selects(PackItemCode("r01")));
    CHECK(ParseConfig("[item_codes]\ninclude = []\nexclude = []", c, error));
    CHECK(c.includeCodes.empty() && c.excludeCodes.empty() && !c.Selects(PackItemCode("r01")));
    CHECK(ParseConfig("[item_codes]\ninclude = [\"gld\"]", c, error));
    CHECK(!c.Selects(PackItemCode("gld"))); // Gold remains owned by the gold collector.
    for (const auto bad : {
        "include = \"jew\"", "include = [jew]", "include = [1]", "include = [\"\"]",
        "include = [\"abcde\"]", "include = [\"JEW\"]", "include = [\"a b\"]", "include = [\"a#b\"]",
        "include = [\"jew\" \"cm1\"]", "include = [\"jew\",,]", "include = [\"jew\"] junk",
        "include = [\"jew\"", "include = [\"jew]", "include = [['jew']]", "include = ['jew', true]",
        "exclude = [\"bad-code\"]", "other = []", "include = []\ninclude = []",
        "exclude = []\nexclude = []", "include = []\n[item_codes]"}) {
        CHECK(ParseConfig("[item_codes]\ninclude = [\"jew\"]", c, error));
        CHECK(!ParseConfig(std::string("[item_codes]\n") + bad, c, error));
        CHECK(!error.empty() && c.Selects(PackItemCode("jew"))); // Failed edits don't replace a valid policy.
    }
    for (const auto bad : {
        "[vanilla]\ngems = true\ngems = false", "[vanilla]\nuber_keys = true",
        "[vanilla]\ncharms = true", "[reimagined]\npower_orbs = true",
        "[reimagined]\nungraded_gems = true", "[reimagined]\nchaos_onyx = true",
        "pickup_range = 4",
        "scan_every_player_actions = 2", "[health_potions]\nbelt_columns = [1]"}) CHECK(!ParseConfig(bad, c, error));
    CHECK(AcceptPickupEntry(true, false, 0, ""));
    CHECK(AcceptPickupEntry(false, true, 1, "d2rl-auto-deposit"));
    CHECK(!AcceptPickupEntry(false, true, 2, "d2rl-auto-deposit"));
    CHECK(!AcceptPickupEntry(false, true, 1, "unknown"));
    CHECK(ParseConfig("[potion_routing.health]\nbelt_columns=[4, 2]\ninventory_fallback=false", c, error));
    CHECK(c.potionRouting.families[0].count==2 && c.potionRouting.families[0].columns[0]==4 && !c.potionRouting.families[0].inventoryFallback);
    for(const auto bad:{"[1,1]","[0]","[5]","[1,2,3,4,1]","1","[2,]","[1.0]","[\"1\"]"})
        CHECK(!ParseConfig(std::string("[potion_routing.health]\nbelt_columns=")+bad,c,error));
    CHECK(!ParseConfig("[potion_routing.health]\ninventory_fallback=1",c,error));
    CHECK(!ParseConfig("[potion_routing.health]\nbelt_columns=[]\nbelt_columns=[1]",c,error));
    CHECK(ParseConfig("[potion_routing.health]\nbelt_columns=[]\ninventory_fallback=false",c,error));
    CHECK(c.potionRouting.families[0].count==0 && !c.potionRouting.families[0].inventoryFallback);
    std::cout << "Default categories, custom item codes, exclusion precedence and invalid-config rejection; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
