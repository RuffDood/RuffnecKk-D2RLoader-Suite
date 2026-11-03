// Shared production pointer handler. The contract test supplies a panel adapter;
// the DLL supplies the real SDK panel. No native UI message text is involved.
void PLAYERX_HUD_POINTER_CLASS::ProcessPointerSample(bool down, bool pointerReady, POINT point, bool foreground) noexcept {
    const bool eligible = active_.load(std::memory_order_acquire) && sessionReady_ && foreground
        && canApply_ != nullptr && canApply_() && nativePanel_.IsHudButtonOpen();
    const bool insideButton = pointerReady && point.x >= config_.button.x && point.y >= config_.button.y
        && point.x < config_.button.x + config_.button.width && point.y < config_.button.y + config_.button.height;
    const auto edges = directClick_.Sample(down, pointerReady, insideButton, eligible);
    if (!eligible || !pointerReady) {
        nativeDragActive_ = false;
        if (nativePanel_.IsOpen()) nativePanel_.Close();
        sliderOpen_ = false;
        return;
    }
    if (edges.toggle) {
        ToggleSlider();
        return; // Opening/closing a panel cannot also apply a player setting.
    }
    sliderOpen_ = nativePanel_.IsOpen();
    if (!sliderOpen_) { nativeDragActive_ = false; return; }
    if (edges.pressed && !insideButton && IsSliderPoint(point)) nativeDragActive_ = true;
    if (nativeDragActive_ && down) {
        selectedPlayers_ = ResolvePlayerCount(point);
        nativePanel_.QueueTooltipValue(selectedPlayers_);
        return;
    }
    if (!nativeDragActive_ || !edges.released) return;
    const auto chosen = ResolvePlayerCount(point);
    nativeDragActive_ = false;
    sliderOpen_ = false;
    if (apply_ == nullptr || !apply_(chosen)) {
        if (context_ != nullptr) context_->LogWarn("PlayerX HUD: selected count could not be queued for application.");
    }
    nativePanel_.Close();
}

void PLAYERX_HUD_POINTER_CLASS::ToggleSlider() noexcept {
    if (nativePanel_.IsOpen()) {
        nativePanel_.Close();
        sliderOpen_ = false;
        nativeDragActive_ = false;
    } else if (canApply_ != nullptr && canApply_()
            && prepare_ != nullptr && prepare_()
            && nativePanel_.Open()) {
        sliderOpen_ = true;
        selectedPlayers_ = std::clamp(
            currentPlayers_.load(std::memory_order_acquire), config_.minimumPlayers, maximumPlayers_);
        nativePanel_.SetTooltipValue(currentPlayers_.load(std::memory_order_acquire));
        nativeDragActive_ = false;
    } else {
        sliderOpen_ = false;
        nativeDragActive_ = false;
        if (context_ != nullptr) {
            context_->LogWarn(
                "Players HUD Slider: the native player-count panel could not be opened.");
        }
    }
}

auto PLAYERX_HUD_POINTER_CLASS::IsSliderPoint(POINT point) const noexcept -> bool {
    if (RectContains(point, config_.track.x, config_.track.y,
            config_.track.width, config_.track.height)) {
        return true;
    }
    const auto selected = std::clamp(selectedPlayers_, config_.minimumPlayers, maximumPlayers_);
    const auto markerX = static_cast<std::int32_t>(std::lround(MarkerLeft(selected)));
    if (RectContains(point, markerX, config_.marker.y,
            config_.marker.width, config_.marker.height)) {
        return true;
    }
    return false;
}

auto PLAYERX_HUD_POINTER_CLASS::MarkerLeft(std::int32_t players) const noexcept -> double {
    const double startCenter = config_.marker.x + config_.marker.width / 2.0;
    const double endCenter = config_.track.x + config_.track.width
        - config_.marker.width / 2.0;
    if (maximumPlayers_ <= config_.minimumPlayers || endCenter <= startCenter) {
        return config_.marker.x;
    }
    const double fraction = static_cast<double>(players - config_.minimumPlayers)
        / static_cast<double>(maximumPlayers_ - config_.minimumPlayers);
    return startCenter + fraction * (endCenter - startCenter)
        - config_.marker.width / 2.0;
}

auto PLAYERX_HUD_POINTER_CLASS::MarkerCenter(std::int32_t players) const noexcept -> double {
    return MarkerLeft(players) + config_.marker.width / 2.0;
}

auto PLAYERX_HUD_POINTER_CLASS::ResolvePlayerCount(POINT point) const noexcept
        -> std::int32_t {
    if (maximumPlayers_ <= config_.minimumPlayers) return config_.minimumPlayers;
    const double start = MarkerCenter(config_.minimumPlayers);
    const double end = MarkerCenter(maximumPlayers_);
    if (end <= start) return config_.minimumPlayers;
    const double fraction = std::clamp(
        (static_cast<double>(point.x) - start) / (end - start), 0.0, 1.0);
    return CountAt(fraction, config_.minimumPlayers, maximumPlayers_);
}
