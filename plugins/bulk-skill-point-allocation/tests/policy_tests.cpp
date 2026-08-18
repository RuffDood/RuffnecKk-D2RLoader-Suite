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
    using namespace RuffnecKk::BulkSkillPointAllocation;

    static_assert(ResolveMode(false, false) == AllocationMode::Single);
    static_assert(ResolveMode(false, true) == AllocationMode::CtrlBatch);
    static_assert(ResolveMode(true, false) == AllocationMode::ShiftAll);
    static_assert(ResolveMode(true, true) == AllocationMode::CtrlBatch);
    static_assert(NativeSkillPacketExtra(AllocationMode::Single, 1) == 0);
    static_assert(NativeSkillPacketExtra(AllocationMode::CtrlBatch, 5) == 4);
    static_assert(NativeSkillPacketExtra(
        AllocationMode::CtrlBatch, 1'000) == 999);
    static_assert(NativeSkillPacketExtra(
        AllocationMode::ShiftAll, 1) == 0xFFFF);
    static_assert(!RequiresPeerPlugin);
    static_assert(NativeContract::OwnedHookSites.size() == 2);
    static_assert(NativeContract::OwnedHookSites[0].rva == 0x000EC700);
    static_assert(NativeContract::OwnedHookSites[0].preflightSize == 29);
    static_assert(!NativeContract::OwnedHookSites[0].conditional);
    static_assert(NativeContract::OwnedHookSites[1].rva == 0x005F4B90);
    static_assert(NativeContract::OwnedHookSites[1].preflightSize == 29);
    static_assert(NativeContract::OwnedHookSites[1].conditional);
    static_assert(NativeContract::ExternalProtectedSites.size() == 21);
    static_assert(NativeContract::RemoteStashProtectedSites.size() == 30);
    static_assert(NativeContract::HasNoOverlap(
        NativeContract::ExternalProtectedSites));
    static_assert(NativeContract::HasNoOverlap(
        NativeContract::RemoteStashProtectedSites));

    REQUIRE(argc == 3);
    const std::filesystem::path gameplayPath(argv[1]);
    const std::filesystem::path stringsPath(argv[2]);
    const auto shipped = LoadSettingsFromCandidates(
        {gameplayPath}, {stringsPath});
    REQUIRE(shipped.gameplaySource == gameplayPath);
    REQUIRE(shipped.stringsSource == stringsPath);
    REQUIRE(shipped.settings.enabled);
    REQUIRE(shipped.settings.skillPointsPerCtrlClick == 5);
    REQUIRE(!shipped.settings.confirmShiftAllocation);
    REQUIRE(!shipped.settings.diagnostics);
    REQUIRE(shipped.settings.shiftConfirmationKey == "shiftConfirmation");
    REQUIRE(shipped.settings.shiftConfirmationFallback
        == "Invest all currently usable skill points in this skill?");

    Settings custom;
    ApplyGameplayConfig(Json::ParseObject(R"json({
        /* strict JSONC is accepted */
        "enabled": true,
        "skillPointsPerCtrlClick": 25,
        "confirmShiftAllocation": true,
        "diagnostics": true
    })json"), custom);
    ApplyStringsConfig(Json::ParseObject(R"json({
        "shiftConfirmationKey": "custom\u004bey",
        "shiftConfirmationFallback": "Tout investir? \uD83D\uDC4D"
    })json"), custom);
    REQUIRE(custom.enabled);
    REQUIRE(custom.skillPointsPerCtrlClick == 25);
    REQUIRE(custom.confirmShiftAllocation);
    REQUIRE(custom.diagnostics);
    REQUIRE(custom.shiftConfirmationKey == "customKey");
    REQUIRE(custom.shiftConfirmationFallback == "Tout investir? 👍");

    REQUIRE(IsUsableLocalizedString(
        "Invest all points?", "shiftConfirmation", "Missing string"));
    REQUIRE(!IsUsableLocalizedString(
        "", "shiftConfirmation", "Missing string"));
    REQUIRE(!IsUsableLocalizedString(
        "shiftConfirmation", "shiftConfirmation", "Missing string"));
    REQUIRE(!IsUsableLocalizedString(
        "Missing string", "shiftConfirmation", "Missing string"));
    REQUIRE(IsShiftConfirmationAcceptEvent(
        "CharacterStatsPanelMessage", "UseSkillPoint"));
    REQUIRE(!IsShiftConfirmationAcceptEvent(
        "CharacterStatsPanelMessage", "Cancel"));
    REQUIRE(!IsShiftConfirmationAcceptEvent(nullptr, "UseSkillPoint"));

    REQUIRE(Throws([] {
        (void)Json::ParseObject("{\"a\":1,\"a\":2}");
    }));
    REQUIRE(Throws([] {
        (void)Json::ParseObject("{\"a\":1,}");
    }));
    REQUIRE(Throws([] {
        (void)Json::ParseObject("{\"a\":1.5}");
    }));
    REQUIRE(Throws([] {
        (void)Json::ParseObject("{\"a\":01}");
    }));
    REQUIRE(Throws([] {
        (void)Json::ParseObject("{\"a\":\"\\uD800\"}");
    }));
    REQUIRE(Throws([] {
        Settings settings;
        ApplyGameplayConfig(
            Json::ParseObject("{\"skillPointsPerCtrlClick\":0}"),
            settings);
    }));
    REQUIRE(Throws([] {
        Settings settings;
        ApplyGameplayConfig(
            Json::ParseObject("{\"skillPointsPerCtrlClick\":1001}"),
            settings);
    }));
    REQUIRE(Throws([] {
        Settings settings;
        ApplyGameplayConfig(
            Json::ParseObject("{\"diagnostics\":1}"),
            settings);
    }));
    REQUIRE(Throws([] {
        Settings settings;
        ApplyGameplayConfig(
            Json::ParseObject("{\"enabled\":1}"),
            settings);
    }));
    REQUIRE(Throws([] {
        Settings settings;
        ApplyStringsConfig(
            Json::ParseObject("{\"shiftConfirmationKey\":\"\"}"),
            settings);
    }));

    const auto globalOnly = BuildConfigCandidates(
        {},
        L"C:/D2R/d2rloader/config",
        L"C:/D2R/d2rloader/config",
        GameplayConfigFileName);
    REQUIRE(globalOnly.size() == 1);
    const auto scoped = BuildConfigCandidates(
        L"C:/D2R/mods/Test/d2rloader/config",
        L"C:/D2R/mods/Test/d2rloader/config",
        L"C:/D2R/d2rloader/config",
        GameplayConfigFileName);
    REQUIRE(scoped.size() == 2);

    D2RL::PluginContext context{};
    context.contextSize = D2RL::PluginContextSize;
    context.loadScope = D2RL::LoadScope::Mod;
    context.activeMod = "Test";
    context.scopeRootDirectory = L"C:\\D2R\\mods\\Test";
    context.pluginConfigPath =
        L"C:\\D2R\\mods\\Test\\d2rloader\\config\\ruffneckk-bulk-skill-point-allocation.toml";
    context.modSupportDirectory =
        L"C:\\D2R\\mods\\Test\\d2rloader";
    const auto resolved = ResolveConfigCandidates(
        &context, GameplayConfigFileName);
    REQUIRE(resolved.size() == 2);
    REQUIRE(resolved[0] == std::filesystem::path(
        L"C:\\D2R\\mods\\Test\\d2rloader\\config\\BulkSkillPointAllocation.json"));
    REQUIRE(resolved[1] == std::filesystem::path(
        L"C:\\D2R\\d2rloader\\config\\BulkSkillPointAllocation.json"));

    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("bulk-skill-tests-" + std::to_string(unique));
    const auto modDirectory = root / "mod";
    const auto globalDirectory = root / "global";
    std::filesystem::create_directories(modDirectory);
    std::filesystem::create_directories(globalDirectory);
    const auto modGameplay = modDirectory / GameplayConfigFileName;
    const auto globalGameplay = globalDirectory / GameplayConfigFileName;
    const auto modStrings = modDirectory / StringsConfigFileName;
    const auto globalStrings = globalDirectory / StringsConfigFileName;

    WriteFile(globalGameplay, "{\"skillPointsPerCtrlClick\":10}");
    WriteFile(globalStrings, "{\"shiftConfirmationKey\":\"global\"}");
    WriteFile(modGameplay, "{ invalid json");
    REQUIRE(Throws([&] {
        (void)LoadSettingsFromCandidates(
            {modGameplay, globalGameplay},
            {modStrings, globalStrings});
    }));
    std::filesystem::remove(modGameplay);
    auto priority = LoadSettingsFromCandidates(
        {modGameplay, globalGameplay},
        {modStrings, globalStrings});
    REQUIRE(priority.settings.skillPointsPerCtrlClick == 10);
    REQUIRE(priority.settings.shiftConfirmationKey == "global");

    WriteFile(modStrings, "{ invalid json");
    REQUIRE(Throws([&] {
        (void)LoadSettingsFromCandidates(
            {modGameplay, globalGameplay},
            {modStrings, globalStrings});
    }));
    std::filesystem::remove(modStrings);
    std::filesystem::remove(globalGameplay);
    std::filesystem::remove(globalStrings);
    const auto defaults = LoadSettingsFromCandidates(
        {modGameplay, globalGameplay},
        {modStrings, globalStrings});
    REQUIRE(!defaults.gameplaySource);
    REQUIRE(!defaults.stringsSource);
    REQUIRE(defaults.settings.skillPointsPerCtrlClick == 5);
    REQUIRE(defaults.settings.shiftConfirmationKey == "shiftConfirmation");

    WriteFile(
        globalGameplay,
        std::string(
            static_cast<std::size_t>(MaximumConfigBytes + 1),
            ' '));
    REQUIRE(Throws([&] {
        (void)LoadSettingsFromCandidates(
            {globalGameplay}, {globalStrings});
    }));

    std::filesystem::remove_all(root);
    return 0;
}
