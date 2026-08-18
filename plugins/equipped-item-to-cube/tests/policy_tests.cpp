#include "policy.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::EquippedItemToCube;

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
        "[plugin]\nenabled = true\n[diagnostics]\n", config, error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[plugin]\nenabled = false\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nunknown = true\n[diagnostics]\nenabled = false\n",
        config,
        error));

    ItemTransferPacket inventoryPacket{};
    inventoryPacket[0] = InventoryTransferOpcode;
    WriteU32(inventoryPacket, 1, 37);
    WriteU32(inventoryPacket, 13, CubeInventoryPage);
    WriteU32(inventoryPacket, 17, 5u | (3u << 16));

    static_assert(IsEquippedBodyLocation(1));
    static_assert(IsEquippedBodyLocation(10));
    static_assert(!IsEquippedBodyLocation(0));
    static_assert(!IsEquippedBodyLocation(11));
    CHECK(ShouldRewriteCubeTransfer(true, inventoryPacket, 4));
    CHECK(!ShouldRewriteCubeTransfer(false, inventoryPacket, 4));
    CHECK(!ShouldRewriteCubeTransfer(true, inventoryPacket, 0));
    CHECK(!ShouldRewriteCubeTransfer(true, inventoryPacket, BodyLocationCount));

    auto wrongOpcode = inventoryPacket;
    wrongOpcode[0] = 0x55;
    CHECK(!ShouldRewriteCubeTransfer(true, wrongOpcode, 4));
    auto wrongPage = inventoryPacket;
    WriteU32(wrongPage, 13, 0);
    CHECK(!ShouldRewriteCubeTransfer(true, wrongPage, 4));

    const auto equippedPacket = RewriteAsEquippedTransfer(inventoryPacket, 4);
    CHECK(equippedPacket[0] == EquippedTransferOpcode);
    CHECK(ReadU32(equippedPacket, 1) == 37);
    CHECK(ReadU32(equippedPacket, 5) == SelfTargetGuid);
    CHECK(ReadU32(equippedPacket, 9) == 4);
    CHECK(ReadU32(equippedPacket, 13) == CubeInventoryPage);
    CHECK(ReadU32(equippedPacket, 17) == (5u | (3u << 16)));
    return EXIT_SUCCESS;
}
