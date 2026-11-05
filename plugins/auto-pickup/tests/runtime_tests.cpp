#include "explicit_preferences.hpp"
#include "../src/plugin.cpp"
#include "../src/potion_routing/module.cpp"
#include <iostream>
#include <vector>
namespace {
int failures{};
std::uint32_t ExpectedDistance = 4;
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
struct Unit {
    std::uint32_t type{}, id{}, code{};
    std::int32_t distance{};
    bool blocked{};
    std::uint32_t mode{3};
    Unit* next{};
    bool accepts{true}, removes{true}, faults{};
} Player, Item, Other, Third;
Unit* FirstItem = &Item;
void* ExpectedPlayer = &Player;
int Room{};
void* Rooms[]{&Room};
std::vector<std::uint32_t> Requests;
bool ResetSessionDuringScan{};
void* __fastcall RoomOf(void*) { return &Room; }
void __fastcall Neighbours(void*, void*** out, std::uint32_t* count) { *out = Rooms; *count = 1; }
void* __fastcall First(void*) { return FirstItem; }
void* __fastcall Next(void* unit) { return static_cast<Unit*>(unit)->next; }
std::uint32_t __fastcall Type(void* unit) { return static_cast<Unit*>(unit)->type; }
std::uint32_t __fastcall Id(void* unit) { return static_cast<Unit*>(unit)->id; }
std::uint32_t __fastcall Code(void* unit) {
    if (ResetSessionDuringScan) {
        ResetSessionDuringScan = false;
        RuffnecKk::AutoPickup::ResetItemRetries();
    }
    return static_cast<Unit*>(unit)->code;
}
std::uint32_t __fastcall Mode(void* unit) { return static_cast<Unit*>(unit)->mode; }
std::int32_t __fastcall Distance(void*, void* item) { return static_cast<Unit*>(item)->distance; }
std::int32_t __fastcall Collision(void*, void* item, std::uint32_t) { return static_cast<Unit*>(item)->blocked ? 1 : 0; }
bool __fastcall Request(void* player, std::uint32_t id, bool ordinary, std::uint32_t distance, bool normal, bool extra) {
    CHECK(player == ExpectedPlayer && ordinary && normal && !extra && distance == ExpectedDistance);
    Requests.push_back(id);
    auto* item = FirstItem;
    while (item && item->id != id) item = item->next;
    CHECK(item != nullptr);
    if (!item) return false;
    if (item->faults) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (item->accepts && item->removes) item->mode = 1;
    return item->accepts;
}
void* __fastcall PotionInventory(void*) {return &Player;}
void* __fastcall PotionGrid(void*,std::uint64_t which,const void*) {
    static std::array<std::uint8_t,32> body{},belt{};
    static std::array<void*,16> bodyItems{},beltItems{};
    auto** items=which?beltItems.data():bodyItems.data();auto& grid=which?belt:body;
    grid[0x10]=16;grid[0x11]=1;std::memcpy(grid.data()+0x18,&items,sizeof(items));return grid.data();
}
std::int32_t __fastcall PotionBelt(void*) {return 2;}
auto __fastcall OriginalMovement(void*, void*, void*, std::int32_t) -> std::int64_t { return 17; }
void FreshItem(std::uint32_t id, std::uint32_t code, std::int32_t distance = 0) {
    Item = {4, id, code, distance}; FirstItem = &Item;
}
}
int main() {
    using namespace RuffnecKk::AutoPickup;
    GetUnitRoom = RoomOf; GetRoomNeighbours = Neighbours; GetRoomFirstUnit = First; GetNextUnit = Next;
    UnitType = Type; UnitId = Id; UnitMode = Mode; GetItemCode = Code;
    UnitDistance = Distance; UnitCollision = Collision; Pickup = Request;
    Player = {0, 1}; Settings = TestPreferences::Pickup(); ResetItemRetries();
    namespace Routing=RuffnecKk::PotionRouting;
    Routing::Ready=true;Routing::Base=reinterpret_cast<std::uint8_t*>(0x140000000ULL);
    Routing::GetInventory=PotionInventory;Routing::GetBeltType=PotionBelt;Routing::ResolveOccupancyGrid=PotionGrid;
    Routing::GetItemCode=Code;Routing::UnitId=Id;Routing::NativePickup=Request;
    FreshItem(10, PackItemCode("r01")); Scan(&Player, 4, 100);
    CHECK(Requests.size() == 1 && Metrics.picked.load() == 1);
    FreshItem(11, PackItemCode("ka3")); Scan(&Player, 4, 100); CHECK(Requests.size() == 1);
    FreshItem(12, PackItemCode("hp5")); Scan(&Player, 4, 100); CHECK(Requests.size() == 1);
    std::string error;
    CHECK(ParseConfig("enabled=true\n[walking]\nenabled=true\n[vanilla]\nrunes=true\nhealth_potions=true\n[potion_routing.health]\nbelt_columns=[1,2]\ninventory_fallback=true", Settings, error));
    Item.blocked = true; Scan(&Player, 4, 100); CHECK(Requests.size() == 1);
    Item.blocked = false; Item.distance = 5; Scan(&Player, 4, 100); CHECK(Requests.size() == 1);
    Item.distance = 4; Scan(&Player, 4, 100); CHECK(Requests.size() == 2);

    // A refused nearest item cannot starve another item. Successful removals
    // remain eligible in adjacent scans, with no player-wide pickup delay.
    FreshItem(20, PackItemCode("r01"), 1); Item.accepts = false;
    Other = {4, 21, PackItemCode("r02"), 2}; Item.next = &Other;
    Scan(&Player, 4, 100); CHECK(Requests.size() == 3 && Requests.back() == 20);
    Scan(&Player, 4, 101); CHECK(Requests.size() == 4 && Requests.back() == 21);
    Scan(&Player, 4, 1099); CHECK(Requests.size() == 4);
    Scan(&Player, 4, 1100); CHECK(Requests.size() == 5 && Requests.back() == 20);
    Item.accepts = true; Scan(&Player, 4, 1101); CHECK(Requests.size() == 5);
    Scan(&Player, 4, 2100); CHECK(Requests.size() == 6 && Item.mode == 1); // Space became available.

    // Native success does not prove the item left the ground (mod hooks can
    // defer work). Never dereference that item after the native request.
    FreshItem(30, PackItemCode("r03")); Item.removes = false;
    Scan(&Player, 4, 2200); Scan(&Player, 4, 2201); CHECK(Requests.size() == 7);
    Scan(&Player, 4, 3200); CHECK(Requests.size() == 8);
    Item.faults = true; Scan(&Player, 4, 4200); CHECK(Requests.size() == 9);
    Item.faults = false; Scan(&Player, 4, 4201); CHECK(Requests.size() == 9);

    Unit anotherPlayer{0, 2}; ExpectedPlayer = &anotherPlayer;
    Scan(&anotherPlayer, 4, 4202); CHECK(Requests.size() == 10); // Retry ownership is per player.
    ExpectedPlayer = &Player;
    Item.id = 31; Scan(&Player, 4, 4203); CHECK(Requests.size() == 11);
    Item.code = PackItemCode("r04"); Scan(&Player, 4, 4204); CHECK(Requests.size() == 12);
    Player.id = UINT32_MAX; Scan(&Player, 4, 4205); CHECK(Requests.size() == 12);
    Player.id = 1; Item.id = UINT32_MAX; Scan(&Player, 4, 4206); CHECK(Requests.size() == 12);

    CHECK(ParseConfig("enabled=true\n[walking]\nenabled=true\n[item_codes]\ninclude = [\"jew\"]\nexclude = [\"r01\"]", Settings, error));
    FreshItem(40, PackItemCode("r01")); Scan(&Player, 4, 4300); CHECK(Requests.size() == 12);
    Item.code = PackItemCode("jew"); Scan(&Player, 4, 4300); CHECK(Requests.size() == 13);
    FreshItem(41, PackItemCode("gld")); Scan(&Player, 4, 4300); CHECK(Requests.size() == 13);

    // Exercise the actual walking hook and both exported teleport scan paths.
    ResetItemRetries(); ScanApiReady = true; Settings = TestPreferences::Pickup();
    FreshItem(50, PackItemCode("r01"), 1); Item.accepts = false;
    Other = {4, 51, PackItemCode("r02"), 2}; Third = {4, 52, PackItemCode("r03"), 8};
    Item.next = &Other; Other.next = &Third;
    OriginalTriggers[1] = OriginalMovement; std::uint8_t opcode = 1;
    CHECK(HookTrigger(nullptr, &Player, &opcode, 1) == 17);
    CHECK(Requests.size() == 14 && Requests.back() == 50);
    CHECK(ExternalScan(&Player)); CHECK(Requests.size() == 15 && Requests.back() == 51);
    CHECK(ExternalScan(&Player)); CHECK(Requests.size() == 15); // Same cooldown for walk and V1.
    ExpectedDistance = 8;
    CHECK(ExternalRangeScan(&Player, 8)); CHECK(Requests.size() == 16 && Requests.back() == 52);
    CHECK(ExternalRangeScan(&Player, 8)); CHECK(Requests.size() == 16); // V2 cannot bypass it either.
    CHECK(!ExternalRangeScan(&Player, 0) && !ExternalRangeScan(&Player, 21));
    ExpectedDistance = 4;
    FreshItem(53, PackItemCode("r04"), 8); Scan(&Player); CHECK(Requests.size() == 16);
    Item.distance = 4; Scan(&Player); CHECK(Requests.size() == 17); // Movement radius unchanged.

    FreshItem(60, PackItemCode("r01")); Item.accepts = false;
    ResetItemRetries(); Scan(&Player, 4, 5000); CHECK(Requests.size() == 18);
    Scan(&Player, 4, 5001); CHECK(Requests.size() == 18);
    D2RL::Lifecycle::GameplayEvent event{D2RL::Lifecycle::GameplayEventRequiredSize, 0, D2RL::Lifecycle::GameplayEventKind::GameLeft, Player.id, 2, 0, 0};
    OnRetrySessionEvent(nullptr, &event, nullptr);
    Scan(&Player, 4, 5002); CHECK(Requests.size() == 19); // New sessions can reuse item/player IDs.
    event.structSize = 0; OnRetrySessionEvent(nullptr, &event, nullptr);
    Scan(&Player, 4, 5003); CHECK(Requests.size() == 19);
    Settings.enabled = false; Scan(&Player, 4, 6002); CHECK(Requests.size() == 19);
    CHECK(Metrics.retrySkips.load() > 0 && Metrics.exceptions.load() == 1);
    Settings.enabled = true; ResetSessionDuringScan = true;
    FreshItem(70, PackItemCode("r01"));
    Scan(&Player, 4, 7000); CHECK(Requests.size() == 19); // A stale scan must not re-arm state after a session reset.
    Scan(&Player, 4, 7001); CHECK(Requests.size() == 20);
    ResetItemRetries();
    CHECK(ParseConfig("enabled=true\n[walking]\nenabled=true\n[item_codes]\ninclude = [\"jew\"]\nexclude = [\"r01\"]", Settings, error));
    FreshItem(80, PackItemCode("jew"), 8); Item.accepts = false;
    ScanApiReady = true; ExpectedDistance = 8;
    CHECK(ExternalRangeScan(&Player, 8)); CHECK(Requests.size() == 21);
    Item.distance = 4; ExpectedDistance = 4;
    CHECK(HookTrigger(nullptr, &Player, &opcode, 1) == 17);
    CHECK(ExternalScan(&Player) && Requests.size() == 21); // Teleport failures also suppress walking/V1 retries.
    Other = {4, 81, PackItemCode("r01"), 0}; Item.next = &Other;
    CHECK(ExternalScan(&Player) && Requests.size() == 21); // Exclusions still apply through the bridge.
    Other.code = PackItemCode("jew");
    CHECK(ExternalScan(&Player) && Requests.size() == 22 && Requests.back() == 81);
    CHECK(ParseConfig("enabled=true\n[walking]\nenabled=true\n[item_codes]\ninclude = [\"jew\"]\nexclude = [\"jew\"]", Settings, error));
    FreshItem(82, PackItemCode("jew"), 8);
    CHECK(ExternalRangeScan(&Player, 8) && Requests.size() == 22);
    // Live UI edits update master/category switches, but never exit diagnostic mode.
    Settings = TestPreferences::Pickup();
    Config uiConfig=TestPreferences::Pickup(); uiConfig.enabled = false; uiConfig.validateOnly = true; uiConfig.vanilla.fill(false);
    Picker::SetChoice(uiConfig, PackItemCode("jew"), Picker::Choice::Include);
    Picker::SetChoice(uiConfig, PackItemCode("r01"), Picker::Choice::Exclude);
    ApplyItemSelection(uiConfig);
    const auto snapshot = SnapshotSettings();
    CHECK(!snapshot.enabled && !snapshot.validateOnly && !snapshot.vanilla[0]);
    CHECK(!ExternalScan(&Player));
    CHECK(HookTrigger(nullptr, &Player, &opcode, 1) == 17 && Requests.size() == 22);
    uiConfig.enabled = true; ApplyItemSelection(uiConfig);
    CHECK(snapshot.Selects(PackItemCode("jew")) && !snapshot.Selects(PackItemCode("r01")));
    ResetItemRetries(); FreshItem(90, PackItemCode("jew")); ExpectedDistance = 4;
    CHECK(ExternalScan(&Player) && Requests.size() == 23 && Requests.back() == 90);
    FreshItem(91, PackItemCode("r01")); CHECK(ExternalScan(&Player) && Requests.size() == 23);
    Settings.enabled = false; CHECK(!ExternalScan(&Player));
    auto ranges = SnapshotSettings(); ranges.enabled = true; ranges.walkingRange = 8; ApplyItemSelection(ranges);
    ResetItemRetries(); FreshItem(92, PackItemCode("jew"), 8); ExpectedDistance = 8;
    CHECK(HookTrigger(nullptr, &Player, &opcode, 1) == 17 && Requests.size() == 24);
    FreshItem(93, PackItemCode("jew"), 8); ExpectedDistance = 4;
    CHECK(ExternalScan(&Player) && Requests.size() == 24); // Legacy V1 remains fixed at 4.
    ExpectedDistance = 9;
    CHECK(ExternalRangeScan(&Player, 9) && Requests.size() == 25); // Standalone V2 keeps the caller's own radius.
    ranges.walkingEnabled = false; ApplyItemSelection(ranges); FreshItem(94, PackItemCode("jew"), 1);
    CHECK(HookTrigger(nullptr, &Player, &opcode, 1) == 17 && Requests.size() == 25);
    CHECK(ExternalRangeScan(&Player, 9) && Requests.size() == 26);
    ResetRuntime();
    CHECK(!ScanApiReady && ItemRetries.Ready({1, 60, PackItemCode("r01")}, 6002));
    std::cout << "Native selection and placement, failed/no-progress/exception retries, other-item fairness, walking/V1/V2 shared state, session/player isolation; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
