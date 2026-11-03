// I Hate TOMLs invokes these callbacks on the UI thread. All other threads
// retain one immutable snapshot for the duration of each native operation.
std::string LiveDocument() {
    std::array<char, MaximumConfigBytes> bytes{};
    std::uint32_t required{};
    if (!Context->ReadConfig(bytes.data(), static_cast<std::uint32_t>(bytes.size()), &required))
        throw std::runtime_error("Cannot read the current configuration.");
    return std::string(bytes.data());
}
HotkeyConfig ParseLiveDocument(std::string_view text) {
    HotkeyConfig config; std::string reason;
    if (!ParseConfig(text, config, reason)) throw std::invalid_argument(reason);
    return config;
}
bool __cdecl GetLiveSetting(const char* key, char* out, std::uint32_t size, void*) noexcept {
    try {
        if (!key || !out || !size || !ruffneckk::remote_stash::LivePage::Known(key)) return false;
        const auto state = RunningSettings.Read();
        const auto saved = ParseLiveDocument(LiveDocument());
        const auto value = ruffneckk::remote_stash::LivePage::Value(std::string_view(key) == "enabled" ? saved : state->settings, key);
        if (value.size() >= size) return false;
        std::memcpy(out, value.c_str(), value.size()+1); return true;
    } catch (...) { return false; }
}
bool ValidateLiveSprite(const HotkeyConfig& config) {
    std::vector<std::uint8_t> normal, low; std::string reason;
    if (config.button.spriteFile.empty()) {
        if (!LoadEmbeddedResource(REMOTE_STASH_DEFAULT_SPRITE_RESOURCE_ID, normal)
            || !LoadEmbeddedResource(REMOTE_STASH_DEFAULT_LOWEND_SPRITE_RESOURCE_ID, low)) return false;
    } else {
        if (!ReadSpriteFile(ResolveConfiguredSpritePath(config.button.spriteFile), normal, reason)) return false;
        if (config.button.lowendSpriteFile.empty()) low = normal;
        else if (!ReadSpriteFile(ResolveConfiguredSpritePath(config.button.lowendSpriteFile), low, reason)) return false;
    }
    ruffneckk::remote_stash::SpriteMetadata a{}, b{};
    return InspectSpA1Sprite(normal.data(), normal.size(), a) && InspectSpA1Sprite(low.data(), low.size(), b)
        && ButtonFramesFit(config.button, a.frameCount) && ButtonFramesFit(config.button, b.frameCount);
}
bool __cdecl SetLiveSetting(const char* key, const char* value, char* error,
        std::uint32_t errorSize, void*) noexcept {
    try {
        namespace Page = ruffneckk::remote_stash::LivePage;
        if (!key || !value || !Page::Known(key)) throw std::invalid_argument("Unknown setting.");
        const std::string_view name(key);
        const auto previous = RunningSettings.Read();
        const auto document = LiveDocument();
        (void)ParseLiveDocument(document);
        auto base = document;
        if (!RuffnecKk::LiveSettings::Scalar(document, "hotkey_enabled")
            && !RuffnecKk::LiveSettings::Scalar(document, "inventory_button_enabled")) {
            // Upgrade the legacy meaning of enabled (hotkey only) without
            // changing behavior when the first hub edit adds the new schema.
            const auto legacy = ParseLiveDocument(document);
            base = RuffnecKk::LiveSettings::Edit(base, "enabled", legacy.enabled ? "true" : "false");
            base = RuffnecKk::LiveSettings::Edit(base, "hotkey_enabled", legacy.hotkeyEnabled ? "true" : "false");
            base = RuffnecKk::LiveSettings::Edit(base, "inventory_button_enabled", legacy.inventoryButtonEnabled ? "true" : "false");
        }
        const auto edited = RuffnecKk::LiveSettings::Edit(base, name, value);
        const auto saved = ParseLiveDocument(edited);
        auto config = previous->settings;
        Page::Copy(config, saved, name);
        const bool visual = name.starts_with("button.") || name == "inventory_button_enabled";
        if (name.starts_with("button.") && !ActiveMpqRoot.empty())
            throw std::invalid_argument("This mod owns button appearance; edit its button.toml override instead.");
        if (!ActiveMpqRoot.empty()) config.button = previous->settings.button;
        if (visual && !ValidateLiveSprite(config)) throw std::invalid_argument("Sprite file or frame indexes are invalid.");
        const bool input = name == "hotkey" || name == "hotkey_enabled";
        // Prepare the established fallback input worker without replacing any
        // native hook. It stays inactive while the SDK route owns input.
        if (BootSettings.enabled && input) {
            if (InputThread && (InputThreadFailed.load() || !InputThreadReady.load())) {
                if (!StopInput()) throw std::runtime_error("Input worker is stopping; retry after restart.");
            }
            if (!InputThread && !StartInput()) {
                (void)StopInput();
                InputStopping.store(false, std::memory_order_release);
                throw std::runtime_error("Cannot prepare the new hotkey; the old binding was kept.");
            }
        }
        config.enabled = BootSettings.enabled;
        auto next = std::make_shared<const LiveState>(LiveState{std::move(config)});
        if (LiveDocument() != document) throw std::runtime_error("Configuration changed externally; retry the edit.");
        if (!Context->WriteConfig(edited.c_str())) throw std::runtime_error("Cannot save; the running settings were kept.");
        RunningSettings.Publish(next);
        if (BootSettings.enabled) {
            if (input) {
                if (InputService && InputAction != D2RL::Input::InvalidHandle) {
                    (void)InputService->unregisterAction(Context, InputAction);
                    InputAction = D2RL::Input::InvalidHandle;
                }
                UsesSdkInput = false;
                HotkeyPressed.store(false, std::memory_order_release);
                HotkeyCaptured.store(false, std::memory_order_release);
                HotkeyRequestPending.store(false, std::memory_order_release);
            }
            if (visual) {
                ++ButtonResourceRevision;
                UnregisterOwnedButton();
                EffectiveButtonSettings = next->settings.button;
                if (next->settings.inventoryButtonEnabled && !RegisterOwnedButton()) {
                    RunningSettings.Publish(previous);
                    const bool savedRestored = Context->WriteConfig(document.c_str());
                    EffectiveButtonSettings = previous->settings.button;
                    ++ButtonResourceRevision;
                    if (previous->settings.inventoryButtonEnabled && !RegisterOwnedButton())
                        Context->LogError("RemoteStash: old button resources could not be restored after a rejected live edit.");
                    if (!savedRestored) throw std::runtime_error("Button registration failed. Running settings restored, but TOML rollback failed; check the saved file.");
                    throw std::runtime_error("Button registration failed; previous settings restored.");
                }
            }
        }
        Context->LogInfo("RemoteStash: I Hate TOMLs setting saved; master switch requires restart, other controls apply live.");
        // The provider refreshes after this callback. Do not call ValueChanged.
        return true;
    } catch (const std::exception& e) {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", e.what());
        return false;
    } catch (...) {
        if (error && errorSize) std::snprintf(error, errorSize, "Setting could not be changed.");
        return false;
    }
}
void StartSettingsMenu() noexcept {
    try {
        SettingsSchema = ruffneckk::remote_stash::LivePage::Schema(!ActiveMpqRoot.empty());
        const auto registered = SettingsMenu.Start(Context, SettingsSchema.c_str(), GetLiveSetting, SetLiveSetting, nullptr);
        Context->LogInfo(registered ? "RemoteStash: I Hate TOMLs live settings registered."
            : "RemoteStash: optional I Hate TOMLs live settings unavailable or deferred.");
    } catch (...) { Context->LogWarn("RemoteStash: optional settings page unavailable."); }
}
