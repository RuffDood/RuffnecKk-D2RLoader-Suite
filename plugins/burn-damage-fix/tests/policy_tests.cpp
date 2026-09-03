#include "burn_damage_fix_policy.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {
int Failures{};

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; \
            ++Failures; \
        } \
    } while (false)

} // namespace

int main() {
    using namespace ruffneckk::burn_damage_fix;

    const auto pluginSourcePath =
        std::filesystem::path{BURN_DAMAGE_FIX_PLUGIN_SOURCE_PATH};
    std::ifstream pluginSourceStream(pluginSourcePath, std::ios::binary);
    const std::string pluginSource{
        std::istreambuf_iterator<char>{pluginSourceStream},
        std::istreambuf_iterator<char>{}};
    CHECK(pluginSourceStream.good() || pluginSourceStream.eof());
    CHECK(!pluginSource.empty());
    CHECK(pluginSource.find("D2RL::GetBuildName(context)")
        != std::string::npos);
    CHECK(pluginSource.find("observed D2R build-name")
        != std::string::npos);
    CHECK(pluginSource.find("92777") == std::string::npos);
    CHECK(pluginSource.find("93847") == std::string::npos);
    CHECK(pluginSource.find("IsSupportedBuild") == std::string::npos);
    CHECK(pluginSource.find("Unsupported D2R build") == std::string::npos);
    CHECK(pluginSource.find("RuntimeBuild ==") == std::string::npos);
    CHECK(pluginSource.find("RuntimeBuild !=") == std::string::npos);

    static_assert(PreservePositiveResistanceAndImmunity == 0);
    static_assert(UnitDoOverlayStat == 178);
    static_assert(DefaultOverlayRepeatFrames == 10);
    static_assert(NativeBurningOverlay == 224);
    static_assert(EmptyStateOverlay == 0xFFFF);
    static_assert(Config::normalizeGenericBurn);
    static_assert(Config::applyFireResistance);
    static_assert(Config::replayFireHit);
    static_assert(Config::suppressNativeBurning);
    static_assert(Config::overlayRepeatFrames == 10);

    CHECK(BurnFireResistance.resistanceStat == 39);
    CHECK(BurnFireResistance.maximumResistanceStat == 40);
    CHECK(BurnFireResistance.pierceStat == 333);
    CHECK(BurnFireResistance.immunityPierceStat == 189);
    CHECK(BurnFireResistance.absorbPercentStat == -1);
    CHECK(BurnFireResistance.absorbFlatStat == -1);
    CHECK(BurnFireResistance.damageReductionIndex == 2);
    CHECK(BurnFireResistance.attackerGate == 0);
    CHECK(BurnFireResistance.flags28 == 1);
    CHECK(BurnFireResistance.reserved2C == 0);
    CHECK(BurnFireResistance.logFlag == 8);

    Config config{};
    CHECK(ShouldResolveBurn(config, 256, 75));
    CHECK(!ShouldResolveBurn(config, 0, 75));
    CHECK(!ShouldResolveBurn(config, 256, 0));
    config.enabled = false;
    CHECK(!ShouldResolveBurn(config, 256, 75));

    config = {};
    CHECK(!ShouldWitnessBurningState(config, 256, 75));
    config.diagnostics = true;
    CHECK(ShouldWitnessBurningState(config, 256, 75));
    CHECK(!ShouldWitnessBurningState(config, 0, 75));
    CHECK(!ShouldWitnessBurningState(config, 256, 0));
    config.enabled = false;
    CHECK(!ShouldWitnessBurningState(config, 256, 75));

    config = {};
    CHECK(ShouldReplayFireHit(config, StatRegenerationEvent, 0));
    CHECK(ShouldReplayFireHit(config, StatRegenerationEvent, 10));
    CHECK(!ShouldReplayFireHit(config, StatRegenerationEvent, 9));
    CHECK(!ShouldReplayFireHit(config, 2, 10));
    config.enabled = false;
    CHECK(!ShouldReplayFireHit(config, StatRegenerationEvent, 10));

    config = {};
    CHECK(ShouldSuppressNativeBurning(config));
    config.enabled = false;
    CHECK(!ShouldSuppressNativeBurning(config));
    CHECK(ClassifyNativeBurningOverlay(NativeBurningOverlay)
        == NativeBurningOverlayAction::Suppress);
    CHECK(ClassifyNativeBurningOverlay(EmptyStateOverlay)
        == NativeBurningOverlayAction::AlreadySuppressed);
    CHECK(ClassifyNativeBurningOverlay(FireHitOverlay)
        == NativeBurningOverlayAction::PreserveCustom);

    CHECK(AcceptResistanceResolver(
        ResistanceResolverStatus::Unchanged, true, true, 0, {}));
    CHECK(!AcceptResistanceResolver(
        ResistanceResolverStatus::Unchanged, true, false, 0, {}));
    CHECK(!AcceptResistanceResolver(
        ResistanceResolverStatus::Unchanged, false, true, 0, {}));
    CHECK(AcceptResistanceResolver(
        ResistanceResolverStatus::TrackedInlineHook,
        false, true, 1, "monsterdisplay"));
    CHECK(!AcceptResistanceResolver(
        ResistanceResolverStatus::TrackedInlineHook,
        false, true, 1, "MonsterDisplay"));
    CHECK(!AcceptResistanceResolver(
        ResistanceResolverStatus::TrackedInlineHook,
        false, true, 2, "monsterdisplay"));
    CHECK(!AcceptResistanceResolver(
        ResistanceResolverStatus::TrackedInlineHook,
        false, false, 1, "monsterdisplay"));
    CHECK(!AcceptResistanceResolver(
        ResistanceResolverStatus::Other,
        true, true, 0, {}));

    CHECK(NormalizeGenericNumerator(256, 0, 0, 0, 123) == 256);
    CHECK(NormalizeGenericNumerator(0, 128, 128, 0, 123) == 128);
    CHECK(NormalizeGenericNumerator(0, 128, 256, 0, 0) == 128);
    CHECK(NormalizeGenericNumerator(0, 128, 256, 0, 127) == 255);
    CHECK(NormalizeGenericNumerator(64, 128, 256, 100, 0) == 320);
    CHECK(NormalizeGenericNumerator(0, 256, 128, 0, 127) == 255);
    CHECK(NormalizeGenericNumerator(-1, 0, 0, 0, 0) == 0);
    CHECK(NormalizeGenericNumerator(
        std::numeric_limits<std::int32_t>::max(), 128, 128, 0, 0)
        == std::numeric_limits<std::int32_t>::max());

    constexpr std::string_view validToml = R"toml(
config_version = 1
enabled = true
[diagnostics]
enabled = false
)toml";
    std::string error;
    CHECK(ParseToml(validToml, config, error));
    CHECK(config.enabled && !config.diagnostics);
    constexpr std::string_view disabledToml = R"toml(
config_version = 1
enabled = false
[diagnostics]
enabled = true
)toml";
    CHECK(ParseToml(disabledToml, config, error));
    CHECK(!config.enabled && config.diagnostics);
    auto unknownTopLevel = std::string(validToml);
    unknownTopLevel.insert(
        unknownTopLevel.find("[diagnostics]"), "unknown = true\n");
    CHECK(!ParseToml(unknownTopLevel, config, error));
    CHECK(!ParseToml(
        "config_version = 1\nenabled = 1\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseToml(
        "config_version = 1\nenabled = true\n"
        "[diagnostics]\nenabled = 0\n",
        config,
        error));
    CHECK(!ParseToml(
        "config_version = 2\nenabled = true\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseToml(
        "config_version = 1\nenabled = true\n"
        "[fixes]\nnormalize_generic_burn = true\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseToml(
        "config_version = 1\nenabled = true\n"
        "[overlay]\nenabled = true\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));

    const auto candidates = BuildConfigCandidates(
        std::filesystem::path{L"mod"},
        std::filesystem::path{L"scope"},
        std::filesystem::path{L"global"},
        std::filesystem::path{L"ruffneckk-burn-damage-fix.toml"});
    CHECK(candidates.size() == 3);
    CHECK(candidates.front()
        == std::filesystem::path{L"mod/ruffneckk-burn-damage-fix.toml"});
    const auto deduplicated = BuildConfigCandidates(
        std::filesystem::path{L"same"},
        std::filesystem::path{L"same"},
        std::filesystem::path{L"global"},
        std::filesystem::path{L"ruffneckk-burn-damage-fix.toml"});
    CHECK(deduplicated.size() == 2);

    CHECK(CanEncodeRel32(0x14044CB32ULL, 0x140500000ULL));
    CHECK(!CanEncodeRel32(0x14044CB32ULL, 0x240500000ULL));

    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
