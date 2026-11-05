void __cdecl FollowUpTick(const D2RL::PluginContext*, void*) noexcept;

auto QueueFollowUp(std::uint64_t ticket) noexcept -> bool {
    if (!Threads || !Threads->runOnGameThread || !Context) return false;
    return Threads->runOnGameThread(Context, FollowUpTick,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(ticket))) == D2RL::Threads::Result::Success;
}

void QueueFollowUpOrRetire(std::uint64_t ticket) noexcept {
    if (QueueFollowUp(ticket)) return;
    { std::lock_guard lock(FollowUpMutex); FollowUps.QueueFailed(ticket); }
    Metrics.queueFailures.fetch_add(1, std::memory_order_relaxed);
    if (ShouldLogDiagnostic()) Context->LogWarn("TeleportPickup: follow-up game update unavailable; a later action can retry.");
}

auto CaptureActor(void* game, void* player, std::uint32_t& id) noexcept -> bool {
    __try {
        if (!game || !player || !GetServerUnit || UnitType(player) != 0) return false;
        id = UnitId(player);
        // GetUnitRoom returns an active room, not the packet handler's game.
        // Resolve this identity from the actual authoritative game instead.
        return id != FollowUpWindow::InvalidId && GetServerUnit(game, 0, id) == player;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void ScheduleFollowUp(void* game, void* player) noexcept {
    if (!Settings.followUpEnabled || !Threads || !Context) return;
    std::uint32_t id{};
    if (!CaptureActor(game, player, id)) {
        TraceProgress("actor rejected by authoritative lookup", Metrics.actorRejected.fetch_add(1, std::memory_order_relaxed) + 1);
        return;
    }
    std::uint64_t ticket{};
    bool active{};
    {
        std::lock_guard lock(FollowUpMutex);
        active = FollowUps.IsActive();
        ticket = FollowUps.Arm(reinterpret_cast<std::uintptr_t>(game), id,
            GetTickCount64(), Settings.followUpIntervalMs, Settings.followUpWindowMs);
    }
    if (!active && ShouldLogDiagnostic()) Context->LogWarn("TeleportPickup: cast received before a live session event; landing scan not armed.");
    if (ticket) {
        TraceProgress("landing scan window scheduled", Metrics.windows.fetch_add(1, std::memory_order_relaxed) + 1);
        QueueFollowUpOrRetire(ticket);
    }
}

auto FindPlayer(void* game, std::uint32_t id) noexcept -> void* {
    if (!game || !GetServerUnit || id == FollowUpWindow::InvalidId) return nullptr;
    void* player = GetServerUnit(game, 0, id);
    return player && UnitType(player) == 0 && UnitId(player) == id ? player : nullptr;
}

void RunFollowUpScans(const FollowUpWindow::PollResult& batch) noexcept {
    // Game pointers are kept only in session-bound SDK work. Player pointers
    // are resolved anew for every scan, after teleport has had time to finish.
    __try {
        for (std::size_t i = 0; i < batch.count; ++i) {
            if (void* player = FindPlayer(reinterpret_cast<void*>(batch.game), batch.players[i])) {
                Metrics.followUpScans.fetch_add(1, std::memory_order_relaxed);
                Scan(player, false);
            } else {
                TraceProgress("player lookup missed", Metrics.lookupMisses.fetch_add(1, std::memory_order_relaxed) + 1);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (ShouldLogDiagnostic()) Context->LogWarn("TeleportPickup: follow-up scan skipped after a native exception.");
    }
}

void __cdecl FollowUpTick(const D2RL::PluginContext* context, void* userData) noexcept {
    std::lock_guard settingsLock(SettingsMutex);
    if (context != Context || !Settings.enabled || !Settings.followUpEnabled) return;
    TraceProgress("game update callback received", Metrics.ticks.fetch_add(1, std::memory_order_relaxed) + 1);
    const auto ticket = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    FollowUpWindow::PollResult batch{};
    {
        std::lock_guard lock(FollowUpMutex);
        batch = FollowUps.Poll(ticket, GetTickCount64(), Settings.followUpIntervalMs);
    }
    if (!batch.again) return;
    if (batch.count) RunFollowUpScans(batch);
    {
        std::lock_guard lock(FollowUpMutex);
        if (!FollowUps.IsCurrent(ticket)) return;
    }
    QueueFollowUpOrRetire(ticket);
}

void __cdecl OnSessionEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    std::lock_guard settingsLock(SettingsMutex);
    if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
    ResetGoldRetries();
    if (Settings.diagnosticsEnabled && Context) {
        char message[160]{};
        std::snprintf(message, sizeof(message), "TeleportPickup: session event=%u; generation=%llu.",
            static_cast<unsigned>(event->kind), static_cast<unsigned long long>(event->sessionGeneration));
        Context->LogInfo(message);
    }
    std::lock_guard lock(FollowUpMutex);
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) FollowUps.Reset(event->sessionGeneration, false);
    else FollowUps.Enter(event->sessionGeneration);
}

auto PrepareFollowUp(bool registerListeners) noexcept -> bool {
    if (!Settings.followUpEnabled) return true;
    if (Context->QueryService(D2RL::ServiceId::Thread, D2RL::ThreadServiceV1Version, &Threads) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(Threads, D2RL::ThreadServiceV1RequiredSize) || !Threads->runOnGameThread
        || Context->QueryService(D2RL::ServiceId::Lifecycle, D2RL::LifecycleServiceV1Version, &Lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceV1Field(Lifecycle, D2RL::LifecycleServiceV1RequiredSize)
        || !Lifecycle->registerGameplayEventListener || !Lifecycle->unregisterGameplayEventListener) {
        Context->LogError("TeleportPickup: follow-up scans need game-thread and session-event services; nothing was patched.");
        return false;
    }
    if (!registerListeners) return true;
    constexpr std::array kinds{D2RL::Lifecycle::GameplayEventKind::GameJoined,
        D2RL::Lifecycle::GameplayEventKind::GameLeft, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const D2RL::Lifecycle::GameplayEventListener listener{D2RL::Lifecycle::GameplayEventListenerSize, 0, kinds[i], 0, OnSessionEvent, nullptr};
        if (Lifecycle->registerGameplayEventListener(Context, &listener, &FollowUpListeners[i]) != D2RL::Lifecycle::Result::Success) {
            Context->LogError("TeleportPickup: could not subscribe to session changes; nothing was patched."); return false;
        }
    }
    return true;
}

void StopFollowUps() noexcept {
    { std::lock_guard lock(FollowUpMutex); FollowUps.Reset(0, false); }
    if (Lifecycle && Context) for (auto& listener : FollowUpListeners) {
        if (listener) Lifecycle->unregisterGameplayEventListener(Context, listener);
        listener = 0;
    }
    Threads = nullptr; Lifecycle = nullptr;
}
