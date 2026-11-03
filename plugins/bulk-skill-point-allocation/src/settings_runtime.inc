// Included in the implementation namespace. The provider invokes setters on
// the game UI thread; game-thread refund operations retain immutable snapshots.
std::filesystem::path LiveConfigurationPath() {
    if (GameplaySource) {
        if (GameplaySource->extension() == L".toml") return *GameplaySource;
        return GameplaySource->parent_path() / L"BulkSkillPointAllocation.toml";
    }
    const auto candidates = ResolveConfigCandidates(Context, L"BulkSkillPointAllocation.toml");
    if (candidates.empty()) throw std::runtime_error("Configuration directory is unavailable.");
    return candidates.front();
}
std::string LiveDocument() {
    const auto path = LiveConfigurationPath();
    if (std::filesystem::exists(path)) return RuffnecKk::LiveSettings::ReadFile(path);
    const auto state = RunningSettings.Read();
    std::string text = "enabled = " + std::string(state->allocation.enabled ? "true" : "false") + "\n";
    for (const auto key : {"skillPointsPerCtrlClick", "confirmShiftAllocation", "diagnostics"})
        text += std::string(key) + " = " + LivePage::Value(*state, key) + "\n";
    text += "\n[refund]\n";
    for (const auto key : {"single_removal", "batch_removal", "all_removal", "batch_points"})
        text += std::string(key) + " = " + LivePage::Value(*state, std::string("refund.") + key) + "\n";
    return text;
}
bool __cdecl GetLiveSetting(const char* key, char* out, std::uint32_t size, void*) noexcept {
    try {
        if (!key || !out || !size || !LivePage::Known(key)) return false;
        const auto saved = Removal::ParseToml(LiveDocument());
        const auto state = RunningSettings.Read();
        const auto value = LivePage::Value(std::string_view(key) == "enabled" ? saved : *state, key);
        if (value.size() >= size) return false;
        std::memcpy(out, value.c_str(), value.size()+1); return true;
    } catch (...) { return false; }
}
bool __cdecl SetLiveSetting(const char* key, const char* value, char* error,
        std::uint32_t errorSize, void*) noexcept {
    try {
        if (!key || !value || !LivePage::Known(key)) throw std::invalid_argument("Unknown setting.");
        const auto before = RunningSettings.Read();
        auto parse = [&](std::string_view text) {
            auto config = Removal::ParseToml(text);
            auto next = *before;
            LivePage::Copy(next, config, key);
            Removal::Validate(next.removal);
            config = std::move(next);
            return config;
        };
        const auto document = LiveDocument();
        const auto path = LiveConfigurationPath();
        RuffnecKk::LiveSettings::Change(RunningSettings, document, key, value, parse,
            [&](const std::string& text) {
                // Refuse an external change made while preparing this edit.
                if (std::filesystem::exists(path) && RuffnecKk::LiveSettings::ReadFile(path) != document) return false;
                return RuffnecKk::LiveSettings::SaveFile(path, text);
            });
        CancelPendingConfirmation();
        Context->LogInfo("BulkSkillPointAllocation: I Hate TOMLs setting saved; master switch requires restart, other controls apply live.");
        // The hub itself refreshes this page after the setter. Never notify it
        // recursively from a hub-originated edit.
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
        SettingsSchema = LivePage::Schema();
        const auto registered = SettingsMenu.Start(Context, SettingsSchema.c_str(), GetLiveSetting, SetLiveSetting, nullptr);
        Context->LogInfo(registered ? "BulkSkillPointAllocation: I Hate TOMLs live settings registered."
            : "BulkSkillPointAllocation: optional I Hate TOMLs live settings unavailable or deferred.");
    } catch (...) { Context->LogWarn("BulkSkillPointAllocation: optional settings page unavailable."); }
}
