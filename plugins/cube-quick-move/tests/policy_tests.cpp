#include "policy.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::CubeQuickMove;

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
    CHECK(!ParseConfig("enabled = true\n", config, error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\nenabled = true\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[other]\nenabled = true\n[diagnostics]\nenabled = false\n",
        config,
        error));

    static_assert(ShouldRecomputeBottomRight(1, 3, 1, 2));
    static_assert(ShouldRecomputeBottomRight(1, 3, 2, 2));
    static_assert(ShouldRecomputeBottomRight(1, 3, 2, 3));
    static_assert(!ShouldRecomputeBottomRight(0, 3, 2, 2));
    static_assert(!ShouldRecomputeBottomRight(1, 0, 2, 2));
    static_assert(!ShouldRecomputeBottomRight(1, 3, 1, 1));
    static_assert(!ShouldRecomputeBottomRight(1, 3, 2, 1));
    static_assert(!ShouldRecomputeBottomRight(1, 3, 0, 2));

    std::array<std::uintptr_t, 12> cells{};
    std::int32_t x{-1};
    std::int32_t y{-1};
    CHECK(TryFindBottomRight(cells.data(), 3, 4, 2, 2, &x, &y));
    CHECK(x == 1 && y == 2);
    cells[2 + 3 * 3] = 1;
    CHECK(TryFindBottomRight(cells.data(), 3, 4, 2, 2, &x, &y));
    CHECK(x == 1 && y == 1);
    cells.fill(1);
    CHECK(!TryFindBottomRight(cells.data(), 3, 4, 2, 2, &x, &y));
    CHECK(!TryFindBottomRight(nullptr, 3, 4, 2, 2, &x, &y));
    return EXIT_SUCCESS;
}
