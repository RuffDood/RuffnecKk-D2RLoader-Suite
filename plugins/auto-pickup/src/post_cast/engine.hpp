#include "reimagined_stat_compat.hpp"
#pragma once
// Native pickup/signature and scheduling code adapted from Auto Pickup.
// See LICENSE and UPSTREAM.md for the original source and changes.
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include "policy.hpp"
#include "follow_up.hpp"
#include "gold_retry.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include "compatibility.hpp"
#include "pickup_bridge.hpp"
#include "action_hooks.hpp"
#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace Reimagined::EmbeddedTeleportPickup {
namespace {
#include "native.hpp"
using RuffnecKk::AutoPickup::FollowUpWindow;
using RuffnecKk::AutoPickup::AcceptPickupEntry;
std::recursive_mutex SettingsMutex;
using ItemScanFn = bool(*)(void*, std::uint32_t) noexcept;
ItemScanFn ItemScan{};
const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::array<TriggerFn, LastTriggerOpcode + 1> OriginalTriggers{};
GetUnitRoomFn GetUnitRoom{};
GetServerUnitFn GetServerUnit{};
GetRoomNeighboursFn GetRoomNeighbours{};
UnitFn GetRoomFirstUnit{}, GetNextUnit{};
UnitIntFn UnitType{}, UnitId{}, UnitMode{};
UnitPairFn UnitDistance{};
CollisionFn UnitCollision{};
PickupFn Pickup{};
GetItemCodeFn GetItemCode{};
using GoldLimitFn = std::uint32_t(__fastcall*)(void*);
using ReadGoldWalletFn = bool(*)(void*, GoldWallet&) noexcept;
GoldLimitFn GetInventoryGoldLimit{};
ReadGoldWalletFn ReadGoldWallet{};
RuffnecKk::NativeStatCompat::Adapter NativeStats;
GoldRetryGuard GoldRetries;
std::mutex GoldRetryMutex;
const D2RL::ThreadServiceV1* Threads{};
const D2RL::LifecycleServiceV1* Lifecycle{};
std::array<D2RL::Lifecycle::ListenerHandle, 3> FollowUpListeners{};
std::mutex FollowUpMutex;
FollowUpWindow FollowUps;
struct RuntimeMetrics {
    std::atomic<std::uint64_t> followUpScans{}, queueFailures{}, goldAttempts{}, goldPicked{}, bridgeScans{};
    std::atomic<std::uint64_t> casts{}, actorRejected{}, windows{}, ticks{}, lookupMisses{}, goldScans{};
    std::atomic<std::uint64_t> goldFullSkips{}, goldRetrySkips{}, goldWalletErrors{};
} Metrics;
std::atomic<std::uint64_t> DiagnosticMessages{};
template<class F> auto At(std::uintptr_t rva) noexcept -> F { return reinterpret_cast<F>(Base + rva); }
auto ShouldLogDiagnostic() noexcept -> bool {
    if (!Context || !Settings.diagnosticsEnabled) return false;
    const auto n = DiagnosticMessages.fetch_add(1, std::memory_order_relaxed) + 1;
    return n <= 8 || n % 100 == 0;
}
void TraceProgress(const char* stage, std::uint64_t count) noexcept {
    if (!Context || !Settings.diagnosticsEnabled || (count > 6 && count % 100 != 0)) return;
    char message[192]{};
    std::snprintf(message, sizeof(message), "TeleportPickup: %s; count=%llu.", stage, static_cast<unsigned long long>(count));
    Context->LogInfo(message);
}
auto ReadNativeGoldWallet(void* player, GoldWallet& wallet) noexcept -> bool {
    if (!GetInventoryGoldLimit || !NativeStats.IsBound(RuffnecKk::NativeStatCompat::Helper::GetUnitStat)) return false;
    wallet = {NativeStats.GetUnitStat(player, 14, 0), GetInventoryGoldLimit(player)};
    return wallet.Valid();
}
auto CheckGoldRetry(std::uint32_t player, GoldWallet wallet, std::uint64_t now) noexcept -> GoldRetryGuard::Decision {
    std::lock_guard lock(GoldRetryMutex);
    return GoldRetries.Check(player, wallet, now);
}
void RememberGoldAttempt(std::uint32_t player, GoldWallet wallet, std::uint64_t now) noexcept {
    std::lock_guard lock(GoldRetryMutex);
    GoldRetries.Attempt(player, wallet, now);
}
void ResetGoldRetries() noexcept {
    std::lock_guard lock(GoldRetryMutex);
    GoldRetries.Reset();
}

void ScanGold(void* player, std::uint64_t now = GetTickCount64()) noexcept {
    if (!Settings.gold || !player) return;
    __try {
        if (UnitType(player) != 0) return;
        const auto playerId = UnitId(player);
        GoldWallet wallet{};
        if (!ReadGoldWallet || !ReadGoldWallet(player, wallet)) {
            Metrics.goldWalletErrors.fetch_add(1, std::memory_order_relaxed); return;
        }
        const auto decision = CheckGoldRetry(playerId, wallet, now);
        if (decision != GoldRetryGuard::Decision::Ready) {
            if (decision == GoldRetryGuard::Decision::Full) Metrics.goldFullSkips.fetch_add(1, std::memory_order_relaxed);
            else if (decision == GoldRetryGuard::Decision::CoolingDown) Metrics.goldRetrySkips.fetch_add(1, std::memory_order_relaxed);
            else Metrics.goldWalletErrors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto* room = GetUnitRoom(player);
        if (!room) return;
        void** buckets{}; std::uint32_t count{}; GetRoomNeighbours(room, &buckets, &count);
        if (!buckets || !count || count > 4096) return;
        void* best{}; std::int32_t nearest = INT_MAX;
        std::size_t visited{};
        std::uint32_t goldSeen{}, withinRange{}, blocked{};
        // Packed lowercase "gld " matches the native item-code representation.
        constexpr std::uint32_t GoldCode = 0x20646C67;
        for (std::uint32_t i = 0; i < count; ++i) for (void* unit = GetRoomFirstUnit(buckets[i]); unit; unit = GetNextUnit(unit)) {
            if (++visited > 262144) return;
            if (UnitType(unit) != ItemType || UnitMode(unit) != GroundMode || GetItemCode(unit) != GoldCode) continue;
            ++goldSeen;
            const auto distance = UnitDistance(player, unit);
            if (distance < 0 || static_cast<std::uint32_t>(distance) > Settings.pickupRange) continue;
            ++withinRange;
            if (UnitCollision(player, unit, PickupCollisionMask) != 0) { ++blocked; continue; }
            if (distance >= nearest) continue;
            best = unit; nearest = distance;
        }
        bool picked{};
        if (best) {
            const auto id = UnitId(best);
            if (id != FollowUpWindow::InvalidId) {
                // Arm before calling native code. A refusal, zero-progress
                // success, or exception must not trigger another request next tick.
                // Observed wallet/cap changes clear this delay on the next scan.
                RememberGoldAttempt(playerId, wallet, now);
                Metrics.goldAttempts.fetch_add(1, std::memory_order_relaxed);
                picked = Pickup(player, id, true, Settings.pickupRange, true, false);
                if (picked) Metrics.goldPicked.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const auto scan = Metrics.goldScans.fetch_add(1, std::memory_order_relaxed) + 1;
        if (Context && Settings.diagnosticsEnabled && (scan <= 8 || scan % 100 == 0 || picked)) {
            char message[256]{};
            std::snprintf(message, sizeof(message), "TeleportPickup: gold scan=%llu; rooms=%u; seen=%u; near=%u; blocked=%u; selected=%s; picked=%s.",
                static_cast<unsigned long long>(scan), count, goldSeen, withinRange, blocked, best ? "true" : "false", picked ? "true" : "false");
            Context->LogInfo(message);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (ShouldLogDiagnostic()) Context->LogWarn("TeleportPickup: gold scan skipped after a native exception.");
    }
}

void ScanItemPlugin(void* player) noexcept {
    if (Settings.includeItemPlugin && player && ItemScan && ItemScan(player, Settings.pickupRange))
        Metrics.bridgeScans.fetch_add(1, std::memory_order_relaxed);
}
void Scan(void* player, bool = false) noexcept {
    if (!Settings.enabled || Settings.validateOnly) return;
    ScanGold(player);
    ScanItemPlugin(player);
}

#include "scheduler.inc"

auto __fastcall HookTrigger(void* game, void* player, void* packet, std::int32_t size) noexcept -> std::int64_t {
    std::lock_guard settingsLock(SettingsMutex);
    const auto opcode = packet && size > 0 ? *static_cast<const std::uint8_t*>(packet) : static_cast<std::uint8_t>(0);
    if (!ActionHooks::IsCast(opcode) || !OriginalTriggers[opcode]) return 1;
    const auto result = OriginalTriggers[opcode](game, player, packet, size);
    TraceProgress("cast action received", Metrics.casts.fetch_add(1, std::memory_order_relaxed) + 1);
    if (Settings.enabled && !Settings.validateOnly) ScheduleFollowUp(game, player);
    return result;
}

#include "pickup_validation.inc"

auto ValidateGoldWalletReaders() noexcept -> bool {
    // Bind once even when initially off, so the gold checkbox can enable safely.

    // Admit the complete cap routine except its multiplier immediate. The
    // original function supplies the active game/mod limit without changing it.
    if (!Context->CheckExpectedBytes(GetInventoryGoldLimitRva, GetInventoryGoldLimitPrefix.data(),
            static_cast<std::uint32_t>(GetInventoryGoldLimitPrefix.size()))
        || !Context->CheckExpectedBytes(GetInventoryGoldLimitRva + GoldLimitSuffixOffset, GetInventoryGoldLimitSuffix.data(),
            static_cast<std::uint32_t>(GetInventoryGoldLimitSuffix.size()))) {
        Context->LogError("TeleportPickup: gold-cap routine fingerprint mismatch; nothing was patched.");
        return false;
    }
    // Also validates the reader called by the cap routine itself, including
    // D2RCore's known replacement. Unknown detours cannot be called safely.
    if (!NativeStats.BindCurrentProcess(reinterpret_cast<std::uintptr_t>(Base),
            RuffnecKk::NativeStatCompat::ToMask(RuffnecKk::NativeStatCompat::Helper::GetUnitStat),
            RuffnecKk::AutoPickup::Compat::StatRedirectProof())) {
        Context->LogError("TeleportPickup: gold stat-reader compatibility check failed; nothing was patched.");
        return false;
    }
    return true;
}

auto ValidateRuntime() noexcept -> bool {
    bool valid = true;
    for (std::uint8_t opcode = FirstTriggerOpcode; opcode <= LastTriggerOpcode; ++opcode) {
        const auto expected = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        valid = Context->CheckExpectedBytes(ServerPacketTableRva + opcode * sizeof(std::uintptr_t), &expected, sizeof(expected)) && valid;
    }
    valid = Context->CheckExpectedBytes(GetUnitRoomRva, GetUnitRoomExpected.data(), static_cast<std::uint32_t>(GetUnitRoomExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetServerUnitRva, GetServerUnitExpected.data(), static_cast<std::uint32_t>(GetServerUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetRoomNeighboursRva, GetRoomNeighboursExpected.data(), static_cast<std::uint32_t>(GetRoomNeighboursExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetRoomFirstUnitRva, GetRoomFirstUnitExpected.data(), static_cast<std::uint32_t>(GetRoomFirstUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetNextUnitRva, GetNextUnitExpected.data(), static_cast<std::uint32_t>(GetNextUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitTypeRva, UnitTypeExpected.data(), static_cast<std::uint32_t>(UnitTypeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitIdRva, UnitIdExpected.data(), static_cast<std::uint32_t>(UnitIdExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitModeRva, UnitModeExpected.data(), static_cast<std::uint32_t>(UnitModeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitDistanceRva, UnitDistanceExpected.data(), static_cast<std::uint32_t>(UnitDistanceExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitCollisionRva, UnitCollisionExpected.data(), static_cast<std::uint32_t>(UnitCollisionExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetItemCodeRva, GetItemCodeExpected.data(), static_cast<std::uint32_t>(GetItemCodeExpected.size())) && valid;
    return ValidatePickupEntry() && valid && ValidateGoldWalletReaders();
}
void ResolveRuntime() noexcept {
    GetUnitRoom = At<GetUnitRoomFn>(GetUnitRoomRva); GetRoomNeighbours = At<GetRoomNeighboursFn>(GetRoomNeighboursRva);
    GetServerUnit = At<GetServerUnitFn>(GetServerUnitRva);
    GetRoomFirstUnit = At<UnitFn>(GetRoomFirstUnitRva); GetNextUnit = At<UnitFn>(GetNextUnitRva);
    UnitType = At<UnitIntFn>(UnitTypeRva); UnitId = At<UnitIntFn>(UnitIdRva); UnitMode = At<UnitIntFn>(UnitModeRva);
    UnitDistance = At<UnitPairFn>(UnitDistanceRva); UnitCollision = At<CollisionFn>(UnitCollisionRva);
    Pickup = At<PickupFn>(PickupRva); GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
    if (NativeStats.IsBound(RuffnecKk::NativeStatCompat::Helper::GetUnitStat)) {
        GetInventoryGoldLimit = At<GoldLimitFn>(GetInventoryGoldLimitRva);
        ReadGoldWallet = ReadNativeGoldWallet;
    }
}
auto InstallMutations() noexcept -> bool {
    const auto replacement = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&HookTrigger));
    for (std::uint8_t opcode = FirstTriggerOpcode; opcode <= LastTriggerOpcode; ++opcode) {
        const auto expected = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        OriginalTriggers[opcode] = reinterpret_cast<TriggerFn>(static_cast<std::uintptr_t>(expected));
        if (!Context->PatchWriteU64(ServerPacketTableRva + opcode * sizeof(std::uintptr_t), &expected, sizeof(expected), replacement)) return false;
    }
    return true;
}
auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    std::lock_guard lock(SettingsMutex);
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[1024]{};
    std::snprintf(message, sizeof(message), "Auto Pickup built-in post-cast: enabled=%s; validation-only=%s; gold=%s; items=%s; pickup-range=%u; interval-ms=%u; window-ms=%u; follow-up-scans=%llu; gold-attempts=%llu; gold-picked=%llu; item-scans=%llu; queue-failures=%llu; casts=%llu; actor-rejected=%llu; windows=%llu; callbacks=%llu; lookup-misses=%llu; gold-full-skips=%llu; gold-retry-skips=%llu; gold-wallet-errors=%llu.",
        Settings.enabled ? "true" : "false", Settings.validateOnly ? "true" : "false", Settings.gold ? "true" : "false", Settings.includeItemPlugin ? "true" : "false",
        Settings.pickupRange, Settings.followUpIntervalMs, Settings.followUpWindowMs,
        static_cast<unsigned long long>(Metrics.followUpScans.load()), static_cast<unsigned long long>(Metrics.goldAttempts.load()),
        static_cast<unsigned long long>(Metrics.goldPicked.load()), static_cast<unsigned long long>(Metrics.bridgeScans.load()),
        static_cast<unsigned long long>(Metrics.queueFailures.load()),
        static_cast<unsigned long long>(Metrics.casts.load()), static_cast<unsigned long long>(Metrics.actorRejected.load()),
        static_cast<unsigned long long>(Metrics.windows.load()), static_cast<unsigned long long>(Metrics.ticks.load()),
        static_cast<unsigned long long>(Metrics.lookupMisses.load()),
        static_cast<unsigned long long>(Metrics.goldFullSkips.load()), static_cast<unsigned long long>(Metrics.goldRetrySkips.load()),
        static_cast<unsigned long long>(Metrics.goldWalletErrors.load()));
    command->plugin->WriteConsoleMessage(message); return D2RL::ConsoleCommandResult::Handled;
}
void ResetRuntime() noexcept {
    StopFollowUps(); ResetGoldRetries(); NativeStats.Reset(); OriginalTriggers.fill(nullptr);
    GetUnitRoom = nullptr; GetServerUnit = nullptr; GetRoomNeighbours = nullptr; GetRoomFirstUnit = nullptr; GetNextUnit = nullptr;
    UnitType = nullptr; UnitId = nullptr; UnitMode = nullptr; UnitDistance = nullptr;
    UnitCollision = nullptr; Pickup = nullptr; GetItemCode = nullptr;
    ReadGoldWallet = nullptr; GetInventoryGoldLimit = nullptr;
    for (auto* n : {&Metrics.followUpScans, &Metrics.queueFailures, &Metrics.goldAttempts, &Metrics.goldPicked, &Metrics.bridgeScans,
            &Metrics.casts, &Metrics.actorRejected, &Metrics.windows, &Metrics.ticks, &Metrics.lookupMisses, &Metrics.goldScans,
            &Metrics.goldFullSkips, &Metrics.goldRetrySkips, &Metrics.goldWalletErrors}) n->store(0);
    DiagnosticMessages.store(0); Settings = {};
}
}
auto Start(const D2RL::PluginContext* context, Config config, ItemScanFn scan, bool registerCommand = true) noexcept -> bool {
    std::lock_guard lock(SettingsMutex);
    Context = context; Base = reinterpret_cast<std::uint8_t*>(context->exeBase); ResetRuntime();
    Settings = config; ItemScan = scan;
    if (!Base || !ValidateRuntime() || !PrepareFollowUp(!Settings.validateOnly)) { StopFollowUps(); return false; }
    if (!Settings.validateOnly) {
        ResolveRuntime();
        if (!InstallMutations()) { StopFollowUps(); return false; }
    }
    if (registerCommand && !context->RegisterConsoleCommand("teleport-pickup", Status, "Show built-in post-cast pickup counters."))
        context->LogWarn("Auto Pickup: post-cast status command unavailable.");
    return true;
}
void Apply(Config config) noexcept {
    std::lock_guard lock(SettingsMutex);
    Settings = config;
    { std::lock_guard followLock(FollowUpMutex); FollowUps.Cancel(); }
    ResetGoldRetries();
}
void Stop() noexcept {
    std::lock_guard lock(SettingsMutex);
    ResetRuntime(); ItemScan = nullptr; Base = nullptr; Context = nullptr;
}
}
