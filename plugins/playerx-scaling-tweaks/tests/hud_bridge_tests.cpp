#include <D2RLPlugin/api.h>
#include <D2RLPlugin/threads.h>
#include "settings_menu.hpp"
#include "native_fingerprint.hpp"
#include "hud/player_hud_slider.hpp"
#include "hud/slider_policy.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace ruffneckk::player_scaling;
namespace {
int checks{}, applied{}, displayed{}, queued{};
bool allowed = true, queueWorks = true;
std::int32_t actual = 64;
Config Settings;
menu::State LiveSettings;
std::atomic_bool Operational{true};
std::uintptr_t Base{};
std::atomic_int32_t PendingHudPlayers{1};
const D2RL::ThreadServiceV1* HudThreads{};
const D2RL::PluginContext* Context{};
D2RL::Threads::Callback callback{};
void* callbackData{};
struct Hud {
    bool started = false;
    template<class... Args> bool Start(Args&&...) { started = true; return true; }
    void Stop() { started = false; }
    void SetCurrentPlayers(std::int32_t value) { displayed = value; }
} HudSlider;
void Check(bool ok, const char* message) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
template<class T> bool TryRead(const void*, std::size_t offset, T& value) {
    Check(offset == 0x2AA6A30, "HUD reads exactly the attested artificial count global");
    value = actual; return true;
}
bool CanApplyPlayerControl() noexcept { return allowed && Operational.load() && !Settings.battleNetSimulationEnabled; }
bool ApplyPlayerControl(std::int32_t players) noexcept { ++applied; actual = players; return true; }
#include "hud_bridge.inc"
auto __cdecl Queue(const D2RL::PluginContext*, D2RL::Threads::Callback cb, void* data) noexcept -> D2RL::Threads::Result {
    ++queued;
    if (!queueWorks) return D2RL::Threads::Result::Unavailable;
    callback = cb; callbackData = data; return D2RL::Threads::Result::Success;
}
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId, uint32_t, const void**) noexcept -> D2RL::ServiceQueryResult {
    return D2RL::ServiceQueryResult::Unavailable;
}
void __cdecl Log(const D2RL::PluginContext*, const char*) noexcept {}
}
int main() {
    using namespace players_hud_slider;
    D2RL::PluginApi api{}; api.apiSize = sizeof(api); api.queryService = Query; api.logInfo = Log; api.logWarn = Log;
    D2RL::PluginContext context{}; context.contextSize = sizeof(context); context.api = &api; Context = &context;
    D2RL::ThreadServiceV1 threads{sizeof(threads), 1, Queue, Queue};
    Settings.maximumCommandPlayers = 64; LiveSettings.Initialize(Settings); HudThreads = &threads;
    Check(ReadHudPlayers() == 64, "actual p64 displayed without clamping or applying");
    Check(applied == 0, "reading/opening does not apply anything");
    for (int n : {1, 8, 16, 24}) {
        const auto before = applied;
        Check(QueueHudPlayers(n), "selection queues");
        Check(applied == before, "UI callback does not mutate native game state");
        callback(Context, callbackData);
        Check(applied == before + 1 && actual == n && displayed == n, "game callback applies selected count");
    }
    Check(Settings.maximumCommandPlayers == 64 && Settings.startGamePlayers == 0, "HUD preserves startup configuration");
    Check(QueueHudPlayers(64), "out-of-range request queues defensively"); callback(Context, callbackData);
    Check(actual == 24, "HUD application never exceeds p24");
    queueWorks = false; const auto before = applied;
    Check(!QueueHudPlayers(8) && applied == before, "remote/unavailable game thread never applies locally"); queueWorks = true;
    Check(QueueHudPlayers(16), "queue before mode transition"); allowed = false;
    callback(Context, callbackData); Check(applied == before, "eligibility checked again when work executes"); allowed = true;
    Settings.battleNetSimulationEnabled = true;
    Check(!QueueHudPlayers(8), "Battle.net simulation denies edits"); Settings.battleNetSimulationEnabled = false;
    Config hidden = Settings; hidden.hudEnabled = false; LiveSettings.Initialize(hidden);
    Check(!QueueHudPlayers(8), "live HUD visibility disables adjustment"); LiveSettings.Initialize(Settings);
    Settings.minimumScalingPlayers = 4;
    Check(QueueHudPlayers(1), "lower input queued"); callback(Context, callbackData); Check(actual == 4, "running minimum enforced");
    Settings.minimumScalingPlayers = 25; Check(!CanUseHud(), "no HUD when baseline exceeds p24"); Settings.minimumScalingPlayers = 1;
    Settings.maximumCommandPlayers = 8; Check(QueueHudPlayers(24), "bound clamp queued"); callback(Context, callbackData); Check(actual == 8, "lower command bound respected");
    Operational.store(false); Check(!QueueHudPlayers(4), "unload prevents application"); Operational.store(true);
    HudThreads = nullptr; StartHud(); Check(!HudSlider.started, "missing UI/thread service leaves existing plugin operational");
    for (int minimum : {1, 4, 24}) for (int maximum : {24}) {
        Check(CountAt(-1.0, minimum, maximum) == minimum, "left endpoint bound");
        Check(CountAt(2.0, minimum, maximum) == maximum, "right endpoint bound");
        for (int i = 0; i <= 100; ++i) {
            const auto count = CountAt(i / 100.0, minimum, maximum);
            Check(count >= minimum && count <= maximum, "all drag fractions stay bounded");
        }
    }
    Check(SelectionMaximum(64) == 24 && SelectionMaximum(8) == 8, "HUD cap is independent of command limit");
    Check(DisplayCount(64) == 64 && DisplayCount(65535) == 65535, "actual display preserves counts above p24");
    std::printf("%d HUD bridge/range checks passed\n", checks);
}
