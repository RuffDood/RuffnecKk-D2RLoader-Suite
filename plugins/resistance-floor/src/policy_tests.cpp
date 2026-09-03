#include "resistance_floor_policy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

using namespace ruffneckk::resistance_floor;

namespace {

int Failures{};

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": CHECK failed: " #expression << '\n';               \
            ++Failures;                                                        \
        }                                                                      \
    } while (false)

auto ReadFile(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

constexpr std::string_view ValidToml = R"toml(
config_version = 3
enabled = true
[players]
enabled = true
minimum_resistance = -1000
[companions]
enabled = true
minimum_resistance = -250
[monsters]
enabled = false
minimum_resistance = -999
[character_screen]
show_resistances_below_minus_100 = true
[troubleshooting]
show_usage_counters = false
)toml";

auto Parses(std::string_view text) -> bool {
    Config config{};
    std::string error;
    return ParseToml(text, config, error);
}

void TestParsing() {
    Config config{};
    std::string error;
    CHECK(ParseToml(ValidToml, config, error));
    CHECK(error.empty());
    CHECK(config.schemaVersion == 3);
    CHECK(config.enabled);
    CHECK(config.players.enabled);
    CHECK(config.players.floor == -1000);
    CHECK(config.playerOwnedUnits.floor == -250);
    CHECK(!config.monsters.enabled);
    CHECK(config.monsters.floor == -999);
    CHECK(config.display.syncCharacterScreen);
    CHECK(!config.diagnostics);

    auto replace = [](std::string text, std::string_view from,
                      std::string_view to) {
        const auto position = text.find(from);
        if (position != std::string::npos) {
            text.replace(position, from.size(), to);
        }
        return text;
    };
    CHECK(!Parses(replace(
        std::string(ValidToml),
        "minimum_resistance = -1000",
        "minimum_resistance = -1001")));
    CHECK(!Parses(replace(
        std::string(ValidToml),
        "minimum_resistance = -1000",
        "minimum_resistance = -99")));
    CHECK(!Parses(replace(std::string(ValidToml), "enabled = true", "enabled = 1")));
    CHECK(!Parses(replace(std::string(ValidToml), "config_version = 3", "config_version = 2")));
    CHECK(!Parses(replace(std::string(ValidToml), "config_version = 3\n", "")));
    CHECK(!Parses(replace(std::string(ValidToml), "config_version", "schema_version")));
    CHECK(!Parses(replace(std::string(ValidToml), "minimum_resistance", "floor")));
    CHECK(!Parses(replace(
        std::string(ValidToml), "[companions]", "[targets.player_owned_units]")));
    CHECK(!Parses(replace(std::string(ValidToml), "[monsters]", "[enemies]")));
    CHECK(!Parses(std::string(ValidToml) + "\nunknown = true\n"));
    CHECK(!Parses(replace(
        std::string(ValidToml),
        "show_resistances_below_minus_100 = true",
        "show_resistances_below_minus_100 = true\nshow_physical_and_magic = true")));
    CHECK(!Parses(replace(
        std::string(ValidToml),
        "show_resistances_below_minus_100 = true",
        "show_resistances_below_minus_100 = true\nposition_from_left = 24")));
}

void TestPolicy() {
    constexpr std::int32_t supported[]{36, 37, 39, 41, 43, 45};
    for (const auto stat : supported) CHECK(IsSupportedResistanceStat(stat));
    CHECK(!IsSupportedResistanceStat(-1));
    CHECK(!IsSupportedResistanceStat(38));
    CHECK(!IsSupportedResistanceStat(40));
    CHECK(!IsSupportedResistanceStat(46));

    Config config{};
    config.players = {true, -1000};
    config.playerOwnedUnits = {true, -250};
    config.monsters = {false, -999};
    CHECK(SelectConfiguredFloor(config, UnitClass::Player, 36) == -1000);
    CHECK(SelectConfiguredFloor(config, UnitClass::PlayerOwned, 45) == -250);
    CHECK(SelectConfiguredFloor(config, UnitClass::Monster, 39) == -100);
    CHECK(SelectConfiguredFloor(config, UnitClass::Unknown, 39) == -100);
    CHECK(SelectConfiguredFloor(config, UnitClass::Player, 40) == -100);
    config.monsters.enabled = true;
    CHECK(SelectConfiguredFloor(config, UnitClass::Monster, 37) == -999);
    config.enabled = false;
    CHECK(SelectConfiguredFloor(config, UnitClass::Player, 36) == -100);

}

void TestCandidatesAndRel32() {
    const auto candidates = BuildConfigCandidates(
        L"C:/D2R/mods/BKVince/d2rloader/config",
        L"C:/D2R/mods/BKVince/d2rloader/config",
        L"C:/D2R/d2rloader/config",
        L"ruffneckk-resistance-floor.toml");
    CHECK(candidates.size() == 2);
    CHECK(candidates[0].generic_wstring().find(L"mods/BKVince")
        != std::wstring::npos);
    CHECK(candidates[1].generic_wstring().find(L"d2rloader/config")
        != std::wstring::npos);

    CHECK(CanEncodeRel32(0x1404524C4ULL, 0x140500000ULL));
    CHECK(CanEncodeRel32(0x1404524C4ULL, 0x13F000000ULL));
    CHECK(!CanEncodeRel32(0x1404524C4ULL, 0x240500000ULL));
}

void TestSourceContracts() {
    const auto plugin = ReadFile(RESISTANCE_FLOOR_PLUGIN_SOURCE);
    const auto relay = ReadFile(RESISTANCE_FLOOR_RELAY_SOURCE);
    const auto toml = ReadFile(RESISTANCE_FLOOR_TOML_SOURCE);
    CHECK(plugin.find(".author = \"RuffnecKk\"") != std::string::npos);
    CHECK(plugin.find("ModScopedOnly") == std::string::npos);
    CHECK(plugin.find("0x4524C4") != std::string::npos);
    CHECK(plugin.find("0x4524E7") != std::string::npos);
    CHECK(plugin.find("0x14E729A") != std::string::npos);
    CHECK(plugin.find("RegisterConsoleCommand") != std::string::npos);
    CHECK(plugin.find("MapSense") == std::string::npos);
    CHECK(plugin.find("OverlayHost") == std::string::npos);
    CHECK(plugin.find("ImGui") == std::string::npos);
    CHECK(plugin.find("show_physical_and_magic") == std::string::npos);
    CHECK(relay.find("movdqu xmmword ptr [rsp+50h], xmm0") != std::string::npos);
    CHECK(relay.find("mov rcx, qword ptr [rsi+10h]") != std::string::npos);
    CHECK(relay.find("mov edx, dword ptr [r14+8h]") != std::string::npos);
    CHECK(toml.find("[companions]") != std::string::npos);
    CHECK(toml.find("minimum_resistance = -1000") != std::string::npos);
    CHECK(toml.find("show_resistances_below_minus_100 = true")
        != std::string::npos);
    CHECK(toml.find("config_version = 3") != std::string::npos);
    CHECK(toml.find("show_usage_counters = false") != std::string::npos);
    CHECK(toml.find("[targets.") == std::string::npos);
    CHECK(toml.find("floor =") == std::string::npos);
    CHECK(toml.find("show_physical_and_magic") == std::string::npos);
    CHECK(toml.find("position_from_") == std::string::npos);
}

} // namespace

int main() {
    TestParsing();
    TestPolicy();
    TestCandidatesAndRel32();
    TestSourceContracts();
    return Failures == 0 ? 0 : 1;
}
