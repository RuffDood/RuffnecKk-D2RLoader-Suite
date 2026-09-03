#include "navigation_policy.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace RuffnecKk::MapSense {
namespace {

struct ProgressionEdge final {
    std::int32_t from{};
    std::int32_t to{};
    std::int32_t dynamicObjectClassId{-1};
};

struct PresetProgressionWitness final {
    std::int32_t from{};
    std::int32_t to{};
    std::uint32_t presetType{};
    std::int32_t presetClassId{};
    NavigationPresetProgressionKind kind{
        NavigationPresetProgressionKind::QuestObject};
};

struct QuestRouteEdge final {
    std::int32_t from{};
    std::int32_t to{};
};

struct QuestPresetWitness final {
    std::int32_t levelId{};
    std::uint32_t presetType{};
    std::int32_t presetClassId{};
    NavigationDestinationSelection selection{
        NavigationDestinationSelection::All};
};

constexpr std::uint32_t PresetMonster = 1U;
constexpr std::uint32_t PresetObject = 2U;

// PrimeMH identifies the Summoner as an NPC-spawn POI and the Arcane Tome as
// a quest-item POI. D2MOO proves that object 60 is created only after Tome
// interaction, so these generated presets are the exact pre-portal fallbacks.
constexpr std::array PresetProgressionWitnesses{
    PresetProgressionWitness{
        74,
        46,
        PresetMonster,
        250,
        NavigationPresetProgressionKind::Boss,
    },
    PresetProgressionWitness{
        74,
        46,
        PresetObject,
        357,
        NavigationPresetProgressionKind::QuestObject,
    },
};

// Static side routes for quests in the normal act quest log. Shared campaign
// routes (Andariel, Claw Viper Temple, Arcane, Travincal, Diablo, Ancients and
// Baal) stay green because a coincident red line adds no direction. Farming,
// secret and Pandemonium areas are intentionally absent.
constexpr std::array QuestRouteGraph{
    QuestRouteEdge{2, 8},    // Blood Moor -> Den of Evil
    QuestRouteEdge{3, 17},   // Cold Plains -> Burial Grounds
    QuestRouteEdge{6, 20},   // Black Marsh -> Forgotten Tower
    QuestRouteEdge{20, 21},  // Forgotten Tower -> Tower Cellar 1
    QuestRouteEdge{21, 22},  // Tower Cellar 1 -> 2
    QuestRouteEdge{22, 23},  // Tower Cellar 2 -> 3
    QuestRouteEdge{23, 24},  // Tower Cellar 3 -> 4
    QuestRouteEdge{24, 25},  // Tower Cellar 4 -> 5

    QuestRouteEdge{47, 48},  // Sewers 1 -> 2 (Radament)
    QuestRouteEdge{48, 49},  // Sewers 2 -> 3
    QuestRouteEdge{42, 56},  // Dry Hills -> Halls of the Dead 1
    QuestRouteEdge{56, 57},  // Halls of the Dead 1 -> 2
    QuestRouteEdge{57, 60},  // Halls of the Dead 2 -> 3
    QuestRouteEdge{43, 62},  // Far Oasis -> Maggot Lair 1
    QuestRouteEdge{62, 63},  // Maggot Lair 1 -> 2
    QuestRouteEdge{63, 64},  // Maggot Lair 2 -> 3

    QuestRouteEdge{76, 85},  // Spider Forest -> Spider Cavern
    QuestRouteEdge{78, 88},  // Flayer Jungle -> Flayer Dungeon 1
    QuestRouteEdge{88, 89},  // Flayer Dungeon 1 -> 2
    QuestRouteEdge{89, 91},  // Flayer Dungeon 2 -> 3
    QuestRouteEdge{80, 92},  // Kurast Bazaar -> Sewers 1
    QuestRouteEdge{81, 92},  // Upper Kurast -> Sewers 1
    QuestRouteEdge{92, 93},  // Sewers 1 -> 2 (Khalim's Heart)
    QuestRouteEdge{80, 94},  // Kurast Bazaar -> Ruined Temple

    QuestRouteEdge{113, 114}, // Crystalline Passage -> Frozen River
    QuestRouteEdge{121, 122}, // Nihlathak's Temple -> Halls of Anguish
    QuestRouteEdge{122, 123}, // Halls of Anguish -> Halls of Pain
    QuestRouteEdge{123, 124}, // Halls of Pain -> Halls of Vaught
};

// Class ids are exact D2R 3.3 Objects.txt records consumed by generated
// PresetUnit records. Monster, SuperUnique and MonPlace presets deliberately
// remain excluded because their encoded ids depend on active table counts and
// no governed runtime decoder exists. Diablo seals are also excluded to avoid
// five simultaneous lines.
constexpr std::array QuestPresetWitnesses{
    QuestPresetWitness{4, PresetObject, 21},   // Cairn Stone Lambda
    QuestPresetWitness{5, PresetObject, 30},   // Tree of Inifuss
    QuestPresetWitness{38, PresetObject, 26},  // Cain's Gibbet
    QuestPresetWitness{28, PresetObject, 108}, // Horadric Malus

    QuestPresetWitness{60, PresetObject, 354}, // Horadric Cube chest
    QuestPresetWitness{61, PresetObject, 149}, // Tainted Sun altar
    QuestPresetWitness{64, PresetObject, 356}, // Staff of Kings chest
    QuestPresetWitness{66, PresetObject, 152}, // Horadric Staff orifice
    QuestPresetWitness{67, PresetObject, 152},
    QuestPresetWitness{68, PresetObject, 152},
    QuestPresetWitness{69, PresetObject, 152},
    QuestPresetWitness{70, PresetObject, 152},
    QuestPresetWitness{71, PresetObject, 152},
    QuestPresetWitness{72, PresetObject, 152},

    QuestPresetWitness{85, PresetObject, 407}, // Khalim's Eye chest
    QuestPresetWitness{91, PresetObject, 406}, // Khalim's Brain chest
    QuestPresetWitness{93, PresetObject, 405}, // Khalim's Heart chest
    QuestPresetWitness{94, PresetObject, 193}, // Lam Esen's Tome
    QuestPresetWitness{83, PresetObject, 404}, // Compelling Orb

    QuestPresetWitness{107, PresetObject, 376}, // Hellforge

    QuestPresetWitness{
        111,
        PresetObject,
        473,
        NavigationDestinationSelection::NearestToPlayer}, // captive cage
    QuestPresetWitness{114, PresetObject, 558}, // frozen Anya
    QuestPresetWitness{120, PresetObject, 546}, // Ancients' altar
};

// Explicit forward-progression graph for all five acts. In outdoor hubs the
// green line follows the campaign route and deliberately ignores optional
// entrances. Once the player enters a multi-floor dungeon, its next floor is
// forward progression even when the dungeon itself was optional. Dynamic
// portals use an exact active-object class witness; quest-selected targets
// such as the correct Tal Rasha tomb remain the red quest-line concern.
// Duplicate `from` entries are ordered alternatives for generated layouts.
constexpr std::array ProgressionGraph{
    ProgressionEdge{1, 2},   // Rogue Encampment -> Blood Moor
    ProgressionEdge{2, 3},   // Blood Moor -> Cold Plains
    ProgressionEdge{3, 4},   // Cold Plains -> Stony Field
    ProgressionEdge{4, 10},  // Stony Field -> Underground Passage Level 1
    ProgressionEdge{5, 6},   // Dark Wood -> Black Marsh
    ProgressionEdge{6, 7},   // Black Marsh -> Tamoe Highland
    ProgressionEdge{7, 26},  // Tamoe Highland -> Monastery Gate
    ProgressionEdge{9, 13},  // Cave Level 1 -> Cave Level 2
    ProgressionEdge{10, 5},  // Underground Passage Level 1 -> Dark Wood
    ProgressionEdge{11, 15}, // Hole Level 1 -> Hole Level 2
    ProgressionEdge{12, 16}, // Pit Level 1 -> Pit Level 2
    ProgressionEdge{26, 27}, // Monastery Gate -> Outer Cloister
    ProgressionEdge{27, 28}, // Outer Cloister -> Barracks
    ProgressionEdge{28, 29}, // Barracks -> Jail Level 1
    ProgressionEdge{29, 30}, // Jail Level 1 -> Jail Level 2
    ProgressionEdge{30, 31}, // Jail Level 2 -> Jail Level 3
    ProgressionEdge{31, 32}, // Jail Level 3 -> Inner Cloister
    ProgressionEdge{32, 33}, // Inner Cloister -> Cathedral
    ProgressionEdge{33, 34}, // Cathedral -> Catacombs Level 1
    ProgressionEdge{34, 35}, // Catacombs Level 1 -> Catacombs Level 2
    ProgressionEdge{35, 36}, // Catacombs Level 2 -> Catacombs Level 3
    ProgressionEdge{36, 37}, // Catacombs Level 3 -> Catacombs Level 4

    ProgressionEdge{40, 41}, // Lut Gholein -> Rocky Waste (town-suppressed)
    ProgressionEdge{41, 42}, // Rocky Waste -> Dry Hills
    ProgressionEdge{42, 43}, // Dry Hills -> Far Oasis
    ProgressionEdge{43, 44}, // Far Oasis -> Lost City
    ProgressionEdge{44, 45}, // Lost City -> Valley of Snakes
    ProgressionEdge{45, 58}, // Valley of Snakes -> Claw Viper Temple 1
    ProgressionEdge{50, 51}, // Harem Level 1 -> Level 2
    ProgressionEdge{51, 52}, // Harem Level 2 -> Palace Cellar Level 1
    ProgressionEdge{52, 53}, // Palace Cellar Level 1 -> Level 2
    ProgressionEdge{53, 54}, // Palace Cellar Level 2 -> Level 3
    ProgressionEdge{54, 74, 298}, // Palace Cellar Level 3 -> Arcane portal
    ProgressionEdge{55, 59}, // Stony Tomb Level 1 -> Level 2
    ProgressionEdge{58, 61}, // Claw Viper Temple Level 1 -> Level 2
    ProgressionEdge{66, 73, 100}, // True tomb -> Duriel's Lair portal
    ProgressionEdge{67, 73, 100},
    ProgressionEdge{68, 73, 100},
    ProgressionEdge{69, 73, 100},
    ProgressionEdge{70, 73, 100},
    ProgressionEdge{71, 73, 100},
    ProgressionEdge{72, 73, 100},
    ProgressionEdge{74, 46, 60}, // Arcane Sanctuary -> Canyon portal

    ProgressionEdge{75, 76},  // Kurast Docks -> Spider Forest
    ProgressionEdge{76, 78},  // Spider Forest -> Flayer Jungle (bypass)
    ProgressionEdge{76, 77},  // Spider Forest -> Great Marsh (fallback)
    ProgressionEdge{77, 78},  // Great Marsh -> Flayer Jungle when connected
    ProgressionEdge{77, 76},  // Great Marsh dead-end -> bypass via Spider Forest
    ProgressionEdge{78, 79},  // Flayer Jungle -> Lower Kurast
    ProgressionEdge{79, 80},  // Lower Kurast -> Kurast Bazaar
    ProgressionEdge{80, 81},  // Kurast Bazaar -> Upper Kurast
    ProgressionEdge{81, 82},  // Upper Kurast -> Kurast Causeway
    ProgressionEdge{82, 83},  // Kurast Causeway -> Travincal
    ProgressionEdge{83, 100}, // Travincal -> Durance of Hate Level 1
    ProgressionEdge{86, 87},  // Swampy Pit Level 1 -> Level 2
    ProgressionEdge{87, 90},  // Swampy Pit Level 2 -> Level 3
    ProgressionEdge{100, 101}, // Durance of Hate Level 1 -> Level 2
    ProgressionEdge{101, 102}, // Durance of Hate Level 2 -> Level 3
    ProgressionEdge{102, 103, 342}, // Hell Gate -> Pandemonium Fortress

    ProgressionEdge{103, 104}, // Pandemonium Fortress -> Outer Steppes
    ProgressionEdge{104, 105}, // Outer Steppes -> Plains of Despair
    ProgressionEdge{105, 106}, // Plains of Despair -> City of the Damned
    ProgressionEdge{106, 107}, // City of the Damned -> River of Flame
    ProgressionEdge{107, 108}, // River of Flame -> Chaos Sanctuary
    ProgressionEdge{108, 109, 566}, // post-Diablo portal -> Harrogath

    ProgressionEdge{109, 110}, // Harrogath -> Bloody Foothills
    ProgressionEdge{110, 111}, // Bloody Foothills -> Frigid Highlands
    ProgressionEdge{111, 112}, // Frigid Highlands -> Arreat Plateau
    ProgressionEdge{112, 113}, // Arreat Plateau -> Crystalline Passage
    ProgressionEdge{113, 115}, // Crystalline Passage -> Glacial Trail
    ProgressionEdge{115, 117}, // Glacial Trail -> Frozen Tundra
    ProgressionEdge{117, 118}, // Frozen Tundra -> Ancients' Way
    ProgressionEdge{118, 120}, // Ancients' Way -> Arreat Summit
    ProgressionEdge{120, 128}, // Arreat Summit -> Worldstone Keep Level 1
    ProgressionEdge{128, 129}, // Worldstone Keep Level 1 -> Level 2
    ProgressionEdge{129, 130}, // Worldstone Keep Level 2 -> Level 3
    ProgressionEdge{130, 131}, // Worldstone Keep Level 3 -> Throne
    ProgressionEdge{131, 132, 563}, // Baal portal -> Worldstone Chamber
};

[[nodiscard]] auto ContainsLevelId(
        std::span<const std::int32_t> ids,
        std::int32_t target) noexcept -> bool {
    return std::find(ids.begin(), ids.end(), target) != ids.end();
}

[[nodiscard]] auto SameDestination(
        const NavigationSubtileDestination& left,
        const NavigationSubtileDestination& right) noexcept -> bool {
    return left.kind == right.kind
        && left.destinationId == right.destinationId
        && left.subtileX == right.subtileX
        && left.subtileY == right.subtileY
        && left.useExactClientCoordinates
            == right.useExactClientCoordinates
        && left.selection == right.selection
        && (!left.useExactClientCoordinates
            || (left.exactClientX == right.exactClientX
                && left.exactClientY == right.exactClientY));
}

void AppendUnique(
        std::span<NavigationSubtileDestination> output,
        std::size_t& count,
        NavigationSubtileDestination destination) noexcept {
    if (count >= output.size()) return;
    for (std::size_t index = 0U; index < count; ++index) {
        if (SameDestination(output[index], destination)) return;
    }
    output[count++] = destination;
}

} // namespace

auto MainProgressionTargetsFor(
        std::int32_t currentLevelId,
        std::span<std::int32_t> output) noexcept -> std::size_t {
    std::size_t count{};
    for (const auto& edge : ProgressionGraph) {
        if (edge.from != currentLevelId) continue;
        if (count >= output.size()) break;
        output[count++] = edge.to;
    }
    return count;
}

auto MainProgressionTargetFor(
        std::int32_t currentLevelId) noexcept
        -> std::optional<std::int32_t> {
    const auto match = std::find_if(
        ProgressionGraph.begin(),
        ProgressionGraph.end(),
        [currentLevelId](const ProgressionEdge& edge) noexcept {
            return edge.from == currentLevelId;
        });
    if (match == ProgressionGraph.end()) return std::nullopt;
    return match->to;
}

auto SelectMainProgressionTargetFor(
        std::int32_t currentLevelId,
        std::span<const NavigationExitCandidate> exits) noexcept
        -> std::optional<std::int32_t> {
    for (const auto& edge : ProgressionGraph) {
        if (edge.from != currentLevelId) continue;
        if (std::find_if(
                exits.begin(),
                exits.end(),
                [&edge](const NavigationExitCandidate& exit) noexcept {
                    return exit.targetLevelId == edge.to;
                }) != exits.end()) {
            return edge.to;
        }
    }
    return std::nullopt;
}

auto StaticQuestRouteTargetsFor(
        std::int32_t currentLevelId,
        std::span<std::int32_t> output) noexcept -> std::size_t {
    if (currentLevelId <= 0 || output.empty()) return 0U;
    std::size_t count{};
    for (const auto& edge : QuestRouteGraph) {
        if (edge.from != currentLevelId || count >= output.size()) continue;
        output[count++] = edge.to;
    }
    return count;
}

auto IsStaticQuestRouteTarget(
        std::int32_t currentLevelId,
        std::int32_t targetLevelId) noexcept -> bool {
    if (currentLevelId <= 0 || targetLevelId <= 0) return false;
    return std::find_if(
        QuestRouteGraph.begin(),
        QuestRouteGraph.end(),
        [currentLevelId, targetLevelId](
                const QuestRouteEdge& edge) noexcept {
            return edge.from == currentLevelId && edge.to == targetLevelId;
        }) != QuestRouteGraph.end();
}

auto StaticQuestPresetTargetFor(
        std::int32_t currentLevelId,
        std::uint32_t presetType,
        std::int32_t presetClassId) noexcept
        -> std::optional<NavigationQuestPresetTarget> {
    if (currentLevelId <= 0 || presetClassId < 0) return std::nullopt;
    const auto match = std::find_if(
        QuestPresetWitnesses.begin(),
        QuestPresetWitnesses.end(),
        [currentLevelId, presetType, presetClassId](
                const QuestPresetWitness& witness) noexcept {
            return witness.levelId == currentLevelId
                && witness.presetType == presetType
                && witness.presetClassId == presetClassId;
        });
    if (match == QuestPresetWitnesses.end()) return std::nullopt;
    return NavigationQuestPresetTarget{.selection = match->selection};
}

auto DynamicMainProgressionTargetFor(
        std::int32_t currentLevelId,
        std::int32_t objectClassId) noexcept
        -> std::optional<std::int32_t> {
    if (currentLevelId <= 0 || objectClassId < 0) return std::nullopt;
    const auto match = std::find_if(
        ProgressionGraph.begin(),
        ProgressionGraph.end(),
        [currentLevelId, objectClassId](
                const ProgressionEdge& edge) noexcept {
            return edge.from == currentLevelId
                && edge.dynamicObjectClassId == objectClassId;
        });
    if (match == ProgressionGraph.end()) return std::nullopt;
    return match->to;
}

auto HasDynamicMainProgressionTargetFor(
        std::int32_t currentLevelId) noexcept -> bool {
    return std::find_if(
        ProgressionGraph.begin(),
        ProgressionGraph.end(),
        [currentLevelId](const ProgressionEdge& edge) noexcept {
            return edge.from == currentLevelId
                && edge.dynamicObjectClassId >= 0;
        }) != ProgressionGraph.end();
}

auto PresetMainProgressionTargetFor(
        std::int32_t currentLevelId,
        std::uint32_t presetType,
        std::int32_t presetClassId) noexcept
        -> std::optional<NavigationPresetProgressionTarget> {
    if (currentLevelId <= 0 || presetClassId < 0) return std::nullopt;
    const auto match = std::find_if(
        PresetProgressionWitnesses.begin(),
        PresetProgressionWitnesses.end(),
        [currentLevelId, presetType, presetClassId](
                const PresetProgressionWitness& witness) noexcept {
            return witness.from == currentLevelId
                && witness.presetType == presetType
                && witness.presetClassId == presetClassId;
        });
    if (match == PresetProgressionWitnesses.end()) return std::nullopt;
    return NavigationPresetProgressionTarget{
        .targetLevelId = match->to,
        .kind = match->kind,
    };
}

auto BuildNavigationPreparationTargets(
        std::int32_t currentLevelId,
        std::span<const std::int32_t> customTargetLevelIds,
        std::span<std::int32_t> output) noexcept -> std::size_t {
    if (currentLevelId <= 0 || output.empty()) return 0U;

    std::size_t count{};
    const auto append = [&output, &count, currentLevelId](
            std::int32_t targetLevelId) noexcept {
        if (targetLevelId <= 0 || targetLevelId == currentLevelId
            || count >= output.size()) {
            return;
        }
        if (std::find(output.begin(), output.begin() + count, targetLevelId)
            != output.begin() + count) {
            return;
        }
        output[count++] = targetLevelId;
    };

    for (const auto& edge : ProgressionGraph) {
        if (edge.from == currentLevelId
            && edge.dynamicObjectClassId < 0) {
            append(edge.to);
        }
    }
    for (const auto& edge : QuestRouteGraph) {
        if (edge.from == currentLevelId) append(edge.to);
    }
    for (const auto targetLevelId : customTargetLevelIds) {
        append(targetLevelId);
    }
    return count;
}

auto CheckedNavigationSubtileCoordinate(
        std::int32_t gameTile,
        std::int32_t relativeSubtile,
        std::int32_t& output) noexcept -> bool {
    constexpr std::int32_t SubtilesPerGameTile = 5;
    const auto value = static_cast<std::int64_t>(gameTile)
        * SubtilesPerGameTile
        + relativeSubtile;
    if (value < 0
        || value > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    output = static_cast<std::int32_t>(value);
    return true;
}

auto EvaluateNavigationResolutionCompleteness(
        std::int32_t currentLevelId,
        std::span<const NavigationExitCandidate> exits) noexcept
        -> NavigationResolutionCompleteness {
    const auto requiredProgression = std::find_if(
        ProgressionGraph.begin(),
        ProgressionGraph.end(),
        [currentLevelId](const ProgressionEdge& edge) noexcept {
            return edge.from == currentLevelId
                && edge.dynamicObjectClassId < 0;
        });
    const auto foundProgression = requiredProgression == ProgressionGraph.end()
        || SelectMainProgressionTargetFor(currentLevelId, exits).has_value();
    const auto foundQuestRoutes = std::all_of(
        QuestRouteGraph.begin(),
        QuestRouteGraph.end(),
        [currentLevelId, exits](const QuestRouteEdge& edge) noexcept {
            if (edge.from != currentLevelId) return true;
            return std::find_if(
                exits.begin(),
                exits.end(),
                [&edge](const NavigationExitCandidate& exit) noexcept {
                    return exit.targetLevelId == edge.to;
                }) != exits.end();
        });
    return foundProgression && foundQuestRoutes
        ? NavigationResolutionCompleteness::Complete
        : NavigationResolutionCompleteness::PartialRetryable;
}

auto BuildNavigationDestinations(
        const NavigationPolicyInput& input,
        std::span<NavigationSubtileDestination> output) noexcept -> std::size_t {
    if (input.currentLevelId == UnknownNavigationLevelId
        || input.inTown
        || output.empty()) {
        return 0U;
    }

    std::size_t count{};
    if (input.waypoint != nullptr) {
        AppendUnique(
            output,
            count,
            NavigationSubtileDestination{
                .destinationId = input.waypoint->destinationId,
                .subtileX = input.waypoint->subtileX,
                .subtileY = input.waypoint->subtileY,
                .kind = NavigationLineKind::Waypoint,
                .exactClientX = input.waypoint->exactClientX,
                .exactClientY = input.waypoint->exactClientY,
                .useExactClientCoordinates =
                    input.waypoint->useExactClientCoordinates,
            });
    }

    const auto progressionTarget = input.progressionTargetOverride.has_value()
        ? input.progressionTargetOverride
        : SelectMainProgressionTargetFor(input.currentLevelId, input.exits);
    const auto durielPortalReplacesOrifice = input.currentLevelId >= 66
        && input.currentLevelId <= 72
        && progressionTarget.value_or(UnknownNavigationLevelId) == 73;
    for (const auto& exit : input.exits) {
        const auto staticQuestRoute = IsStaticQuestRouteTarget(
            input.currentLevelId,
            exit.targetLevelId);
        if (progressionTarget && exit.targetLevelId == *progressionTarget
            && !staticQuestRoute) {
            AppendUnique(
                output,
                count,
                NavigationSubtileDestination{
                    .destinationId = exit.destinationId,
                    .subtileX = exit.subtileX,
                    .subtileY = exit.subtileY,
                    .kind = NavigationLineKind::Progression,
                    .exactClientX = exit.exactClientX,
                    .exactClientY = exit.exactClientY,
                    .useExactClientCoordinates =
                        exit.useExactClientCoordinates,
                });
        }
        if (ContainsLevelId(
                input.customTargetLevelIds,
                exit.targetLevelId)) {
            AppendUnique(
                output,
                count,
                NavigationSubtileDestination{
                    .destinationId = exit.destinationId,
                    .subtileX = exit.subtileX,
                    .subtileY = exit.subtileY,
                    .kind = NavigationLineKind::CustomLevel,
                    .exactClientX = exit.exactClientX,
                    .exactClientY = exit.exactClientY,
                    .useExactClientCoordinates =
                        exit.useExactClientCoordinates,
                });
        }
        if (staticQuestRoute) {
            AppendUnique(
                output,
                count,
                NavigationSubtileDestination{
                    .destinationId = exit.destinationId,
                    .subtileX = exit.subtileX,
                    .subtileY = exit.subtileY,
                    .kind = NavigationLineKind::Quest,
                    .exactClientX = exit.exactClientX,
                    .exactClientY = exit.exactClientY,
                    .useExactClientCoordinates =
                        exit.useExactClientCoordinates,
                });
        }
    }

    for (const auto& quest : input.questTargets) {
        // In the true tomb, object 152 is the pre-portal staff orifice. Once
        // the exact class-100 portal to Duriel's Lair exists, green owns that
        // progression and the now-stale red orifice must disappear.
        if (durielPortalReplacesOrifice) continue;
        AppendUnique(
            output,
            count,
            NavigationSubtileDestination{
                .destinationId = quest.destinationId,
                .subtileX = quest.subtileX,
                .subtileY = quest.subtileY,
                .kind = NavigationLineKind::Quest,
                .exactClientX = quest.exactClientX,
                .exactClientY = quest.exactClientY,
                .useExactClientCoordinates =
                    quest.useExactClientCoordinates,
                .selection = quest.selection,
            });
    }
    return count;
}

} // namespace RuffnecKk::MapSense
