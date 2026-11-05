#pragma once
#include "engine.hpp"
#include <D2RLPlugin/api.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace AutoInteract {
// The native boundary must gather and mutate only during this synchronous
// authoritative callback. Request must resolve the selected identity again and
// repeat reach/state/key/classification checks immediately before dispatch.
struct NativePort {
    void* state{};
    bool (*snapshot)(void*, std::uint32_t, std::span<Object>, std::size_t&) noexcept{};
    bool (*request)(void*, Identity, const Config&) noexcept{};
    void (*session)(void*, const D2RL::Lifecycle::GameplayEvent&) noexcept{};
};
class Runtime {
public:
    static constexpr std::size_t MaximumObjects = 1024;
    struct Counters {
        Engine::Counters engine;
        std::uint64_t scans{}, queueFailures{}, refused{}, readFailures{}, scanTotalUs{}, scanMaxUs{};
    };
    auto Start(const D2RL::PluginContext* context, Config config, NativePort port) noexcept -> bool {
        Stop();
        if (!D2RL::HasContext(context) || context->apiVersion < 3 || !port.snapshot || !port.request) return false;
        Runtime* expected{};
        if (!CallbackOwner.compare_exchange_strong(expected, this)) return false;
        context_ = context; config_ = config; port_ = port;
        if (context->QueryService(D2RL::ServiceId::Thread, 1, &threads_) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceV1Field(threads_, D2RL::ThreadServiceV1RequiredSize) || !threads_->runOnGameThread
            || context->QueryService(D2RL::ServiceId::Lifecycle, 1, &lifecycle_) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(lifecycle_, D2RL::LifecycleServiceV1RequiredSize)
            || !lifecycle_->registerGameplayEventListener || !lifecycle_->unregisterGameplayEventListener) {
            Stop(); return false;
        }
        if (!config.enabled || config.validateOnly) return true;
        {
            std::lock_guard lock(mutex_); running_ = true;
        }
        constexpr std::array kinds{D2RL::Lifecycle::GameplayEventKind::GameJoined,
            D2RL::Lifecycle::GameplayEventKind::GameLeft, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady};
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const D2RL::Lifecycle::GameplayEventListener registration{D2RL::Lifecycle::GameplayEventListenerSize,
                0, kinds[i], 0, Session, this};
            if (lifecycle_->registerGameplayEventListener(context_, &registration, &listeners_[i])
                    != D2RL::Lifecycle::Result::Success || !listeners_[i]) {
                Stop(); return false;
            }
        }
        return true;
    }
    void Stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            running_ = false; schedule_.Stop(); engine_.Reset();
        }
        if (lifecycle_ && context_) for (auto& handle : listeners_) {
            if (handle) (void)lifecycle_->unregisterGameplayEventListener(context_, handle);
            handle = 0;
        }
        // SDK removes queued callbacks on owner unload. Until then callbacks
        // receive this same stable Runtime instance and see running_ == false.
        context_ = nullptr; threads_ = nullptr; lifecycle_ = nullptr; port_ = {};
        Runtime* expected = this;
        (void)CallbackOwner.compare_exchange_strong(expected, nullptr);
    }
    auto Status() noexcept -> Counters {
        std::lock_guard lock(mutex_);
        return {engine_.metrics, scans_, queueFailures_, refused_, readFailures_, scanTotalUs_, scanMaxUs_};
    }
    template<class Prepare> auto Reconfigure(Config config, Prepare&& prepare) -> bool {
        std::lock_guard lock(mutex_);
        if (!prepare()) return false;
        config_ = config;
        return true;
    }
    // now is injected by the small SDK callback so clock-boundary tests execute
    // the same control flow used in production.
    void Tick(const D2RL::PluginContext* context, std::uint64_t ticket, std::uint64_t now) noexcept {
        std::lock_guard lock(mutex_);
        if (!running_ || context != context_ || !schedule_.Current(ticket)) return;
        if (schedule_.Due(ticket, now, config_.intervalMs) && config_.enabled && !config_.validateOnly) {
            const auto began = std::chrono::steady_clock::now();
            std::size_t count{};
            ++scans_;
            if (port_.snapshot(port_.state, schedule_.Player(), objects_, count) && count <= objects_.size()) {
                const auto id = engine_.Choose(std::span(objects_.data(), count), config_, now);
                if (id.Valid() && engine_.Arm(id, now, config_.retryMs))
                    if (!port_.request(port_.state, id, config_)) ++refused_;
            } else ++readFailures_;
            const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count());
            scanTotalUs_ += elapsed; scanMaxUs_ = (std::max)(scanMaxUs_, elapsed);
            if (config_.diagnostics && scans_ % 100 == 0) {
                char message[288]{};
                std::snprintf(message, sizeof(message), "Auto Interact performance: scans=%llu; scan-avg-us=%llu; scan-max-us=%llu; last-objects=%zu; interval-ms=%u; range=%u; attempts=%llu.",
                    static_cast<unsigned long long>(scans_), static_cast<unsigned long long>(scanTotalUs_ / scans_),
                    static_cast<unsigned long long>(scanMaxUs_), count, config_.intervalMs, config_.interactRange,
                    static_cast<unsigned long long>(engine_.metrics.attempts));
                context_->LogInfo(message);
            }
        }
        QueueLocked(ticket);
    }
    void OnSession(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event) noexcept {
        if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
        std::lock_guard lock(mutex_);
        if (!running_ || context != context_) return;
        if (port_.session) port_.session(port_.state, *event);
        if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined
            || event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
            if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft && config_.diagnostics) {
                char message[384]{};
                std::snprintf(message, sizeof(message), "Auto Interact session ended: scans=%llu; attempts=%llu; observed-completed=%llu; no-progress=%llu; native-refused=%llu; read-failures=%llu; queue-failures=%llu.",
                    static_cast<unsigned long long>(scans_), static_cast<unsigned long long>(engine_.metrics.attempts),
                    static_cast<unsigned long long>(engine_.metrics.completed), static_cast<unsigned long long>(engine_.metrics.noProgress),
                    static_cast<unsigned long long>(refused_), static_cast<unsigned long long>(readFailures_), static_cast<unsigned long long>(queueFailures_));
                context_->LogInfo(message);
            }
            schedule_.Stop(); engine_.Reset(); return;
        }
        if (event->kind != D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) return;
        const auto ticket = schedule_.Enter(event->sessionGeneration, event->playerId);
        if (ticket) { engine_.Reset(); QueueLocked(ticket); }
    }
private:
    static void __cdecl Session(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event, void* data) noexcept {
        static_cast<Runtime*>(data)->OnSession(context, event);
    }
    static void __cdecl GameTick(const D2RL::PluginContext* context, void* data) noexcept {
        if (auto* owner = CallbackOwner.load()) {
            const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            owner->Tick(context, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)), now);
        }
    }
    void QueueLocked(std::uint64_t ticket) noexcept {
        // Each callback receives an immutable ticket, never a mutable member's
        // address. Old callbacks cannot accidentally adopt a new generation.
        if (!threads_ || threads_->runOnGameThread(context_, GameTick,
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(ticket))) != D2RL::Threads::Result::Success) {
            schedule_.Stop(); ++queueFailures_;
            if (context_ && config_.diagnostics)
                context_->LogWarn("Auto Interact: authoritative game updates unavailable; scans stopped. Remote TCP/IP clients are unsupported.");
        }
    }
    // A plugin owns one stable runtime for the DLL's lifetime. GameTick routes
    // through it; userData contains only the session ticket.
    inline static std::atomic<Runtime*> CallbackOwner{};
    std::mutex mutex_;
    bool running_{};
    Config config_;
    NativePort port_;
    Engine engine_;
    Schedule schedule_;
    std::array<Object, MaximumObjects> objects_{};
    std::uint64_t scans_{}, queueFailures_{}, refused_{}, readFailures_{}, scanTotalUs_{}, scanMaxUs_{};
    const D2RL::PluginContext* context_{};
    const D2RL::ThreadServiceV1* threads_{};
    const D2RL::LifecycleServiceV1* lifecycle_{};
    std::array<D2RL::Lifecycle::ListenerHandle, 3> listeners_{};
};
}
