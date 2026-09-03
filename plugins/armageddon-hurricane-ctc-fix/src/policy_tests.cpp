#include "armageddon_ctc_policy.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ruffneckk::armageddon_ctc;

int main() {
    static_assert(IsSupportedSkill(ArmageddonSkillId));
    static_assert(IsSupportedSkill(HurricaneSkillId));
    static_assert(!IsSupportedSkill(248));

    constexpr Config defaults{};
    static_assert(IsSkillEnabled(defaults, ArmageddonSkillId));
    static_assert(IsSkillEnabled(defaults, HurricaneSkillId));
    static_assert(!IsSkillEnabled(defaults, 60));

    static_assert(ShouldEraseExpiredSeedBeforeActive(false));
    static_assert(!ShouldEraseExpiredSeedBeforeActive(true));
    static_assert(ShouldEraseExpiredSeedAfterActive(0, false));
    static_assert(!ShouldEraseExpiredSeedAfterActive(0, true));
    static_assert(!ShouldEraseExpiredSeedAfterActive(1, false));

    const std::array<std::uint8_t, 5> expectedFingerprint{
        0x48, 0x89, 0x5C, 0x24, 0x10};
    auto matchingFingerprint = expectedFingerprint;
    auto changedFingerprint = expectedFingerprint;
    changedFingerprint[4] ^= 0x01;
    assert(MatchesFingerprint(
        matchingFingerprint.data(),
        expectedFingerprint));
    assert(!MatchesFingerprint(
        changedFingerprint.data(),
        expectedFingerprint));
    assert(!MatchesFingerprint(nullptr, expectedFingerprint));

    Config config{};
    std::string error;
    assert(ParseToml(R"toml(
enabled = true
[skills]
armageddon = false
hurricane = true
[diagnostics]
enabled = true
)toml", config, error));
    assert(config.enabled);
    assert(!config.armageddon);
    assert(config.hurricane);
    assert(config.diagnostics);
    assert(!IsSkillEnabled(config, ArmageddonSkillId));
    assert(IsSkillEnabled(config, HurricaneSkillId));

    assert(!ParseToml("unknown = true", config, error));
    assert(!ParseToml("[skills]\narmageddon = 1", config, error));
    assert(!ParseToml("[skills]\nunknown = true", config, error));
    assert(!ParseToml("[diagnostics]\nenabled = 1", config, error));

    std::ifstream packagedConfig(
        ARMAGEDDON_CTC_CONFIG_FILE,
        std::ios::binary);
    assert(packagedConfig.is_open());
    const std::string packagedText{
        std::istreambuf_iterator<char>(packagedConfig),
        std::istreambuf_iterator<char>()};
    assert(ParseToml(packagedText, config, error));
    assert(config.enabled);
    assert(config.armageddon);
    assert(config.hurricane);
    assert(!config.diagnostics);

    const std::vector<std::filesystem::path> directories{
        L"C:/game/mods/example/d2rloader/config",
        L"C:/game/d2rloader/config",
        L"C:/game/d2rloader/config",
    };
    const auto candidates = BuildConfigCandidates(
        directories,
        L"ruffneckk-armageddon-hurricane-ctc-fix.toml");
    assert(candidates.size() == 2);
    assert(candidates.front().parent_path()
        == L"C:/game/mods/example/d2rloader/config");

    std::ifstream pluginSource(
        ARMAGEDDON_CTC_PLUGIN_FILE,
        std::ios::binary);
    assert(pluginSource.is_open());
    const std::string source{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    assert(source.find("ModScopedOnly") == std::string::npos);
    assert(source.find("IsSupportedBuild") == std::string::npos);
    assert(source.find("only D2R build") == std::string::npos);
    assert(source.find("D2RL::GetBuildName") != std::string::npos);
    assert(source.find("native fingerprint accepted") != std::string::npos);
    assert(source.find("D2RL::PluginFlags::Server") != std::string::npos);
    assert(source.find("0x589930") != std::string::npos);
    assert(source.find("0x575DE0") != std::string::npos);
    assert(source.find("0x574E90") != std::string::npos);
    assert(source.find("0x3351B0") != std::string::npos);
    assert(source.find("SkillsItemEffectOffset = 0x20A")
        != std::string::npos);
    assert(source.find("SkillsAuraStateOffset = 0xA0")
        != std::string::npos);
    assert(source.find("SkillListUsedOffset = 0x18")
        != std::string::npos);
    assert(source.find("SyntheticSkillSize = 0x60")
        != std::string::npos);
    assert(source.find("expired seed cleanups=%llu")
        != std::string::npos);
}
