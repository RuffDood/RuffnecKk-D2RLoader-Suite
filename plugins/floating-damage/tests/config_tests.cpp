#include "config_parser.hpp"
#include "default_config.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

bool Same(float left, float right) {
    return std::fabs(left - right) < 0.0001f;
}

bool SameColor(const ImVec4& left, const ImVec4& right) {
    return Same(left.x, right.x) && Same(left.y, right.y)
        && Same(left.z, right.z) && Same(left.w, right.w);
}

bool Expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

bool RejectWithoutMutation(std::string_view text) {
    FloatingDamage::Config output{};
    output.maxNumbersOnScreen = 777;
    output.fontIndex = 9;
    output.normalColor = ImVec4(0.1f, 0.2f, 0.3f, 0.4f);
    std::string error;
    if (FloatingDamage::ParseConfigToml(text, output, error)) return false;
    return !error.empty()
        && output.maxNumbersOnScreen == 777
        && output.fontIndex == 9
        && SameColor(output.normalColor, ImVec4(0.1f, 0.2f, 0.3f, 0.4f));
}

bool AcceptLegacyHotkeyWithoutApplying(std::string_view text) {
    FloatingDamage::Config output{};
    output.enabled = false;
    output.maxNumbersOnScreen = 777;
    output.fontIndex = 9;
    std::string error;
    if (!FloatingDamage::ParseConfigToml(text, output, error)) return false;
    const FloatingDamage::Config defaults{};
    return error.empty()
        && output.enabled == defaults.enabled
        && output.maxNumbersOnScreen == defaults.maxNumbersOnScreen
        && output.fontIndex == defaults.fontIndex;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Expected the shipped TOML path.\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    const std::string shipped{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    bool ok = Expect(input.is_open() && !input.bad(), "Could not read the shipped TOML.");
    ok &= Expect(
        shipped == RuffnecKk::FloatingDamage::DefaultConfigToml,
        "Generated default TOML is not byte-exact with the shipped file.");

    FloatingDamage::Config config{};
    std::string error;
    ok &= Expect(
        FloatingDamage::ParseConfigToml(shipped, config, error),
        error.c_str());
    ok &= Expect(config.enabled, "enabled mismatch");
    ok &= Expect(!config.diagnosticsEnabled, "diagnostics mismatch");
    ok &= Expect(config.maxNumbersOnScreen == 160, "max_numbers mismatch");
    ok &= Expect(config.fontIndex == 0, "default font_index mismatch");
    ok &= Expect(!config.colorByDamageType, "color_by_damage_type mismatch");

    auto legacyShipped = shipped;
    const auto appearanceTable = legacyShipped.find("[appearance]");
    ok &= Expect(appearanceTable != std::string::npos,
        "appearance table missing for legacy migration test");
    if (appearanceTable != std::string::npos) {
        legacyShipped.insert(
            appearanceTable,
            "[hotkey]\n"
            "toggle_hotkey_enabled = false\n"
            "toggle_hotkey = \"MOUSE4\"\n\n");
        FloatingDamage::Config legacy{};
        std::string legacyError;
        ok &= Expect(FloatingDamage::ParseConfigToml(
            legacyShipped, legacy, legacyError), legacyError.c_str());
        ok &= Expect(legacy.enabled == config.enabled
            && legacy.maxNumbersOnScreen == config.maxNumbersOnScreen
            && legacy.fontIndex == config.fontIndex
            && Same(legacy.textSize, config.textSize),
            "legacy hotkey values changed the effective configuration");
    }

    auto kodiaConfig = shipped;
    const auto defaultFont = kodiaConfig.find("font_index = 0");
    ok &= Expect(defaultFont != std::string::npos, "default font setting missing");
    if (defaultFont != std::string::npos) {
        kodiaConfig.replace(defaultFont, std::string("font_index = 0").size(),
            "font_index = 12");
        FloatingDamage::Config kodia{};
        std::string kodiaError;
        ok &= Expect(FloatingDamage::ParseConfigToml(
            kodiaConfig, kodia, kodiaError), kodiaError.c_str());
        ok &= Expect(kodia.fontIndex == 12, "Kodia font_index rejected");
    }
    ok &= Expect(Same(config.textSize, 38.0f), "text_size mismatch");
    ok &= Expect(Same(config.criticalHitSize, 48.0f), "critical size mismatch");
    ok &= Expect(config.textOutlineWidth == 1, "outline mismatch");
    ok &= Expect(Same(config.shadowLeftRightOffset, 0.0f)
        && Same(config.shadowUpDownOffset, 0.0f), "shadow offsets mismatch");
    ok &= Expect(Same(config.displayTimeSeconds, 0.85f)
        && Same(config.criticalDisplayTimeSeconds, 0.95f)
        && Same(config.fadeOutStart, 0.75f), "display timing mismatch");
    ok &= Expect(Same(config.spawnSize, 0.01f)
        && Same(config.popBounceSize, 1.75f)
        && Same(config.popInTimeSeconds, 0.08f)
        && Same(config.settleTimeSeconds, 0.12f), "pop animation mismatch");
    ok &= Expect(Same(config.upwardDriftSpeed, 45.0f)
        && Same(config.sidewaysSpread, 0.0f)
        && Same(config.spawnHeightOffset, 0.0f), "drift mismatch");
    ok &= Expect(config.enableHitCombining
        && config.maxCombinedHitSize == 999999
        && config.combineWindowMs == 500, "combining mismatch");
    ok &= Expect(Same(config.extendDisplayOnHitSeconds, 0.52f)
        && Same(config.hitPulseSize, 1.24f)
        && Same(config.hitPulseTimeSeconds, 0.13f), "pulse mismatch");
    ok &= Expect(config.showTickPopups
        && Same(config.tickPopupTimeSeconds, 0.70f)
        && Same(config.tickPopupSize, 0.60f)
        && Same(config.tickPopupTravel, 64.0f)
        && Same(config.tickPopupHeightOffset, -28.0f), "tick mismatch");
    ok &= Expect(config.spreadNumbersHorizontally
        && config.numberOfColumns == 7
        && Same(config.columnSpacing, 40.0f)
        && Same(config.stackHeightStep, 24.0f)
        && Same(config.columnReuseTimeSeconds, 0.60f)
        && Same(config.maxStackHeight, 96.0f), "layout mismatch");
    ok &= Expect(config.showDpsCounter
        && Same(config.horizontalPositionPercent, 2.0f)
        && Same(config.verticalPositionPercent, 98.0f)
        && Same(config.dpsSampleTimeSeconds, 5.0f), "DPS mismatch");
    ok &= Expect(config.previewNumberCount == 8
        && Same(config.previewSpread, 32.0f), "preview mismatch");
    ok &= Expect(SameColor(config.normalColor, ImVec4(0.92f, 0.92f, 0.88f, 1.0f))
        && SameColor(config.criticalColor, ImVec4(1.0f, 0.84f, 0.27f, 1.0f))
        && SameColor(config.physicalColor, ImVec4(0.92f, 0.92f, 0.88f, 1.0f))
        && SameColor(config.fireColor, ImVec4(1.0f, 0.45f, 0.12f, 1.0f))
        && SameColor(config.lightningColor, ImVec4(1.0f, 0.95f, 0.35f, 1.0f))
        && SameColor(config.coldColor, ImVec4(0.45f, 0.78f, 1.0f, 1.0f))
        && SameColor(config.poisonColor, ImVec4(0.35f, 0.90f, 0.30f, 1.0f))
        && SameColor(config.magicColor, ImVec4(0.72f, 0.45f, 1.0f, 1.0f))
        && SameColor(config.outlineColor, ImVec4(0.16f, 0.11f, 0.03f, 1.0f))
        && SameColor(config.shadowColor, ImVec4(0.16f, 0.11f, 0.02f, 1.0f)),
        "color mismatch");

    ok &= Expect(RejectWithoutMutation("[unknown]\nx = 1\n"), "unknown table accepted");
    ok &= Expect(RejectWithoutMutation("[general]\nwat = true\n"), "unknown key accepted");
    ok &= Expect(RejectWithoutMutation("[general]\nenabled = true\nenabled = false\n"), "duplicate key accepted");
    ok &= Expect(RejectWithoutMutation("[general]\nenabled = true\n[general]\nfont_index = 0\n"), "duplicate table accepted");
    ok &= Expect(RejectWithoutMutation("[diagnostics]\nenabled = false\nenabled = true\n"), "duplicate diagnostics accepted");
    ok &= Expect(RejectWithoutMutation("[diagnostics]\nverbose = true\n"), "unknown diagnostics key accepted");
    ok &= Expect(RejectWithoutMutation("[general]\ntoggle_hotkey = \"D\"\n"), "wrong-table key accepted");
    ok &= Expect(RejectWithoutMutation("[general]\nmax_numbers_on_screen = nope\n"), "bad integer accepted");
    ok &= Expect(RejectWithoutMutation("[animation]\nspawn_size = nan\n"), "non-finite number accepted");
    ok &= Expect(RejectWithoutMutation("[general]\nfont_index = 13\n"), "out-of-range font accepted");
    ok &= Expect(RejectWithoutMutation("[colors]\nnormal = [1.1, 0, 0, 1]\n"), "out-of-range color accepted");
    ok &= Expect(AcceptLegacyHotkeyWithoutApplying(
        "[hotkey]\n"
        "toggle_hotkey_enabled = false\n"
        "toggle_hotkey = \"MOUSE4\"\n"),
        "legacy hotkey settings were rejected or applied");
    ok &= Expect(RejectWithoutMutation(
        "[hotkey]\nunknown_hotkey = \"SHIFT+Z\"\n"),
        "unknown legacy hotkey key accepted");
    ok &= Expect(RejectWithoutMutation("enabled = true\n"), "setting before table accepted");
    ok &= Expect(RejectWithoutMutation("[general\nenabled = true\n"), "malformed table accepted");

    return ok ? 0 : 1;
}
