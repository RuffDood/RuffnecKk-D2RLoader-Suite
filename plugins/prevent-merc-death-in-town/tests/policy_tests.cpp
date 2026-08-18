#include "policy.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::PreventMercDeathInTown;

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
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = 0\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = false\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseConfig("[broken\nenabled = true\n", config, error));

    static_assert(IsHirelingClass(271));
    static_assert(IsHirelingClass(338));
    static_assert(IsHirelingClass(359));
    static_assert(IsHirelingClass(560));
    static_assert(IsHirelingClass(561));
    static_assert(!IsHirelingClass(0));
    static_assert(!IsHirelingClass(270));
    static_assert(IsProjectedLethal(256, -256));
    static_assert(IsProjectedLethal(1, -2));
    static_assert(!IsProjectedLethal(256, -255));
    static_assert(!IsProjectedLethal(0, 0));
    static_assert(!IsProjectedLethal(1, 1));
    return EXIT_SUCCESS;
}
