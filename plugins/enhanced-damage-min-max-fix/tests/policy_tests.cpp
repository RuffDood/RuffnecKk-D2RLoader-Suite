#include "policy.hpp"

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
    return EXIT_SUCCESS;
}
