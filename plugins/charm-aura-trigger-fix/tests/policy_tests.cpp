#include "policy.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::CharmAuraTriggerFix;

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
        "[diagnostics]\nenabled = true\n[plugin]\nenabled = false\n",
        config,
        error));
    CHECK(!config.enabled && config.diagnosticsEnabled);
    CHECK(!ParseConfig("[plugin]\nenabled = true\n", config, error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = false\n"
        "enabled = true\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nverbose = false\n",
        config,
        error));

    static_assert(IsEligible(true, 3, 0x10));
    static_assert(IsEligible(true, 3, 0x30));
    static_assert(!IsEligible(false, 3, 0x10));
    static_assert(!IsEligible(true, 6, 0x10));
    static_assert(!IsEligible(true, 7, 0x10));
    static_assert(!IsEligible(true, 3, 0));

    constexpr PackedStatRecord stats[]{
        {97u << 16U | 42u, 1},
        {151u << 16U | 99u, 12},
        {151u << 16U | 100u, 0},
    };
    static_assert(HasNonzeroStat(stats, 3, 97));
    static_assert(HasNonzeroStat(stats, 3, 151));
    static_assert(!HasNonzeroStat(stats, 3, 150));
    static_assert(!HasNonzeroStat(nullptr, 3, 151));
    CHECK(StatId(stats[1].packed) == 151);
    return EXIT_SUCCESS;
}
