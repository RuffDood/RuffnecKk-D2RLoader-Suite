#include "navigation_level_catalog.hpp"

#include <array>
#include <cstddef>

namespace RuffnecKk::MapSense {
namespace {

struct CanonicalLevelName final {
    std::int32_t levelId{};
    std::string_view name{};
};

// Generated from the read-only D2R 3.3.93847 Levels.txt *StringName column.
// It is intentionally independent from any guessed compiled LevelsTxt layout.
constexpr std::array CanonicalLevelNames{
    CanonicalLevelName{1, "Rogue Encampment"},
    CanonicalLevelName{2, "Blood Moor"},
    CanonicalLevelName{3, "Cold Plains"},
    CanonicalLevelName{4, "Stony Field"},
    CanonicalLevelName{5, "Dark Wood"},
    CanonicalLevelName{6, "Black Marsh"},
    CanonicalLevelName{7, "Tamoe Highland"},
    CanonicalLevelName{8, "Den of Evil"},
    CanonicalLevelName{9, "Cave Level 1"},
    CanonicalLevelName{10, "Underground Passage Level 1"},
    CanonicalLevelName{11, "Hole Level 1"},
    CanonicalLevelName{12, "Pit Level 1"},
    CanonicalLevelName{13, "Cave Level 2"},
    CanonicalLevelName{14, "Underground Passage Level 2"},
    CanonicalLevelName{15, "Hole Level 2"},
    CanonicalLevelName{16, "Pit Level 2"},
    CanonicalLevelName{17, "Burial Grounds"},
    CanonicalLevelName{18, "Crypt"},
    CanonicalLevelName{19, "Mausoleum"},
    CanonicalLevelName{20, "Forgotten Tower"},
    CanonicalLevelName{21, "Tower Cellar Level 1"},
    CanonicalLevelName{22, "Tower Cellar Level 2"},
    CanonicalLevelName{23, "Tower Cellar Level 3"},
    CanonicalLevelName{24, "Tower Cellar Level 4"},
    CanonicalLevelName{25, "Tower Cellar Level 5"},
    CanonicalLevelName{26, "Monastery Gate"},
    CanonicalLevelName{27, "Outer Cloister"},
    CanonicalLevelName{28, "Barracks"},
    CanonicalLevelName{29, "Jail Level 1"},
    CanonicalLevelName{30, "Jail Level 2"},
    CanonicalLevelName{31, "Jail Level 3"},
    CanonicalLevelName{32, "Inner Cloister"},
    CanonicalLevelName{33, "Cathedral"},
    CanonicalLevelName{34, "Catacombs Level 1"},
    CanonicalLevelName{35, "Catacombs Level 2"},
    CanonicalLevelName{36, "Catacombs Level 3"},
    CanonicalLevelName{37, "Catacombs Level 4"},
    CanonicalLevelName{38, "Tristram"},
    CanonicalLevelName{39, "Moo Moo Farm"},
    CanonicalLevelName{40, "Lut Gholein"},
    CanonicalLevelName{41, "Rocky Waste"},
    CanonicalLevelName{42, "Dry Hills"},
    CanonicalLevelName{43, "Far Oasis"},
    CanonicalLevelName{44, "Lost City"},
    CanonicalLevelName{45, "Valley of Snakes"},
    CanonicalLevelName{46, "Canyon of the Magi"},
    CanonicalLevelName{47, "Sewers Level 1"},
    CanonicalLevelName{48, "Sewers Level 2"},
    CanonicalLevelName{49, "Sewers Level 3"},
    CanonicalLevelName{50, "Harem Level 1"},
    CanonicalLevelName{51, "Harem Level 2"},
    CanonicalLevelName{52, "Palace Cellar Level 1"},
    CanonicalLevelName{53, "Palace Cellar Level 2"},
    CanonicalLevelName{54, "Palace Cellar Level 3"},
    CanonicalLevelName{55, "Stony Tomb Level 1"},
    CanonicalLevelName{56, "Halls of the Dead Level 1"},
    CanonicalLevelName{57, "Halls of the Dead Level 2"},
    CanonicalLevelName{58, "Claw Viper Temple Level 1"},
    CanonicalLevelName{59, "Stony Tomb Level 2"},
    CanonicalLevelName{60, "Halls of the Dead Level 3"},
    CanonicalLevelName{61, "Claw Viper Temple Level 2"},
    CanonicalLevelName{62, "Maggot Lair Level 1"},
    CanonicalLevelName{63, "Maggot Lair Level 2"},
    CanonicalLevelName{64, "Maggot Lair Level 3"},
    CanonicalLevelName{65, "Ancient Tunnels"},
    CanonicalLevelName{66, "Tal Rasha's Tomb"},
    CanonicalLevelName{67, "Tal Rasha's Tomb"},
    CanonicalLevelName{68, "Tal Rasha's Tomb"},
    CanonicalLevelName{69, "Tal Rasha's Tomb"},
    CanonicalLevelName{70, "Tal Rasha's Tomb"},
    CanonicalLevelName{71, "Tal Rasha's Tomb"},
    CanonicalLevelName{72, "Tal Rasha's Tomb"},
    CanonicalLevelName{73, "Duriel's Lair"},
    CanonicalLevelName{74, "Arcane Sanctuary"},
    CanonicalLevelName{75, "Kurast Docktown"},
    CanonicalLevelName{76, "Spider Forest"},
    CanonicalLevelName{77, "Great Marsh"},
    CanonicalLevelName{78, "Flayer Jungle"},
    CanonicalLevelName{79, "Lower Kurast"},
    CanonicalLevelName{80, "Kurast Bazaar"},
    CanonicalLevelName{81, "Upper Kurast"},
    CanonicalLevelName{82, "Kurast Causeway"},
    CanonicalLevelName{83, "Travincal"},
    CanonicalLevelName{84, "Arachnid Lair"},
    CanonicalLevelName{85, "Spider Cavern"},
    CanonicalLevelName{86, "Swampy Pit Level 1"},
    CanonicalLevelName{87, "Swampy Pit Level 2"},
    CanonicalLevelName{88, "Flayer Dungeon Level 1"},
    CanonicalLevelName{89, "Flayer Dungeon Level 2"},
    CanonicalLevelName{90, "Swampy Pit Level 3"},
    CanonicalLevelName{91, "Flayer Dungeon Level 3"},
    CanonicalLevelName{92, "Sewers Level 1"},
    CanonicalLevelName{93, "Sewers Level 2"},
    CanonicalLevelName{94, "Ruined Temple"},
    CanonicalLevelName{95, "Disused Fane"},
    CanonicalLevelName{96, "Forgotten Reliquary"},
    CanonicalLevelName{97, "Forgotten Temple"},
    CanonicalLevelName{98, "Ruined Fane"},
    CanonicalLevelName{99, "Disused Reliquary"},
    CanonicalLevelName{100, "Durance of Hate Level 1"},
    CanonicalLevelName{101, "Durance of Hate Level 2"},
    CanonicalLevelName{102, "Durance of Hate Level 3"},
    CanonicalLevelName{103, "The Pandemonium Fortress"},
    CanonicalLevelName{104, "Outer Steppes"},
    CanonicalLevelName{105, "Plains of Despair"},
    CanonicalLevelName{106, "City of the Damned"},
    CanonicalLevelName{107, "River of Flame"},
    CanonicalLevelName{108, "The Chaos Sanctuary"},
    CanonicalLevelName{109, "Harrogath"},
    CanonicalLevelName{110, "Bloody Foothills"},
    CanonicalLevelName{111, "Frigid Highlands"},
    CanonicalLevelName{112, "Arreat Plateau"},
    CanonicalLevelName{113, "Crystalline Passage"},
    CanonicalLevelName{114, "Frozen River"},
    CanonicalLevelName{115, "Glacial Trail"},
    CanonicalLevelName{116, "Drifter Cavern"},
    CanonicalLevelName{117, "Frozen Tundra"},
    CanonicalLevelName{118, "The Ancients' Way"},
    CanonicalLevelName{119, "Icy Cellar"},
    CanonicalLevelName{120, "Arreat Summit"},
    CanonicalLevelName{121, "Nihlathak's Temple"},
    CanonicalLevelName{122, "Halls of Anguish"},
    CanonicalLevelName{123, "Halls of Pain"},
    CanonicalLevelName{124, "Halls of Vaught"},
    CanonicalLevelName{125, "Abaddon"},
    CanonicalLevelName{126, "Pit of Acheron"},
    CanonicalLevelName{127, "Infernal Pit"},
    CanonicalLevelName{128, "The Worldstone Keep Level 1"},
    CanonicalLevelName{129, "The Worldstone Keep Level 2"},
    CanonicalLevelName{130, "The Worldstone Keep Level 3"},
    CanonicalLevelName{131, "Throne of Destruction"},
    CanonicalLevelName{132, "The Worldstone Chamber"},
    CanonicalLevelName{133, "Matron's Den"},
    CanonicalLevelName{134, "Forgotten Sands"},
    CanonicalLevelName{135, "Furnace of Pain"},
    CanonicalLevelName{136, "Tristram"},
    CanonicalLevelName{137, "ColossalSummit"},
};

[[nodiscard]] constexpr auto AsciiEqualInsensitive(
        std::string_view left,
        std::string_view right) noexcept -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto fold = [](char value) constexpr noexcept {
            return value >= 'A' && value <= 'Z'
                ? static_cast<char>(value - 'A' + 'a')
                : value;
        };
        if (fold(left[index]) != fold(right[index])) return false;
    }
    return true;
}

} // namespace

auto ResolveCanonicalLevelName(
        std::string_view name) noexcept -> std::optional<std::int32_t> {
    std::optional<std::int32_t> result;
    for (const auto& entry : CanonicalLevelNames) {
        if (!AsciiEqualInsensitive(name, entry.name)) continue;
        if (result && *result != entry.levelId) return std::nullopt;
        result = entry.levelId;
    }
    return result;
}

} // namespace RuffnecKk::MapSense
