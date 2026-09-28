#include "native_fingerprint.hpp"
#include "player_scaling_policy.hpp"
#include "players_command.hpp"

#include <D2RLPlugin/lifecycle.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

using namespace ruffneckk::player_scaling;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

auto ReadText(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) Fail("cannot open " + path.string());
    std::string text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}

auto ReplaceOne(
        std::string source,
        const std::string& from,
        const std::string& to) -> std::string {
    const auto position = source.find(from);
    if (position == std::string::npos) {
        Fail("fixture token not found: " + from);
    }
    source.replace(position, from.size(), to);
    return source;
}

void TestPlayerControlsConfig() {
    auto root = toml::parse(ReadText(PLAYER_SCALING_TOML_FILE));
    auto& count = *root["player-count"].as_table();
    count.insert_or_assign("maximum-command-players", 64);
    count.insert_or_assign("start-game-players", 16);
    count.insert_or_assign("hotkeys", toml::array{
        toml::table{{"keys", "Shift+1"}, {"players", 1}},
        toml::table{{"keys", "Ctrl+S"}, {"players", 32}}});
    const auto parse = [&](Config& result, std::string& error) {
        std::ostringstream text;
        text << root;
        return ParseToml(text.str(), result, error);
    };
    Config result{};
    std::string error;
    Require(parse(result, error), "startup and hotkeys parse: " + error);
    Require(result.startGamePlayers == 16 && result.hotkeys.size() == 2,
        "startup and repeated hotkeys retained");
    Require(result.hotkeys[0].key == 0x31 && result.hotkeys[0].modifier == 1
            && result.hotkeys[1].key == 0x53 && result.hotkeys[1].modifier == 2,
        "key chords map to SDK key and modifier values");
    count.erase("start-game-players");
    count.erase("hotkeys");
    Require(parse(result, error) && result.startGamePlayers == 0
            && result.hotkeys.empty(), "old TOMLs keep controls disabled");
    count.insert_or_assign("start-game-players", 65);
    Require(!parse(result, error), "startup above configured cap rejected");
    count.insert_or_assign("start-game-players", 0);
    count.insert_or_assign("hotkeys", toml::array{
        toml::table{{"keys", "Shift+1"}, {"players", 1}},
        toml::table{{"keys", "shift+1"}, {"players", 8}}});
    Require(!parse(result, error), "duplicate normalized chord rejected");
    count.insert_or_assign("hotkeys", toml::array{});
    Require(parse(result, error) && result.hotkeys.empty(), "empty list disables hotkeys");
    for (const auto* keys : {"Shift+Ctrl+1", "", "Shift+", "F25", "Escape", "Alt+?"}) {
        count.insert_or_assign("hotkeys", toml::array{
            toml::table{{"keys", keys}, {"players", 1}}});
        Require(!parse(result, error), "unsupported chord rejected");
    }
    count.insert_or_assign("hotkeys", toml::array{
        toml::table{{"keys", "Alt+F12"}, {"players", 65}}});
    Require(!parse(result, error), "hotkey above cap rejected");
    count.insert_or_assign("hotkeys", toml::array{
        toml::table{{"keys", "Alt+F12"}, {"players", 4}}});
    count.insert_or_assign("minimum-scaling-players", 4);
    count.insert_or_assign("start-game-players", 1);
    Require(!parse(result, error), "startup below floor rejected");
    count.insert_or_assign("start-game-players", 0);
    Require(parse(result, error) && result.hotkeys[0].key == 0x7B
            && result.hotkeys[0].modifier == 3, "function key with modifier accepted");
    count.insert_or_assign("hotkeys", toml::array{
        toml::table{{"keys", "Shift+1"}, {"players", 1}}});
    Require(!parse(result, error), "hotkey below floor rejected");
    count.insert_or_assign("hotkeys", toml::array{1});
    Require(!parse(result, error), "malformed hotkey list rejected");
}

void TestNativeCommandRange() {
    struct NativeSetting {
        std::int32_t minimum{1};
        std::int32_t maximum{8};
        std::int32_t value{1};
        int writes{};
    } setting;
    const auto setRange = [](void* object, std::int32_t minimum,
            std::int32_t maximum) noexcept {
        auto& state = *static_cast<NativeSetting*>(object);
        state.minimum = minimum;
        state.maximum = maximum;
    };
    const auto setCount = [](void* object, std::int32_t requested) noexcept {
        auto& state = *static_cast<NativeSetting*>(object);
        state.value = std::clamp(requested, state.minimum, state.maximum);
        ++state.writes;
    };
    Config config{};
    config.maximumCommandPlayers = 16;
    setCount(&setting, ClampPlayersCommand(16, config));
    Require(setting.value == 8, "reproduce 1.0.2 downstream native clamp");
    for (const auto requested : {1, 8, 9, 16, 17, 65535}) {
        setting = {}; // Also model a native range reset between commands.
        const auto accepted = ApplyPlayersCommand(
            &setting, requested, config, setRange, setCount);
        Require(accepted == std::min(requested, 16)
                && setting.value == accepted && setting.writes == 1,
            "native setting retains the configured extended command");
    }
    config.minimumScalingPlayers = 4;
    config.maximumCommandPlayers = 65535;
    for (const auto requested : {-1, 0, 1, 4, 16, 65535, 65536}) {
        setting = {};
        const auto accepted = ApplyPlayersCommand(
            &setting, requested, config, setRange, setCount);
        Require(setting.value == std::clamp(requested, 4, 65535)
                && setting.value == accepted,
            "native range honors both configured bounds");
    }
    config = {};
    ApplyPlayersCommand(&setting, 16, config, setRange, setCount);
    Require(setting.value == 8 && setting.maximum == 8,
        "default configuration restores vanilla p8 ceiling");
    config.maximumCommandPlayers = 16;
    config.battleNetSimulationEnabled = true;
    ApplyPlayersCommand(&setting, 16, config, setRange, setCount);
    Require(setting.value == 1 && setting.minimum == 1 && setting.maximum == 1,
        "Battle.net simulation cannot expand its p1 lock");
}

void TestDefaultToml() {
    const auto text = ReadText(PLAYER_SCALING_TOML_FILE);
    Config config{};
    std::string error;
    Require(ParseToml(text, config, error), "default TOML: " + error);
    Require(config.schemaVersion == 1, "schema version");
    Require(config.enabled, "plugin enabled");
    Require(!config.battleNetSimulationEnabled,
        "Battle.net simulation defaults to false");
    Require(config.minimumScalingPlayers == 1, "vanilla minimum");
    Require(config.maximumCommandPlayers == 8, "vanilla command maximum");
    Require(config.startGamePlayers == 0 && config.hotkeys.size() == 2,
        "template keeps startup optional and exactly two shortcuts");
    Require(config.hotkeys[0].key == 0x31 && config.hotkeys[0].modifier == 1
        && config.hotkeys[0].players == 1 && config.hotkeys[1].key == 0x32
        && config.hotkeys[1].modifier == 1 && config.hotkeys[1].players == 2,
        "template defaults to Shift+1/p1 and Shift+2/p2");
    Require(config.monsterLife.enabled
            && config.monsterLife.maximumPlayers == 0,
        "life defaults");
    Require(config.monsterExperience.enabled
            && config.monsterExperience.maximumPlayers == 0,
        "experience defaults");
    Require(config.monsterOffense.enabled
            && config.monsterOffense.maximumPlayers == 0,
        "offense defaults");
    Require(!config.noDrop.playersCommandSimulatesNearbyParty,
        "native NoDrop default");
    Require(!IsNoDropPartySimulationEnabled(config),
        "native NoDrop helper default");
}

void TestLegacyTomlUpgrade() {
    constexpr std::string_view legacyToml = R"toml(
config-version = 1
enabled = true

[player-count]
minimum-scaling-players = 1
maximum-command-players = 8

[monster-life-scaling]
enabled = true
maximum-players = 0

[monster-experience-scaling]
enabled = true
maximum-players = 0

[monster-offense-scaling]
enabled = true
maximum-players = 0

[no-drop]
players-command-simulates-nearby-party = false

[troubleshooting]
show-usage-counters = false

[d2rl]
match = true
)toml";
    Config config{};
    std::string error;
    Require(ParseToml(legacyToml, config, error),
        "0.1.1 TOML upgrade compatibility: " + error);
    Require(!config.battleNetSimulationEnabled,
        "legacy TOML keeps Battle.net simulation disabled");
}

void TestBattleNetSimulationToml() {
    const auto original = ReadText(PLAYER_SCALING_TOML_FILE);
    const auto text = ReplaceOne(original,
        "enabled = false", "enabled = true");
    Config config{};
    std::string error;
    Require(ParseToml(text, config, error),
        "Battle.net simulation TOML: " + error);
    Require(config.battleNetSimulationEnabled,
        "Battle.net simulation parses true");
    Require(config.minimumScalingPlayers == 1
            && config.maximumCommandPlayers == 8,
        "Battle.net simulation preserves existing player limits");
}

void TestCommandAndChannelPolicy() {
    Config config{};
    Require(!HasNativePlayerCountScaling(0),
        "native zero-count exemption");
    Require(!HasNativePlayerCountScaling(-1),
        "native negative-count exemption");
    Require(HasNativePlayerCountScaling(1),
        "native positive count is eligible");
    Require(ClampPlayersCommand(-20, config) == 1, "negative command");
    Require(ClampPlayersCommand(1, config) == 1, "vanilla command floor");
    Require(ClampPlayersCommand(5, config) == 5, "command middle");
    Require(ClampPlayersCommand(999, config) == 8,
        "vanilla command ceiling");

    config.minimumScalingPlayers = 4;
    config.maximumCommandPlayers = 64;
    config.monsterLife.maximumPlayers = 16;
    Require(ClampPlayersCommand(1, config) == 4, "configured command floor");
    Require(ClampPlayersCommand(999, config) == 64,
        "configured command ceiling");
    Require(ResolveScalingCount(
            1, config.minimumScalingPlayers, config.monsterLife) == 4,
        "life baseline");
    Require(ResolveScalingCount(
            5, config.minimumScalingPlayers, config.monsterLife) == 5,
        "life scales above baseline");
    Require(ResolveScalingCount(
            16, config.minimumScalingPlayers, config.monsterLife) == 16,
        "life cap boundary");
    Require(ResolveScalingCount(
            17, config.minimumScalingPlayers, config.monsterLife) == 16,
        "life capped after boundary");
    Require(ResolveScalingCount(
            64, config.minimumScalingPlayers,
            config.monsterExperience) == 64,
        "experience unlimited");

    ScalingChannel frozen{false, 0};
    Require(ResolveScalingCount(64, 4, frozen) == 4,
        "disabled channel freezes at baseline");
    Require(MonsterLifeBonusPercent(1) == 0, "p1 life bonus");
    Require(MonsterLifeBonusPercent(4) == 150, "p4 life bonus");
    Require(MonsterLifeBonusPercent(8) == 350, "p8 life bonus");
    Require(MonsterLifeBonusPercent(9) == 350, "p9 life extension");
    Require(MonsterLifeBonusPercent(16) == 700, "p16 life extension");
    Require(MonsterExperienceBonusPercent(8) == 350, "p8 XP bonus");
    Require(MonsterExperienceBonusPercent(9) == 350, "p9 XP extension");
    Require(MonsterExperienceBonusPercent(16) == 420, "p16 XP extension");
    Require(MonsterExperienceBonusPercent(17) == 430, "XP continues p17");
}

void TestNoDropPolicy() {
    NoDropConfig config{};
    const auto nativeSoloP4 = ResolveNoDropCounts(4, 1, config);
    Require(nativeSoloP4.playersCommand == 4
            && nativeSoloP4.nearbyPartyMembers == 1
            && nativeSoloP4.effectivePlayers == 2,
        "native solo /players 4");

    const auto nativePartyFour = ResolveNoDropCounts(4, 4, config);
    Require(nativePartyFour.playersCommand == 4
            && nativePartyFour.nearbyPartyMembers == 4
            && nativePartyFour.effectivePlayers == 4,
        "native four-nearby party");

    config.playersCommandSimulatesNearbyParty = true;
    const auto simulatedP4 = ResolveNoDropCounts(4, 1, config);
    Require(simulatedP4.playersCommand == 4
            && simulatedP4.nearbyPartyMembers == 4
            && simulatedP4.effectivePlayers == 4,
        "/players 4 simulates four nearby party members");

    const auto simulatedP8 = ResolveNoDropCounts(8, 1, config);
    Require(simulatedP8.playersCommand == 8
            && simulatedP8.nearbyPartyMembers == 8
            && simulatedP8.effectivePlayers == 8
            && ResolveNoDropMonsterCap(
                4, simulatedP8.effectivePlayers, true) == 8,
        "/players 8 simulation survives the native monster-count cap");

    const auto higherRealNearby = ResolveNoDropCounts(4, 6, config);
    Require(higherRealNearby.playersCommand == 6
            && higherRealNearby.nearbyPartyMembers == 6
            && higherRealNearby.effectivePlayers == 6,
        "simulation never lowers a higher real nearby-party count");

    const auto extended = ResolveNoDropCounts(64, 1, config);
    Require(extended.playersCommand == 64
            && extended.nearbyPartyMembers == 64
            && extended.effectivePlayers == 64,
        "extended command count is simulated without a separate NoDrop cap");

    Require(ResolveNoDropMonsterCap(2, simulatedP4.effectivePlayers, false)
            == 2,
        "native NoDrop preserves the monster-count cap");

    Config masterMode{};
    masterMode.noDrop.playersCommandSimulatesNearbyParty = true;
    Require(IsNoDropPartySimulationEnabled(masterMode),
        "NoDrop simulation remains enabled without Battle.net simulation");
    masterMode.battleNetSimulationEnabled = true;
    Require(!IsNoDropPartySimulationEnabled(masterMode),
        "Battle.net simulation takes NoDrop simulation priority");
}

void TestIndependentNoDropMinimum() {
    auto root = toml::parse(ReadText(PLAYER_SCALING_TOML_FILE));
    auto& noDrop = *root["no-drop"].as_table();
    noDrop.insert_or_assign("minimum-effective-players", 5);
    std::ostringstream text;
    text << root;
    Config config{};
    std::string error;
    Require(ParseToml(text.str(), config, error),
        "independent NoDrop minimum parses: " + error);
    Require(config.noDrop.minimumEffectivePlayers == 5,
        "configured minimum retained independently of command range");
    const auto dropCount = [](int players, int nearby, int monsterCount,
            const Config& settings) {
        const auto counts = ResolveNoDropCounts(players, nearby, EffectiveNoDropConfig(settings));
        const auto cap = ResolveNoDropMonsterCap(monsterCount, counts.effectivePlayers,
            IsNoDropPartySimulationEnabled(settings), settings.noDrop.minimumEffectivePlayers);
        return (std::min)(counts.effectivePlayers, cap);
    };
    Require(dropCount(1, 1, 1, config) == 5, "minimum survives a p1 monster cap");
    Require(dropCount(8, 1, 8, config) == 5, "solo p8 native half credit is raised to five");
    Require(dropCount(8, 8, 8, config) == 8, "higher nearby-party count is preserved");
    Require(dropCount(8, 8, 2, config) == 5,
        "minimum does not remove the native monster cap above the floor");
    auto lowMinimum = config;
    lowMinimum.noDrop.minimumEffectivePlayers = 2;
    Require(dropCount(9, 0, 9, lowMinimum) == 4,
        "zero nearby party members do not gain extra half-credit above the floor");
    Require(ResolveScalingCount(1, config.minimumScalingPlayers, config.monsterLife) == 1
        && ResolveScalingCount(1, config.minimumScalingPlayers, config.monsterExperience) == 1
        && ResolveScalingCount(1, config.minimumScalingPlayers, config.monsterOffense) == 1
        && ClampPlayersCommand(1, config) == 1,
        "NoDrop minimum leaves command and monster channels independent");
    config.noDrop.playersCommandSimulatesNearbyParty = true;
    Require(dropCount(8, 1, 1, config) == 8, "party simulation above the minimum survives the monster cap");
    config.battleNetSimulationEnabled = true;
    Require(IsNoDropAdjustmentEnabled(config) && !IsNoDropPartySimulationEnabled(config)
        && dropCount(1, 1, 1, config) == 5 && dropCount(8, 1, 8, config) == 5,
        "Battle.net simulation preserves the minimum but disables command party simulation");
    config.noDrop.minimumEffectivePlayers = 1;
    Require(!IsNoDropAdjustmentEnabled(config) && dropCount(1, 1, 1, config) == 1,
        "default minimum in master mode preserves native behavior");
    config.battleNetSimulationEnabled = false;
    config.noDrop.playersCommandSimulatesNearbyParty = false;
    for (int players = 1; players <= 16; ++players) {
        for (int nearby = 1; nearby <= players; ++nearby) {
            for (int monster = 1; monster <= 16; ++monster) {
                const auto native = (std::min)(NativeNoDropEffectiveCount(players, nearby), monster);
                Require(dropCount(players, nearby, monster, config) == native,
                    "default minimum preserves existing native drop counts");
                auto floor = config;
                floor.noDrop.minimumEffectivePlayers = 5;
                Require(dropCount(players, nearby, monster, floor) == (std::max)(native, 5),
                    "independent floor is exactly applied after the native monster cap");
            }
        }
    }
    const auto parse = [&] {
        std::ostringstream updated;
        updated << root;
        return ParseToml(updated.str(), config, error);
    };
    noDrop.erase("minimum-effective-players");
    Require(parse() && config.noDrop.minimumEffectivePlayers == 1,
        "legacy TOMLs default to an inactive minimum");
    for (const auto minimum : {1, 5, 64, 65535}) {
        noDrop.insert_or_assign("minimum-effective-players", minimum);
        Require(parse() && config.noDrop.minimumEffectivePlayers == minimum,
            "independent minimum may exceed the command maximum");
        Require(dropCount(1, 1, 1, config) == minimum, "configured bounds reach final effective count");
    }
    for (const auto invalid : {-1, 0, 65536}) {
        noDrop.insert_or_assign("minimum-effective-players", invalid);
        Require(!parse(), "out-of-range minimum rejected");
    }
    noDrop.insert_or_assign("minimum-effective-players", "5");
    Require(!parse(), "string minimum rejected");
    noDrop.insert_or_assign("minimum-effective-players", 5.0);
    Require(!parse(), "fractional type rejected");
    noDrop.insert_or_assign("minimum-effective-players", true);
    Require(!parse(), "boolean minimum rejected");
}

void TestInvalidToml() {
    const auto original = ReadText(PLAYER_SCALING_TOML_FILE);
    Config config{};
    std::string error;

    auto text = ReplaceOne(original,
        "maximum-command-players = 8",
        "maximum-command-players = 65535");
    Require(ParseToml(text, config, error)
            && config.maximumCommandPlayers == 65535,
        "technical command maximum accepted");

    text = ReplaceOne(original,
        "minimum-scaling-players = 1", "minimum-scaling-players = 4");
    text = ReplaceOne(text,
        "maximum-command-players = 8", "maximum-command-players = 3");
    Require(!ParseToml(text, config, error)
            && error.find("at least") != std::string::npos,
        "maximum below baseline rejected");

    text = ReplaceOne(original,
        "minimum-scaling-players = 1", "minimum-scaling-players = 4");
    text = ReplaceOne(text,
        "maximum-players = 0", "maximum-players = 3");
    text = ReplaceOne(text, "\nplayers = 1\n", "\nplayers = 4\n");
    text = ReplaceOne(text, "\nplayers = 2\n", "\nplayers = 4\n");
    Require(!ParseToml(text, config, error)
            && error.find("monster-life-scaling.maximum-players")
                != std::string::npos,
        "life cap below baseline rejected");

    text = ReplaceOne(original,
        "maximum-command-players = 8", "maximum-command-players = 65536");
    Require(!ParseToml(text, config, error)
            && error.find("65535") != std::string::npos,
        "technical command maximum enforced");

    text = ReplaceOne(original,
        "players-command-simulates-nearby-party = false",
        "players-command-simulates-nearby-party = false\n"
        "minimum_players_command = 4");
    Require(!ParseToml(text, config, error)
            && error.find("unknown setting") != std::string::npos,
        "retired NoDrop settings rejected");

    text = ReplaceOne(original,
        "enabled = false", "enabled = false\nunknown-option = true");
    Require(!ParseToml(text, config, error)
            && error.find("battle-net-simulation.unknown-option")
                != std::string::npos,
        "unknown Battle.net simulation key rejected");

    text = ReplaceOne(original,
        "[player-count]\n",
        "[battle-net-simulation-extra]\n"
        "enabled = false\n\n"
        "[player-count]\n");
    Require(!ParseToml(text, config, error)
            && error.find("unknown setting: battle-net-simulation-extra")
                != std::string::npos,
        "unknown Battle.net simulation table rejected");

    text = ReplaceOne(original,
        "show-usage-counters = false",
        "show-usage-counters = false\nunknown-option = 1");
    Require(!ParseToml(text, config, error)
            && error.find("unknown setting") != std::string::npos,
        "unknown option rejected");

    text = ReplaceOne(original, "match = true", "match = false");
    Require(!ParseToml(text, config, error)
            && error == "d2rl.match must be true",
        "D2RLoader match false rejected");
}

void TestStaticContract() {
    const auto plugin = ReadText(PLAYER_SCALING_PLUGIN_FILE);
    const auto fingerprint = ReadText(PLAYER_SCALING_FINGERPRINT_FILE);
    constexpr auto pluginFlags = D2RL::PluginFlags::Shared
        | D2RL::PluginFlags::NativeHooks;
    Require(plugin.find(".author = \"RuffnecKk\"") != std::string::npos,
        "author metadata");
    Require(plugin.find(".id = \"ruffneckk-playerx-scaling-tweaks\"")
            != std::string::npos,
        "plugin id metadata");
    Require(plugin.find(".name = \"PlayerX Scaling Tweaks\"")
            != std::string::npos,
        "plugin name metadata");
    Require(plugin.find("ruffneckk-playerx-scaling-tweaks.toml")
            != std::string::npos,
        "configuration filename");
    Require(D2RL::HasValidPluginRole(pluginFlags)
            && D2RL::HasFlag(pluginFlags, D2RL::PluginFlags::Shared),
        "valid shared execution role");
    Require(plugin.find(
            ".flags = D2RL::PluginFlags::Shared | "
            "D2RL::PluginFlags::NativeHooks") != std::string::npos,
        "plugin metadata declares the shared execution role");
    Require(plugin.find("InstallInlineHook") != std::string::npos,
        "managed entry hook");
    Require(plugin.find("installSite(native::PlayersAtoiCallRva")
            != std::string::npos
            && plugin.find("installSite(native::NoDropMonsterCapCallRva")
                != std::string::npos
            && plugin.find("PatchCallRel32") == std::string::npos,
        "Loader-owned callsite hooks without external relays");
    Require(plugin.find("InstallBattleNetSimulationPatches")
            != std::string::npos
            && plugin.find("PatchBytes") != std::string::npos,
        "managed Battle.net simulation patches");
    Require(plugin.find("nearbyPartySlot") != std::string::npos
            && plugin.find("effectiveNoDropSlot") != std::string::npos
            && plugin.find("_AddressOfReturnAddress") == std::string::npos,
        "NoDrop ABI stack slots passed explicitly by the site hooks");
    Require(plugin.find("HasNativePlayerCountScaling(output[4])")
            != std::string::npos,
        "native monster exemption gate");
    Require(plugin.find("IsMonsterPlayerCountExempt(monster)")
            != std::string::npos,
        "native monster alignment exemption gate");
    Require(plugin.find("GetCurrentProcessId()") != std::string::npos,
        "ownership mutex is process-scoped");
    Require(plugin.find("VirtualQuery") != std::string::npos,
        "fingerprint reads validate mapped pages");
    Require(plugin.find("== 92777") == std::string::npos
            && plugin.find("== 93847") == std::string::npos
            && plugin.find("== 93787") == std::string::npos,
        "no build-number gate");
    Require(fingerprint.find("\"rva\": \"0x542F40\"")
            != std::string::npos,
        "bonus fingerprint");
    Require(fingerprint.find("\"rva\": \"0x588EF8\"")
            != std::string::npos,
        "damage fingerprint");
    Require(fingerprint.find("\"rva\": \"0x440A7D\"")
            != std::string::npos,
        "NoDrop fingerprint");
    Require(fingerprint.find("\"rva\": \"0x440AC1\"")
            != std::string::npos,
        "NoDrop monster-cap fingerprint");
    Require(fingerprint.find("\"rva\": \"0x54306E\"")
            != std::string::npos,
        "player-count output layout fingerprint");
    Require(fingerprint.find("\"rva\": \"0x188833\"")
            != std::string::npos,
        "/players dispatcher fingerprint");
    Require(fingerprint.find("\"rva\": \"0x42521C\"")
            != std::string::npos
            && fingerprint.find("\"rva\": \"0x425222\"")
                != std::string::npos,
        "real player-count selection fingerprint");
    Require(fingerprint.find("\"rva\": \"0x346ED\"")
            != std::string::npos
            && fingerprint.find("\"rva\": \"0x3474E\"")
                != std::string::npos
            && fingerprint.find("\"rva\": \"0xE1A09\"")
                != std::string::npos
            && fingerprint.find("\"rva\": \"0xE1A57\"")
                != std::string::npos,
        "Offline Difficulty lock fingerprints");
    Require(fingerprint.find("\"rva\": \"0xE19E0\"")
            != std::string::npos
            && fingerprint.find("\"rva\": \"0xE1A2F\"")
                != std::string::npos
            && fingerprint.find("\"rva\": \"0x188874\"")
                != std::string::npos
            && fingerprint.find("\"rva\": \"0xD2ED90\"")
                != std::string::npos,
        "Offline Difficulty active reset fingerprints");
}

} // namespace

int main() {
    TestPlayerControlsConfig();
    TestNativeCommandRange();
    TestDefaultToml();
    TestLegacyTomlUpgrade();
    TestBattleNetSimulationToml();
    TestCommandAndChannelPolicy();
    TestNoDropPolicy();
    TestIndependentNoDropMinimum();
    TestInvalidToml();
    TestStaticContract();
    std::cout << "PASS: PlayerX Scaling Tweaks policy and static contract\n";
    return 0;
}
