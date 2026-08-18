#include "policy.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main(int argc, char** argv) {
    using namespace RuffnecKk::MassIdentify;

    REQUIRE(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();

    Config config{};
    std::string error;
    REQUIRE(ParseConfig(stream.str(), config, error));
    REQUIRE(config.enabled);
    REQUIRE(!config.freeIdentification);
    REQUIRE(!config.targets.includeCube);
    REQUIRE(!config.targets.includePersonalStash);
    REQUIRE(!config.targets.includeSharedStash);
    REQUIRE(!config.diagnosticsEnabled);
    REQUIRE(IncludesTarget(config.targets, TargetContainer::Inventory));

    constexpr auto allEnabled = R"toml(
[mass_identify]
enabled = true
freeIdentification = true
includeCube = true
includePersonalStash = true
includeSharedStash = true
)toml";
    REQUIRE(ParseConfig(allEnabled, config, error));
    REQUIRE(config.enabled);
    REQUIRE(config.freeIdentification);
    REQUIRE(!config.diagnosticsEnabled);
    REQUIRE(IncludesTarget(config.targets, TargetContainer::Cube));
    REQUIRE(IncludesTarget(
        config.targets, TargetContainer::PersonalStash));
    REQUIRE(IncludesTarget(config.targets, TargetContainer::SharedStash));

    constexpr auto diagnostics = R"toml(
[mass_identify]
enabled = false
freeIdentification = false
includeCube = false
includePersonalStash = false
includeSharedStash = false
[diagnostics]
enabled = true
)toml";
    REQUIRE(ParseConfig(diagnostics, config, error));
    REQUIRE(!config.enabled);
    REQUIRE(config.diagnosticsEnabled);

    REQUIRE(!ParseConfig(
        "[mass_identify]\nenabled=true\nfreeIdentification=false\n"
        "includeCube=false\nincludePersonalStash=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[mass_identify]\nenabled=1\nfreeIdentification=false\n"
        "includeCube=false\nincludePersonalStash=false\n"
        "includeSharedStash=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[mass_identify]\nenabled=true\nenabled=false\n"
        "freeIdentification=false\nincludeCube=false\n"
        "includePersonalStash=false\nincludeSharedStash=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[mass_identify]\nenabled=true\nfreeIdentification=false\n"
        "includeCube=false\nincludePersonalStash=false\n"
        "includeSharedStash=false\nunknown=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[other]\nenabled=true\nfreeIdentification=false\n"
        "includeCube=false\nincludePersonalStash=false\n"
        "includeSharedStash=false\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "[mass_identify]\nenabled=true\nfreeIdentification=false\n"
        "includeCube=false\nincludePersonalStash=false\n"
        "includeSharedStash=false\n[diagnostics]\n",
        config,
        error));

    const auto packet = MakeRequest(0x12345678u);
    REQUIRE(IsPrivateRequest(
        packet.data(), static_cast<std::int32_t>(packet.size())));
    REQUIRE(ReadU32(packet.data(), 1) == 0x12345678u);
    REQUIRE(!IsPrivateRequest(packet.data(), 20));
    auto wrongGuard = packet;
    WriteU32(wrongGuard, 9, 0);
    REQUIRE(!IsPrivateRequest(
        wrongGuard.data(), static_cast<std::int32_t>(wrongGuard.size())));

    REQUIRE(IsSupportedInventoryPage(InventoryPage));
    REQUIRE(IsSupportedInventoryPage(CubePage));
    REQUIRE(!IsSupportedInventoryPage(StashPage));

    std::array<std::uint8_t, ItemDataInventoryPageOffset + 1> itemData{};
    itemData[ItemDataInventoryPageOffset] = StashPage;
    REQUIRE(ReadInventoryPageFromItemData(itemData.data()) == StashPage);
    REQUIRE(ReadInventoryPageFromItemData(nullptr) == InvalidInventoryPage);

    REQUIRE(ShouldCaptureGesture(
        true, true, true, true, IdentifyTomeCode, true));
    REQUIRE(!ShouldCaptureGesture(
        true, false, true, true, IdentifyTomeCode, true));
    REQUIRE(!ShouldCaptureGesture(
        true, true, true, false, IdentifyTomeCode, true));
    REQUIRE(!ShouldCaptureGesture(
        true, true, true, true, IdentifyTomeCode, false));
    REQUIRE(!ShouldCaptureGesture(
        true, true, true, true, 0x206B6274u, true));

    REQUIRE(IdentificationBudget(false, -1) == 0);
    REQUIRE(IdentificationBudget(false, 0) == 0);
    REQUIRE(IdentificationBudget(false, 7) == 7);
    REQUIRE(IdentificationBudget(true, 0)
        == (std::numeric_limits<std::int32_t>::max)());
    return 0;
}
