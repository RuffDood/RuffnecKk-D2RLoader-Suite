#include "explicit_preferences.hpp"
#include "runtime.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using namespace AutoInteract;
namespace {
struct Work { D2RL::Threads::Callback callback; void* data; };
std::vector<Work> queue;
std::vector<D2RL::Lifecycle::GameplayEventListener> listeners;
std::vector<D2RL::Lifecycle::ListenerHandle> removed;
bool rejectQueue{}, missingService{}, refuse{true}, busy{}, tooMany{};
unsigned failListener{}, snapshots{}, requests{}, mode{};
std::uint32_t observedPlayer{};
auto __cdecl Queue(const D2RL::PluginContext*, D2RL::Threads::Callback cb, void* data) noexcept -> D2RL::Threads::Result {
    if (rejectQueue) return D2RL::Threads::Result::Unavailable;
    queue.push_back({cb, data}); return D2RL::Threads::Result::Success;
}
auto __cdecl Register(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEventListener* listener,
        D2RL::Lifecycle::ListenerHandle* handle) noexcept -> D2RL::Lifecycle::Result {
    if (listeners.size() + 1 == failListener) return D2RL::Lifecycle::Result::Unavailable;
    listeners.push_back(*listener); *handle = listeners.size(); return D2RL::Lifecycle::Result::Success;
}
auto __cdecl Unregister(const D2RL::PluginContext*, D2RL::Lifecycle::ListenerHandle handle) noexcept -> D2RL::Lifecycle::Result {
    removed.push_back(handle); return D2RL::Lifecycle::Result::Success;
}
D2RL::ThreadServiceV1 threads{D2RL::ThreadServiceV1Size, 1, nullptr, Queue};
D2RL::LifecycleServiceV1 lifecycle{D2RL::LifecycleServiceV1Size, 1, nullptr, nullptr, Register, Unregister};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t version, const void** out) noexcept -> D2RL::ServiceQueryResult {
    *out = nullptr;
    if (missingService || version != 1) return D2RL::ServiceQueryResult::Unavailable;
    if (id == D2RL::ServiceId::Thread) *out = &threads;
    if (id == D2RL::ServiceId::Lifecycle) *out = &lifecycle;
    return *out ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
auto Snapshot(void*, std::uint32_t player, std::span<Object> objects, std::size_t& count) noexcept -> bool {
    ++snapshots; observedPlayer = player;
    if (busy) { count = 0; return true; }
    if (tooMany) { count = objects.size() + 1; return true; }
    objects[0] = {{player, 10, 100}, Kind::Chest, mode, 2, true, false, false, false, true};
    objects[1] = {{player, 11, 100}, Kind::Chest, mode, 3, true, false, false, false, true};
    count = 2; return true;
}
auto Request(void*, Identity, const Config&) noexcept -> bool { ++requests; return !refuse; }
void Reset() {
    queue.clear(); listeners.clear(); removed.clear();
    rejectQueue = missingService = busy = tooMany = false; refuse = true;
    failListener = snapshots = requests = mode = 0;
    threads.serviceSize = D2RL::ThreadServiceV1Size; lifecycle.serviceVersion = 1;
}
auto Take() -> Work { CHECK(!queue.empty()); auto w = queue.front(); queue.erase(queue.begin()); return w; }
void Run(Runtime& runtime, const D2RL::PluginContext* ctx, std::uint64_t now) {
    auto w = Take(); runtime.Tick(ctx, reinterpret_cast<std::uintptr_t>(w.data), now);
}
void Event(Runtime& runtime, const D2RL::PluginContext* ctx, D2RL::Lifecycle::GameplayEventKind kind,
        std::uint64_t generation = 1, std::uint32_t player = 7) {
    const D2RL::Lifecycle::GameplayEvent event{D2RL::Lifecycle::GameplayEventSize, 0, kind, player, generation, 0, 0};
    runtime.OnSession(ctx, &event);
}
}
int main() {
    D2RL::PluginApi api{}; api.apiSize = D2RL::PluginApiSize; api.queryService = Query;
    D2RL::PluginContext ctx{}; ctx.contextSize = D2RL::PluginContextSize; ctx.apiVersion = 3; ctx.api = &api;
    Config config=TestPreferences::Interact();
    NativePort port{nullptr, Snapshot, Request};
    Runtime runtime;
    Reset(); CHECK(runtime.Start(&ctx, config, port)); CHECK(listeners.size() == 3 && queue.empty());
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::GameJoined);
    CHECK(queue.empty());
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady);
    CHECK(queue.size() == 1);
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady);
    CHECK(queue.size() == 1);
    Run(runtime, &ctx, 100); CHECK(snapshots == 1 && requests == 1 && queue.size() == 1 && observedPlayer == 7);
    Run(runtime, &ctx, 150); CHECK(snapshots == 1 && requests == 1 && queue.size() == 1);
    Run(runtime, &ctx, 200); CHECK(requests == 2); // another chest despite refusal
    Run(runtime, &ctx, 300); CHECK(requests == 2);
    mode = 1; Run(runtime, &ctx, 400); CHECK(runtime.Status().engine.completed == 2 && requests == 2);
    auto old = Take();
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::GameLeft);
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, 2, 7);
    runtime.Tick(&ctx, reinterpret_cast<std::uintptr_t>(old.data), 500);
    CHECK(queue.size() == 1 && requests == 2); // stale callback neither scans nor requeues
    mode = 0; Run(runtime, &ctx, 500); CHECK(requests == 3);
    busy = true; Run(runtime, &ctx, 600); CHECK(requests == 3);
    busy = false; tooMany = true; Run(runtime, &ctx, 700); CHECK(runtime.Status().readFailures == 1 && requests == 3);
    tooMany = false; rejectQueue = true; Run(runtime, &ctx, 800);
    CHECK(queue.empty() && runtime.Status().queueFailures == 1);
    rejectQueue = false;
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, 2, 7);
    CHECK(queue.size() == 1); // readiness after a retired queue can retry
    old = Take(); runtime.Stop(); CHECK(removed.size() == 3);
    old.callback(&ctx, old.data); CHECK(queue.empty());
    // Missing/short/incompatible services and each partial registration failure
    // clean up and never start native work.
    Reset(); missingService = true; CHECK(!runtime.Start(&ctx, config, port)); CHECK(listeners.empty());
    Reset(); threads.serviceSize = 8; CHECK(!runtime.Start(&ctx, config, port));
    Reset(); lifecycle.serviceVersion = 2; CHECK(!runtime.Start(&ctx, config, port));
    for (unsigned i = 1; i <= 3; ++i) {
        Reset(); failListener = i; CHECK(!runtime.Start(&ctx, config, port)); CHECK(removed.size() == i - 1 && queue.empty());
    }
    Reset(); config.enabled = false; CHECK(runtime.Start(&ctx, config, port)); CHECK(listeners.empty()); runtime.Stop();
    Reset(); config = TestPreferences::Interact(); config.validateOnly = true; CHECK(runtime.Start(&ctx, config, port)); CHECK(listeners.empty()); runtime.Stop();
    // Out-of-box work uses only SDK listeners/queues: the fixture supplies no
    // patch or inline-hook functions and no pickup-plugin interface at all.
    Reset(); config = TestPreferences::Interact(); CHECK(runtime.Start(&ctx, config, port));
    Event(runtime, &ctx, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, 5, 9);
    old = Take(); old.callback(&ctx, old.data); CHECK(requests == 1 && observedPlayer == 9 && queue.size() == 1);
    Config changed=TestPreferences::Interact(); changed.enabled = false;
    CHECK(runtime.Reconfigure(changed, [] { return true; }));
    const auto prior = snapshots;
    Run(runtime, &ctx, UINT64_C(1000000000000000)); CHECK(snapshots == prior && requests == 1 && queue.size() == 1);
    changed.enabled = true; changed.interactRange = 1;
    CHECK(runtime.Reconfigure(changed, [] { return true; }));
    Run(runtime, &ctx, UINT64_C(1000000000000100)); CHECK(snapshots == prior + 1 && requests == 1);
    changed.interactRange = 0;
    CHECK(!runtime.Reconfigure(changed, [] { return false; }));
    Run(runtime, &ctx, UINT64_C(1000000000000200)); CHECK(requests == 1); // refused save retains cap
    CHECK(runtime.Reconfigure(changed, [] { return true; }));
    Run(runtime, &ctx, UINT64_C(1000000000000300)); CHECK(requests == 2 && queue.size() == 1);
    runtime.Stop();
    std::cout << "SDK runtime, refusal, lifecycle and cleanup passed\n";
}
