#include "policy.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::EnhancedDamageMinMaxFix;

    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    CHECK(input.good());
    const std::string text{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Config config{};
    std::string error;
    CHECK(ParseConfig(text, config, error));
    CHECK(config.enabled);
    CHECK(!config.diagnosticsEnabled);
    CHECK(ParseConfig(
        "[plugin]\nenabled = false\n[diagnostics]\nenabled = true\n",
        config,
        error));
    CHECK(!config.enabled && config.diagnosticsEnabled);
    CHECK(!ParseConfig("", config, error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = yes\n[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = false = true\n",
        config,
        error));

    static_assert(PackStat(ItemMaxDamagePercentStat) == 0x00110000);
    static_assert(PackStat(ItemMinDamagePercentStat) == 0x00120000);
    static_assert(PackStat(ItemMaxDamagePercentStat, 1) == 0x00110001);
    static_assert(IsEnhancedDamagePackedStat(0x00110000));
    static_assert(IsEnhancedDamagePackedStat(0x00120000));
    static_assert(!IsEnhancedDamagePackedStat(0x00110001));
    static_assert(!IsEnhancedDamagePackedStat(17));

    static_assert(ShouldRestoreSuppressedUpdate(
        ItemUnitType,
        AddItemStatPercentOperation,
        PackStat(ItemMaxDamagePercentStat),
        false,
        510,
        0));
    static_assert(ShouldRestoreSuppressedUpdate(
        ItemUnitType,
        AddItemStatPercentOperation,
        PackStat(ItemMinDamagePercentStat),
        false,
        505,
        500));
    static_assert(!ShouldRestoreSuppressedUpdate(
        ItemUnitType,
        AddItemStatPercentOperation,
        PackStat(ItemMaxDamagePercentStat),
        true,
        510,
        0));
    static_assert(!ShouldRestoreSuppressedUpdate(
        ItemUnitType,
        AddItemStatPercentOperation,
        PackStat(ItemMaxDamagePercentStat),
        false,
        500,
        500));
    static_assert(!ShouldRestoreSuppressedUpdate(
        0,
        AddItemStatPercentOperation,
        PackStat(ItemMaxDamagePercentStat),
        false,
        510,
        0));
    static_assert(!ShouldRestoreSuppressedUpdate(
        ItemUnitType,
        12,
        PackStat(ItemMaxDamagePercentStat),
        false,
        510,
        0));

    // The diagnostic line is a check contract: pin its exact text and fields.
    char line[320]{};
    CHECK(FormatCorrectionLog(
        line, sizeof(line), PackStat(ItemMaxDamagePercentStat), 0, 35) > 0);
    CHECK(std::string(line)
        == "Enhanced Damage Min/Max Fix 1.2.4 restored an off-weapon update "
           "(stat=17, retained=0, evaluated=35).");
    CHECK(FormatCorrectionLog(
        line, sizeof(line), PackStat(ItemMinDamagePercentStat), -5, 505) > 0);
    CHECK(std::string(line).find(
        std::string(CorrectionLogPhrase)
        + " (stat=18, retained=-5, evaluated=505).") != std::string::npos);
    CHECK(std::string(CorrectionLogFormat).find(
        std::string(CorrectionLogPhrase)
        + " (stat=%u, retained=%d, evaluated=%d).") != std::string::npos);

    // With diagnostics off, no line is written and the cap is not consumed.
    std::atomic<std::uint64_t> logged{};
    for (int attempt = 0; attempt < 20; ++attempt) {
        CHECK(!ShouldLogCorrection(false, logged));
    }
    CHECK(logged.load() == 0);

    // With diagnostics on, only the first MaximumDiagnosticLogs lines are written.
    static_assert(MaximumDiagnosticLogs == 8);
    int written{};
    for (int attempt = 0; attempt < 20; ++attempt) {
        written += ShouldLogCorrection(true, logged) ? 1 : 0;
    }
    CHECK(written == static_cast<int>(MaximumDiagnosticLogs));
    return EXIT_SUCCESS;
}
