#include "policy.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using namespace AutoInteract;
int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::ifstream file(argv[1]);
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    Config c; std::string error;
    CHECK(!text.empty() && ParseConfig(text, c, error));
    CHECK(c.enabled && !c.validateOnly && c.chests && c.corpses && c.rocks && c.weaponRacks && c.armorRacks);
    CHECK(!c.lockedChests && c.intervalMs == 100 && c.retryMs == 1000);
    for (auto bad : {"enabled = 1", "enabled=true\nenabled=false", "retry_ms=999", "retry_ms=60001", "interval_ms=49",
            "interval_ms=1001", "interval_ms=-1", "interval_ms=100ms", "unknown=true", "[unknown]\nx=true"}) {
        const auto old = c.intervalMs;
        CHECK(!ParseConfig(bad, c, error)); CHECK(c.intervalMs == old);
    }
    CHECK(ParseConfig("locked_chests=true\nretry_ms=60000\ninterval_ms=50\n[d2rl]\nmatch=[]", c, error));
    CHECK(c.lockedChests && c.retryMs == 60000 && c.intervalMs == 50);
    CHECK(PlayerReady(1) && PlayerReady(5));
    for (std::uint32_t mode = 0; mode < 30; ++mode) if (mode != 1 && mode != 5) CHECK(!PlayerReady(mode));
    Semantics s{"ordinary", 3, 4, 0, true, false, true};
    CHECK(Classify(s) == Kind::Chest); s.init = 57; CHECK(Classify(s) == Kind::Chest);
    s.init = 45; CHECK(Classify(s) == Kind::Excluded); // Mephisto bridge
    s.init = 3; s.operate = 68; CHECK(Classify(s) == Kind::Excluded); // Evil Urn
    for (int op : {2, 6, 8, 10, 12, 15, 16, 17, 18, 21, 23, 24, 25, 28, 32, 33, 39, 40, 41, 49, 52, 53, 57, 58, 59, 65, 68, 200}) {
        s.operate = static_cast<std::uint16_t>(op); CHECK(Classify(s) == Kind::Excluded);
    }
    s = {"WeaponRackCustom", 0, 20, 0, true, false, true}; CHECK(Classify(s) == Kind::WeaponRack);
    s.operate = 19; CHECK(Classify(s) == Kind::ArmorRack);
    s = {"RockC", 0, 14, 0, true, false, true}; CHECK(Classify(s) == Kind::Rock);
    s.key = "RockD"; CHECK(Classify(s) == Kind::Rock);
    s.key = "UnknownCustom"; CHECK(Classify(s) == Kind::Excluded);
    s.key = "CorpseSkeleton"; s.init = 2; s.subclass = 8; CHECK(Classify(s) == Kind::Corpse);
    s.key = "WirtsBody"; CHECK(Classify(s) == Kind::Excluded);
    for (auto key : {"HiddenStash", "Rock", "Log", "Crate", "JugOutdoor1", "MephistoPillar1", "Barrel3"}) {
        s = {key, 0, 14, 0, true, false, true}; CHECK(Classify(s) == Kind::Chest);
    }
    for (auto key : {"Basket1", "Basket2", "Urn1", "EJar1", "IceCaveJar1", "Box1"}) {
        s = {key, 2, 3, 8, true, false, true}; CHECK(Classify(s) == Kind::Chest);
    }
    s = {"DungeonBasket", 33, 3, 8, true, false, true}; CHECK(Classify(s) == Kind::Chest);
    s = {"DungeonRockPile", 33, 3, 8, true, false, true}; CHECK(Classify(s) == Kind::Rock);
    s = {"Barrel", 0, 5, 0, true, false, true}; CHECK(Classify(s) == Kind::Chest);
    s = {"JunglePillar2", 2, 51, 0, true, false, true}; CHECK(Classify(s) == Kind::Chest);
    s = {"SewerChest", 2, 4, 8, true, false, true}; CHECK(Classify(s) == Kind::Chest);
    for (auto key : {"Bank", "Stash", "PersonalStash", "IceCaveEvilUrn", "UnknownCustom", "BarrelExploding"}) {
        for (auto op : {3, 5, 7, 14, 32, 51, 68}) {
            for (auto init : {0, 2, 33}) {
                s = {key, static_cast<std::uint16_t>(init), static_cast<std::uint16_t>(op), 0, true, false, true};
                CHECK(Classify(s) == Kind::Excluded);
            }
        }
    }
    s = {"ordinary", 3, 4, 0, true, false, true};
    s.verifiedCallbacks = false; CHECK(Classify(s) == Kind::Excluded);
    s.verifiedCallbacks = true; s.door = true; CHECK(Classify(s) == Kind::Excluded);
    s.door = false; s.selectable = false; CHECK(Classify(s) == Kind::Excluded);
    s.selectable = true;
    for (auto subclass : {1, 2, 4, 7}) { s.subclass = static_cast<std::uint8_t>(subclass); CHECK(Classify(s) == Kind::Excluded); }
    c = {}; c.corpses = true; c.rocks = false; CHECK(!Selected(Kind::Rock, c) && Selected(Kind::Corpse, c));
    std::cout << "policy/config passed\n";
}
