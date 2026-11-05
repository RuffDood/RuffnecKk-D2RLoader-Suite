// Native contracts adapted from RuffnecKk Auto Pickup; see UPSTREAM.md.
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include "policy.hpp"
#include "potion_routing/module.hpp"
#include "compatibility.hpp"
#include "pickup_bridge.hpp"
#include "action_hooks.hpp"
#include "item_retry.hpp"
#include "picker_ui.hpp"
#include "auto_interact/service.hpp"
#include "teleport_owner.hpp"
#include "post_cast/engine.hpp"
#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace RuffnecKk::AutoPickup {
namespace {
#include "native.hpp"
// Preserve legacy bridge V1 distance; walking and built-in post-cast have their own settings.
constexpr std::uint32_t StandardPickupDistance = 4;
const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
bool StandaloneTeleport{}, EmbeddedTeleportReady{};
std::shared_mutex SettingsMutex;
Picker::Interface SettingsPanel;
void CancelWalkingFollowUps() noexcept;
auto SnapshotSettings() -> Config { std::shared_lock lock(SettingsMutex); return Settings; }
auto PostCastConfig(const Config& config) -> Reimagined::EmbeddedTeleportPickup::Config {
    Reimagined::EmbeddedTeleportPickup::Config result;
    result.enabled = config.enabled && config.teleportEnabled;
    result.validateOnly = config.validateOnly; result.gold = config.teleportGold; result.includeItemPlugin = config.teleportItems;
    result.pickupRange = config.teleportRange; result.followUpIntervalMs = config.intervalMs;
    result.followUpWindowMs = config.windowMs; result.diagnosticsEnabled = config.diagnosticsEnabled;
    return result;
}
void ApplyItemSelection(Config config) {
    {
        std::unique_lock lock(SettingsMutex);
        // Diagnostic mode is a startup admission setting, never enabled by UI.
        config.validateOnly = Settings.validateOnly;
        config.intervalMs = Settings.intervalMs; config.windowMs = Settings.windowMs;
        config.diagnosticsEnabled = Settings.diagnosticsEnabled;
        Settings = std::move(config);
    }
    const auto current = SnapshotSettings();
    if (!current.enabled || !current.walkingEnabled) CancelWalkingFollowUps();
    // Do not hold the scan-policy lock while taking the scheduler lock.
    if (EmbeddedTeleportReady) Reimagined::EmbeddedTeleportPickup::Apply(PostCastConfig(SnapshotSettings()));
}
std::array<TriggerFn, LastTriggerOpcode + 1> OriginalTriggers{};
GetUnitRoomFn GetUnitRoom{};
GetRoomNeighboursFn GetRoomNeighbours{};
UnitFn GetRoomFirstUnit{}, GetNextUnit{};
UnitIntFn UnitType{}, UnitId{}, UnitMode{};
UnitPairFn UnitDistance{};
CollisionFn UnitCollision{};
PickupFn Pickup{};
GetItemCodeFn GetItemCode{};
std::mutex ScanApiMutex;
bool ScanApiReady{};
ItemRetryCooldown ItemRetries;
std::mutex ItemRetryMutex;
std::uint64_t RetryEpoch{};
const D2RL::LifecycleServiceV1* RetryLifecycle{};
std::array<D2RL::Lifecycle::ListenerHandle, 3> RetryListeners{};
using GetWalkingPlayerFn = void*(__fastcall*)(void*, std::uint32_t, std::uint32_t);
GetWalkingPlayerFn GetWalkingPlayer{};
const D2RL::ThreadServiceV1* WalkingThreads{};
std::recursive_mutex WalkingMutex;
FollowUpWindow WalkingFollowUps;
constexpr std::uint32_t WalkingIntervalMs = 100, WalkingSettleMs = 500;
struct RuntimeMetrics {
    std::atomic<std::uint64_t> actions{}, scans{}, attempts{}, picked{}, refused{}, external{}, exceptions{}, retrySkips{};
    std::atomic<std::uint64_t> walkingScans{}, walkingQueueFailures{};
} Metrics;
constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize, .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-auto-pickup", .name = "Auto Pickup", .version = "3.0.1",
    .author = "RuffnecKk",
    .description = "Walking and post-cast pickup with live item selection and radius settings.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};
template<class F> auto At(std::uintptr_t rva) noexcept -> F { return reinterpret_cast<F>(Base + rva); }
auto ReadConfiguration() -> bool {
    std::array<char, MaximumConfigBytes> text{}; std::uint32_t required{};
    if (!Context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &required)) {
        Context->LogError("AutoPickupAnything: configuration could not be read."); return false;
    }
    Config parsed; std::string error;
    if (!ParseConfig(text.data(), parsed, error)) {
        Context->LogError(("AutoPickupAnything: invalid configuration: " + error).c_str()); return false;
    }
    Settings = parsed; return true;
}

auto CurrentRetryEpoch() noexcept -> std::uint64_t {
    std::lock_guard lock(ItemRetryMutex);
    return RetryEpoch;
}
auto ItemReady(PickupIdentity key, std::uint64_t now, std::uint64_t epoch) noexcept -> bool {
    std::lock_guard lock(ItemRetryMutex);
    return epoch == RetryEpoch && ItemRetries.Ready(key, now);
}
auto BeginItemRequest(PickupIdentity key, std::uint64_t now, std::uint64_t epoch) noexcept -> bool {
    std::lock_guard lock(ItemRetryMutex);
    return epoch == RetryEpoch && ItemRetries.TryArm(key, now);
}
void ResetItemRetries() noexcept {
    std::lock_guard lock(ItemRetryMutex);
    ++RetryEpoch;
    ItemRetries.Reset();
}
void __cdecl OnRetrySessionEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
    std::lock_guard lock(WalkingMutex);
    ResetItemRetries();
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) WalkingFollowUps.Reset(event->sessionGeneration, false);
    else WalkingFollowUps.Enter(event->sessionGeneration);
}
void StopRetryLifecycle() noexcept {
    {
        std::lock_guard lock(WalkingMutex);
        WalkingFollowUps.Reset(0, false); WalkingThreads = nullptr;
    }
    if (RetryLifecycle && Context) for (auto& handle : RetryListeners) {
        if (handle) RetryLifecycle->unregisterGameplayEventListener(Context, handle);
        handle = 0;
    }
    RetryLifecycle = nullptr;
}
auto PrepareRetryLifecycle(bool registerListeners) noexcept -> bool {
    if (Context->QueryService(D2RL::ServiceId::Thread, D2RL::ThreadServiceV1Version, &WalkingThreads) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(WalkingThreads, D2RL::ThreadServiceV1RequiredSize) || !WalkingThreads->runOnGameThread
        || Context->QueryService(D2RL::ServiceId::Lifecycle, D2RL::LifecycleServiceV1Version, &RetryLifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceV1Field(RetryLifecycle, D2RL::LifecycleServiceV1RequiredSize)
        || !RetryLifecycle->registerGameplayEventListener || !RetryLifecycle->unregisterGameplayEventListener) {
        Context->LogError("Auto Pickup: walking pickup needs game-thread and session-event services; no hooks installed.");
        WalkingThreads = nullptr; RetryLifecycle = nullptr; return false;
    }
    if (!registerListeners) return true;
    constexpr std::array kinds{D2RL::Lifecycle::GameplayEventKind::GameJoined,
        D2RL::Lifecycle::GameplayEventKind::GameLeft, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const D2RL::Lifecycle::GameplayEventListener listener{D2RL::Lifecycle::GameplayEventListenerSize, 0, kinds[i], 0, OnRetrySessionEvent, nullptr};
        if (RetryLifecycle->registerGameplayEventListener(Context, &listener, &RetryListeners[i]) != D2RL::Lifecycle::Result::Success) {
            StopRetryLifecycle();
            Context->LogError("AutoPickupAnything: session subscription failed; no hooks installed."); return false;
        }
    }
    return true;
}

void ScanNative(void* player, std::uint32_t distanceLimit, std::uint64_t now) noexcept {
    if (!Settings.enabled || Settings.validateOnly || !player) return;
    if (distanceLimit < 1 || distanceLimit > Reimagined::PickupBridge::MaximumPickupDistance) return;
    const auto epoch = CurrentRetryEpoch();
    __try {
        Metrics.scans.fetch_add(1, std::memory_order_relaxed);
        auto* room = GetUnitRoom(player);
        if (!room || UnitType(player) != 0) return;
        const auto playerId = UnitId(player);
        if (playerId == UINT32_MAX) return;
        void** rooms{}; std::uint32_t count{};
        GetRoomNeighbours(room, &rooms, &count);
        if (!rooms || !count || count > 4096) return;
        void* selectedUnit{};
        PickupIdentity selected{}; bool found{}; std::int32_t nearest = INT_MAX; std::size_t visited{};
        for (std::uint32_t i = 0; i < count; ++i) for (void* unit = GetRoomFirstUnit(rooms[i]); unit; unit = GetNextUnit(unit)) {
            if (++visited > 262144) return;
            if (UnitType(unit) != ItemType || UnitMode(unit) != GroundMode) continue;
            const auto code = GetItemCode(unit);
            if (!Settings.Selects(code)) continue;
            const auto potion = PotionRouting::ClassifyPackedCode(code);
            if (potion.family != PotionRouting::Family::Unknown
                && !PotionRouting::CanRoute(player, unit, Settings.potionRouting.families[static_cast<std::size_t>(potion.family)])) continue;
            const auto distance = UnitDistance(player, unit);
            if (distance < 0 || static_cast<std::uint32_t>(distance) > distanceLimit
                || distance >= nearest || UnitCollision(player, unit, PickupCollisionMask) != 0) continue;
            const PickupIdentity candidate{playerId, UnitId(unit), code};
            if (!candidate.Valid()) continue;
            if (!ItemReady(candidate, now, epoch)) { Metrics.retrySkips.fetch_add(1, std::memory_order_relaxed); continue; }
            selected = candidate; selectedUnit = unit; found = true; nearest = distance;
        }
        if (!found) return;
        // Arm every request before native code: false, an exception, or a
        // reported success that leaves the item on the ground all need a delay.
        // Successful removals disappear from enumeration; other items stay ready.
        if (!BeginItemRequest(selected, now, epoch)) { Metrics.retrySkips.fetch_add(1, std::memory_order_relaxed); return; }
        const auto attempt = Metrics.attempts.fetch_add(1, std::memory_order_relaxed) + 1;
        // This is the ordinary native request. No belt hook, destination
        // override, inventory fallback, stack manipulation or deposit call.
        const auto potion = PotionRouting::ClassifyPackedCode(selected.code);
        const bool picked = potion.family == PotionRouting::Family::Unknown
            ? Pickup(player, selected.item, true, distanceLimit, true, false)
            : PotionRouting::Pickup(player, selectedUnit, selected.item, distanceLimit, Settings.potionRouting.families[static_cast<std::size_t>(potion.family)]);
        (picked ? Metrics.picked : Metrics.refused).fetch_add(1, std::memory_order_relaxed);
        if (Context && Settings.diagnosticsEnabled && (attempt <= 6 || attempt % 100 == 0)) {
            char message[192]{};
            std::snprintf(message, sizeof(message), "Auto Pickup: item request=%llu; item=%u; distance=%d; radius=%u; picked=%s.",
                static_cast<unsigned long long>(attempt), selected.item, nearest, distanceLimit, picked ? "true" : "false");
            Context->LogInfo(message);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const auto n = Metrics.exceptions.fetch_add(1, std::memory_order_relaxed) + 1;
        if (Context && (n <= 3 || n % 100 == 0)) Context->LogWarn("AutoPickupAnything: native scan skipped after an exception.");
    }
}
void Scan(void* player, std::uint32_t distanceLimit = 0, std::uint64_t now = GetTickCount64()) noexcept {
    // Keep the guard outside the SEH body; the native scan sees one complete
    // policy even if Apply & Save runs on the UI thread during a movement scan.
    std::shared_lock lock(SettingsMutex);
    if (!distanceLimit) {
        if (!Settings.walkingEnabled) return;
        distanceLimit = Settings.walkingRange;
    }
    ScanNative(player, distanceLimit, now);
}
auto ExternalRangeScan(void* player, std::uint32_t distanceLimit) noexcept -> bool {
    std::lock_guard lock(ScanApiMutex);
    if (!ScanApiReady || !player
        || distanceLimit < 1 || distanceLimit > Reimagined::PickupBridge::MaximumPickupDistance) return false;
    { std::shared_lock settingsLock(SettingsMutex); if (!Settings.enabled || Settings.validateOnly) return false; }
    Metrics.external.fetch_add(1, std::memory_order_relaxed);
    Scan(player, distanceLimit); return true;
}
auto ExternalScan(void* player) noexcept -> bool { return ExternalRangeScan(player, StandardPickupDistance); }
constexpr Reimagined::PickupBridge::ApiV1 ScanApi{sizeof(Reimagined::PickupBridge::ApiV1), Reimagined::PickupBridge::Version, ExternalScan};
constexpr Reimagined::PickupBridge::ApiV2 RangeScanApi{sizeof(Reimagined::PickupBridge::ApiV2), Reimagined::PickupBridge::RangeVersion, ExternalRangeScan};

#include "walking_pickup.inc"
auto __fastcall HookTrigger(void* game, void* player, void* packet, std::int32_t size) noexcept -> std::int64_t {
    const auto opcode = packet && size > 0 ? *static_cast<const std::uint8_t*>(packet) : static_cast<std::uint8_t>(0);
    if (!Reimagined::ActionHooks::IsMovement(opcode) || !OriginalTriggers[opcode]) return 1;
    const auto result = OriginalTriggers[opcode](game, player, packet, size);
    Metrics.actions.fetch_add(1, std::memory_order_relaxed);
    Scan(player);
    ScheduleWalkingFollowUp(game, player);
    return result;
}
#include "pickup_validation.inc"
auto ValidateRuntime() noexcept -> bool {
    bool valid = true;
    for (std::uint8_t opcode = FirstTriggerOpcode; opcode <= LastTriggerOpcode; ++opcode) {
        const auto expected = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Base + TriggerHandlerRvas[opcode]));
        valid = Context->CheckExpectedBytes(ServerPacketTableRva + opcode * sizeof(std::uintptr_t), &expected, sizeof(expected)) && valid;
    }
    valid = Context->CheckExpectedBytes(GetUnitRoomRva, GetUnitRoomExpected.data(), static_cast<std::uint32_t>(GetUnitRoomExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetRoomNeighboursRva, GetRoomNeighboursExpected.data(), static_cast<std::uint32_t>(GetRoomNeighboursExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetRoomFirstUnitRva, GetRoomFirstUnitExpected.data(), static_cast<std::uint32_t>(GetRoomFirstUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetNextUnitRva, GetNextUnitExpected.data(), static_cast<std::uint32_t>(GetNextUnitExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitTypeRva, UnitTypeExpected.data(), static_cast<std::uint32_t>(UnitTypeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitIdRva, UnitIdExpected.data(), static_cast<std::uint32_t>(UnitIdExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitModeRva, UnitModeExpected.data(), static_cast<std::uint32_t>(UnitModeExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitDistanceRva, UnitDistanceExpected.data(), static_cast<std::uint32_t>(UnitDistanceExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(UnitCollisionRva, UnitCollisionExpected.data(), static_cast<std::uint32_t>(UnitCollisionExpected.size())) && valid;
    valid = Context->CheckExpectedBytes(GetItemCodeRva, GetItemCodeExpected.data(), static_cast<std::uint32_t>(GetItemCodeExpected.size())) && valid;
    // Same authoritative identity lookup already used by built-in post-cast
    // pickup, also required when legacy Teleport owns the cast hooks.
    valid = Context->CheckExpectedBytes(Reimagined::EmbeddedTeleportPickup::GetServerUnitRva,
        Reimagined::EmbeddedTeleportPickup::GetServerUnitExpected.data(),
        static_cast<std::uint32_t>(Reimagined::EmbeddedTeleportPickup::GetServerUnitExpected.size())) && valid;
    return ValidatePickupEntry() && valid;
}
void ResolveRuntime() noexcept {
    GetUnitRoom = At<GetUnitRoomFn>(GetUnitRoomRva); GetRoomNeighbours = At<GetRoomNeighboursFn>(GetRoomNeighboursRva);
    GetRoomFirstUnit = At<UnitFn>(GetRoomFirstUnitRva); GetNextUnit = At<UnitFn>(GetNextUnitRva);
    UnitType = At<UnitIntFn>(UnitTypeRva); UnitId = At<UnitIntFn>(UnitIdRva); UnitMode = At<UnitIntFn>(UnitModeRva);
    UnitDistance = At<UnitPairFn>(UnitDistanceRva); UnitCollision = At<CollisionFn>(UnitCollisionRva);
    Pickup = At<PickupFn>(PickupRva); GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
    GetWalkingPlayer = At<GetWalkingPlayerFn>(Reimagined::EmbeddedTeleportPickup::GetServerUnitRva);
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
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    std::shared_lock lock(SettingsMutex);
    char message[768]{};
    std::snprintf(message, sizeof(message), "Auto Pickup 3.0.1: enabled=%s; validation-only=%s; walking-range=%u; post-cast=%s; post-cast-range=%u; scans=%llu; attempts=%llu; picked=%llu; refused=%llu; external-scans=%llu; exceptions=%llu; retry-skips=%llu; walking-follow-up-scans=%llu; walking-queue-failures=%llu.",
        Settings.enabled ? "true" : "false", Settings.validateOnly ? "true" : "false",
        Settings.walkingRange, StandaloneTeleport ? "standalone-owned" : Settings.teleportEnabled ? "built-in" : "off", Settings.teleportRange,
        static_cast<unsigned long long>(Metrics.scans.load()), static_cast<unsigned long long>(Metrics.attempts.load()),
        static_cast<unsigned long long>(Metrics.picked.load()), static_cast<unsigned long long>(Metrics.refused.load()),
        static_cast<unsigned long long>(Metrics.external.load()), static_cast<unsigned long long>(Metrics.exceptions.load()),
        static_cast<unsigned long long>(Metrics.retrySkips.load()), static_cast<unsigned long long>(Metrics.walkingScans.load()),
        static_cast<unsigned long long>(Metrics.walkingQueueFailures.load()));
    command->plugin->WriteConsoleMessage(message); return D2RL::ConsoleCommandResult::Handled;
}
void ResetRuntime() noexcept {
    SettingsPanel.Stop();
    PotionRouting::Stop();
    AutoInteract::Stop();
    Reimagined::EmbeddedTeleportPickup::Stop(); EmbeddedTeleportReady = false; StandaloneTeleport = false;
    { std::lock_guard lock(ScanApiMutex); ScanApiReady = false; }
    StopRetryLifecycle(); ResetItemRetries();
    OriginalTriggers.fill(nullptr);
    { std::unique_lock lock(SettingsMutex); Settings = {}; }
    GetUnitRoom = nullptr; GetRoomNeighbours = nullptr; GetRoomFirstUnit = nullptr; GetNextUnit = nullptr;
    UnitType = nullptr; UnitId = nullptr; UnitMode = nullptr; UnitDistance = nullptr;
    UnitCollision = nullptr; Pickup = nullptr; GetItemCode = nullptr;
    GetWalkingPlayer = nullptr;
    for (auto* n : {&Metrics.actions, &Metrics.scans, &Metrics.attempts, &Metrics.picked, &Metrics.refused, &Metrics.external, &Metrics.exceptions, &Metrics.retrySkips,
        &Metrics.walkingScans, &Metrics.walkingQueueFailures}) n->store(0);
}
}
D2RL_PLUGIN_EXPORT auto AutoPickupAnythingGetScanApi(std::uint32_t version, std::uint32_t size) noexcept -> const void* {
    if (version == Reimagined::PickupBridge::RangeVersion && size >= sizeof(RangeScanApi)) return &RangeScanApi;
    return Reimagined::PickupBridge::SupportedRequest(version, size) ? &ScanApi : nullptr;
}
D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }
D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->apiVersion < D2RL_PLUGIN_API_VERSION) return false;
    Context = context; Base = reinterpret_cast<std::uint8_t*>(context->exeBase); ResetRuntime();
    const auto* observedBuild = D2RL::GetBuildName(context);
    char buildDiagnostic[192]{};
    std::snprintf(buildDiagnostic, sizeof(buildDiagnostic),
        "Auto Pickup: observed D2R %s; validating the complete native fingerprint before pickup hooks.",
        observedBuild ? observedBuild : "unknown");
    context->LogInfo(buildDiagnostic);
    if (!Base || !ReadConfiguration()) return false;
    StandaloneTeleport = StandaloneTeleportPresent(context);
    {
        if (!ValidateRuntime()) { context->LogError("AutoPickupAnything: compatibility check failed; no hooks installed."); return false; }
        if (!PotionRouting::Prepare(context)) return false;
        if (!PrepareRetryLifecycle(!Settings.validateOnly)) { PotionRouting::Stop(); return false; }
        if (!StandaloneTeleport) {
            if (!Reimagined::EmbeddedTeleportPickup::Start(context, PostCastConfig(Settings), ExternalRangeScan, !Reimagined::PickupCoexist::Inspect(context, L"d2rl-teleport-auto-pickup.dll", Reimagined::PickupCoexist::Role::YieldingTeleport).cooperative)) {
                Reimagined::EmbeddedTeleportPickup::Stop(); StopRetryLifecycle(); PotionRouting::Stop();
                context->LogError("Auto Pickup: built-in post-cast compatibility/services failed; no walking hooks installed."); return false;
            }
            EmbeddedTeleportReady = true;
        } else context->LogInfo("Auto Pickup: older Teleport DLL detected; legacy post-cast pickup remains active. Edit teleport-auto-pickup.toml and restart to change its settings. Optionally update Teleport to 0.3.5+ to manage them in Pickit.");
        if (Settings.validateOnly) context->LogInfo("AutoPickupAnything: VALIDATION PASSED; diagnostic mode, no hooks installed.");
        else {
            ResolveRuntime(); if (!PotionRouting::Activate() || !InstallMutations()) { Reimagined::EmbeddedTeleportPickup::Stop(); EmbeddedTeleportReady = false; StopRetryLifecycle(); PotionRouting::Stop(); return false; }
            { std::lock_guard lock(ScanApiMutex); ScanApiReady = true; }
            context->LogInfo("Auto Pickup 3.0.1 active; normal stacking and placement, built-in post-cast or standalone delegation.");
        }
    }
    if (!context->RegisterConsoleCommand("auto-pickup", Status, "Show item selection pickup counters.")) context->LogWarn("AutoPickupAnything: status command unavailable.");
    if (!AutoInteract::Start(context, Settings.validateOnly))
        context->LogWarn("Auto Pickup: built-in Auto Interact is inactive; item pickup remains available. See preceding diagnostic.");
    try {
        if (!SettingsPanel.Start(context, SnapshotSettings, ApplyItemSelection, StandaloneTeleport, false, AutoInteract::SettingsApi()))
            context->LogWarn("AutoPickupAnything: optional item picker could not initialize its services or window. TOML configuration remains available.");
    } catch (...) { SettingsPanel.Stop(); context->LogWarn("AutoPickupAnything: optional item picker initialization failed."); }
    return true;
}
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { ResetRuntime(); Base = nullptr; Context = nullptr; }
}
