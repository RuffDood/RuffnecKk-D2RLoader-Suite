// Follow movement on authoritative game updates, not just the command that
// starts a path. Reuse the session-bound identity scheduler from post-cast
// pickup. No player/item pointer survives a callback, and no new hooks are used.
void __cdecl WalkingTick(const D2RL::PluginContext*, void*) noexcept;

auto WalkingPickupEnabled() noexcept -> bool {
    // Do not copy potentially large item override lists on every game update.
    std::shared_lock lock(SettingsMutex);
    return Settings.enabled && Settings.walkingEnabled && !Settings.validateOnly;
}

void CancelWalkingFollowUps() noexcept {
    std::lock_guard lock(WalkingMutex);
    WalkingFollowUps.Cancel();
}

void QueueWalkingTick(std::uint64_t ticket) noexcept {
    if (WalkingThreads && Context && WalkingThreads->runOnGameThread(Context, WalkingTick,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(ticket))) == D2RL::Threads::Result::Success) return;
    WalkingFollowUps.QueueFailed(ticket);
    Metrics.walkingQueueFailures.fetch_add(1, std::memory_order_relaxed);
}

auto WalkingActor(void* game, std::uint32_t id) noexcept -> void* {
    __try {
        if (!game || !GetWalkingPlayer || id == UINT32_MAX) return nullptr;
        auto* player = GetWalkingPlayer(game, 0, id);
        return player && UnitType(player) == 0 && UnitId(player) == id ? player : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

auto CaptureWalkingActor(void* game, void* player, std::uint32_t& id) noexcept -> bool {
    __try {
        if (!player || UnitType(player) != 0) return false;
        id = UnitId(player);
        return WalkingActor(game, id) == player;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

auto IsWalking(void* player) noexcept -> bool {
    __try {
        const auto mode = UnitMode(player);
        return mode == 2 || mode == 3 || mode == 6; // Walk, run, town walk.
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void ScheduleWalkingFollowUp(void* game, void* player, std::uint64_t now = GetTickCount64()) noexcept {
    std::lock_guard lock(WalkingMutex);
    if (!WalkingThreads || !WalkingPickupEnabled()) return;
    std::uint32_t id{};
    if (!CaptureWalkingActor(game, player, id)) return;
    const auto ticket = WalkingFollowUps.Arm(reinterpret_cast<std::uintptr_t>(game), id,
        now, WalkingIntervalMs, WalkingSettleMs);
    if (ticket) QueueWalkingTick(ticket);
}

void WalkingTickAt(const D2RL::PluginContext* context, std::uint64_t ticket, std::uint64_t now) noexcept {
    std::lock_guard lock(WalkingMutex);
    if (context != Context || !WalkingThreads) return;
    if (!WalkingPickupEnabled()) { WalkingFollowUps.Cancel(); return; }
    const auto batch = WalkingFollowUps.Poll(ticket, now, WalkingIntervalMs);
    if (!batch.again) return;
    for (std::size_t i = 0; i < batch.count; ++i) {
        auto* player = WalkingActor(reinterpret_cast<void*>(batch.game), batch.players[i]);
        if (!player) continue;
        Metrics.walkingScans.fetch_add(1, std::memory_order_relaxed);
        Scan(player, 0, now); // Live walking radius and item policy.
        // Keep following a long path while the player moves. A short grace
        // period catches arrival/animation transitions, then polling retires.
        if (IsWalking(player)) WalkingFollowUps.Arm(batch.game, batch.players[i], now, WalkingIntervalMs, WalkingSettleMs);
    }
    if (WalkingFollowUps.IsCurrent(ticket)) QueueWalkingTick(ticket);
}

void __cdecl WalkingTick(const D2RL::PluginContext* context, void* data) noexcept {
    WalkingTickAt(context, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)), GetTickCount64());
}
