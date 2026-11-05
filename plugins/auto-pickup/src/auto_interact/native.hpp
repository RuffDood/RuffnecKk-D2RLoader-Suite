#pragma once
#include "runtime.hpp"
#include "object_catalog.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include <atomic>

namespace AutoInteract {
struct NativeBindings {
    using Unit = void*(__fastcall*)(void*);
    using Number = std::uint32_t(__fastcall*)(void*);
    using Lookup = void*(__fastcall*)(void*, std::uint32_t, std::uint32_t);
    using Pair = std::int32_t(__fastcall*)(void*, void*);
    Unit room{}, first{}, next{}, game{}, record{}, inventory{}, cursor{}, firstItem{}, nextItem{};
    Number type{}, id{}, mode{}, dead{}, selectable{};
    Lookup lookup{};
    void(__fastcall* neighbours)(void*, void***, std::uint32_t*){};
    std::int32_t(__fastcall* count)(std::uint8_t){};
    void*(__fastcall* row)(std::uint8_t, std::uint32_t){};
    Pair distance{}, reach{};
    std::int32_t(__fastcall* collision)(void*, void*, std::uint32_t){};
    std::int32_t(__fastcall* operate)(void*, void*, std::uint32_t, std::uint32_t, bool){};
    std::int32_t(__fastcall* itemType)(void*, std::uint32_t){};
    std::uint32_t(__fastcall* itemPage)(void*){};
    std::int32_t(*quantity)(void*, void*) noexcept{};
};
using PlayerActionFn = std::int32_t(__fastcall*)(void*, void*, void*, std::uint32_t,
    std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);

class Native {
public:
    auto Admit(const D2RL::PluginContext*, const Config&) noexcept -> bool;
    auto Activate() noexcept -> bool;
    auto PrepareSettings(const Config&) noexcept -> bool; // Caller holds the runtime's scan lock.
    void ApplySettings(const Config& config) noexcept { config_ = config; } // Same scan lock.
    void Stop() noexcept;
    auto Port() noexcept -> NativePort;
    void Observe(void* game, void* player) noexcept;
    void Session(const D2RL::Lifecycle::GameplayEvent&) noexcept;
    auto Snapshot(std::uint32_t player, std::span<Object>, std::size_t&) noexcept -> bool;
    auto Request(Identity, const Config&) noexcept -> bool;
    auto ActorObservations() const noexcept -> std::uint64_t { return observations_.load(); }
private:
    friend struct NativeTestAccess;
    struct Actor { void* game{}; void* player{}; std::uint32_t id{UINT32_MAX}; std::uint64_t epoch{}; };
    struct SessionState { std::uintptr_t game{}; std::uint32_t player{UINT32_MAX}; std::uint64_t generation{}, epoch{}; };
    auto State() noexcept -> SessionState;
    void Remember(SessionState, std::uintptr_t) noexcept;
    auto Resolve(std::uint32_t player) noexcept -> Actor;
    auto PlayerCanInteract(void*) noexcept -> bool;
    auto HasKeys(void*) noexcept -> bool;
    auto ReadObject(Actor, void*, Object&, const Config&) noexcept -> bool;
    void LoadCatalogs() noexcept;
    auto ObjectKey(const std::uint8_t*, std::uint32_t, std::uint8_t, std::uint32_t) noexcept -> std::string_view;
    static auto Quantity(void*, void*) noexcept -> std::int32_t;
    NativeBindings b_{};
    Config config_;
    std::array<ObjectCatalog, 2> catalogs_;
    const D2RL::PluginContext* context_{};
    std::uintptr_t base_{};
    std::atomic<bool> active_{};
    std::atomic<std::uint64_t> observations_{};
    std::atomic<std::uint64_t> loggedRequests_{};
    std::mutex mutex_;
    SessionState state_;
    RuffnecKk::NativeStatCompat::Adapter stats_;
};
}
