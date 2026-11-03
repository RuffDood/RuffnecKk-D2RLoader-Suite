// Read the int32 source of the already-governed RIP-relative load at 0x42521C.
// No unvalidated setting-object field or new native function is introduced.
auto ReadHudPlayers() noexcept -> std::int32_t {
    if (!Operational.load(std::memory_order_acquire) || Settings.battleNetSimulationEnabled) return 1;
    std::int32_t displacement{};
    std::memcpy(&displacement, native::GetPlayerCountArtificialCap.data() + 2, sizeof(displacement));
    const auto rva = native::GetPlayerCountArtificialCapRva + native::GetPlayerCountArtificialCap.size()
        + static_cast<std::uintptr_t>(displacement);
    std::int32_t players{};
    return TryRead(reinterpret_cast<const void*>(Base), rva, players)
        ? players_hud_slider::DisplayCount(players) : 1;
}
auto CanUseHud() noexcept -> bool {
    return LiveSettings.Read()->hudEnabled && CanApplyPlayerControl()
        && Settings.minimumScalingPlayers <= players_hud_slider::SelectionMaximum(Settings.maximumCommandPlayers);
}
void __cdecl ApplyQueuedHud(const D2RL::PluginContext* context, void*) noexcept {
    if (context != Context || !CanUseHud()) return;
    const auto players = std::clamp(PendingHudPlayers.load(std::memory_order_acquire),
        Settings.minimumScalingPlayers, players_hud_slider::SelectionMaximum(Settings.maximumCommandPlayers));
    if (ApplyPlayerControl(players)) HudSlider.SetCurrentPlayers(players);
}
auto QueueHudPlayers(std::int32_t players) noexcept -> bool {
    if (!CanUseHud() || !HudThreads || !HudThreads->runOnGameThread) return false;
    PendingHudPlayers.store(players, std::memory_order_release);
    return HudThreads->runOnGameThread(Context, ApplyQueuedHud, nullptr) == D2RL::Threads::Result::Success;
}
void StartHud() noexcept {
    if (Settings.battleNetSimulationEnabled || Settings.minimumScalingPlayers > players_hud_slider::SelectionMaximum(Settings.maximumCommandPlayers)) return;
    if (Context->QueryService(D2RL::ServiceId::Thread, 1, &HudThreads) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(HudThreads, D2RL::ThreadServiceV1RequiredSize)
        || !HudThreads->runOnGameThread || !HudThreads->runOnUiThread) {
        HudThreads = nullptr;
        Context->LogWarn("PlayerX HUD unavailable: required thread service is absent. Existing controls remain active.");
        return;
    }
    players_hud_slider::HudPlayerSliderConfig hud;
    hud.minimumPlayers = Settings.minimumScalingPlayers;
    const auto dx = Settings.hudX - hud.button.x;
    const auto dy = Settings.hudY - hud.button.y;
    hud.button.x += dx; hud.button.y += dy;
    hud.track.x += dx; hud.track.y += dy;
    hud.marker.x += dx; hud.marker.y += dy;
    const auto moveX = std::clamp(hud.track.x, 0, 3200 - hud.track.width) - hud.track.x;
    hud.track.x += moveX; hud.marker.x += moveX;
    const auto moveY = std::clamp(hud.marker.y, -hud.tooltip.y, 1801 - hud.marker.height) - hud.marker.y;
    hud.track.y += moveY; hud.marker.y += moveY;
    if (!HudSlider.Start(Context, hud, players_hud_slider::SelectionMaximum(Settings.maximumCommandPlayers),
        ReadHudPlayers, CanUseHud, CanUseHud, QueueHudPlayers)) {
        HudSlider.Stop();
        Context->LogWarn("PlayerX HUD unavailable; existing commands, hotkeys and I Hate TOMLs remain active.");
    } else Context->LogInfo("PlayerX: direct-click p24 HUD ready; I Hate TOMLs preserved.");
}
