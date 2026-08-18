#include "policy.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
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
    using namespace RuffnecKk::ItemDurability;

    static_assert(IsBowOrCrossbowItemTypeCode(PackItemTypeCode('b', 'o', 'w')));
    static_assert(IsBowOrCrossbowItemTypeCode(PackItemTypeCode('x', 'b', 'o', 'w')));
    static_assert(!IsBowOrCrossbowItemTypeCode(PackItemTypeCode('a', 'r', 'm')));

    std::array<std::uint8_t, CompiledItemTypeRecordStride * 4> itemTypes{};
    const auto setType = [&](std::size_t index, std::uint32_t code,
        std::uint16_t equivalent = 0) {
        auto* record = itemTypes.data() + index * CompiledItemTypeRecordStride;
        std::memcpy(record + CompiledItemTypeCodeOffset, &code, sizeof(code));
        std::memcpy(record + CompiledItemTypeEquivalentOneOffset,
            &equivalent, sizeof(equivalent));
    };
    setType(0, PackItemTypeCode('a', 'r', 'm'));
    setType(1, PackItemTypeCode('b', 'o', 'w'));
    setType(2, PackItemTypeCode('x', 'b', 'o', 'w'));
    setType(3, PackItemTypeCode('a', 'b', 'o', 'w'), 1);

    std::array<std::uint8_t, CompiledItemRecordStride * 4> items{};
    for (std::uint16_t index = 0; index < 4; ++index) {
        auto* record = items.data()
            + static_cast<std::size_t>(index) * CompiledItemRecordStride;
        record[CompiledItemNoDurabilityOffset] = 1;
        std::memcpy(record + CompiledItemPrimaryTypeOffset, &index, sizeof(index));
    }
    const auto mutation = ApplyRangedDurabilityToCompiledTables(
        items.data(), 4, itemTypes.data(), 4);
    REQUIRE(mutation.valid);
    REQUIRE(mutation.itemRecordsUpdated == 3);
    REQUIRE(mutation.itemTypesUpdated == 3);
    REQUIRE(ApplyRangedDurabilityToCompiledTables(
        items.data(), 4, itemTypes.data(), 4).itemRecordsUpdated == 0);

    REQUIRE(PreventsLoss(50, 49));
    REQUIRE(!PreventsLoss(50, 50));
    REQUIRE(EffectiveChanceBasisPoints(4, 50) == 200);
    REQUIRE(TargetEtherealMaxDurability(20, 50) == 11);
    REQUIRE(ApplyVanillaEtherealHalving(
        EncodeForVanillaEtherealHalving(20, 50)) == 11);

    REQUIRE(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    Config config{};
    std::string error;
    REQUIRE(ParseConfig(stream.str(), config, error));
    REQUIRE(config.enabled);
    REQUIRE(!config.durabilityLossEnabled);
    REQUIRE(config.normalResistancePercent == 0);
    REQUIRE(config.etherealResistancePercent == 0);
    REQUIRE(config.etherealMaximumPercent == 50);
    REQUIRE(!config.forceMaximumDurability);
    REQUIRE(!config.bowsAndCrossbowsHaveDurability);
    REQUIRE(!config.diagnosticsEnabled);

    auto legacy = stream.str();
    const auto pluginSection = legacy.find("[plugin]");
    REQUIRE(pluginSection != std::string::npos);
    const auto nextSection = legacy.find("[durability_loss]", pluginSection);
    REQUIRE(nextSection != std::string::npos);
    legacy.erase(pluginSection, nextSection - pluginSection);
    REQUIRE(ParseConfig(legacy, config, error));
    REQUIRE(config.enabled);

    auto disabled = stream.str();
    const auto enabled = disabled.find("enabled = true");
    REQUIRE(enabled != std::string::npos);
    disabled.replace(enabled, std::string("enabled = true").size(),
        "enabled = false");
    REQUIRE(ParseConfig(disabled, config, error));
    REQUIRE(!config.enabled);

    auto invalid = stream.str();
    invalid += "\n[extra]\nenabled = true\n";
    REQUIRE(!ParseConfig(invalid, config, error));
    REQUIRE(!ParseConfig("[durability_loss]\nenabled = maybe\n", config, error));
    return 0;
}
