#include "explicit_preferences.hpp"
#include "../src/plugin.cpp"
#include <iostream>
#include <vector>

using namespace RuffnecKk::AutoPickup;
namespace {
int failures{}, Game{}, Room{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
struct Unit {
    std::uint32_t type{}, id{}, mode{3};
    int x{};
    bool blocked{}, accepts{true};
    Unit* next{};
} Player, Item, Other, Third;
void* Rooms[]{&Room};
std::vector<std::uint32_t> Requests, Ranges;
std::vector<std::uint64_t> Queued;
bool QueueAllowed{true}, ActorPresent{true};
D2RL::PluginContext TestContext{};
void* __fastcall RoomOf(void*) { return &Room; }
void __fastcall Neighbours(void*, void*** rooms, std::uint32_t* count) { *rooms = Rooms; *count = 1; }
void* __fastcall First(void*) { return &Item; }
void* __fastcall Next(void* unit) { return static_cast<Unit*>(unit)->next; }
std::uint32_t __fastcall Type(void* unit) { return static_cast<Unit*>(unit)->type; }
std::uint32_t __fastcall Id(void* unit) { return static_cast<Unit*>(unit)->id; }
std::uint32_t __fastcall Mode(void* unit) { return static_cast<Unit*>(unit)->mode; }
std::uint32_t __fastcall Code(void*) { return PackItemCode("r01"); }
std::int32_t __fastcall Distance(void* player, void* item) { return std::abs(static_cast<Unit*>(player)->x - static_cast<Unit*>(item)->x); }
std::int32_t __fastcall Collision(void*, void* item, std::uint32_t) { return static_cast<Unit*>(item)->blocked ? 1 : 0; }
void* __fastcall Find(void* game, std::uint32_t type, std::uint32_t id) {
    return ActorPresent && game == &Game && type == 0 && id == Player.id ? &Player : nullptr;
}
bool __fastcall Request(void* player, std::uint32_t id, bool ordinary, std::uint32_t range, bool normal, bool extra) {
    CHECK(player == &Player && ordinary && normal && !extra);
    Requests.push_back(id); Ranges.push_back(range);
    for (auto* item = &Item; item; item = item->next) if (item->id == id) {
        CHECK(Distance(player, item) <= static_cast<int>(range));
        if (item->accepts) item->mode = 1;
        return item->accepts;
    }
    CHECK(false); return false;
}
auto __fastcall Movement(void*, void*, void*, std::int32_t) -> std::int64_t { Player.mode = 2; return 17; }
auto __cdecl Queue(const D2RL::PluginContext* context, D2RL::Threads::Callback callback, void* data) noexcept -> D2RL::Threads::Result {
    CHECK(context == &TestContext && callback == WalkingTick);
    if (!QueueAllowed) return D2RL::Threads::Result::Unavailable;
    Queued.push_back(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)));
    return D2RL::Threads::Result::Success;
}
const D2RL::ThreadServiceV1 Threads{D2RL::ThreadServiceV1Size, D2RL::ThreadServiceV1Version, nullptr, Queue};
void Session(D2RL::Lifecycle::GameplayEventKind kind, std::uint64_t generation) {
    const D2RL::Lifecycle::GameplayEvent event{D2RL::Lifecycle::GameplayEventRequiredSize, 0, kind, Player.id, generation, 0, 0};
    OnRetrySessionEvent(&TestContext, &event, nullptr);
}
void Fresh(std::uint32_t range = 4) {
    Settings = TestPreferences::Pickup(); Settings.walkingRange = range;
    Context = &TestContext; WalkingThreads = &Threads; GetWalkingPlayer = Find;
    Player = {0, 1, 1, 0}; Item = {4, 10, 3, 20};
    GetUnitRoom = RoomOf; GetRoomNeighbours = Neighbours; GetRoomFirstUnit = First; GetNextUnit = Next;
    UnitType = Type; UnitId = Id; UnitMode = Mode; GetItemCode = Code;
    UnitDistance = Distance; UnitCollision = Collision; Pickup = Request;
    OriginalTriggers[1] = Movement; Requests.clear(); Ranges.clear(); Queued.clear();
    QueueAllowed = ActorPresent = true;
    { std::lock_guard lock(WalkingMutex); WalkingFollowUps.Reset(0, false); }
    Session(D2RL::Lifecycle::GameplayEventKind::GameJoined, 1);
}
auto Start() -> std::uint64_t {
    std::uint8_t opcode = 1;
    CHECK(HookTrigger(&Game, &Player, &opcode, 1) == 17);
    CHECK(Queued.size() == 1);
    return GetTickCount64();
}
void Tick(std::uint64_t now) {
    CHECK(!Queued.empty());
    if (Queued.empty()) return;
    const auto ticket = Queued.back(); Queued.clear();
    WalkingTickAt(&TestContext, ticket, now);
}
}
int main() {
    // A single command begins outside range. Later updates observe the new
    // position and collect without requiring another click/movement packet.
    Fresh(8); auto now = Start(); CHECK(Requests.empty());
    Player.x = 12; Tick(now + 100);
    CHECK(Requests.size() == 1 && Requests.back() == 10 && Ranges.back() == 8);

    // Increasing the actual configured radius admits farther ground items.
    for (std::uint32_t radius = 1; radius <= 20; ++radius) {
        Fresh(radius); Item.x = static_cast<int>(radius); Start();
        CHECK(Requests.size() == 1 && Ranges.back() == radius);
        Fresh(radius); Item.x = static_cast<int>(radius) + 1; Start(); CHECK(Requests.empty());
    }

    // Long paths keep their follow-up window alive, with one queued callback.
    Fresh(); now = Start();
    for (unsigned i = 1; i <= 35; ++i) { Tick(now + i * 100); CHECK(Queued.size() == 1); }
    CHECK(Requests.empty()); Player.x = 16; Tick(now + 3600);
    CHECK(Requests.size() == 1 && Ranges.back() == 4);
    Player.mode = 1; Tick(now + 4100); CHECK(Queued.empty()); // Stop polling after settling.

    // One action can collect a group; refusal cooldowns and collision remain.
    Fresh(20); Item.x = 4; Item.accepts = false;
    Other = {4, 11, 3, 5}; Third = {4, 12, 3, 6}; Item.next = &Other; Other.next = &Third;
    now = Start(); CHECK(Requests.size() == 1 && Requests.back() == 10);
    Tick(now + 100); Tick(now + 200);
    CHECK(Requests.size() == 3 && Requests[1] == 11 && Requests[2] == 12);
    Tick(now + 300); CHECK(Requests.size() == 3);
    for (unsigned i = 4; i <= 11; ++i) Tick(now + i * 100);
    CHECK(Requests.size() == 4 && Requests.back() == 10);
    Fresh(20); Item.blocked = true; now = Start(); Tick(now + 100); CHECK(Requests.empty());

    // A saved radius change is observed by the pending walk, independently
    // of post-cast enablement and legacy standalone ownership.
    Fresh(); StandaloneTeleport = true; now = Start();
    auto config = SnapshotSettings(); config.walkingRange = 20; config.teleportEnabled = false; ApplyItemSelection(config);
    Tick(now + 100); CHECK(Requests.size() == 1 && Ranges.back() == 20);
    StandaloneTeleport = false;

    // Disable cancels outstanding work; re-enable cannot revive an old ticket.
    Fresh(); now = Start(); const auto ticket = Queued.back();
    config = SnapshotSettings(); config.enabled = false; ApplyItemSelection(config);
    config.enabled = true; ApplyItemSelection(config); Player.x = 20;
    WalkingTickAt(&TestContext, ticket, now + 100); CHECK(Requests.empty());
    Fresh(); now = Start(); config = SnapshotSettings(); config.walkingEnabled = false; ApplyItemSelection(config);
    Player.x = 20; Tick(now + 100); CHECK(Requests.empty() && Queued.empty());

    // Leave/rejoin, missing actors, diagnostics and queue failures retire work.
    Fresh(); now = Start(); Session(D2RL::Lifecycle::GameplayEventKind::GameLeft, 2);
    Session(D2RL::Lifecycle::GameplayEventKind::GameJoined, 3); Player.x = 20;
    Tick(now + 100); CHECK(Requests.empty() && Queued.empty());
    Fresh(); now = Start(); ActorPresent = false; Player.x = 20; Tick(now + 100); CHECK(Requests.empty());
    Fresh(); QueueAllowed = false; ScheduleWalkingFollowUp(&Game, &Player, 1000);
    CHECK(Queued.empty() && Metrics.walkingQueueFailures.load() > 0);
    QueueAllowed = true; ScheduleWalkingFollowUp(&Game, &Player, 1001); CHECK(Queued.size() == 1);
    Fresh(); Settings.validateOnly = true; ScheduleWalkingFollowUp(&Game, &Player); CHECK(Queued.empty());
    Fresh(); now = Start(); const auto stale = Queued.back(); StopRetryLifecycle();
    WalkingTickAt(&TestContext, stale, now + 100); CHECK(Requests.empty());
    ResetRuntime(); Context = nullptr;
    std::cout << "Walking updates, 1..20 boundaries, long paths, burst/refusal fairness, live radius, legacy ownership and lifecycle; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
