#include <D2RLPlugin/api.h>
#include "settings_menu.hpp"
#include "settings_file.hpp"
#include "default_config.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>

using namespace ruffneckk::player_scaling;
namespace {
int checks{}, writes{}, notifications{};
bool saveWorks = true, readWorks = true;
std::string StoredDocument = DefaultConfig;
menu::State LiveSettings;
const D2RL::PluginContext* Context{};
struct Hub {
    void ValueChanged(const char*) { ++notifications; }
    template<class Get, class Set> bool Start(const D2RL::PluginContext*, const char* schema, Get, Set, void*) {
        return toml::parse(schema).contains("setting");
    }
} SettingsMenu;
bool ReadSettingsDocument(std::string& document) { document = StoredDocument; return readWorks; }
bool WriteSettingsDocument(const std::string& expected, const std::string& document) {
    if (!saveWorks || expected != StoredDocument) return false;
    ++writes; StoredDocument = document; return true;
}
#include "settings_menu_callbacks.inc"
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
void __cdecl Log(const D2RL::PluginContext*, const char*) noexcept {}
bool Set(const char* key, const std::string& value) {
    char error[512]{};
    const bool ok = SetMenuSetting(key, value.c_str(), error, sizeof(error), nullptr);
    if (!ok) std::printf("Refused %s: %s\n", key, error);
    return ok;
}
std::string Get(const char* key) {
    char value[8192]{};
    Check(GetMenuSetting(key, value, sizeof(value), nullptr), "getter succeeded");
    return value;
}
void Reset(std::string document = DefaultConfig) {
    StoredDocument = std::move(document);
    Config config; std::string error;
    Check(ParseToml(StoredDocument, config, error), "fixture valid");
    LiveSettings.Initialize(config);
}
void TestSettings() {
    Reset();
    StartSettingsMenu();
    const auto schema = toml::parse(menu::Schema());
    Check(schema["setting"].as_array()->size() == std::size(menu::Settings) + 1, "all fields exposed");
    for (const auto& setting : menu::Settings) {
        const auto before = Get(setting.key);
        Check(Set(setting.key, before), "every field round trips through actual callbacks");
        Check(Get(setting.key) == before, "getter matches stored or running value");
    }
    Check(Set("hud-player-slider.enabled", "false"), "HUD visibility saves and applies live");
    Check(!LiveSettings.Read()->hudEnabled, "live HUD disabled");
    Check(Set("hud-player-slider.enabled", "true"), "HUD can be reenabled live");
    Check(Set("hud-player-slider.x", "1600"), "HUD position saved");
    Check(LiveSettings.Read()->hudX == 1571 && Get("hud-player-slider.x") == "1600", "position waits for restart");
    Check(!Set("hud-player-slider.y", "1802"), "HUD coordinate bounds validated");
    Check(Set("monster-life-scaling.maximum-players", "16"), "live life cap");
    Check(ResolveScalingCount(64, 1, LiveSettings.Read()->monsterLife) == 16, "life policy uses new cap");
    Check(Set("monster-experience-scaling.enabled", "false"), "live experience toggle");
    Check(ResolveScalingCount(64, 1, LiveSettings.Read()->monsterExperience) == 1, "experience fixed at baseline");
    Check(Set("monster-offense-scaling.maximum-players", "8"), "live offense cap");
    Check(Set("no-drop.minimum-effective-players", "5"), "live NoDrop minimum");
    Check(Set("no-drop.players-command-simulates-nearby-party", "true"), "live NoDrop simulation");
    Check(Set("troubleshooting.show-usage-counters", "true"), "live counters");
    Check(Set("enabled", "false"), "save disabled for restart");
    Check(LiveSettings.Read()->enabled, "master remains active until restart");
    Check(Get("enabled") == "false", "restart getter shows saved value");
    Check(Set("battle-net-simulation.enabled", "true"), "save Battle.net mode");
    Check(!LiveSettings.Read()->battleNetSimulationEnabled, "Battle.net mode remains unchanged");
    Check(Set("player-count.maximum-command-players", "64"), "save extended bound");
    Check(LiveSettings.Read()->maximumCommandPlayers == 8, "bound remains unchanged");
    Check(Set("player-count.start-game-players", "16"), "save startup count");
    Check(LiveSettings.Read()->startGamePlayers == 0, "startup value remains unchanged");
    Check(Set("player-count.hotkeys", menu::Quote("[{keys = \"Ctrl+F3\", players = 16}]")), "replace hotkeys");
    Check(Set("player-count.hotkeys", menu::Quote("[]")), "remove inline hotkeys");
    Check(Set("player-count.hotkeys", menu::Quote("[{keys = \"Alt+2\", players = 2}]")), "add hotkeys");
    Check(LiveSettings.Read()->hotkeys.size() == 2, "startup hotkeys never replaced");
    Check(Set("monster-life-scaling.maximum-players", "32"), "live edit after pending restart changes");
    Check(!LiveSettings.Read()->battleNetSimulationEnabled && LiveSettings.Read()->enabled
        && LiveSettings.Read()->maximumCommandPlayers == 8, "live edits never activate saved startup fields");
    Config reloaded; std::string error;
    Check(ParseToml(StoredDocument, reloaded, error) && !reloaded.enabled && reloaded.battleNetSimulationEnabled
        && reloaded.maximumCommandPlayers == 64 && reloaded.startGamePlayers == 16
        && reloaded.hotkeys.size() == 1, "saved document reloads with pending settings");
    const auto before = StoredDocument;
    saveWorks = false;
    Check(!Set("monster-life-scaling.maximum-players", "48"), "write failure rejected");
    saveWorks = true;
    Check(StoredDocument == before && LiveSettings.Read()->monsterLife.maximumPlayers == 32, "failed save leaves file and policy unchanged");
    readWorks = false;
    Check(!Set("troubleshooting.show-usage-counters", "false"), "read failure rejected");
    readWorks = true;
    const int beforeWrites = writes;
    Check(!Set("no-drop.minimum-effective-players", "0"), "invalid count refused");
    Check(!Set("monster-life-scaling.enabled", "1"), "wrong type refused");
    Check(!Set("monster-life-scaling.enabled", "true\ninjected = 1"), "injection refused");
    Check(!Set("player-count.hotkeys", menu::Quote("[{keys=\"A\",players=1},{keys=\"A\",players=2}]")), "duplicate binding refused");
    Check(!Set("unknown", "true"), "unknown setting refused");
    Check(writes == beforeWrites, "invalid values never write");
    Check(!SetMenuSetting(nullptr, "true", nullptr, 0, nullptr), "null arguments refused");
    char small[1]; Check(!GetMenuSetting("enabled", small, 1, nullptr), "short getter buffer refused");
    Check(!GetMenuSetting(nullptr, small, 1, nullptr), "null getter key refused");
    Reset();
    std::atomic_bool done{};
    std::atomic_bool coherent{true};
    std::thread reader([&] {
        while (!done.load()) {
            const auto snapshot = LiveSettings.Read();
            if (snapshot->monsterLife.maximumPlayers < 0 || snapshot->minimumScalingPlayers != 1
                || snapshot->hotkeys.size() != 2) coherent.store(false);
        }
    });
    for (int i = 1; i <= 200; ++i) Check(Set("monster-life-scaling.maximum-players", std::to_string(i)), "repeated edit");
    done.store(true); reader.join();
    Check(coherent.load() && notifications == 0, "coherent reads and no redundant hub refreshes");
    Reset();
    Check(Set("player-count.hotkeys", menu::Quote("[]")), "remove bindings before raising baseline");
    Check(Set("player-count.minimum-scaling-players", "4"), "save baseline");
    Check(!Set("monster-life-scaling.maximum-players", "2"), "saved baseline validation");
    Reset(StoredDocument); // Simulate restart, now running p4.
    Check(Set("player-count.minimum-scaling-players", "1"), "save lower future baseline");
    Check(!Set("monster-life-scaling.maximum-players", "2"), "running baseline also validated");
    Check(LiveSettings.Read()->minimumScalingPlayers == 4, "baseline unchanged before restart");
}
void TestDocuments() {
    std::string original = DefaultConfig;
    original.insert(0, "# Preserve player comments\n");
    std::string crlf;
    for (char c : original) { if (c == '\n') crlf += '\r'; crlf += c; }
    Reset(crlf);
    Check(Set("monster-life-scaling.maximum-players", "16"), "CRLF edit");
    auto expected = crlf;
    const auto table = expected.find("[monster-life-scaling]");
    const auto offset = expected.find("maximum-players = 0", table) + std::strlen("maximum-players = ");
    expected.replace(offset, 1, "16");
    Check(StoredDocument == expected, "only selected value changes, all other bytes preserved");
    Check(Set("player-count.hotkeys", menu::Quote("[]")), "CRLF hotkey rewrite");
    Check(StoredDocument.find("# Press Shift+1") != std::string::npos, "hotkey comments preserved");
    for (std::size_t i = 0; i < StoredDocument.size(); ++i) if (StoredDocument[i] == '\n') Check(i && StoredDocument[i-1] == '\r', "CRLF preserved");
    original = DefaultConfig;
    const auto bnet = original.find("[battle-net-simulation]");
    original.erase(bnet, original.find("[player-count]", bnet) - bnet);
    const auto minimum = original.find("minimum-effective-players = 1");
    original.erase(minimum, std::strlen("minimum-effective-players = 1"));
    Reset(original);
    Check(Set("battle-net-simulation.enabled", "true"), "missing optional table appended");
    Check(Set("no-drop.minimum-effective-players", "5"), "missing optional key inserted");
    original = DefaultConfig;
    const auto cap = original.find("maximum-players = 0", original.find("[monster-life-scaling]"));
    original.replace(cap, std::strlen("maximum-players = 0"), "\"maximum-players\" = 0 # keep inline");
    Reset(original);
    Check(Set("monster-life-scaling.maximum-players", "16"), "quoted key supported");
    Check(StoredDocument.find("\"maximum-players\" = 16 # keep inline") != std::string::npos, "inline comment preserved");
}
void TestFile() {
    const auto path = std::filesystem::current_path() / (L"playerx-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".toml");
    Check(!std::filesystem::exists(path), "isolated file fixture");
    { std::ofstream file(path, std::ios::binary); file << "original\r\n"; }
    std::string text;
    Check(menu::ReadFileDocument(path, text) && text == "original\r\n", "file read preserves bytes");
    Check(!menu::SaveFileDocument(path, "stale", "bad"), "stale file update refused");
    Check(menu::SaveFileDocument(path, text, "updated\r\n"), "atomic replacement succeeds");
    Check(menu::ReadFileDocument(path, text) && text == "updated\r\n", "replacement readback");
    Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "read-only fixture");
    Check(!menu::SaveFileDocument(path, text, "bad"), "read-only replacement refused");
    Check(menu::ReadFileDocument(path, text) && text == "updated\r\n", "failed replacement preserves original");
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    Check(DeleteFileW(path.c_str()) != FALSE, "fixture cleanup");
}
}
int main(int argc, char** argv) {
    D2RL::PluginApi api{}; api.apiSize = sizeof(api); api.logInfo = Log; api.logWarn = Log;
    D2RL::PluginContext context{}; context.contextSize = sizeof(context); context.api = &api; Context = &context;
    TestSettings(); TestDocuments(); TestFile();
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        const std::string document{std::istreambuf_iterator<char>(input), {}};
        Reset(document);
        const auto old = *LiveSettings.Read();
        Check(Set("hud-player-slider.enabled", "false"), "legacy/current settings accept optional HUD field");
        Check(LiveSettings.Read()->maximumCommandPlayers == old.maximumCommandPlayers
            && LiveSettings.Read()->startGamePlayers == old.startGamePlayers
            && LiveSettings.Read()->hotkeys.size() == old.hotkeys.size(), "existing player settings preserved");
        Config saved; std::string reason;
        Check(ParseToml(StoredDocument, saved, reason) && saved.maximumCommandPlayers == old.maximumCommandPlayers,
            "in-memory edit preserves saved command limit");
        std::ifstream unchanged(argv[1], std::ios::binary);
        const std::string after{std::istreambuf_iterator<char>(unchanged), {}};
        Check(after == document, "external settings fixture was read only");
    }
    std::printf("%d settings/callback/file checks passed\n", checks);
}
