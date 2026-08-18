#include "native_contract.hpp"
#include "policy.hpp"

#include <D2RLPlugin/context.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}

template<class Callback>
auto Throws(Callback&& callback) -> bool {
    try {
        callback();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void WriteFile(
    const std::filesystem::path& path,
    std::string_view contents
) {
    std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) throw std::runtime_error("test file write failed");
}

} // namespace

#define REQUIRE(value) \
    do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main(int argc, char** argv) {
    using namespace RuffnecKk::LarzukSockets;

    static_assert(QualityIndex(4) == 0);
    static_assert(QualityIndex(6) == 1);
    static_assert(QualityIndex(5) == 2);
    static_assert(QualityIndex(7) == 3);
    static_assert(QualityIndex(8) == 4);
    static_assert(!QualityIndex(2));
    static_assert(IsValidRule({1, 1}));
    static_assert(IsValidRule({2, 6}));
    static_assert(!IsValidRule({0, 1}));
    static_assert(!IsValidRule({3, 2}));
    static_assert(!IsValidRule({1, 7}));
    static_assert(EffectiveLegalMaximum(6, 1, 1) == 1);
    static_assert(EffectiveLegalMaximum(6, 1, 2) == 2);
    static_assert(EffectiveLegalMaximum(4, 2, 3) == 4);
    static_assert(EffectiveLegalMaximum(6, 0, 2) == 0);
    static_assert(ResolveSockets({2, 2}, 6, 0) == 2);
    static_assert(ResolveSockets({4, 6}, 2, 123) == 2);
    static_assert(ResolveSockets({1, 3}, 6, 0) == 1);
    static_assert(ResolveSockets({1, 3}, 6, 1) == 2);
    static_assert(ResolveSockets({1, 3}, 6, 2) == 3);
    static_assert(ResolveSockets({1, 4}, 6, 7) == 4);
    static_assert(NativeContract::AddSocketsRva == 0x375560);
    static_assert(NativeContract::LarzukReturnRva == 0x4FD580);
    static_assert(NativeContract::UnitClassIdOffset == 0x04);
    static_assert(NativeContract::Helpers.size() == 8);

    REQUIRE(argc == 2);
    const std::filesystem::path shippedPath(argv[1]);
    const auto shipped = LoadConfigFromCandidates({shippedPath});
    REQUIRE(shipped.source == shippedPath);
    REQUIRE(shipped.config.enabled);
    REQUIRE(!shipped.config.diagnostics);
    REQUIRE(HasRules(shipped.config.rules));
    for (std::size_t difficulty = 0;
         difficulty < DifficultyCount;
         ++difficulty) {
        const auto* magic = FindRule(
            shipped.config.rules,
            static_cast<std::uint8_t>(difficulty),
            4);
        REQUIRE(magic != nullptr && magic->has_value());
        REQUIRE((*magic)->minSockets == 1);
        REQUIRE((*magic)->maxSockets == 2);
        for (const auto quality : {6, 5, 7, 8}) {
            const auto* rule = FindRule(
                shipped.config.rules,
                static_cast<std::uint8_t>(difficulty),
                quality);
            REQUIRE(rule != nullptr && rule->has_value());
            REQUIRE((*rule)->minSockets == 1);
            REQUIRE((*rule)->maxSockets == 1);
        }
    }

    const auto vanilla = ParseConfig(ParseJson(R"json({
        "normal":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
        "nightmare":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
        "hell":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
        "diagnostics":false
    })json"));
    REQUIRE(!HasRules(vanilla.rules));

    REQUIRE(Throws([] {
        (void)ParseConfig(ParseJson(R"json({"enabled":1})json"));
    }));
    const auto enabled = ParseConfig(ParseJson(R"json({
        "enabled":true,
        "normal":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
        "nightmare":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
        "hell":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
        "diagnostics":false
    })json"));
    REQUIRE(enabled.enabled);
    REQUIRE(Throws([] {
        (void)ParseJson(R"json({"normal":{},"normal":{}})json");
    }));
    REQUIRE(Throws([] {
        (void)ParseConfig(ParseJson(R"json({
            "normal":{"magic":null,"rare":null,"set":null,"unique":null},
            "nightmare":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
            "hell":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
            "diagnostics":false
        })json"));
    }));
    REQUIRE(Throws([] {
        (void)ParseConfig(ParseJson(R"json({
            "normal":{"magic":{"minSockets":0,"maxSockets":2},"rare":null,"set":null,"unique":null,"crafted":null},
            "nightmare":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
            "hell":{"magic":null,"rare":null,"set":null,"unique":null,"crafted":null},
            "diagnostics":false
        })json"));
    }));

    const auto globalCandidates = BuildConfigCandidates(
        {},
        L"C:/D2R/d2rloader/config",
        L"C:/D2R/d2rloader/config");
    REQUIRE(globalCandidates.size() == 1);
    const auto modCandidates = BuildConfigCandidates(
        L"C:/D2R/mods/Test/d2rloader/config",
        L"C:/D2R/d2rloader/config",
        L"C:/D2R/d2rloader/config");
    REQUIRE(modCandidates.size() == 2);

    D2RL::PluginContext context{};
    context.contextSize = D2RL::PluginContextSize;
    context.loadScope = D2RL::LoadScope::Mod;
    context.activeMod = "Test";
    context.scopeRootDirectory = L"C:\\D2R\\mods\\Test";
    context.pluginConfigPath =
        L"C:\\D2R\\mods\\Test\\d2rloader\\config\\ruffneckk-larzuk-sockets.toml";
    context.modSupportDirectory =
        L"C:\\D2R\\mods\\Test\\d2rloader";
    const auto resolved = ResolveConfigCandidates(&context);
    REQUIRE(resolved.size() == 2);
    REQUIRE(resolved[0] == std::filesystem::path(
        L"C:\\D2R\\mods\\Test\\d2rloader\\config\\ForceLarzukSockets.json"));
    REQUIRE(resolved[1] == std::filesystem::path(
        L"C:\\D2R\\d2rloader\\config\\ForceLarzukSockets.json"));

    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("larzuk-sockets-tests-" + std::to_string(unique));
    const auto modDirectory = root / "mod";
    const auto globalDirectory = root / "global";
    std::filesystem::create_directories(modDirectory);
    std::filesystem::create_directories(globalDirectory);
    const auto modConfig = modDirectory / ConfigFileName;
    const auto globalConfig = globalDirectory / ConfigFileName;

    std::ifstream shippedInput(shippedPath, std::ios::binary);
    const std::string shippedText{
        std::istreambuf_iterator<char>(shippedInput),
        std::istreambuf_iterator<char>{}};
    WriteFile(globalConfig, shippedText);
    WriteFile(modConfig, "{ invalid json");
    REQUIRE(Throws([&] {
        (void)LoadConfigFromCandidates({modConfig, globalConfig});
    }));
    std::filesystem::remove(modConfig);
    REQUIRE(LoadConfigFromCandidates({modConfig, globalConfig}).source
        == globalConfig);
    std::filesystem::remove(globalConfig);
    REQUIRE(Throws([&] {
        (void)LoadConfigFromCandidates({modConfig, globalConfig});
    }));
    WriteFile(
        globalConfig,
        std::string(
            static_cast<std::size_t>(MaximumConfigBytes + 1),
            ' '));
    REQUIRE(Throws([&] {
        (void)LoadConfigFromCandidates({globalConfig});
    }));

    std::filesystem::remove_all(root);
    return 0;
}
