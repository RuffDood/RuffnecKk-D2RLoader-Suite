// Included in the implementation namespace. I Hate TOMLs calls these on the UI thread.
std::filesystem::path LiveConfigurationPath() {
    if (LoadedConfigPath != "embedded defaults") return std::filesystem::path(LoadedConfigPath);
    const auto candidates = ConfigCandidates();
    if (candidates.empty()) throw std::runtime_error("Configuration directory is unavailable.");
    return candidates.front();
}

std::string LiveDocument(const std::filesystem::path& path) {
    return std::filesystem::exists(path)
        ? RuffnecKk::LiveSettings::ReadFile(path) : std::string(DefaultConfig);
}

bool __cdecl GetLiveSetting(const char* key, char* out, std::uint32_t size, void*) noexcept {
    CallbackGuard guard;
    if (!guard.CanProcess()) return false;
    try {
        if (!key || !out || !size || !LivePage::Known(key)) return false;
        const auto running = RunningSettings.Read();
        const auto value = LivePage::IsLive(key) ? LivePage::Value(*running, key)
            : LivePage::Value(LivePage::Parse(LiveDocument(LiveConfigurationPath())), key);
        if (value.size() >= size) return false;
        std::memcpy(out, value.c_str(), value.size() + 1);
        return true;
    } catch (...) { return false; }
}

bool __cdecl SetLiveSetting(const char* key, const char* value, char* error,
        std::uint32_t errorSize, void*) noexcept {
    CallbackGuard guard;
    if (!guard.CanProcess()) return false;
    try {
        if (!key || !value || !LivePage::Known(key)) throw std::invalid_argument("Unknown setting.");
        const auto path = LiveConfigurationPath();
        const auto existed = std::filesystem::exists(path);
        const auto document = LiveDocument(path);
        const auto running = RunningSettings.Read();
        const auto fileValue = LivePage::FileValue(key, value);
        (void)LivePage::Parse(document);
        const auto edited = LivePage::Edit(document, key, fileValue);
        const auto next = std::make_shared<const Config>(
            LivePage::CopyLive(*running, LivePage::Parse(edited), key));
        if (std::filesystem::exists(path) != existed
                || (existed && RuffnecKk::LiveSettings::ReadFile(path) != document))
            throw std::runtime_error("Configuration changed externally; retry the edit.");
        if (!RuffnecKk::LiveSettings::SaveFile(path, edited))
            throw std::runtime_error("Cannot save; the running settings were kept.");
        RunningSettings.Publish(next);
        // The provider refreshes after returning. A notification here would re-enter it.
        return true;
    } catch (const std::exception& exception) {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return false;
    } catch (...) {
        if (error && errorSize) std::snprintf(error, errorSize, "Setting could not be changed.");
        return false;
    }
}

void StartSettingsMenu() noexcept {
    const auto registered = SettingsMenu.Start(Context, LivePage::Schema, GetLiveSetting, SetLiveSetting, nullptr);
    Context->LogInfo(registered
        ? "AutomaticMaterialsDeposit: I Hate TOMLs live settings registered."
        : "AutomaticMaterialsDeposit: optional I Hate TOMLs live settings unavailable or deferred.");
}
