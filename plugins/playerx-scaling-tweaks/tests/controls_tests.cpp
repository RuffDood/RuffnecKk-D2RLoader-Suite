#include "player_controls.hpp"
#include <cstdlib>
#include <iostream>

using namespace ruffneckk::player_scaling;

void Require(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct LoaderHarness {
    static inline LoaderHarness* current{};
    std::array<D2RL::Input::ActionRegistration, 8> actions{};
    std::array<D2RL::Lifecycle::GameplayEventListener, 3> listeners{};
    std::size_t actionCount{}, listenerCount{}, removedActions{}, removedListeners{};
    std::vector<std::int32_t> applied{};
    bool available{true}, eligible{true}, applyWorks{true}, failListener{};
    D2RL::PluginApi api{.apiSize = D2RL::PluginApiSize, .queryService = Query};
    D2RL::PluginContext context{.contextSize = sizeof(D2RL::PluginContext), .apiVersion = 3, .api = &api};
    D2RL::InputServiceV1 input{D2RL::InputServiceV1Size, 1, RegisterAction, UnregisterAction, nullptr};
    D2RL::LifecycleServiceV1 lifecycle{D2RL::LifecycleServiceV1Size, 1, nullptr, nullptr, RegisterListener, UnregisterListener};

    LoaderHarness() { current = this; }
    static auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id,
            std::uint32_t, const void** service) noexcept -> D2RL::ServiceQueryResult {
        *service = nullptr;
        if (!current->available) return D2RL::ServiceQueryResult::Unavailable;
        if (id == D2RL::ServiceId::Input) *service = &current->input;
        if (id == D2RL::ServiceId::Lifecycle) *service = &current->lifecycle;
        return *service ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::UnknownService;
    }
    static auto __cdecl RegisterAction(const D2RL::PluginContext*,
            const D2RL::Input::ActionRegistration* action, D2RL::Input::ActionHandle* handle) noexcept -> D2RL::Input::Result {
        current->actions[current->actionCount++] = *action;
        *handle = current->actionCount;
        return D2RL::Input::Result::Success;
    }
    static auto __cdecl UnregisterAction(const D2RL::PluginContext*, D2RL::Input::ActionHandle) noexcept -> D2RL::Input::Result {
        ++current->removedActions;
        return D2RL::Input::Result::Success;
    }
    static auto __cdecl RegisterListener(const D2RL::PluginContext*,
            const D2RL::Lifecycle::GameplayEventListener* listener, D2RL::Lifecycle::ListenerHandle* handle) noexcept -> D2RL::Lifecycle::Result {
        if (current->failListener) return D2RL::Lifecycle::Result::Unavailable;
        current->listeners[current->listenerCount++] = *listener;
        *handle = current->listenerCount;
        return D2RL::Lifecycle::Result::Success;
    }
    static auto __cdecl UnregisterListener(const D2RL::PluginContext*, D2RL::Lifecycle::ListenerHandle) noexcept -> D2RL::Lifecycle::Result {
        ++current->removedListeners;
        return D2RL::Lifecycle::Result::Success;
    }
    static auto Eligible() noexcept -> bool { return current->eligible; }
    static auto Apply(std::int32_t players) noexcept -> bool {
        if (!current->applyWorks) return false;
        current->applied.push_back(players);
        return true;
    }
    void Event(D2RL::Lifecycle::GameplayEventKind kind, std::uint64_t generation) {
        const D2RL::Lifecycle::GameplayEvent event{
            .structSize = D2RL::Lifecycle::GameplayEventSize,
            .kind = kind, .sessionGeneration = generation};
        for (std::size_t i = 0; i < listenerCount; ++i) {
            if (listeners[i].kind == kind) listeners[i].callback(&context, &event, listeners[i].userData);
        }
    }
    auto Key(std::size_t index, D2RL::Input::ActionEventKind kind) -> D2RL::Input::ActionResult {
        const D2RL::Input::ActionEvent event{
            .structSize = D2RL::Input::ActionEventSize, .action = index + 1,
            .kind = kind, .binding = actions[index].defaultPrimary};
        return actions[index].callback(&context, &event, actions[index].userData);
    }
};

int main() {
    using namespace D2RL::Lifecycle;
    using namespace D2RL::Input;
    Config config{};
    config.maximumCommandPlayers = 32;
    config.startGamePlayers = 16;
    config.hotkeys = {{0x31, 1, 1}, {0x32, 1, 2}};
    {
        LoaderHarness loader;
        PlayerControls controls;
        Require(controls.Start(&loader.context, config, LoaderHarness::Eligible, LoaderHarness::Apply), "register controls");
        Require(loader.actions[0].defaultPrimary.key == Key::Number1
            && loader.actions[0].defaultPrimary.modifier == Modifier::Shift, "first TOML binding registered");
        Require(loader.Key(0, ActionEventKind::Pressed) == ActionResult::Ignored, "menu hotkey ignored");
        loader.Event(GameplayEventKind::GameJoined, 1);
        Require(loader.applied.empty(), "join alone does not write before ready");
        loader.Event(GameplayEventKind::LocalPlayerReady, 1);
        Require(loader.applied == std::vector<std::int32_t>{16}, "startup sets p16");
        Require(loader.Key(0, ActionEventKind::Pressed) == ActionResult::Handled, "hotkey sets p1");
        loader.Key(0, ActionEventKind::Pressed);
        loader.Event(GameplayEventKind::LocalPlayerReady, 1);
        loader.Event(GameplayEventKind::GameJoined, 1);
        Require(loader.applied == std::vector<std::int32_t>{16, 1}, "repeat keys and ready events preserve manual choice");
        loader.Key(0, ActionEventKind::Released);
        loader.Key(0, ActionEventKind::Pressed);
        Require(loader.applied.size() == 3, "fresh press applies once");
        loader.Event(GameplayEventKind::GameLeft, 1);
        Require(loader.Key(1, ActionEventKind::Pressed) == ActionResult::Ignored, "left game blocks controls");
        loader.Event(GameplayEventKind::LocalPlayerReady, 2);
        Require(loader.applied.back() == 16 && loader.applied.size() == 4, "next game reapplies startup");
        loader.eligible = false;
        Require(loader.Key(1, ActionEventKind::Pressed) == ActionResult::Ignored, "native mode restriction respected");
        loader.Event(GameplayEventKind::LocalPlayerReady, 3);
        Require(loader.applied.size() == 4, "restricted mode blocks startup");
        controls.Stop();
        Require(loader.removedActions == 2 && loader.removedListeners == 3, "unload removes all callbacks");
        controls.Stop();
        Require(loader.removedActions == 2, "cleanup is idempotent");
    }
    {
        LoaderHarness loader;
        PlayerControls controls;
        config.startGamePlayers = 0;
        Require(controls.Start(&loader.context, config, LoaderHarness::Eligible, LoaderHarness::Apply), "hotkeys without startup");
        loader.Event(GameplayEventKind::LocalPlayerReady, 1);
        Require(loader.applied.empty(), "zero preserves Loader startup value");
        loader.applyWorks = false;
        Require(loader.Key(0, ActionEventKind::Pressed) == ActionResult::Ignored, "failed apply does not claim handled");
        loader.applyWorks = true;
        loader.Key(1, ActionEventKind::Pressed);
        Require(loader.applied == std::vector<std::int32_t>{2}, "second shortcut selects p2");
        controls.Stop();
    }
    {
        LoaderHarness loader;
        PlayerControls controls;
        loader.available = false;
        Require(!controls.Start(&loader.context, config, LoaderHarness::Eligible, LoaderHarness::Apply), "requested features require services");
        Config legacy{};
        Require(controls.Start(&loader.context, legacy, LoaderHarness::Eligible, LoaderHarness::Apply), "legacy config does not need services");
        config.battleNetSimulationEnabled = true;
        Require(controls.Start(&loader.context, config, LoaderHarness::Eligible, LoaderHarness::Apply)
            && loader.actionCount == 0 && loader.listenerCount == 0, "simulation registers no artificial controls");
    }
    {
        LoaderHarness loader;
        PlayerControls controls;
        config.battleNetSimulationEnabled = false;
        loader.failListener = true;
        Require(!controls.Start(&loader.context, config, LoaderHarness::Eligible, LoaderHarness::Apply)
            && loader.removedActions == 2, "partial registration rolls back actions");
    }
    Require(AllowsPlayersCommand(0) && AllowsPlayersCommand(5) && AllowsPlayersCommand(7)
        && !AllowsPlayersCommand(1) && !AllowsPlayersCommand(6) && !AllowsPlayersCommand(-1), "native dispatcher eligibility");
    std::cout << "PASS: startup and hotkey SDK callbacks\n";
}
