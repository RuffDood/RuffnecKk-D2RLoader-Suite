#include "native_contract.hpp"
#include "policy.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::VendorStockRefresh;

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
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = TRUE\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = false\n"
        "extra = false\n",
        config,
        error));
    CHECK(!ParseConfig("[plugin]\nenabled = true\n", config, error));

    CHECK(RefreshActionForPanel(false) == NormalRefreshAction);
    CHECK(RefreshActionForPanel(true) == VanillaGambleRefreshAction);
    CHECK(ShouldShowNormalRefresh(false));
    CHECK(!ShouldShowNormalRefresh(true));

    constexpr WidgetRect vanillaGold{421, 1305, 313, 58};
    constexpr WidgetRect vanillaRefresh{877, 1277, 112, 112};
    constexpr auto vanillaPlacement = CenterBelow(vanillaGold, vanillaRefresh);
    static_assert(vanillaPlacement.valid);
    static_assert(vanillaPlacement.x == 521);
    static_assert(vanillaPlacement.y == 1382);

    constexpr WidgetRect moddedGold{600, 1500, 500, 80};
    constexpr WidgetRect moddedRefresh{1100, 1400, 160, 160};
    constexpr auto moddedPlacement = CenterBelow(moddedGold, moddedRefresh);
    static_assert(moddedPlacement.valid);
    static_assert(moddedPlacement.x == 770);
    static_assert(moddedPlacement.y == 1607);

    constexpr auto fallbackGold = UnionRect(
        WidgetRect{427, 1304, 57, 57},
        WidgetRect{487, 1309, 249, 48});
    static_assert(fallbackGold.x == 427);
    static_assert(fallbackGold.y == 1304);
    static_assert(fallbackGold.width == 309);
    static_assert(fallbackGold.height == 57);
    static_assert(!CenterBelow(WidgetRect{}, vanillaRefresh).valid);
    static_assert(!CenterBelow(vanillaGold, WidgetRect{}).valid);

    CHECK(ShouldArmNormalRefresh(true, NormalVendorMode, true, true));
    CHECK(!ShouldArmNormalRefresh(false, NormalVendorMode, true, true));
    CHECK(!ShouldArmNormalRefresh(true, GambleVendorMode, true, true));
    CHECK(!ShouldArmNormalRefresh(true, NormalVendorMode, false, true));
    CHECK(!ShouldArmNormalRefresh(true, NormalVendorMode, true, false));

    using namespace NativeContract;
    CHECK(Matches(VanillaBuilder.data(), VanillaBuilder));
    CHECK(MatchesRelayBuilder(VanillaBuilder.data()));
    auto relayedBuilder = VanillaBuilder;
    relayedBuilder[BuilderCallDisplacementOffset + 0] = 0xA7;
    relayedBuilder[BuilderCallDisplacementOffset + 1] = 0xE0;
    relayedBuilder[BuilderCallDisplacementOffset + 2] = 0xD3;
    relayedBuilder[BuilderCallDisplacementOffset + 3] = 0x03;
    CHECK(!Matches(relayedBuilder.data(), VanillaBuilder));
    CHECK(MatchesRelayBuilder(relayedBuilder.data()));
    auto mutatedBuilder = relayedBuilder;
    mutatedBuilder[0x19] ^= 0x01;
    CHECK(!MatchesRelayBuilder(mutatedBuilder.data()));

    CHECK(Matches(RelayStubOpcode.data(), RelayStubOpcode));
    auto invalidRelay = RelayStubOpcode;
    invalidRelay[1] = 0x15;
    CHECK(!Matches(invalidRelay.data(), RelayStubOpcode));
    CHECK(Matches(D2RCoreProviderEntry.data(), D2RCoreProviderEntry));
    auto invalidProvider = D2RCoreProviderEntry;
    invalidProvider[0x20] ^= 0x01;
    CHECK(!Matches(invalidProvider.data(), D2RCoreProviderEntry));
    CHECK(Matches(D2RCoreForwardingWitness.data(), D2RCoreForwardingWitness));
    auto invalidForwarding = D2RCoreForwardingWitness;
    invalidForwarding[0x0B] ^= 0x01;
    CHECK(!Matches(invalidForwarding.data(), D2RCoreForwardingWitness));
    CHECK(Matches(DownstreamQueueEntry.data(), DownstreamQueueEntry));
    auto invalidDownstream = DownstreamQueueEntry;
    invalidDownstream[0x14] ^= 0x01;
    CHECK(!Matches(invalidDownstream.data(), DownstreamQueueEntry));

    const auto forwardTarget = ResolveRelativeTarget(0x1000, 5, 0x200);
    CHECK(forwardTarget && *forwardTarget == 0x1205);
    const auto backwardTarget = ResolveRelativeTarget(0x1000, 6, -0x206);
    CHECK(backwardTarget && *backwardTarget == 0x0E00);
    CHECK(!AddSignedDisplacement(0, -1));
    CHECK(!AddSignedDisplacement(
        std::numeric_limits<std::uintptr_t>::max(), 1));
    return EXIT_SUCCESS;
}
