// Compile the real hook/scheduler/collector with injected native functions.
// No game process is opened and no native game address is called.
#include "../src/post_cast/engine.hpp"
#include <iostream>

namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
struct Unit {
    std::uint32_t type{}, id{}, code{}, mode{};
    void* room{};
    Unit* next{};
    std::int32_t distance{};
    bool blocked{};
};
struct Room { void* neighbours[1]{}; Unit* first{}; } StartRoom, LandingRoom;
struct Game { Unit* player{}; } World;
Unit Player, Gold;
D2RL::PluginContext FakeContext{};
D2RL::Threads::Callback Queued{};
void* QueueToken{};
int QueueCalls{}, PickupCalls{}, LookupCalls{}, ItemCalls{};
std::uint32_t ItemRange{};
bool Items(void*, std::uint32_t range) noexcept { ++ItemCalls; ItemRange = range; return true; }
bool AcceptPickup{true};
bool MakeGoldProgress{true}, WalletReadable{true}, FaultPickup{};
std::int32_t CarriedGold{100}, PickupAmount{1};
std::uint32_t WalletLimit{1000};
void* LastEnumeratedRoom{};

void* __fastcall Lookup(void* game, std::uint32_t type, std::uint32_t id) {
    ++LookupCalls;
    if (game != &World || type != 0 || !World.player || World.player->id != id) return nullptr;
    return World.player;
}
void* __fastcall RoomOf(void* unit) { return static_cast<Unit*>(unit)->room; }
std::uint32_t __fastcall Type(void* unit) { return static_cast<Unit*>(unit)->type; }
std::uint32_t __fastcall Id(void* unit) { return static_cast<Unit*>(unit)->id; }
std::uint32_t __fastcall Code(void* unit) { return static_cast<Unit*>(unit)->code; }
std::uint32_t __fastcall Mode(void* unit) { return static_cast<Unit*>(unit)->mode; }
void __fastcall Neighbours(void* room, void*** rooms, std::uint32_t* count) {
    LastEnumeratedRoom = room;
    *rooms = static_cast<Room*>(room)->neighbours; *count = 1;
}
void* __fastcall First(void* room) { return static_cast<Room*>(room)->first; }
void* __fastcall Next(void* unit) { return static_cast<Unit*>(unit)->next; }
std::int32_t __fastcall Distance(void*, void* item) { return static_cast<Unit*>(item)->distance; }
std::int32_t __fastcall Collision(void*, void* item, std::uint32_t) { return static_cast<Unit*>(item)->blocked ? 1 : 0; }
bool Wallet(void*, Reimagined::EmbeddedTeleportPickup::GoldWallet& wallet) noexcept {
    wallet = {CarriedGold, WalletLimit}; return WalletReadable;
}
std::int32_t __fastcall ReadStat(void* player, std::int32_t stat, std::uint16_t layer) noexcept {
    CHECK(player == World.player && stat == 14 && layer == 0);
    return CarriedGold;
}
std::uint32_t __fastcall CarryLimit(void* player) {
    CHECK(player == World.player); return WalletLimit;
}
bool ReadStatWitness(void*, std::uintptr_t, std::byte* bytes, std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) bytes[i] = std::byte{0xAA};
    return true;
}
bool __fastcall Pickup(void* player, std::uint32_t id, bool, std::uint32_t maxDistance, bool, bool) {
    CHECK(player == World.player && id == Gold.id);
    CHECK(maxDistance == Reimagined::EmbeddedTeleportPickup::Settings.pickupRange);
    ++PickupCalls;
    if (FaultPickup) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (AcceptPickup && MakeGoldProgress) CarriedGold += PickupAmount;
    return AcceptPickup;
}
D2RL::Threads::Result __cdecl Queue(const D2RL::PluginContext*, D2RL::Threads::Callback callback, void* token) noexcept {
    Queued = callback; QueueToken = token; ++QueueCalls;
    return D2RL::Threads::Result::Success;
}
const D2RL::ThreadServiceV1 FakeThreads{D2RL::ThreadServiceV1Size, D2RL::ThreadServiceV1Version, nullptr, Queue};
}
int main() {
    using namespace Reimagined::EmbeddedTeleportPickup;
    StartRoom.neighbours[0] = &StartRoom;
    LandingRoom.neighbours[0] = &LandingRoom;
    Player = {0, 42, 0, 0, &StartRoom}; World.player = &Player;
    Gold = {4, 99, 0x20646C67, 3, &LandingRoom}; LandingRoom.first = &Gold;
    Context = &FakeContext; Threads = &FakeThreads;
    GetServerUnit = Lookup; GetUnitRoom = RoomOf; UnitType = Type; UnitId = Id;
    GetItemCode = Code; UnitMode = Mode; GetRoomNeighbours = Neighbours;
    GetRoomFirstUnit = First; GetNextUnit = Next; UnitDistance = Distance;
    UnitCollision = Collision; Reimagined::EmbeddedTeleportPickup::Pickup = ::Pickup;
    ReadGoldWallet = Wallet;
    Settings.validateOnly = false; Settings.includeItemPlugin = false;
    Settings.diagnosticsEnabled = false;
    FollowUps.Enter(1);

    GoldWallet wallet;
    CHECK(!ReadNativeGoldWallet(&Player, wallet)); // Unbound native readers never guess.
    using namespace RuffnecKk::NativeStatCompat;
    std::array<HelperWitness, 7> helpers{};
    helpers[0] = {"test-stat", 0, "AA", 0, "", "", 0, 0, {}, {}};
    const AdmissionContract contract{"test-only", helpers};
    CHECK(NativeStats.Bind({nullptr, ReadStatWitness}, {reinterpret_cast<std::uintptr_t>(&ReadStat), 1}, {}, contract, ToMask(Helper::GetUnitStat)));
    GetInventoryGoldLimit = CarryLimit;
    CHECK(ReadNativeGoldWallet(&Player, wallet) && wallet.carried == CarriedGold && wallet.limit == WalletLimit);
    CarriedGold = -1; CHECK(!ReadNativeGoldWallet(&Player, wallet));
    CarriedGold = 100;

    std::uint32_t id{};
    // Regression: an active room is not the server game pointer. This was the
    // cause of every cast being rejected in 0.1.0.
    CHECK(GetUnitRoom(&Player) != &World);
    CHECK(CaptureActor(&World, &Player, id) && id == 42);
    CHECK(!CaptureActor(&StartRoom, &Player, id));
    ScheduleFollowUp(&World, &Player);
    CHECK(QueueCalls == 1 && Queued != nullptr);
    Player.room = &LandingRoom; // Teleport finishes after the action callback.
    CHECK(FindPlayer(&World, 42) == &Player); // ID lookup survives room change.
    Sleep(150);
    if (Queued) Queued(Context, QueueToken); // Later SDK update, with no walk.
    CHECK(PickupCalls == 1 && Metrics.goldPicked.load() == 1);
    CHECK(LastEnumeratedRoom == &LandingRoom && LookupCalls >= 4);
    CHECK(Metrics.followUpScans.load() == 1 && QueueCalls == 2);

    Gold.blocked = true; Scan(&Player); CHECK(PickupCalls == 1);
    Gold.blocked = false; Gold.distance = 5; Scan(&Player); CHECK(PickupCalls == 1);
    Gold.distance = 4; Scan(&Player);
    CHECK(PickupCalls == 2 && Metrics.goldPicked.load() == 2);
    Gold.distance = 8; Settings.pickupRange = 8; Scan(&Player);
    CHECK(PickupCalls == 3 && Metrics.goldPicked.load() == 3);
    Gold.distance = 9; Scan(&Player); CHECK(PickupCalls == 3);
    Settings.pickupRange = 1; Gold.distance = 2; Scan(&Player); CHECK(PickupCalls == 3);
    Gold.distance = 1; Scan(&Player); CHECK(PickupCalls == 4);
    Gold.distance = 0; AcceptPickup = false; Scan(&Player);
    CHECK(PickupCalls == 5 && Metrics.goldPicked.load() == 4);
    Settings.gold = false; Scan(&Player); CHECK(PickupCalls == 5);
    // Built-in scheduling calls the same item selector directly, without another DLL.
    ItemScan = Items; Settings.gold = true; CarriedGold = static_cast<std::int32_t>(WalletLimit);
    Settings.includeItemPlugin = true; Settings.pickupRange = 8;
    Scan(&Player); CHECK(ItemCalls == 1 && ItemRange == 8 && PickupCalls == 5);
    Settings.includeItemPlugin = false; Scan(&Player); CHECK(ItemCalls == 1);
    World.player = nullptr;
    CHECK(FindPlayer(&World, 42) == nullptr);
    CHECK(!CaptureActor(&World, &Player, id));
    StopFollowUps();
    const auto queuesBeforeStaleCallback = QueueCalls;
    if (Queued) Queued(Context, QueueToken);
    CHECK(QueueCalls == queuesBeforeStaleCallback && PickupCalls == 5);
    World.player = &Player; Player.room = &LandingRoom;
    Settings.gold = true; Settings.includeItemPlugin = false; Settings.pickupRange = 4;
    AcceptPickup = false; MakeGoldProgress = false; Gold.distance = 0;
    ResetGoldRetries();
    const auto beforeGoldChecks = PickupCalls;
    CarriedGold = 1000; WalletLimit = 1000;
    for (std::uint64_t now = 0; now < 2000; now += 100) ScanGold(&Player, now);
    CHECK(PickupCalls == beforeGoldChecks && Metrics.goldFullSkips.load() >= 20);
    CarriedGold = 1200; ScanGold(&Player, 2000); CHECK(PickupCalls == beforeGoldChecks);
    CarriedGold = 900; ScanGold(&Player, 2100); CHECK(PickupCalls == beforeGoldChecks + 1);
    Gold.id = 100; // A re-dropped remainder can have a new ID; keep the wallet delay.
    ScanGold(&Player, 2200); ScanGold(&Player, 3099);
    CHECK(PickupCalls == beforeGoldChecks + 1 && Metrics.goldRetrySkips.load() >= 2);
    ScanGold(&Player, 3100); CHECK(PickupCalls == beforeGoldChecks + 2);
    AcceptPickup = true; ScanGold(&Player, 4100); CHECK(PickupCalls == beforeGoldChecks + 3);
    // A native 'success' without a balance change must also be throttled.
    Gold.id = 101; ScanGold(&Player, 4200); CHECK(PickupCalls == beforeGoldChecks + 3);
    CarriedGold = 901; ScanGold(&Player, 4201); CHECK(PickupCalls == beforeGoldChecks + 4);
    CarriedGold = 800; ScanGold(&Player, 4202); CHECK(PickupCalls == beforeGoldChecks + 5);
    WalletLimit = 2000; ScanGold(&Player, 4203); CHECK(PickupCalls == beforeGoldChecks + 6);
    WalletReadable = false; ScanGold(&Player, 5203); CHECK(PickupCalls == beforeGoldChecks + 6);
    WalletReadable = true; CarriedGold = -1; ScanGold(&Player, 5204); CHECK(PickupCalls == beforeGoldChecks + 6);
    CarriedGold = 1999; MakeGoldProgress = true; ScanGold(&Player, 5205);
    CHECK(PickupCalls == beforeGoldChecks + 7 && CarriedGold == 2000);
    ScanGold(&Player, 5305); CHECK(PickupCalls == beforeGoldChecks + 7); // A partial pickup filled the wallet.
    CarriedGold = 1990; MakeGoldProgress = false; ScanGold(&Player, 5306);
    CHECK(PickupCalls == beforeGoldChecks + 8);
    ScanGold(&Player, 5307); CHECK(PickupCalls == beforeGoldChecks + 8);
    const D2RL::Lifecycle::GameplayEvent leave{D2RL::Lifecycle::GameplayEventRequiredSize, 0, D2RL::Lifecycle::GameplayEventKind::GameLeft, Player.id, 2, 0, 0};
    OnSessionEvent(Context, &leave, nullptr);
    ScanGold(&Player, 5308); CHECK(PickupCalls == beforeGoldChecks + 9); // Session change clears old retry state.
    ScanGold(&Player, 5309); CHECK(PickupCalls == beforeGoldChecks + 9);
    // Identical wallet values on another player do not inherit the first player's delay.
    Unit other = Player; other.id = 43; World.player = &other;
    ScanGold(&other, 5310); CHECK(PickupCalls == beforeGoldChecks + 10);
    World.player = &Player;
    CarriedGold = 1900; FaultPickup = true;
    ScanGold(&Player, 6400); CHECK(PickupCalls == beforeGoldChecks + 11);
    FaultPickup = false; ScanGold(&Player, 6500); CHECK(PickupCalls == beforeGoldChecks + 11);
    ScanGold(&Player, 7400); CHECK(PickupCalls == beforeGoldChecks + 12);
    Context = &FakeContext; Threads = &FakeThreads; World.player = &Player;
    FollowUps.Enter(3); Settings.enabled = true; Settings.followUpEnabled = true;
    ScheduleFollowUp(&World, &Player); const auto beforeDisable = PickupCalls;
    auto saved = Settings; saved.enabled = false; Apply(saved);
    if (Queued) Queued(Context, QueueToken);
    CHECK(PickupCalls == beforeDisable && FollowUps.IsActive());
    saved.enabled = true; Apply(saved); ScheduleFollowUp(&World, &Player);
    CHECK(FollowUps.IsCurrent(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(QueueToken))));
    NativeStats.Reset(); GetInventoryGoldLimit = nullptr;
    Context = nullptr;
    std::cout << "Actual scheduler/collector, full/over-cap wallets, refused/zero-progress retries, changing pile IDs, spending/cap changes, item bridge and session/player isolation; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
