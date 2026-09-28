#pragma once

#include "player_scaling_policy.hpp"
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/input.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <array>
#include <cstdio>

namespace ruffneckk::player_scaling {

// Mirrors the eligibility branch in the native /players dispatcher.
constexpr auto AllowsPlayersCommand(std::int32_t mode) noexcept -> bool {
    return mode == 0 || mode == 5 || mode == 7;
}

class PlayerControls {
public:
    using CanApply = bool(*)() noexcept;
    using Apply = bool(*)(std::int32_t) noexcept;

    auto Start(const D2RL::PluginContext* context, const Config& config,
            CanApply canApply, Apply apply) noexcept -> bool {
        Stop();
        if (!config.enabled || config.battleNetSimulationEnabled
                || (config.startGamePlayers == 0 && config.hotkeys.empty())) return true;
        context_ = context;
        config_ = &config;
        canApply_ = canApply;
        apply_ = apply;
        try {
            if (!context_ || !canApply_ || !apply_
                    || context_->QueryService(D2RL::ServiceId::Lifecycle, 1, &lifecycle_)
                        != D2RL::ServiceQueryResult::Success
                    || !D2RL::HasLifecycleServiceV1Field(lifecycle_, D2RL::LifecycleServiceV1RequiredSize)
                    || !lifecycle_->registerGameplayEventListener
                    || !lifecycle_->unregisterGameplayEventListener) return Fail();
            if (!config.hotkeys.empty()) {
                if (context_->QueryService(D2RL::ServiceId::Input, 1, &input_)
                            != D2RL::ServiceQueryResult::Success
                        || !D2RL::HasInputServiceV1Field(input_, D2RL::InputServiceV1RequiredSize)
                        || !input_->registerAction || !input_->unregisterAction) return Fail();
                actions_.resize(config.hotkeys.size());
                for (std::size_t i = 0; i < actions_.size(); ++i) {
                    auto& action = actions_[i];
                    action.owner = this;
                    action.hotkey = config.hotkeys[i];
                    char id[80]{}, name[80]{};
                    // Include the configured binding/value so TOML edits create
                    // a new action rather than inheriting an old saved binding.
                    std::snprintf(id, sizeof(id), "set-%u-%u-p%d",
                        action.hotkey.modifier, action.hotkey.key, action.hotkey.players);
                    std::snprintf(name, sizeof(name), "Set players %d", action.hotkey.players);
                    const D2RL::Input::ActionRegistration registration{
                        .structSize = D2RL::Input::ActionRegistrationSize,
                        .logicalId = id, .displayName = name, .category = "PlayerX",
                        .defaultPrimary = {static_cast<D2RL::Input::Key>(action.hotkey.key),
                            static_cast<D2RL::Input::Modifier>(action.hotkey.modifier)},
                        .defaultSecondary = {D2RL::Input::Key::None, D2RL::Input::Modifier::None},
                        .callback = OnInput, .userData = &action,
                    };
                    if (input_->registerAction(context_, &registration, &action.handle)
                            != D2RL::Input::Result::Success || !action.handle) return Fail();
                }
            }
            constexpr std::array kinds{
                D2RL::Lifecycle::GameplayEventKind::GameJoined,
                D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
                D2RL::Lifecycle::GameplayEventKind::GameLeft};
            for (std::size_t i = 0; i < kinds.size(); ++i) {
                const D2RL::Lifecycle::GameplayEventListener listener{
                    .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
                    .kind = kinds[i], .callback = OnGameplay, .userData = this};
                if (lifecycle_->registerGameplayEventListener(context_, &listener, &listeners_[i])
                        != D2RL::Lifecycle::Result::Success || !listeners_[i]) return Fail();
            }
            return true;
        } catch (...) {
            return Fail();
        }
    }

    void Stop() noexcept {
        ready_ = false;
        generation_ = 0;
        if (D2RL::HasLifecycleServiceV1Field(lifecycle_, D2RL::LifecycleServiceV1RequiredSize)
                && lifecycle_->unregisterGameplayEventListener) {
            for (auto handle : listeners_) {
                if (handle) (void)lifecycle_->unregisterGameplayEventListener(context_, handle);
            }
        }
        if (D2RL::HasInputServiceV1Field(input_, D2RL::InputServiceV1RequiredSize)
                && input_->unregisterAction) {
            for (auto& action : actions_) {
                if (action.handle) (void)input_->unregisterAction(context_, action.handle);
            }
        }
        listeners_ = {};
        actions_.clear();
        lifecycle_ = nullptr;
        input_ = nullptr;
        context_ = nullptr;
        config_ = nullptr;
        canApply_ = nullptr;
        apply_ = nullptr;
    }

private:
    struct Action {
        PlayerControls* owner{};
        PlayerHotkey hotkey{};
        D2RL::Input::ActionHandle handle{};
        bool held{};
    };
    const D2RL::PluginContext* context_{};
    const Config* config_{};
    const D2RL::InputServiceV1* input_{};
    const D2RL::LifecycleServiceV1* lifecycle_{};
    std::vector<Action> actions_{};
    std::array<D2RL::Lifecycle::ListenerHandle, 3> listeners_{};
    CanApply canApply_{};
    Apply apply_{};
    std::uint64_t generation_{};
    bool ready_{};

    auto Fail() noexcept -> bool {
        if (context_) context_->LogError(
            "PlayerX: startup/hotkey registration failed. Loader Lifecycle v1 and (for hotkeys) Input v1 are required; check for binding conflicts.");
        Stop();
        return false;
    }

    void ResetSession(std::uint64_t generation) noexcept {
        generation_ = generation;
        ready_ = false;
        for (auto& action : actions_) action.held = false;
    }

    static void __cdecl OnGameplay(const D2RL::PluginContext*,
            const D2RL::Lifecycle::GameplayEvent* event, void* data) noexcept {
        if (!data || !D2RL::Lifecycle::HasGameplayEventField(
                event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
        auto& self = *static_cast<PlayerControls*>(data);
        if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
            self.ResetSession(0);
            return;
        }
        if (!event->sessionGeneration) return;
        if (self.generation_ != event->sessionGeneration) self.ResetSession(event->sessionGeneration);
        if (event->kind != D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady || self.ready_) return;
        self.ready_ = true;
        if (self.config_->startGamePlayers && self.canApply_()) {
            if (!self.apply_(self.config_->startGamePlayers)) self.context_->LogWarn(
                "PlayerX: could not apply the configured starting player count.");
        }
    }

    static auto __cdecl OnInput(const D2RL::PluginContext*,
            const D2RL::Input::ActionEvent* event, void* data) noexcept
            -> D2RL::Input::ActionResult {
        using namespace D2RL::Input;
        if (!data || !HasActionEventField(event, ActionEventRequiredSize)) return ActionResult::Ignored;
        auto& action = *static_cast<Action*>(data);
        auto& self = *action.owner;
        if (event->action != action.handle) return ActionResult::Ignored;
        if (event->kind == ActionEventKind::Released) {
            const auto handled = action.held;
            action.held = false;
            return handled ? ActionResult::Handled : ActionResult::Ignored;
        }
        if (event->kind != ActionEventKind::Pressed || !self.ready_ || !self.canApply_()) return ActionResult::Ignored;
        if (action.held) return ActionResult::Handled;
        action.held = self.apply_(action.hotkey.players);
        return action.held ? ActionResult::Handled : ActionResult::Ignored;
    }
};

} // namespace ruffneckk::player_scaling
