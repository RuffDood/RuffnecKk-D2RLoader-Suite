#include "cast_triggers_policy.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ruffneckk::cast_triggers;

int main() {
    static_assert(IsCastAnimation(PlayerModeCast, PlayerModeCast));
    static_assert(IsCastAnimation(PlayerModeSequence, PlayerModeCast));
    static_assert(!IsCastAnimation(PlayerModeSequence, PlayerModeSequence));
    static_assert(IsEligibleSkillRecord(
        PlayerModeCast,
        PlayerModeCast,
        0));
    static_assert(!IsEligibleSkillRecord(
        PlayerModeCast,
        PlayerModeCast,
        SkillFlagRepeat));
    static_assert(IsEligibleChannelingSkillRecord(
        PlayerModeCast,
        PlayerModeCast,
        SkillFlagRepeat));
    static_assert(IsEligibleChannelingSkillRecord(
        PlayerModeSequence,
        PlayerModeSequence,
        SkillFlagRepeat));
    static_assert(!IsEligibleChannelingSkillRecord(
        PlayerModeCast,
        PlayerModeCast,
        0));
    static_assert(ClassifySourceSkillRecord(
        PlayerModeCast,
        PlayerModeCast,
        0) == SourceTriggerKind::OnCast);
    static_assert(ClassifySourceSkillRecord(
        PlayerModeCast,
        PlayerModeCast,
        SkillFlagRepeat) == SourceTriggerKind::WhileChanneling);
    static_assert(ClassifySourceSkillRecord(
        PlayerModeSequence,
        PlayerModeSequence,
        SkillFlagRepeat) == SourceTriggerKind::WhileChanneling);
    static_assert(ClassifySourceSkillRecord(
        PlayerModeSequence,
        PlayerModeSequence,
        0) == SourceTriggerKind::None);
    static_assert(ClassifySourceSkillRecord(
        7,
        7,
        SkillFlagRepeat) == SourceTriggerKind::None);
    static_assert(!IsEligibleSkillRecord(7, 7, 0));
    static_assert(DefaultChannelingIntervalFrames == 50);
    static_assert(HasChannelingIntervalElapsed(150, 100, 50));
    static_assert(!HasChannelingIntervalElapsed(149, 100, 50));
    static_assert(HasChannelingIntervalElapsed(
        10,
        0xFFFFFFF0U,
        26));
    static_assert(!HasChannelingIntervalElapsed(150, 100, 0));
    static_assert(IsPendingInputFresh(250, 0, false));
    static_assert(!IsPendingInputFresh(251, 0, false));
    static_assert(IsPendingInputFresh(251, 0, true));
    static_assert(IsPendingInputFresh(24, 0xFFFFFFF0U, false));
    static_assert(IsManualPlayerCast(1, 1, 0, 0, PlayerUnitType));
    static_assert(!IsManualPlayerCast(0, 1, 0, 0, PlayerUnitType));
    static_assert(!IsManualPlayerCast(1, 0, 1, 0, PlayerUnitType));
    static_assert(!IsManualPlayerCast(1, 1, 0, 0, 1));
    static_assert(SameLevelMarker == 63);
    static_assert(SameLevelMarker > 0);
    static_assert(SameLevelMarker <= 63);
    static_assert(NativeEventEffectTargetFlag == 1);
    static_assert(NativeNormalSkillTargetFlag == 0);
    static_assert(ShouldResolveTriggeredSkillTarget(true, true, 1));
    static_assert(!ShouldResolveTriggeredSkillTarget(true, true, 0));
    static_assert(!ShouldResolveTriggeredSkillTarget(false, true, 1));
    static_assert(!ShouldResolveTriggeredSkillTarget(true, false, 1));
    static_assert(ShouldUseNativeUnitTarget(
        true,
        NativeSourceTargetKind::Unit));
    static_assert(!ShouldUseNativeUnitTarget(
        true,
        NativeSourceTargetKind::Position));
    static_assert(!ShouldUseNativeUnitTarget(
        false,
        NativeSourceTargetKind::Unit));
    static_assert(ShouldUseNativePositionTarget(
        true,
        NativeSourceTargetKind::Position));
    static_assert(!ShouldUseNativePositionTarget(
        true,
        NativeSourceTargetKind::None));
    static_assert(!ShouldUseNativePositionTarget(
        false,
        NativeSourceTargetKind::Position));
    static_assert(ShouldPreferPlayerInputTarget(
        true,
        NativeSourceTargetKind::Unit));
    static_assert(ShouldPreferPlayerInputTarget(
        true,
        NativeSourceTargetKind::Position));
    static_assert(!ShouldPreferPlayerInputTarget(
        true,
        NativeSourceTargetKind::None));
    static_assert(!ShouldPreferPlayerInputTarget(
        false,
        NativeSourceTargetKind::Unit));
    static_assert([] {
        constexpr std::array attackModes{
            PlayerModeAttack1,
            PlayerModeAttack2,
            PlayerModeThrow,
            PlayerModeKick,
            PlayerModeSpecial1,
            PlayerModeSpecial2,
            PlayerModeSpecial3,
            PlayerModeSpecial4,
        };
        for (const auto mode : attackModes) {
            if (!IsAttackAnimation(mode, 0)
                    || !IsAttackAnimation(PlayerModeSequence, mode)) {
                return false;
            }
        }
        return true;
    }());
    static_assert([] {
        constexpr std::array nonAttackModes{
            std::uint8_t{0}, // DT
            std::uint8_t{1}, // NU
            std::uint8_t{2}, // WL
            std::uint8_t{3}, // RN
            std::uint8_t{4}, // GH
            std::uint8_t{5}, // TN
            std::uint8_t{6}, // TW
            std::uint8_t{9}, // BL
            PlayerModeCast,
            std::uint8_t{17}, // DD
            PlayerModeSequence,
            std::uint8_t{19}, // KB
        };
        for (const auto mode : nonAttackModes) {
            if (IsAttackAnimation(mode, 0)
                    || IsAttackAnimation(PlayerModeSequence, mode)) {
                return false;
            }
        }
        return true;
    }());
    static_assert(IsAcceptedPlayerSkillInput(0));
    static_assert(!IsAcceptedPlayerSkillInput(1));
    static_assert(!IsAcceptedPlayerSkillInput(4));
    constexpr CombatTriggerConfig combatConfig{369, 370, 371, 372};
    static_assert(HasDistinctCombatStatIds(combatConfig));
    static_assert(CombatTriggerForStatId(combatConfig, 370)
        == CombatTriggerKind::CriticalStrike);
    static_assert(CombatTriggerForStatId(combatConfig, 371)
        == CombatTriggerKind::CrushingBlow);
    static_assert(CombatTriggerForStatId(combatConfig, 372)
        == CombatTriggerKind::OpenWounds);
    static_assert(CombatTriggerForStatId(combatConfig, 369)
        == CombatTriggerKind::AttackAttempt);
    Config triggerConfig{};
    triggerConfig.combatTriggers = combatConfig;
    triggerConfig.whileChanneling.stats = {373, 374};
    triggerConfig.sourceSkillTriggers = {
        {"frost_nova", {44}, {375, 376}},
        {"cold_spells", {39, 40, 44}, {377, 378}},
    };
    assert(HasDistinctSyntheticStatIds(triggerConfig));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::None,
        368));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::None,
        369));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::None,
        373));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::None,
        375));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::OnCast,
        44,
        CombatTriggerKind::None,
        368));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::OnCast,
        44,
        CombatTriggerKind::None,
        375));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::OnCast,
        44,
        CombatTriggerKind::None,
        378));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::OnCast,
        47,
        CombatTriggerKind::None,
        375));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::OnCast,
        44,
        CombatTriggerKind::None,
        373));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::OnCast,
        44,
        CombatTriggerKind::None,
        370));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::WhileChanneling,
        41,
        CombatTriggerKind::None,
        373));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::WhileChanneling,
        41,
        CombatTriggerKind::None,
        374));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::WhileChanneling,
        44,
        CombatTriggerKind::None,
        368));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::WhileChanneling,
        44,
        CombatTriggerKind::None,
        375));
    assert(ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::CriticalStrike,
        370));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::CriticalStrike,
        368));
    assert(!ShouldExposeSyntheticStat(
        triggerConfig,
        SourceTriggerKind::None,
        -1,
        CombatTriggerKind::CriticalStrike,
        373));
    static_assert(PackedEventStatId(370 << 16) == 370);
    static_assert([] {
        std::uint32_t low = 1;
        std::uint32_t high = 0;
        const bool result = RollNativePercent(low, high, 86);
        return result && low == 0x6AC690C5U && high == 0;
    }());
    static_assert([] {
        std::uint32_t low = 1;
        std::uint32_t high = 0;
        const bool result = RollNativePercent(low, high, 85);
        return !result && low == 0x6AC690C5U && high == 0;
    }());
    static_assert([] {
        std::uint32_t low = 1;
        std::uint32_t high = 2;
        const bool result = RollNativePercent(low, high, 0);
        return !result && low == 1 && high == 2;
    }());

    constexpr std::string_view validToml = R"toml(
enabled = true
[on_cast]
include_skill_ids = [48, 44, 48]
exclude_skill_ids = [41]
[while_channeling]
enabled = true
interval_frames = 50
include_skill_ids = [52, 52]
exclude_skill_ids = [53]
fixed_stat_id = 373
same_level_stat_id = 374
[[source_skill_triggers]]
name = "frost_nova"
source_skill_ids = [44, 44]
fixed_stat_id = 375
same_level_stat_id = 376
[[source_skill_triggers]]
name = "cold_spells"
source_skill_ids = [48, 44]
fixed_stat_id = 377
same_level_stat_id = 378
[combat_triggers]
attack_attempt_stat_id = 369
critical_strike_stat_id = 370
crushing_blow_stat_id = 371
open_wounds_stat_id = 372
[diagnostics]
enabled = true
)toml";
    Config config{};
    std::string error;
    assert(ParseToml(validToml, config, error));
    assert(config.enabled);
    assert(config.onCast.includeSkillIds
        == std::vector<std::int32_t>({44, 48}));
    assert(config.onCast.excludeSkillIds
        == std::vector<std::int32_t>({41}));
    assert(config.whileChanneling.enabled);
    assert(config.whileChanneling.intervalFrames == 50);
    assert(config.whileChanneling.includeSkillIds
        == std::vector<std::int32_t>({52}));
    assert(config.whileChanneling.excludeSkillIds
        == std::vector<std::int32_t>({53}));
    assert(config.whileChanneling.stats.fixedStatId == 373);
    assert(config.whileChanneling.stats.sameLevelStatId == 374);
    assert(config.sourceSkillTriggers.size() == 2);
    assert(config.sourceSkillTriggers[0].name == "frost_nova");
    assert(config.sourceSkillTriggers[0].sourceSkillIds
        == std::vector<std::int32_t>({44}));
    assert(config.sourceSkillTriggers[0].stats.fixedStatId == 375);
    assert(config.sourceSkillTriggers[0].stats.sameLevelStatId == 376);
    assert(config.sourceSkillTriggers[1].name == "cold_spells");
    assert(config.sourceSkillTriggers[1].sourceSkillIds
        == std::vector<std::int32_t>({44, 48}));
    assert(config.sourceSkillTriggers[1].stats.fixedStatId == 377);
    assert(config.sourceSkillTriggers[1].stats.sameLevelStatId == 378);
    assert(config.combatTriggers.attackAttemptStatId == 369);
    assert(config.combatTriggers.criticalStrikeStatId == 370);
    assert(config.combatTriggers.crushingBlowStatId == 371);
    assert(config.combatTriggers.openWoundsStatId == 372);
    assert(HasDistinctSyntheticStatIds(config));
    assert(config.diagnostics);
    assert(IsConfiguredSourceSkill(config, 44));
    assert(!IsConfiguredSourceSkill(config, 41));
    assert(!IsConfiguredSourceSkill(config, 47));
    assert(IsConfiguredChannelingSourceSkill(config, 52));
    assert(!IsConfiguredChannelingSourceSkill(config, 53));
    assert(!IsConfiguredChannelingSourceSkill(config, 51));

    assert(ParseToml(
        "[while_channeling]\nenabled = false\ninterval_frames = 25",
        config,
        error));
    assert(!config.whileChanneling.enabled);
    assert(config.whileChanneling.intervalFrames == 25);
    assert(!IsConfiguredChannelingSourceSkill(config, 52));

    assert(!ParseToml("unknown = true", config, error));
    assert(!ParseToml(
        "[on_cast]\ninclude_skill_ids = [65536]",
        config,
        error));
    assert(!ParseToml(
        "[on_cast]\nexclude_skill_ids = [\"Inferno\"]",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\ninterval_frames = 0",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\ninterval_frames = 65536",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\ninterval_frames = \"50\"",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\nenabled = 1",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\nunknown = true",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\nfixed_stat_id = 65536",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\nsame_level_stat_id = \"374\"",
        config,
        error));
    assert(!ParseToml(
        "source_skill_triggers = true",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\n"
        "source_skill_ids = [44]\nfixed_stat_id = 375",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"\"\n"
        "source_skill_ids = [44]\nfixed_stat_id = 375",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = 44\n"
        "source_skill_ids = [44]\nfixed_stat_id = 375",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"empty\"\n"
        "source_skill_ids = []\nfixed_stat_id = 375",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"missing_stats\"\n"
        "source_skill_ids = [44]",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"bad_skill\"\n"
        "source_skill_ids = [65536]\nfixed_stat_id = 375",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"unknown\"\n"
        "source_skill_ids = [44]\nfixed_stat_id = 375\nother = 1",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"duplicate\"\n"
        "source_skill_ids = [44]\nfixed_stat_id = 375\n"
        "[[source_skill_triggers]]\nname = \"duplicate\"\n"
        "source_skill_ids = [48]\nfixed_stat_id = 376",
        config,
        error));
    assert(!ParseToml(
        "[while_channeling]\nfixed_stat_id = 373\n"
        "[[source_skill_triggers]]\nname = \"collision\"\n"
        "source_skill_ids = [44]\nfixed_stat_id = 373",
        config,
        error));
    assert(!ParseToml(
        "[[source_skill_triggers]]\nname = \"pair_collision\"\n"
        "source_skill_ids = [44]\nfixed_stat_id = 375\n"
        "same_level_stat_id = 375",
        config,
        error));
    assert(!ParseToml(
        "[combat_triggers]\ncritical_strike_stat_id = 65536",
        config,
        error));
    assert(!ParseToml(
        "[combat_triggers]\ncritical_strike_stat_id = -1",
        config,
        error));
    assert(!ParseToml(
        "[combat_triggers]\ncritical_strike_stat_id = \"370\"",
        config,
        error));
    assert(!ParseToml(
        "[combat_triggers]\ncritical_strike_stat_id = 370\n"
        "crushing_blow_stat_id = 370",
        config,
        error));
    assert(!ParseToml(
        "[combat_triggers]\nunknown = 370",
        config,
        error));
    assert(!ParseToml(
        "[diagnostics]\nenabled = 1",
        config,
        error));

    std::ifstream packagedConfig(
        CAST_TRIGGERS_CONFIG_FILE,
        std::ios::binary);
    assert(packagedConfig.is_open());
    const std::string packagedText{
        std::istreambuf_iterator<char>(packagedConfig),
        std::istreambuf_iterator<char>()};
    assert(ParseToml(packagedText, config, error));
    assert(config.enabled);
    assert(config.onCast.includeSkillIds.empty());
    assert(config.onCast.excludeSkillIds.empty());
    assert(config.whileChanneling.enabled);
    assert(config.whileChanneling.intervalFrames == 50);
    assert(config.whileChanneling.includeSkillIds.empty());
    assert(config.whileChanneling.excludeSkillIds.empty());
    assert(config.whileChanneling.stats.fixedStatId == 0);
    assert(config.whileChanneling.stats.sameLevelStatId == 0);
    assert(!IsConfiguredChannelingSourceSkill(config, 41));
    assert(config.sourceSkillTriggers.empty());
    assert(config.combatTriggers.attackAttemptStatId == 0);
    assert(config.combatTriggers.criticalStrikeStatId == 0);
    assert(config.combatTriggers.crushingBlowStatId == 0);
    assert(config.combatTriggers.openWoundsStatId == 0);
    assert(!config.diagnostics);

    const std::vector<std::filesystem::path> directories{
        L"C:/game/mods/example/d2rloader/config",
        L"C:/game/d2rloader/config",
        L"C:/game/d2rloader/config",
    };
    const auto candidates = BuildConfigCandidates(
        directories,
        L"ruffneckk-cast-triggers.toml");
    assert(candidates.size() == 2);
    assert(candidates.front().filename()
        == L"ruffneckk-cast-triggers.toml");

    std::ifstream pluginSource(CAST_TRIGGERS_PLUGIN_FILE, std::ios::binary);
    assert(pluginSource.is_open());
    const std::string source{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    assert(source.find("ModScopedOnly") == std::string::npos);
    assert(source.find("D2RL::PluginFlags::Server") != std::string::npos);
    assert(source.find("ClassifySourceSkillRecord") != std::string::npos);
    assert(source.find("SameLevelMarker") != std::string::npos);
    assert(source.find("IsSupportedBuild") == std::string::npos);
    assert(source.find("only governed D2R build aliases")
        == std::string::npos);
    assert(source.find("ValidateNativeFingerprint") != std::string::npos);
    assert(source.find("SkillHandlerContextWitnessExpected")
        != std::string::npos);
    assert(source.find("GameFrameLayoutWitnessExpected")
        != std::string::npos);
    assert(source.find("SkillsRecordStrideWitnessExpected")
        != std::string::npos);
    assert(source.find("PathGetFirstPointXExpected")
        != std::string::npos);
    assert(source.find("PathGetFirstPointYExpected")
        != std::string::npos);
    assert(source.find("PlayerSkillPositionInputExpected")
        != std::string::npos);
    assert(source.find("PlayerSkillUnitInputExpected")
        != std::string::npos);
    assert(source.find("GetServerUnitExpected") != std::string::npos);
    assert(source.find("ActiveSkillLayoutWitnessExpected")
        != std::string::npos);
    assert(source.find("PlayerInputTargetEntry") != std::string::npos);
    assert(source.find("PlayerInputTargetScope") == std::string::npos);
    assert(source.find("CapturePlayerInputTarget") != std::string::npos);
    assert(source.find("ResolvePlayerInputTarget") != std::string::npos);
    assert(source.find("HookPlayerSkillPositionInput")
        != std::string::npos);
    assert(source.find("HookPlayerSkillUnitInput")
        != std::string::npos);
    assert(source.find("DispatchAttackAttemptFromInput")
        != std::string::npos);
    assert(source.find("SourceTargetObservationScope")
        != std::string::npos);
    assert(source.find("HookGetTargetUnit") != std::string::npos);
    assert(source.find("HookPathGetFirstPointX") != std::string::npos);
    assert(source.find("HookPathGetFirstPointY") != std::string::npos);
    assert(source.find("GetDynamicPathExpected") != std::string::npos);
    assert(source.find("SourceTargetObservationPath")
        != std::string::npos);
    assert(source.find("GetPathXExpected") == std::string::npos);
    assert(source.find("GetPathYExpected") == std::string::npos);
    assert(source.find("GetPathDirectionExpected") == std::string::npos);
    assert(source.find("SetPathDirectionExpected") == std::string::npos);
    assert(source.find("RestoreSourceDirection") == std::string::npos);
    assert(source.find("capturedPostCastDirection") == std::string::npos);
    assert(source.find("targetMatchesAim") == std::string::npos);
    assert(source.find("native-unit-target") != std::string::npos);
    assert(source.find("native-position") != std::string::npos);
    [[maybe_unused]] const auto observationScope = source.find(
        "SourceTargetObservationScope observation");
    [[maybe_unused]] const auto nativeSkillHandler = source.find(
        "nativeResult = OriginalSkillHandler");
    assert(observationScope != std::string::npos);
    assert(nativeSkillHandler != std::string::npos);
    assert(observationScope < nativeSkillHandler);
    assert(source.find("ForwardPoint") == std::string::npos);
    assert(source.find("ShouldResolveTriggeredSkillTarget")
        != std::string::npos);
    assert(source.find("ShouldUseNativeUnitTarget")
        != std::string::npos);
    assert(source.find("IsPendingInputFresh")
        != std::string::npos);
    assert(source.find("ShouldDispatchChanneling") != std::string::npos);
    assert(source.find("ReadCurrentGameFrame") != std::string::npos);
    assert(source.find("GetTickCount") == std::string::npos);
    assert(source.find("std::chrono") == std::string::npos);
    assert(source.find("EVENT_SetEvent") == std::string::npos);
    assert(source.find("HookFillDamageValues") != std::string::npos);
    assert(source.find("HookCopyDamage") != std::string::npos);
    assert(source.find("HookDestroyDamage") != std::string::npos);
    assert(source.find("HookEventFunc15") != std::string::npos);
    assert(source.find("HookEventFunc16") != std::string::npos);
    assert(source.find("HookEventFunc20") != std::string::npos);
    assert(source.find("ActiveSourceTrigger") != std::string::npos);
    assert(source.find("ActiveSourceSkillId") != std::string::npos);
    assert(source.find("ShouldExposeSyntheticStat(") != std::string::npos);
    assert(source.find("HookDispatchUnitStatEvent") != std::string::npos);
    assert(source.find("HookMoveDamage") != std::string::npos);
    assert(source.find("MoveDamageRva") != std::string::npos);
    [[maybe_unused]] const auto fillDamageHook = source.find(
        "void __fastcall HookFillDamageValues(");
    [[maybe_unused]] const auto copyDamageHook = source.find(
        "void* __fastcall HookCopyDamage(",
        fillDamageHook);
    [[maybe_unused]] const auto staleCriticalReset = source.find(
        "RemoveCriticalDamageMarker(damage);",
        fillDamageHook);
    [[maybe_unused]] const auto criticalPrediction = source.find(
        "predictedCritical = PredictNativeCriticalStrike(",
        fillDamageHook);
    assert(fillDamageHook != std::string::npos);
    assert(copyDamageHook != std::string::npos);
    assert(staleCriticalReset != std::string::npos);
    assert(criticalPrediction != std::string::npos);
    assert(staleCriticalReset < criticalPrediction);
    assert(criticalPrediction < copyDamageHook);
    assert(source.find("ProcExecutionScope") != std::string::npos);
    assert(source.find("PassiveCriticalStatId") != std::string::npos);
    assert(source.find("D2RL::GetBuildName") != std::string::npos);
    assert(source.find("D2RL::GetBuildVersion") != std::string::npos);
    assert(source.find("MaximumDiagnosticTraceEntries = 64")
        != std::string::npos);
    assert(source.find("void RecordDiagnostic") != std::string::npos);
    assert(source.find("SnapshotDiagnosticTrace") != std::string::npos);
    assert(source.find("void LogDiagnostic") == std::string::npos);
    assert(source.find("Cast Triggers cadence/input:") != std::string::npos);
    assert(source.find("Cast Triggers combat:") != std::string::npos);
    assert(source.find("combat chains suppressed=%llu") != std::string::npos);
    assert(source.find("critical marker overflows=%llu") != std::string::npos);
    [[maybe_unused]] const auto recordDiagnostic = source.find(
        "void RecordDiagnostic");
    [[maybe_unused]] const auto recordDiagnosticEnd = source.find(
        "auto SnapshotDiagnosticTrace", recordDiagnostic);
    assert(recordDiagnostic != std::string::npos);
    assert(recordDiagnosticEnd != std::string::npos);
    assert(source.substr(
        recordDiagnostic,
        recordDiagnosticEnd - recordDiagnostic).find("Context->Log")
        == std::string::npos);
    assert(source.find("combat-hook logging=deferred")
        != std::string::npos);
    assert(source.find("diagnostics=\"") != std::string::npos);
    assert(source.find("? \"buffered\" : \"off\"")
        != std::string::npos);
}
