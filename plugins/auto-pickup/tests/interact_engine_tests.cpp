#include "explicit_preferences.hpp"
#include "engine.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using namespace AutoInteract;
auto Chest(std::uint32_t id, int distance = 2) -> Object {
    return {{7, id, 111}, Kind::Chest, 0, distance, true, false, false, false, true};
}
int main() {
    Config c=TestPreferences::Interact();
    std::array objects{Chest(10), Chest(11, 3)};
    Engine e;
    CHECK(e.Choose(objects, c, 100).object == 10);
    CHECK(e.Arm(objects[0].id, 100, c.retryMs));
    CHECK(!e.Arm(objects[0].id, 101, c.retryMs));
    CHECK(e.Choose(objects, c, 200).object == 11); // refused nearest does not starve next
    CHECK(e.Arm(objects[1].id, 200, c.retryMs));
    CHECK(!e.Choose(objects, c, 300).Valid());
    CHECK(e.Choose(objects, c, 1100).object == 10);
    CHECK(e.metrics.noProgress == 1 && e.metrics.completed == 0);
    objects[1].mode = 1;
    (void)e.Choose(objects, c, 1101); CHECK(e.metrics.completed == 1);
    auto o = Chest(20);
    for (int field = 0; field < 7; ++field) {
        o = Chest(20);
        switch (field) {
        case 0: o.mode = 2; break; case 1: o.inReach = false; break; case 2: o.blocked = true; break;
        case 3: o.distance = -1; break; case 4: o.kind = Kind::Excluded; break;
        case 5: o.validTrap = false; break; case 6: o.id.player = UINT32_MAX; break;
        }
        CHECK(!Engine::Eligible(o, c));
    }
    o = Chest(20, 100); CHECK(Engine::Eligible(o, c)); // NO arbitrary pickup radius; native reach required
    o.locked = true; CHECK(!Engine::Eligible(o, c));
    c.lockedChests = true; CHECK(!Engine::Eligible(o, c));
    o.keysAvailable = true; CHECK(Engine::Eligible(o, c));
    o.kind = Kind::Rock; CHECK(!Engine::Eligible(o, c));
    c = TestPreferences::Interact(); c.enabled = false; CHECK(!Engine::Eligible(Chest(20), c));
    c = TestPreferences::Interact(); c.validateOnly = true; CHECK(!Engine::Eligible(Chest(20), c)); c = TestPreferences::Interact();
    e.Reset(); CHECK(e.Choose(objects, c, 200).object == 10);
    CHECK(e.Arm(objects[0].id, 200, 1000));
    CHECK(e.Choose(objects, c, 1).object == 10); // clock rollback clears stale cooldown
    e.Reset();
    for (std::uint32_t i = 0; i < Engine::Capacity; ++i) CHECK(e.Arm({7, i, 1}, 0, 1000));
    CHECK(!e.Arm({7, 10000, 1}, 999, 1000)); CHECK(e.metrics.saturated == 1);
    CHECK(!e.Arm({7, 0, 1}, 999, 1000));
    CHECK(e.Arm({7, 10000, 1}, 1000, 1000));
    CHECK(!e.Arm({}, 1000, 1000));
    e.Reset(); CHECK(e.Arm({7, 1, 1}, UINT64_MAX - 10, 1000)); CHECK(!e.Arm({7, 1, 1}, UINT64_MAX - 1, 1000));
    // Distinct players and replacement classes do not inherit another identity's delay.
    e.Reset(); CHECK(e.Arm({7, 10, 111}, 100, 1000));
    auto other = Chest(10); other.id.player = 8; CHECK(e.Choose(std::span(&other, 1), c, 200).Valid());
    other.id.player = 7; other.id.objectClass = 112; CHECK(e.Choose(std::span(&other, 1), c, 200).Valid());
    // Exercise full-cache collisions, observed completions, expiry and reuse.
    std::vector<Object> crowded;
    e.Reset();
    for (std::uint32_t i = 0; i < Engine::Capacity; ++i) {
        crowded.push_back(Chest(i, 2)); CHECK(e.Arm(crowded.back().id, 100, 1000));
    }
    const auto completed = e.metrics.completed;
    CHECK(!e.Choose(crowded, c, 200).Valid());
    for (std::size_t i = 0; i < crowded.size(); i += 2) crowded[i].mode = 1;
    CHECK(!e.Choose(crowded, c, 201).Valid());
    CHECK(e.metrics.completed == completed + Engine::Capacity / 2);
    CHECK(e.Choose(crowded, c, 1100).object == 1);
    CHECK(e.Arm(crowded[1].id, 1100, 1000));
    CHECK(e.Choose(crowded, c, 1101).object == 3);
    std::cout << "engine/retry/fairness passed\n";
}
