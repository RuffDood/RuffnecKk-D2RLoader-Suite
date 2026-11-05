#include "explicit_preferences.hpp"
#include "native.hpp"
#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using namespace AutoInteract;
namespace {
struct Unit { std::uint32_t type{}, klass{}, id{}, mode{}; void* data{}; bool dead{}, selectable{true}, reachable{true}, blocked{}; int distance{2}, quantity{1}; };
struct Data { void* record{}; std::uint8_t interact{}; };
std::array<std::uint8_t, 0x168> record;
std::array<std::uint8_t, 0x107> game;
Unit player, object, key;
Data objectData;
std::vector<Unit*> units;
void* rooms[1];
int inventory, room;
bool cursor{}, badRow{}, loop{}, failRead{}, socketed{}, hasKey{}, invalidCount{};
unsigned requests{};
unsigned reachCalls{}, collisionCalls{};
auto __fastcall Type(void* p) -> std::uint32_t { return static_cast<Unit*>(p)->type; }
auto __fastcall Id(void* p) -> std::uint32_t { return static_cast<Unit*>(p)->id; }
auto __fastcall Mode(void* p) -> std::uint32_t { return static_cast<Unit*>(p)->mode; }
auto __fastcall Dead(void* p) -> std::uint32_t { return static_cast<Unit*>(p)->dead; }
auto __fastcall Selectable(void* p) -> std::uint32_t { return static_cast<Unit*>(p)->selectable; }
auto __fastcall Game(void*) -> void* { return game.data(); }
auto __fastcall Inventory(void*) -> void* { return &inventory; }
auto __fastcall Cursor(void*) -> void* { return cursor ? &key : nullptr; }
auto __fastcall Room(void*) -> void* {
    if (failRead) RaiseException(0xE0000001, 0, 0, nullptr);
    return &room;
}
void __fastcall Neighbours(void*, void*** out, std::uint32_t* count) { *out = rooms; *count = 1; }
auto __fastcall First(void*) -> void* { return units.empty() ? nullptr : units[0]; }
auto __fastcall Next(void* p) -> void* {
    if (loop) return p;
    for (std::size_t i = 0; i < units.size(); ++i) if (units[i] == p) return i + 1 < units.size() ? units[i + 1] : nullptr;
    return nullptr;
}
auto __fastcall Lookup(void* g, std::uint32_t type, std::uint32_t id) -> void* {
    if (g != game.data()) return nullptr;
    if (type == 0 && player.id == id) return &player;
    if (type == 2 && object.id == id) return &object;
    return nullptr;
}
auto __fastcall Record(void* p) -> void* { return static_cast<Data*>(static_cast<Unit*>(p)->data)->record; }
auto __fastcall Count(std::uint8_t bank) -> std::int32_t { return bank == 3 && !invalidCount ? 1000 : -1; }
auto __fastcall Row(std::uint8_t bank, std::uint32_t klass) -> void* { return !badRow && bank == 3 && klass == object.klass ? record.data() : nullptr; }
auto __fastcall Distance(void*, void* p) -> std::int32_t { return static_cast<Unit*>(p)->distance; }
auto __fastcall Reach(void*, void* p) -> std::int32_t { ++reachCalls; return static_cast<Unit*>(p)->reachable; }
auto __fastcall Collision(void*, void* p, std::uint32_t mask) -> std::int32_t { ++collisionCalls; CHECK(mask == 0x804); return static_cast<Unit*>(p)->blocked; }
auto __fastcall Operate(void* g, void* p, std::uint32_t id, std::uint32_t item, bool checkCollision) -> std::int32_t {
    CHECK(g == game.data() && p == &player && id == object.id && item == UINT32_MAX && checkCollision);
    ++requests; object.mode = 1; return 0;
}
auto __fastcall FirstItem(void*) -> void* { return hasKey ? &key : nullptr; }
auto __fastcall NextItem(void*) -> void* { return nullptr; }
auto __fastcall ItemType(void* p, std::uint32_t type) -> std::int32_t { CHECK(type == 41); return p == &key; }
auto __fastcall ItemPage(void*) -> std::uint32_t { return socketed ? 4U : 0U; }
auto Quantity(void*, void* p) noexcept -> std::int32_t { return static_cast<Unit*>(p)->quantity; }
auto Bindings() -> NativeBindings {
    NativeBindings b;
    b.room = Room; b.first = First; b.next = Next; b.game = Game; b.record = Record;
    b.inventory = Inventory; b.cursor = Cursor; b.firstItem = FirstItem; b.nextItem = NextItem;
    b.type = Type; b.id = Id; b.mode = Mode; b.dead = Dead; b.selectable = Selectable;
    b.lookup = Lookup; b.neighbours = Neighbours; b.count = Count; b.row = Row;
    b.distance = Distance; b.reach = Reach; b.collision = Collision; b.operate = Operate;
    b.itemType = ItemType; b.itemPage = ItemPage; b.quantity = Quantity;
    return b;
}
void Reset() {
    player = {0, 1, 7, 1}; object = {2, 111, 10, 0}; key = {4, 200, 20, 0};
    record = {}; std::memcpy(record.data(), "ordinary", 9);
    record[0x85] = 1; record[0x127] = 8; record[0x15C] = 3; record[0x15D] = 7;
    record[0x15E] = 4; record[0x15F] = 11; // adjacent byte fields must stay separate
    objectData = {record.data(), 0}; object.data = &objectData;
    game = {}; game[0x106] = 3; units = {&player, &object}; rooms[0] = &room;
    cursor = badRow = loop = failRead = socketed = hasKey = invalidCount = false; requests = 0;
}
auto Event(std::uint64_t generation = 1) -> D2RL::Lifecycle::GameplayEvent {
    return {D2RL::Lifecycle::GameplayEventSize, 0, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, 7, generation, 0, 0};
}
}
namespace AutoInteract {
struct NativeTestAccess {
    static void Setup(Native& native, Config c = TestPreferences::Interact()) { native.config_ = c; native.b_ = Bindings(); native.active_.store(true); }
    static void Catalog(Native& native, const std::filesystem::path& root) {
        D2RL::PluginApi api{};
        api.logInfo = api.logWarn = [](const D2RL::PluginContext*, const char*) noexcept {};
        D2RL::PluginContext context{}; context.api = &api; context.activeMod = "Fixture"; context.modDirectory = root.c_str();
        native.context_ = &context; native.LoadCatalogs(); native.context_ = nullptr;
    }
    static void ClearCatalog(Native& native) { native.catalogs_ = {}; }
};
}
int main() {
    Reset(); Native native; NativeTestAccess::Setup(native);
    native.Session(Event());
    std::array<Object, 8> out; std::size_t count{};
    CHECK(native.Snapshot(7, out, count) && count == 0); // needs witnessed server actor
    native.Observe(game.data(), &object); CHECK(native.ActorObservations() == 0);
    native.Observe(game.data(), &player); CHECK(native.ActorObservations() == 1);
    CHECK(native.Snapshot(7, out, count) && count == 1 && out[0].kind == Kind::Chest);
    CHECK(native.Request(out[0].id, TestPreferences::Interact())); CHECK(requests == 1 && object.mode == 1);
    CHECK(!native.Request(out[0].id, TestPreferences::Interact()) && requests == 1); // already used
    // User-reported scenery containers must traverse the actual native
    // snapshot/request adapter, with active rows and ordinary dispatcher args.
    struct ContainerCase { const char* key; std::uint8_t init, op; };
    for (const auto& c : {ContainerCase{"Log", 0, 14}, ContainerCase{"HiddenStash", 0, 14},
            ContainerCase{"Rock", 0, 14}, ContainerCase{"Basket1", 2, 3}, ContainerCase{"DungeonBasket", 33, 3},
            ContainerCase{"JunglePillar1", 2, 51}, ContainerCase{"Barrel", 0, 5}, ContainerCase{"SewerChest", 2, 4}}) {
        std::memset(record.data(), 0, 64); std::memcpy(record.data(), c.key, std::strlen(c.key));
        record[0x15C] = c.init; record[0x15E] = c.op; object.mode = 0;
        objectData.interact = c.op == 4 ? 0 : 0x80; // high bit is a lock only for native chest operation 4
        CHECK(native.Snapshot(7, out, count) && count == 1 && out[0].kind == Kind::Chest);
        Config off; off.chests = false; CHECK(!native.Request(out[0].id, off));
        const auto before = requests; CHECK(native.Request(out[0].id, TestPreferences::Interact()) && requests == before + 1 && object.mode == 1);
    }
    std::memset(record.data(), 0, 64); std::memcpy(record.data(), "ordinary", 9);
    record[0x15C] = 3; record[0x15E] = 4; objectData.interact = 0;
    object.mode = 0;
    Config ranged=TestPreferences::Interact(); ranged.interactRange = 20; NativeTestAccess::Setup(native, ranged);
    object.distance = 20; object.reachable = false; reachCalls = collisionCalls = 0;
    CHECK(native.Snapshot(7, out, count) && count == 1);
    CHECK(reachCalls == 0 && collisionCalls == 1 && !out[0].inReach);
    CHECK(native.Request(out[0].id, ranged) && object.mode == 1); // distance 20, original dispatcher, no movement API
    std::string source = "Class\tInitFn\tOperateFn\tSubClass\tSelectable0\tIsDoor\n";
    for (unsigned i = 0; i < 1000; ++i) source += i == 111 ? "Log\t0\t14\t8\t1\t0\n" : "Unused" + std::to_string(i) + "\t0\t0\t0\t0\t0\n";
    namespace fs = std::filesystem;
    const auto fixture = fs::temp_directory_path() / ("auto-interact-catalog-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    const auto excel = fixture / "Fixture.mpq/data/global/excel";
    CHECK(fs::create_directories(excel));
    { std::ofstream file(excel / "objects.txt", std::ios::binary); file << source; }
    NativeTestAccess::Catalog(native, fixture);
    std::memset(record.data(), 0, 64); record[0] = 111; record[0x15C] = 0; record[0x15E] = 14; object.mode = 0;
    CHECK(native.Snapshot(7, out, count) && count == 1 && out[0].kind == Kind::Chest);
    CHECK(native.Request(out[0].id, ranged)); // numeric compiled Class field, source key plus active tuple
    // A BIN shadow without -txt makes loose source keys untrustworthy.
    { std::ofstream file(excel / "objects.bin", std::ios::binary); file << "shadow"; }
    NativeTestAccess::Catalog(native, fixture); object.mode = 0;
    CHECK(native.Snapshot(7, out, count) && count == 0);
    CHECK(fs::remove(excel / "objects.bin") && fs::remove(excel / "objects.txt"));
    for (auto dir = excel; dir != fixture.parent_path(); dir = dir.parent_path()) CHECK(fs::remove(dir));
    NativeTestAccess::ClearCatalog(native);
    std::memset(record.data(), 0, 64); std::memcpy(record.data(), "ordinary", 9); record[0x15C] = 3; record[0x15E] = 4;
    object.mode = 0; object.distance = 21; collisionCalls = 0;
    CHECK(!native.Request({7, 10, 111}, ranged) && collisionCalls == 0);
    object.distance = 20; object.blocked = true; CHECK(!native.Request({7, 10, 111}, ranged)); object.blocked = false;
    object.selectable = false; CHECK(!native.Request({7, 10, 111}, ranged)); object.selectable = true;
    object.mode = 2; reachCalls = collisionCalls = 0; CHECK(native.Snapshot(7, out, count) && count == 1);
    CHECK(reachCalls == 0 && collisionCalls == 0); // used objects only supply completion identity/mode
    object.mode = 0; std::memset(record.data(), 0, 64); std::memcpy(record.data(), "Barrel", 6);
    record[0x15C] = 0; record[0x15E] = 5;
    CHECK(!native.Request({7, 10, 111}, ranged)); // native kick barrels retain reach, preventing walk-to-skill fallback
    std::memset(record.data(), 0, 64); std::memcpy(record.data(), "ordinary", 9);
    record[0x15C] = 3; record[0x15E] = 4; object.distance = 2; object.reachable = true;
    NativeTestAccess::Setup(native);
    for (unsigned mode : {0U, 2U, 3U, 4U, 7U, 10U, 17U, 19U}) {
        player.mode = mode; CHECK(native.Snapshot(7, out, count) && count == 0);
        CHECK(!native.Request({7, 10, 111}, TestPreferences::Interact()));
    }
    player.mode = 5; CHECK(native.Snapshot(7, out, count) && count == 1);
    player.dead = true; CHECK(native.Snapshot(7, out, count) && count == 0); player.dead = false;
    cursor = true; CHECK(native.Snapshot(7, out, count) && count == 0); cursor = false;
    CHECK(!native.Request({7, 10, 112}, TestPreferences::Interact())); // class changed between selection/request
    object.reachable = false; CHECK(!native.Request({7, 10, 111}, TestPreferences::Interact())); object.reachable = true;
    object.blocked = true; CHECK(!native.Request({7, 10, 111}, TestPreferences::Interact())); object.blocked = false;
    object.selectable = false; CHECK(!native.Request({7, 10, 111}, TestPreferences::Interact())); object.selectable = true;
    badRow = true; CHECK(native.Snapshot(7, out, count) && count == 0); badRow = false;
    invalidCount = true; CHECK(native.Snapshot(7, out, count) && count == 0); invalidCount = false;
    record[0x15E] = 68; CHECK(native.Snapshot(7, out, count) && count == 0); record[0x15E] = 4;
    record[0x15C] = 45; CHECK(native.Snapshot(7, out, count) && count == 0); record[0x15C] = 3;
    record[0xFA] = 1; CHECK(native.Snapshot(7, out, count) && count == 0); record[0xFA] = 0;
    for (std::uint8_t subclass : std::array<std::uint8_t, 7>{1, 2, 4, 16, 32, 64, 128}) {
        record[0x127] = subclass; CHECK(native.Snapshot(7, out, count) && count == 0);
    }
    record[0x127] = 8;
    objectData.interact = 10; CHECK(!native.Request({7, 10, 111}, TestPreferences::Interact()));
    objectData.interact = 0x80; CHECK(!native.Request({7, 10, 111}, TestPreferences::Interact()));
    Config locked=TestPreferences::Interact(); locked.lockedChests = true; NativeTestAccess::Setup(native, locked);
    CHECK(!native.Request({7, 10, 111}, locked));
    hasKey = true; key.quantity = 0; CHECK(!native.Request({7, 10, 111}, locked));
    key.quantity = 1; socketed = true; CHECK(!native.Request({7, 10, 111}, locked)); socketed = false;
    CHECK(native.Request({7, 10, 111}, locked)); CHECK(key.quantity == 1); // adapter never consumes the key
    object.mode = 0; hasKey = false; player.klass = 6;
    CHECK(native.Request({7, 10, 111}, locked)); // native Assassin exemption
    object.mode = 0; objectData.interact = 0;
    failRead = true; CHECK(!native.Snapshot(7, out, count) && count == 0); failRead = false;
    loop = true; CHECK(!native.Snapshot(7, out, count)); loop = false;
    units = {&object, &object}; std::array<Object, 1> small;
    CHECK(!native.Snapshot(7, small, count)); units = {&player, &object};
    native.Session(Event(2)); CHECK(native.Snapshot(7, out, count) && count == 0);
    CHECK(!native.Request({7, 10, 111}, locked));
    native.Observe(game.data(), &player); CHECK(native.Snapshot(7, out, count) && count == 1);
    player.id = 8; CHECK(native.Snapshot(7, out, count) && count == 0); player.id = 7;
    native.Stop(); CHECK(native.Snapshot(7, out, count) && count == 0);
    std::cout << "native identity, busy/reach/collision, locks, revalidation and bounds passed\n";
}
