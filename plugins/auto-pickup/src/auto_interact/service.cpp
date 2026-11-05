#include <Windows.h>
#include "native.hpp"
#include "settings.hpp"
#include "service.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <cstdio>
#include <string>

namespace AutoInteract {
namespace {
const D2RL::PluginContext* Context{};
Runtime Controller;
Native World;
Config Settings;
std::mutex SettingsMutex;
bool Loaded{}, LiveSettings{}, StartupBlocked{};
std::uint64_t Revision{1};
auto ReadSettings(Config& settings, std::string& text, std::string& error) -> bool {
    std::array<char, 65536> buffer{}; std::uint32_t required{};
    if (!Context || !Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)
        || !required || required > buffer.size() || buffer[required - 1] != '\0') { error = "Could not read Auto Interact config."; return false; }
    text.assign(buffer.data(), required - 1);
    RuffnecKk::AutoPickup::Config root; if (!RuffnecKk::AutoPickup::ParseConfig(text, root, error)) return false;
    settings = root.interact; return true;
}
auto ReadValues(S::Values* out) noexcept -> S::Result {
    std::lock_guard lock(SettingsMutex);
    if (!Loaded) return S::Result::Unavailable;
    if (!out || out->structSize < sizeof(S::Values)) return S::Result::Invalid;
    try {
        Config current; std::string text, error;
        if (!ReadSettings(current, text, error)) return S::Result::Unavailable;
        *out = ValuesFor(current); out->revision = Revision;
        out->state = (LiveSettings ? S::Live : 0U) | (Settings.validateOnly ? S::ValidationOnly : 0U) | (StartupBlocked ? S::StartupBlocked : 0U)
            | ((!LiveSettings && current.enabled) || current.validateOnly != Settings.validateOnly ? S::RestartRequired : 0U);
        return S::Result::Success;
    } catch (...) { return S::Result::Unavailable; }
}
auto SaveValues(const S::Values* expected, const S::Values* draft, char* error, std::uint32_t capacity) noexcept -> S::Result {
    std::lock_guard lock(SettingsMutex);
    auto fail = [&](S::Result result, const char* message) { if (error && capacity) std::snprintf(error, capacity, "%s", message); return result; };
    if (error && capacity) error[0] = 0;
    if (!Loaded) return fail(S::Result::Unavailable, "Auto Interact is no longer loaded.");
    if (!expected || !draft || !S::Valid(*expected) || !S::Valid(*draft)) return fail(S::Result::Invalid, "Invalid Auto Interact settings.");
    if (expected->revision != Revision) return fail(S::Result::Conflict, "Settings changed. Close and reopen before saving.");
    try {
        Config current, applied; std::string text, updated, why;
        if (!ReadSettings(current, text, why)) return fail(S::Result::Unavailable, why.c_str());
        if (!S::Same(ValuesFor(current), *expected)) return fail(S::Result::Conflict, "Settings changed outside this window. Close and reopen before saving.");
        if (!SaveSettings(text, *expected, *draft, updated, applied, why)) return fail(S::Result::Invalid, why.c_str());
        if (updated.size() >= 65536) return fail(S::Result::Invalid, "Auto Interact config exceeds the size limit.");
        auto result = S::Result::WriteFailed;
        const auto runtime = [&] { auto c = applied; c.validateOnly = Settings.validateOnly; return c; }();
        if (!Controller.Reconfigure(runtime, [&] {
            if (LiveSettings && !World.PrepareSettings(applied)) { result = S::Result::Incompatible; return false; }
            if (!Context->WriteConfig(updated.c_str())) return false;
            if (LiveSettings) World.ApplySettings(runtime);
            return true;
        })) return fail(result, result == S::Result::Incompatible
            ? "Locked-chest key checks are unsupported on this runtime. Nothing was saved."
            : "Loader refused the Auto Interact config write. Nothing was applied.");
        Settings = runtime; ++Revision;
        if ((!LiveSettings && applied.enabled) || applied.validateOnly != runtime.validateOnly)
            return fail(S::Result::RestartRequired, "Saved. Restart the game to enable Auto Interact.");
        return fail(S::Result::Success, "Saved.");
    } catch (...) { return fail(S::Result::Unavailable, "Auto Interact settings could not be saved."); }
}
const S::Api SettingsApiTable{sizeof(S::Api), S::Version, ReadValues, SaveValues};
auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    std::lock_guard lock(SettingsMutex);
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    const auto n = Controller.Status();
    char text[768]{};
    std::snprintf(text, sizeof(text), "Pickit Auto Interact: enabled=%s; validate-only=%s; actor-observations=%llu; scans=%llu; attempts=%llu; completed=%llu; no-progress=%llu; cooldown-skips=%llu; native-refused=%llu; read-failures=%llu; queue-failures=%llu; range=%u; scan-avg-us=%llu; scan-max-us=%llu.",
        Settings.enabled ? "true" : "false", Settings.validateOnly ? "true" : "false",
        static_cast<unsigned long long>(World.ActorObservations()), static_cast<unsigned long long>(n.scans),
        static_cast<unsigned long long>(n.engine.attempts), static_cast<unsigned long long>(n.engine.completed),
        static_cast<unsigned long long>(n.engine.noProgress), static_cast<unsigned long long>(n.engine.cooling),
        static_cast<unsigned long long>(n.refused), static_cast<unsigned long long>(n.readFailures), static_cast<unsigned long long>(n.queueFailures),
        Settings.interactRange, static_cast<unsigned long long>(n.scans ? n.scanTotalUs / n.scans : 0), static_cast<unsigned long long>(n.scanMaxUs));
    command->plugin->WriteConsoleMessage(text); return D2RL::ConsoleCommandResult::Handled;
}
void StopLocked() noexcept { Loaded = false; LiveSettings = false; StartupBlocked = false; ++Revision; Controller.Stop(); World.Stop(); }
}
auto SettingsApi() noexcept -> const S::Api* { return &SettingsApiTable; }
void Stop() noexcept { std::lock_guard lock(SettingsMutex); StopLocked(); Context = nullptr; }
auto Start(const D2RL::PluginContext* context, bool validationOnly) noexcept -> bool {
    std::lock_guard lock(SettingsMutex);
    if (!D2RL::HasContext(context) || context->apiVersion < D2RL_PLUGIN_API_VERSION) return false;
    StopLocked(); Context = context;
    try {
        std::string text, error;
        if (!ReadSettings(Settings, text, error)) { context->LogError(("Pickit Auto Interact: invalid config: " + error).c_str()); return false; }
        RuffnecKk::AutoPickup::Config root; RuffnecKk::AutoPickup::ParseConfig(text, root, error);
        if (!root.interactConfigured && context->pluginConfigPath) {
            const auto legacy = std::filesystem::path(context->pluginConfigPath).parent_path() / L"auto-interact.toml";
            if (std::filesystem::exists(legacy)) {
                if (std::filesystem::file_size(legacy) >= 65536) { context->LogError("Pickit: legacy Auto Interact config exceeds limit."); return false; }
                std::ifstream file(legacy, std::ios::binary); std::string previous{std::istreambuf_iterator<char>(file), {}};
                std::string updated;
                if (!file || !MigrateLegacy(text, previous, updated, error) || !context->WriteConfig(updated.c_str()) || !ReadSettings(Settings, text, error)) {
                    context->LogError("Pickit: Auto Interact migration failed; legacy file retained, opening inactive."); return false;
                }
                context->LogInfo("Pickit: migrated Auto Interact settings into [auto_interact]; legacy config retained.");
            }
        }
        Settings.validateOnly = Settings.validateOnly || validationOnly;
        Loaded = true;
        // Old DLLs cannot cooperate with the built-in observer. Avoid a second
        // opener even when the loader has not yet initialized the old DLL.
        bool legacyPresent = GetModuleHandleW(L"d2rl-auto-interact.dll") != nullptr;
        if (context->pluginDirectory) legacyPresent |= std::filesystem::exists(std::filesystem::path(context->pluginDirectory) / L"d2rl-auto-interact.dll");
        if (context->modDirectory) legacyPresent |= std::filesystem::exists(std::filesystem::path(context->modDirectory) / L"d2rloader/plugins/d2rl-auto-interact.dll");
        if (context->scopeRootDirectory) legacyPresent |= std::filesystem::exists(std::filesystem::path(context->scopeRootDirectory) / L"d2rloader/plugins/d2rl-auto-interact.dll");
        std::array<wchar_t, 32768> exe{}; if (GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())))
            legacyPresent |= std::filesystem::exists(std::filesystem::path(exe.data()).parent_path() / L"d2rloader/plugins/d2rl-auto-interact.dll");
        if (legacyPresent) { StartupBlocked = true; context->LogWarn("Pickit: remove the retired d2rl-auto-interact.dll and restart to enable built-in Auto Interact; item pickup remains active."); return false; }
        if (Settings.enabled || Settings.validateOnly) {
            if (!World.Admit(context, Settings) || !Controller.Start(context, Settings, World.Port())) { StartupBlocked = true; Controller.Stop(); World.Stop(); return false; }
            if (Settings.enabled && !Settings.validateOnly && !World.Activate()) { StartupBlocked = true; Controller.Stop(); World.Stop(); return false; }
            LiveSettings = Settings.enabled && !Settings.validateOnly;
        }
        if (!context->RegisterConsoleCommand("auto-interact", Status, "Show Auto Interact state and counters."))
            context->LogWarn("Auto Interact: status command unavailable.");
        context->LogInfo(Settings.validateOnly ? "Auto Interact: native validation passed; diagnostic mode, no observer or scans."
            : Settings.enabled ? "Pickit Auto Interact active. Move or cast once to acquire the authoritative actor; SDK scans then continue while idle."
            : "Auto Interact: disabled in configuration.");
        Loaded = true; return true;
    } catch (...) { Controller.Stop(); World.Stop(); context->LogError("Auto Interact: initialization failed; work stopped."); return false; }
}
}
