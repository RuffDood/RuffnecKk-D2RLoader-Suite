#include "policy.hpp"
#include "rare_patch.hpp"
#include "relay.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace RuffnecKk::ProgressiveAffixes;

namespace {

void Check(bool condition) {
    if (!condition) throw std::runtime_error("progressive affix policy check failed");
}

Config Parse(std::string_view text) {
    std::istringstream input{std::string(text)};
    return ParseConfig(input);
}

bool Throws(std::string_view text) {
    try {
        static_cast<void>(Parse(text));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

void CheckDistribution(
        const WeightedCategory& category,
        std::int32_t itemLevel,
        const std::vector<std::uint32_t>& expected) {
    const auto* step = FindStep(category.steps, itemLevel);
    Check(step != nullptr);
    std::vector<std::uint32_t> actual(category.counts.size());
    for (std::uint32_t roll = 0; roll < TotalWeight(*step); ++roll) {
        const auto count = PickWeightedCount(category, *step, roll);
        const auto position = std::find(category.counts.begin(), category.counts.end(), count);
        Check(position != category.counts.end());
        ++actual[static_cast<std::size_t>(position - category.counts.begin())];
    }
    Check(actual == expected);
}

} // namespace

int main(int argc, char** argv) {
    Check(argc == 2);
    std::ifstream shipped(argv[1]);
    Check(shipped.is_open());
    const auto config = ParseConfig(shipped);
    Check(config.enabled);
    Check(!config.diagnostics);
    Check(config.magic.size() == 4);
    Check(config.rare.size() == 2);
    Check(config.crafted.size() == 1);

    Check(config.magic[0].name == "weapons_and_armor");
    Check(config.magic[0].steps[0].minimumItemLevel == 65);
    Check(config.magic[1].steps[0].minimumItemLevel == 85);
    Check(config.magic[2].steps[0].minimumItemLevel == 90);
    Check(config.magic[3].itemTypes[0].wildcard);
    Check(FindStep(config.magic[0].steps, 64) == nullptr);
    Check(FindStep(config.magic[0].steps, 65)->minimumAffixes == 2);

    Check(config.rare[0].name == "rare_jewels");
    CheckDistribution(config.rare[0], 1, {5000, 5000});
    CheckDistribution(config.rare[0], 44, {5000, 5000});
    CheckDistribution(config.rare[0], 45, {3750, 6250});
    CheckDistribution(config.rare[0], 64, {3750, 6250});
    CheckDistribution(config.rare[0], 65, {2500, 7500});
    CheckDistribution(config.rare[0], 84, {2500, 7500});
    CheckDistribution(config.rare[0], 85, {0, 10000});
    CheckDistribution(config.rare[1], 1, {1250, 3750, 3750, 1250});
    CheckDistribution(config.rare[1], 44, {1250, 3750, 3750, 1250});
    CheckDistribution(config.rare[1], 45, {0, 2500, 5000, 2500});
    CheckDistribution(config.rare[1], 65, {0, 0, 5000, 5000});
    CheckDistribution(config.rare[1], 85, {0, 0, 0, 10000});
    CheckDistribution(config.crafted[0], 1, {4000, 2000, 2000, 2000});
    CheckDistribution(config.crafted[0], 31, {0, 6000, 2000, 2000});
    CheckDistribution(config.crafted[0], 51, {0, 0, 8000, 2000});
    CheckDistribution(config.crafted[0], 71, {0, 0, 0, 10000});

    const auto legacy = Parse(R"toml(
[plugin]
enabled = true
diagnostics = false
[[rare.categories]]
name = "jewels"
item_types = ["jewl"]
counts = [3, 4]
[[rare.categories.steps]]
minimum_item_level = 1
weights = [1, 1]
[[rare.categories]]
name = "all_other_items"
item_types = ["*"]
counts = [3, 4, 5, 6]
[[rare.categories.steps]]
minimum_item_level = 1
weights = [1, 3, 3, 1]
)toml");
    Check(legacy.rare.size() == 2);
    CheckDistribution(legacy.rare[0], 1, {1, 1});

    const auto diagnostics = Parse(R"toml(
[plugin]
enabled = false
[diagnostics]
enabled = true
)toml");
    Check(!diagnostics.enabled);
    Check(diagnostics.diagnostics);
    Check(Throws(R"toml(
[plugin]
enabled = false
diagnostics = false
[diagnostics]
enabled = true
)toml"));

    const auto disabled = Parse("[plugin]\nenabled = false\n");
    Check(!disabled.enabled);
    Check(disabled.magic.empty());

    Check(Throws("[plugin]\nenabled = true\nunknown = false\n"));
    Check(Throws("[plugin]\nenabled = 1\n"));
    Check(Throws("[plugin]\nenabled = true\n"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[magic]
weapons_and_armor = 65
jewels_rings_and_amulets = 85
charms = 90
[rare_jewels]
from_level_1 = [50, 49.9]
[regular_rare_items]
from_level_1 = [12.5, 37.5, 37.5, 12.5]
[crafted]
from_level_1 = [40, 20, 20, 20]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[magic]
weapons_and_armor = 65
jewels_rings_and_amulets = 85
charms = 90
[rare_jewels]
from_level_1 = [50, 25, 25]
[regular_rare_items]
from_level_1 = [12.5, 37.5, 37.5, 12.5]
[crafted]
from_level_1 = [40, 20, 20, 20]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[magic]
weapons_and_armor = 65
jewels_rings_and_amulets = 85
charms = 90
[rare_jewels]
from_level_1 = [37.555, 62.445]
[regular_rare_items]
from_level_1 = [12.5, 37.5, 37.5, 12.5]
[crafted]
from_level_1 = [40, 20, 20, 20]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[magic]
weapons_and_armor = 65
jewels_rings_and_amulets = 85
charms = 90
[rare_jewels]
from_level_45 = [50, 50]
[regular_rare_items]
from_level_1 = [12.5, 37.5, 37.5, 12.5]
[crafted]
from_level_1 = [40, 20, 20, 20]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[magic]
weapons_and_armor = 65
jewels_rings_and_amulets = 85
charms = 90
[rare_jewels]
from_level_1 = [50, 50]
[regular_rare_items]
from_level_1 = [12.5, 37.5, 37.5, 12.5]
[crafted]
from_level_1 = [40, 20, 20, 20]
[[rare.categories]]
name = "fallback"
item_types = ["*"]
counts = [3, 4]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[[rare.categories]]
name = "fallback"
item_types = ["*"]
counts = [3, 4]
[[rare.categories.steps]]
minimum_item_level = 1
weights = [1]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[[crafted.categories]]
name = "fallback"
item_types = ["*"]
counts = [1, 2, 3, 4]
[[crafted.categories.steps]]
minimum_item_level = 1
weights = [0, 0, 0, 0]
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[[magic.categories]]
name = "fallback"
item_types = ["*"]
[[magic.categories.steps]]
minimum_item_level = 1
minimum_affixes = 3
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[[magic.categories]]
name = "fallback"
item_types = ["*"]
[[magic.categories.steps]]
minimum_item_level = 1
minimum_affixes = 1
[[magic.categories]]
name = "late"
item_types = ["weap"]
[[magic.categories.steps]]
minimum_item_level = 65
minimum_affixes = 2
)toml"));
    Check(Throws(R"toml(
[plugin]
enabled = true
[[magic.categories]]
name = "invalid_code"
item_types = ["too-long"]
[[magic.categories.steps]]
minimum_item_level = 1
minimum_affixes = 2
[[magic.categories]]
name = "fallback"
item_types = ["*"]
[[magic.categories.steps]]
minimum_item_level = 1
minimum_affixes = 1
)toml"));

    constexpr auto target = std::uintptr_t{0x1122334455667788ULL};
    std::array<std::uint8_t, RelayStride> relay{};
    BuildPreservingFirstTwoArgumentsRelay(relay, target);
    constexpr std::array<std::uint8_t, 25> expected{
        0x51,
        0x52,
        0x48, 0x83, 0xEC, 0x28,
        0x48, 0xB8,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0xFF, 0xD0,
        0x48, 0x83, 0xC4, 0x28,
        0x5A,
        0x59,
        0xC3,
    };
    Check(std::equal(expected.begin(), expected.end(), relay.begin()));
    Check(std::all_of(
        relay.begin() + static_cast<std::ptrdiff_t>(expected.size()),
        relay.end(),
        [](std::uint8_t byte) { return byte == 0xCC; }));

    constexpr auto rareSite = std::uintptr_t{0x000000014058BC90ULL};
    constexpr auto rareRelay = rareSite + 0x1000;
    constexpr auto rareReturn = rareSite + RareSelectionPatchSize;
    std::array<std::uint8_t, RareSelectionPatchSize> rarePatch{};
    Check(BuildRareSelectionPatch(rarePatch, rareSite, rareRelay, rareReturn));
    constexpr std::array<std::uint8_t, RareSelectionPatchSize> expectedRarePatch{
        0xE8, 0xFB, 0x0F, 0x00, 0x00,
        0x44, 0x8B, 0xE0,
        0x4C, 0x8B, 0xCB,
        0xE9, 0x0E, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    };
    Check(rarePatch == expectedRarePatch);

    std::int32_t callDisplacement{};
    std::memcpy(&callDisplacement, rarePatch.data() + 1, sizeof(callDisplacement));
    Check(rareSite + 5 + callDisplacement == rareRelay);
    std::int32_t jumpDisplacement{};
    std::memcpy(&jumpDisplacement, rarePatch.data() + 12, sizeof(jumpDisplacement));
    Check(rareSite + 16 + jumpDisplacement == rareReturn);
    Check(std::all_of(
        rarePatch.begin() + 16,
        rarePatch.end(),
        [](std::uint8_t byte) { return byte == 0x90; }));

    constexpr std::array<std::uint8_t, 3> stackCorruptingEncoding{
        0x41, 0x8B, 0xE0,
    };
    Check(std::search(
        rarePatch.begin(),
        rarePatch.end(),
        stackCorruptingEncoding.begin(),
        stackCorruptingEncoding.end()) == rarePatch.end());

    auto unchanged = rarePatch;
    Check(!BuildRareSelectionPatch(
        unchanged,
        rareSite,
        rareSite + static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max()) + 6,
        rareReturn));
    Check(unchanged == rarePatch);
    return 0;
}
