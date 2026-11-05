// Actual routing adapter exercised with native-call doubles, no game process.
#include "../src/potion_routing/module.cpp"
#include <iostream>
using namespace RuffnecKk::PotionRouting;
namespace {
int failures{}, manual{}, calls{}, expectedSlot{};
bool expectFallback{}, fault{};
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << ": " #x "\n";                                   \
      ++failures;                                                              \
    }                                                                          \
  } while (false)
struct Unit {
  std::uint32_t code{}, guid{};
};
Unit health{PackItemCode("hp5"), 100}, mana{PackItemCode("mp5"), 101},
    rejuv{PackItemCode("rvl"), 102}, other{PackItemCode("hp4"), 999},
    beltItem{};
std::array<std::uint8_t, 32> bodyGrid{}, beltGrid{};
std::array<void *, 16> bodyItems{}, beltItems{};
int player{}, inventory{};
int beltType{3};
void *__fastcall Inventory(void *) { return &inventory; }
void *__fastcall Grid(void *, std::uint64_t which, const void *) {
  return which ? beltGrid.data() : bodyGrid.data();
}
std::int32_t __fastcall BeltType(void *) { return beltType; }
std::uint32_t __fastcall Code(void *p) { return static_cast<Unit *>(p)->code; }
std::uint32_t __fastcall Guid(void *p) { return static_cast<Unit *>(p)->guid; }
std::int32_t __fastcall Manual(void *, void *, std::int32_t *out,
                               bool) noexcept {
  ++manual;
  *out = 14;
  return 1;
}
bool __fastcall Request(void *actor, std::uint32_t guid, bool normal,
                        std::uint32_t distance, bool b, bool c) {
  ++calls;
  CHECK(actor == &player && normal && b && !c && distance == 9);
  Unit *item = guid == 100 ? &health : guid == 101 ? &mana : &rejuv;
  std::int32_t slot = -2;
  const auto result = HookGetFreeBeltSlot(&inventory, item, &slot, false);
  CHECK(slot == expectedSlot);
  CHECK(result == (expectFallback ? 0 : 1));
  std::int32_t unrelated = -1;
  CHECK(HookGetFreeBeltSlot(&inventory, &other, &unrelated, false) == 1 &&
        unrelated == 14);
  if (fault)
    RaiseException(0xE0421111, 0, 0, nullptr);
  return true;
}
void Setup() {
  bodyGrid = {};
  beltGrid = {};
  bodyItems = {};
  beltItems = {};
  bodyGrid[0x10] = 16;
  bodyGrid[0x11] = 1;
  beltGrid[0x10] = 4;
  beltGrid[0x11] = 4;
  void **body = bodyItems.data();
  void **belt = beltItems.data();
  std::memcpy(bodyGrid.data() + 0x18, &body, sizeof(body));
  std::memcpy(beltGrid.data() + 0x18, &belt, sizeof(belt));
  bodyItems[8] = &beltItem;
  Base = reinterpret_cast<std::uint8_t *>(0x140000000ULL);
  GetInventory = Inventory;
  GetBeltType = BeltType;
  ResolveOccupancyGrid = Grid;
  GetItemCode = Code;
  UnitId = Guid;
  NativePickup = Request;
  OriginalGetFreeBeltSlot = Manual;
  Ready = true;
}
} // namespace
int main() {
  Setup();
  Config settings;
  settings.families[0] = Route{{1,2,0,0},2,true};
  auto &route = settings.families[0];
  expectedSlot = 0;
  CHECK(CanRoute(&player, &health, route));
  CHECK(Pickup(&player, &health, 100, 9, route));
  CHECK(!Inside && !RouteAdvisorModule);
  beltItems[0] = &health;
  expectedSlot = 4;
  CHECK(Pickup(&player, &health, 100, 9, route));
  beltType = 2;
  expectedSlot = 1;
  CHECK(Pickup(&player, &health, 100, 9,
               route)); // one-row belt respects actual height
  beltItems[1] = &mana;
  expectFallback = true;
  expectedSlot = -1;
  CHECK(Pickup(&player, &health, 100, 9, route));
  route.inventoryFallback = false;
  CHECK(!CanRoute(&player, &health, route));
  const auto before = calls;
  CHECK(!Pickup(&player, &health, 100, 9, route));
  CHECK(calls == before);
  CHECK(ParseColumns("[4,2]", route));
  beltItems = {};
  expectFallback = false;
  expectedSlot = 3;
  CHECK(Pickup(&player, &health, 100, 9, route));
  CHECK(!Pickup(&player, &health, 999, 9, route));
  fault = true;
  CHECK(!Pickup(&player, &health, 100, 9, route));
  CHECK(!Inside && !RouteAdvisorModule);
  fault = false;
  std::int32_t slot = -1;
  CHECK(HookGetFreeBeltSlot(&inventory, &health, &slot, false) == 1 &&
        slot == 14); // manual pickup restored
  route.count = 0;
  route.inventoryFallback = true;
  expectFallback = true;
  expectedSlot = -1;
  CHECK(Pickup(&player, &health, 100, 9, route));
  route.inventoryFallback = false;
  CHECK(!CanRoute(&player, &health, route));
  Stop();
  CHECK(!CanRoute(&player, &health, route));
  std::cout << "Potion columns, belt height, fallback, GUID scope and "
               "exception cleanup: "
            << failures << " failures\n";
  return failures ? 1 : 0;
}
