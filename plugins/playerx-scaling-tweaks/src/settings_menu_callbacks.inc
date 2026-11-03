// Included by the real plugin and its callback test. File access and the hub
// connection are supplied by the host; policy and transaction code are shared.
bool __cdecl GetMenuSetting(const char* key, char* output, std::uint32_t size, void*) noexcept {
    try {
        if (!key || !output || !size) return false;
        const auto* setting = menu::Find(key);
        if (!setting) return false;
        std::scoped_lock lock(LiveSettings.editMutex);
        std::string value;
        if (setting->restart) {
            std::string document, error;
            Config saved;
            if (!ReadSettingsDocument(document) || !ParseToml(document, saved, error)) return false;
            value = std::string_view(key) == "player-count.hotkeys"
                ? menu::Quote(menu::HotkeysValue(toml::parse(document))) : menu::Value(saved, *setting);
        } else value = menu::Value(*LiveSettings.Read(), *setting);
        if (value.size() >= size) return false;
        std::memcpy(output, value.c_str(), value.size() + 1);
        return true;
    } catch (...) { return false; }
}
bool __cdecl SetMenuSetting(const char* key, const char* value, char* error, std::uint32_t size, void*) noexcept {
    const auto fail = [&](const char* message) {
        if (error && size) std::snprintf(error, size, "%s", message);
        return false;
    };
    try {
        if (!key || !value) return fail("Missing setting or value.");
        const auto* setting = menu::Find(key);
        if (!setting) return fail("Unknown setting.");
        std::scoped_lock lock(LiveSettings.editMutex);
        std::string document, reason;
        if (!ReadSettingsDocument(document)) return fail("Cannot read the selected PlayerX configuration.");
        if (!LiveSettings.Change(document, *setting, value,
                [&](const std::string& updated) { return WriteSettingsDocument(document, updated); }, reason)) return fail(reason.c_str());
        // The hub owns its refresh after this callback; do not call ValueChanged.
        return true;
    } catch (const std::exception& exception) { return fail(exception.what()); }
      catch (...) { return fail("The setting could not be changed."); }
}
void StartSettingsMenu() noexcept {
    try {
        const bool registered = SettingsMenu.Start(Context, menu::Schema().c_str(), GetMenuSetting, SetMenuSetting, nullptr);
        Context->LogInfo(registered ? "PlayerX: I Hate TOMLs live settings joined."
            : "PlayerX: optional I Hate TOMLs integration unavailable or deferred.");
    } catch (...) { Context->LogWarn("PlayerX: optional settings page could not be registered."); }
}
