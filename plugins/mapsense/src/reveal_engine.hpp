#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

struct ExternalAtlasGeometrySnapshot;

enum class RevealOutcome : std::uint32_t {
    Complete,
    Unavailable,
    Armed,
    Disarmed,
};

struct RevealCounters {
    std::uint64_t levels{};
    std::uint64_t rooms{};
    std::uint64_t failures{};
    std::uint64_t traversalLimits{};
    std::uint64_t staticRoomCandidates{};
    std::uint64_t staticRoomsMaterialized{};
    std::uint64_t staticRoomsReleased{};
    std::uint64_t staticRoomFailures{};
};

struct NativeAutomapCellKeyValue final {
    std::int16_t frame{};
    std::int32_t x{};
    std::int32_t y{};
};

// D2R creates ordinary newly explored cells with tag 0 and recreates cells
// loaded from its automap sidecar with tag 1 before inserting both into the
// same renderer-owned tree. The serializer emits only keys whose low tag byte
// is zero. MapSense therefore uses the native restored-cell tag for generated
// atlas cells: they render normally but are rebuilt from MapSense's exact
// seed/difficulty intent instead of being appended to D2R's bounded sidecar.
inline constexpr std::uint16_t NativeAutomapOrdinaryCellTag = 0U;
inline constexpr std::uint16_t NativeAutomapRestoredCellTag = 1U;
inline constexpr std::uint16_t NativeAutomapSyntheticCellTag =
    NativeAutomapRestoredCellTag;

[[nodiscard]] constexpr auto NativeAutomapCellIsSerialized(
        std::uint16_t tag) noexcept -> bool {
    return (tag & 0x00FFU) == 0U;
}

// D2R serializes a tree node only when the first byte of its 12-byte cell key
// is zero. Each emitted key contributes three uint16 values, and the serializer
// reports their byte length through a signed 16-bit intermediate. The bound is
// therefore on emitted zero-tag keys, not on tree+0x20's total node count.
inline constexpr std::uint64_t NativeAutomapSerializedBytesPerCell = 6U;
inline constexpr std::uint64_t NativeAutomapMaximumEmittedTreeCells =
    static_cast<std::uint64_t>(
        (std::numeric_limits<std::int16_t>::max)())
    / NativeAutomapSerializedBytesPerCell;

[[nodiscard]] constexpr auto CanAppendNativeAutomapEmittedCell(
        std::uint64_t emittedCellCount) noexcept -> bool {
    return emittedCellCount < NativeAutomapMaximumEmittedTreeCells;
}

// Converts one immutable mapgen tile into the exact 12-byte native cell-key
// coordinate basis. D2R's ordinary tile path performs the same
// (tileX-tileY)*8, (tileX+tileY)*4 conversion and raises orientations >= 0x10
// by 24 independently from the floor/wall owner tree selection.
[[nodiscard]] constexpr auto BuildNativeAutomapCellKeyValue(
        std::int32_t frame,
        std::int32_t tileX,
        std::int32_t tileY,
        bool raised,
        NativeAutomapCellKeyValue& output) noexcept -> bool {
    output = {};
    if (frame < 0
        || frame > (std::numeric_limits<std::int16_t>::max)()
        || tileX < 0 || tileY < 0) {
        return false;
    }
    const auto x = (static_cast<std::int64_t>(tileX) - tileY) * 8;
    auto y = (static_cast<std::int64_t>(tileX) + tileY) * 4;
    if (raised) y += 24;
    if (x < (std::numeric_limits<std::int32_t>::min)()
        || x > (std::numeric_limits<std::int32_t>::max)()
        || y < (std::numeric_limits<std::int32_t>::min)()
        || y > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    output = {
        .frame = static_cast<std::int16_t>(frame),
        .x = static_cast<std::int32_t>(x),
        .y = static_cast<std::int32_t>(y),
    };
    return true;
}

struct NativeAutomapLevelLayer final {
    std::int32_t levelId{-1};
    std::int32_t layer{-1};
};

// Immutable renderer-facing coordinate catalog. Its exact session, seed,
// difficulty, act and authoritative Levels.Layer mapping are published before
// optional terrain insertion. readyLayers tracks completed external terrain
// or a proven zero-work label-only layer; it must not suppress labels over an
// already-visible native map.
struct NativeAutomapLayerCatalog final {
    std::uint64_t sessionGeneration{};
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::uint8_t act{};
    std::uint64_t geometryDigest{};
    std::vector<NativeAutomapLevelLayer> levels;
    std::vector<std::int32_t> readyLayers;
};

[[nodiscard]] inline auto FindNativeAutomapLayer(
        const NativeAutomapLayerCatalog& catalog,
        std::int32_t levelId,
        std::int32_t& output) noexcept -> bool {
    output = -1;
    const auto found = std::lower_bound(
        catalog.levels.begin(),
        catalog.levels.end(),
        levelId,
        [](const NativeAutomapLevelLayer& entry,
                std::int32_t requested) noexcept {
            return entry.levelId < requested;
        });
    if (found == catalog.levels.end() || found->levelId != levelId
        || found->layer < 0) {
        return false;
    }
    output = found->layer;
    return true;
}

[[nodiscard]] inline auto NativeAutomapLayerIsReady(
        const NativeAutomapLayerCatalog& catalog,
        std::int32_t layer) noexcept -> bool {
    return layer >= 0 && std::binary_search(
        catalog.readyLayers.begin(),
        catalog.readyLayers.end(),
        layer);
}

[[nodiscard]] inline auto NativeAutomapLevelsShareLayer(
        const NativeAutomapLayerCatalog& catalog,
        std::int32_t currentLevelId,
        std::int32_t anchoredLevelId) noexcept -> bool {
    std::int32_t currentLayer{};
    std::int32_t anchoredLayer{};
    return FindNativeAutomapLayer(catalog, currentLevelId, currentLayer)
        && FindNativeAutomapLayer(catalog, anchoredLevelId, anchoredLayer)
        && currentLayer == anchoredLayer;
}

[[nodiscard]] inline auto NativeAutomapLevelsShareReadyLayer(
        const NativeAutomapLayerCatalog& catalog,
        std::int32_t currentLevelId,
        std::int32_t anchoredLevelId) noexcept -> bool {
    std::int32_t currentLayer{};
    return NativeAutomapLevelsShareLayer(
            catalog, currentLevelId, anchoredLevelId)
        && FindNativeAutomapLayer(catalog, currentLevelId, currentLayer)
        && NativeAutomapLayerIsReady(catalog, currentLayer);
}

enum class NativeAutomapAtlasPublicationStatus : std::uint8_t {
    Unavailable,
    Accepted,
    InProgress,
    WaitingForOwner,
    AwaitingWitness,
    Complete,
    Stale,
    Failed,
};

enum class NativeAutomapActiveOwnerState : std::uint8_t {
    Pending,
    Ready,
    Mismatch,
};

// Publication is permitted only when D2R already owns a non-null active
// automap layer whose id exactly matches the authoritative Levels.Layer for
// the player. MapSense must never create or switch a layer to satisfy this
// predicate.
[[nodiscard]] constexpr auto ClassifyNativeAutomapActiveOwner(
        bool ownerPresent,
        std::int32_t ownerLayer,
        std::int32_t expectedLayer) noexcept
        -> NativeAutomapActiveOwnerState {
    if (!ownerPresent) return NativeAutomapActiveOwnerState::Pending;
    return ownerLayer == expectedLayer && expectedLayer >= 0
        ? NativeAutomapActiveOwnerState::Ready
        : NativeAutomapActiveOwnerState::Mismatch;
}

// A missing or previous-layer owner is normal while the automap is closed or
// D2R is completing a level transition. Neither state is a publication
// failure: the publisher must sleep until a real automap pass wakes it.
[[nodiscard]] constexpr auto NativeAutomapOwnerRequiresPulse(
        NativeAutomapActiveOwnerState state) noexcept -> bool {
    return state != NativeAutomapActiveOwnerState::Ready;
}

// Tag-1 atlas cells are transient across D2R's destructive native owner
// switch. Completion remains reusable only while the same native layer is
// still active; returning from another layer must republish that layer.
[[nodiscard]] constexpr auto NativeAutomapLayerCompletionIsReusable(
        bool ready,
        std::int32_t lastActiveLayer,
        std::int32_t currentLayer) noexcept -> bool {
    return ready && currentLayer >= 0 && lastActiveLayer == currentLayer;
}

struct NativeAutomapAtlasCounters final {
    std::uint64_t atlasesAccepted{};
    std::uint64_t atlasesCompleted{};
    std::uint64_t layerCatalogsPublished{};
    std::uint64_t cellsAttempted{};
    std::uint64_t cellsInserted{};
    std::uint64_t duplicateCells{};
    std::uint64_t failures{};
    std::uint64_t pendingCells{};
    std::uint64_t ownerPending{};
    std::uint64_t ownerMismatches{};
    std::uint64_t witnessPasses{};
    std::uint64_t witnessFailures{};
    std::uint64_t layerTransitions{};
    std::uint64_t maximumFloorTreeCells{};
    std::uint64_t maximumWallTreeCells{};
};

inline constexpr std::uint32_t StaticPoiRoomWarpMask = 0x00000FF0U;
inline constexpr std::uint32_t StaticPoiRoomWaypointMask = 0x00030000U;
inline constexpr std::uint32_t StaticPoiRoomHasRoomMask = 0x00100000U;
inline constexpr std::uint32_t StaticPoiRoomPresetUnitsAddedMask =
    0x02000000U;

enum class StaticPoiRoomAction : std::uint8_t {
    Ignore,
    Reuse,
    Materialize,
    Wait,
};

// Selects only rooms that can own an exact native exit or waypoint. The
// distant-name path never asks D2R to create an ActiveRoom.
[[nodiscard]] constexpr auto SelectStaticPoiRoomAction(
        std::uint32_t roomFlags,
        bool hasRoomTile,
        bool hasCompletePresetUnits,
        bool hasActiveRoom) noexcept -> StaticPoiRoomAction {
    const bool needsRoomTile =
        (roomFlags & StaticPoiRoomWarpMask) != 0U && !hasRoomTile;
    const bool needsPresetUnit =
        (roomFlags & StaticPoiRoomWaypointMask) != 0U
            && !hasCompletePresetUnits;
    if (!needsRoomTile && !needsPresetUnit) {
        return (roomFlags
                & (StaticPoiRoomWarpMask | StaticPoiRoomWaypointMask)) != 0U
            ? StaticPoiRoomAction::Reuse
            : StaticPoiRoomAction::Ignore;
    }
    if (hasActiveRoom || (roomFlags & StaticPoiRoomHasRoomMask) != 0U) {
        return StaticPoiRoomAction::Wait;
    }
    return StaticPoiRoomAction::Materialize;
}

struct StaticClientRoomLease final {
    void* room{};
    bool owned{};
};

inline constexpr std::int32_t UnknownRevealDifficulty = -1;
inline constexpr std::int32_t UnknownRevealLevelId = -1;
inline constexpr std::size_t RevealDifficultyCount = 3U;
inline constexpr std::size_t RevealPersistenceActCount = 5U;
inline constexpr std::size_t RevealPersistenceLevelCapacity = 256U;
inline constexpr std::size_t ProgressiveRevealVisCapacity = 8U;
inline constexpr std::size_t ProgressiveRevealLevelCapacity = 256U;

// Pure coordinator state shared with policy tests. A native automap
// observation may arrive while a readiness retry for the same level is
// already pending; that observation must upgrade the existing request rather
// than being discarded as a duplicate.
struct RevealReplayRequestState final {
    std::uint64_t sessionGeneration{};
    std::int32_t targetLevelId{UnknownRevealLevelId};
    std::uint32_t retriesRemaining{};
    bool playerReady{};
    bool automapObserved{};
    bool reconcilePending{};
    bool callbackQueued{};
};

[[nodiscard]] constexpr auto MergePendingRevealAutomapObservation(
        RevealReplayRequestState& pending,
        std::int32_t targetLevelId,
        bool automapObserved) noexcept -> bool {
    if (!pending.reconcilePending) return false;
    if (targetLevelId > 0 && pending.targetLevelId != targetLevelId) {
        return false;
    }
    pending.automapObserved = pending.automapObserved || automapObserved;
    return true;
}

[[nodiscard]] constexpr auto CanConfirmReplayedLevelReveal(
        bool automapObserved,
        RevealOutcome outcome) noexcept -> bool {
    return automapObserved && outcome == RevealOutcome::Complete;
}

struct RevealReplaySubmissionPolicy final {
    bool waitForAutomap{};
    bool submitWholeAct{};
    bool submitCurrentLevel{};
};

// Native reveal calls are UI-thread only and expensive. A real automap pass is
// the readiness witness. Whole-act work is scheduled once; after that work is
// accepted, a newly entered level still needs its own local fallback if it was
// not reached by the generated Vis graph.
[[nodiscard]] constexpr auto MakeRevealReplaySubmissionPolicy(
        bool automapObserved,
        bool revealWholeAct,
        bool actAccepted,
        bool levelAccepted) noexcept -> RevealReplaySubmissionPolicy {
    if (!automapObserved) {
        return {.waitForAutomap = true};
    }
    if (revealWholeAct) {
        return {
            .waitForAutomap = false,
            .submitWholeAct = !actAccepted,
            .submitCurrentLevel = actAccepted && !levelAccepted,
        };
    }
    return {
        .waitForAutomap = false,
        .submitWholeAct = false,
        .submitCurrentLevel = !levelAccepted,
    };
}

// Pure bounded breadth-first queue used by passive distant-name discovery.
// It stores stable LevelIds only. Native Level/Room pointers never survive an
// individual UI-thread callback.
class ProgressiveRevealGraphState final {
public:
    void Reset() noexcept {
        levels_.fill(UnknownRevealLevelId);
        levelCount_ = 0U;
        cursor_ = 0U;
    }

    [[nodiscard]] auto Begin(std::int32_t rootLevelId) noexcept -> bool {
        Reset();
        return Add(rootLevelId);
    }

    [[nodiscard]] auto Add(std::int32_t levelId) noexcept -> bool {
        if (levelId <= 0) return false;
        if (Contains(levelId)) return true;
        if (levelCount_ >= levels_.size()) return false;
        levels_[levelCount_++] = levelId;
        return true;
    }

    [[nodiscard]] auto Contains(std::int32_t levelId) const noexcept -> bool {
        for (std::size_t index = 0U; index < levelCount_; ++index) {
            if (levels_[index] == levelId) return true;
        }
        return false;
    }

    [[nodiscard]] auto HasCurrent() const noexcept -> bool {
        return cursor_ < levelCount_;
    }

    [[nodiscard]] auto Current() const noexcept -> std::int32_t {
        return HasCurrent() ? levels_[cursor_] : UnknownRevealLevelId;
    }

    [[nodiscard]] auto Advance() noexcept -> bool {
        if (!HasCurrent()) return false;
        ++cursor_;
        return true;
    }

    [[nodiscard]] auto LevelCount() const noexcept -> std::size_t {
        return levelCount_;
    }

    [[nodiscard]] auto LevelAt(std::size_t index) const noexcept
            -> std::int32_t {
        return index < levelCount_
            ? levels_[index]
            : UnknownRevealLevelId;
    }

    [[nodiscard]] auto Cursor() const noexcept -> std::size_t {
        return cursor_;
    }

private:
    std::array<std::int32_t, ProgressiveRevealLevelCapacity> levels_{};
    std::size_t levelCount_{};
    std::size_t cursor_{};
};

// Authoritative contiguous act boundaries from the shipped D2R 3.3 Levels.txt
// catalog (level ids 1..137). This lets reveal persistence bind an accepted
// request to the DRLG that was actually resolved, rather than guessing from a
// later lifecycle transition.
[[nodiscard]] constexpr auto RevealActForLevelId(
        std::int32_t levelId) noexcept -> std::int32_t {
    if (levelId >= 1 && levelId <= 39) return 0;
    if (levelId >= 40 && levelId <= 74) return 1;
    if (levelId >= 75 && levelId <= 102) return 2;
    if (levelId >= 103 && levelId <= 108) return 3;
    if (levelId >= 109 && levelId <= 137) return 4;
    return -1;
}

enum class RevealDifficultyObservation : std::uint8_t {
    Invalid,
    Initialized,
    Unchanged,
    Changed,
};

// Pure process-lifetime intent and per-session de-duplication state. It stores
// only stable ids, never DRLG pointers, generated coordinates, or automap bits.
// Callers must first observe a validated 0..2 client difficulty.
class RevealPersistenceState final {
public:
    void ResetProcess() noexcept {
        difficulty_ = UnknownRevealDifficulty;
        revealAll_ = false;
        rememberedActs_.fill(false);
        rememberedLevelCount_ = 0U;
        sessionGeneration_ = 0U;
        ResetSessionAcceptance();
    }

    void BeginSession(std::uint64_t sessionGeneration) noexcept {
        if (sessionGeneration_ == sessionGeneration) return;
        sessionGeneration_ = sessionGeneration;
        ResetSessionAcceptance();
    }

    [[nodiscard]] auto ObserveDifficulty(
            std::int32_t difficulty) noexcept
            -> RevealDifficultyObservation {
        if (!IsValidDifficulty(difficulty)) {
            return RevealDifficultyObservation::Invalid;
        }
        if (difficulty_ == UnknownRevealDifficulty) {
            difficulty_ = difficulty;
            return RevealDifficultyObservation::Initialized;
        }
        if (difficulty_ == difficulty) {
            return RevealDifficultyObservation::Unchanged;
        }
        difficulty_ = difficulty;
        revealAll_ = false;
        rememberedActs_.fill(false);
        rememberedLevelCount_ = 0U;
        ResetSessionAcceptance();
        return RevealDifficultyObservation::Changed;
    }

    [[nodiscard]] auto Difficulty() const noexcept -> std::int32_t {
        return difficulty_;
    }

    [[nodiscard]] auto SessionGeneration() const noexcept -> std::uint64_t {
        return sessionGeneration_;
    }

    [[nodiscard]] auto SetRevealAll(
            std::int32_t difficulty,
            bool armed) noexcept -> bool {
        if (!MatchesDifficulty(difficulty)) return false;
        revealAll_ = armed;
        return true;
    }

    void ClearRevealAll() noexcept {
        revealAll_ = false;
    }

    [[nodiscard]] auto IsRevealAllArmed(
            std::int32_t difficulty) const noexcept -> bool {
        return MatchesDifficulty(difficulty) && revealAll_;
    }

    [[nodiscard]] auto RememberLevel(
            std::int32_t difficulty,
            std::int32_t levelId) noexcept -> bool {
        if (!MatchesDifficulty(difficulty) || levelId <= 0) return false;
        return AddUnique(
            rememberedLevels_,
            rememberedLevelCount_,
            levelId);
    }

    [[nodiscard]] auto RememberAct(
            std::int32_t difficulty,
            std::int32_t act) noexcept -> bool {
        if (!MatchesDifficulty(difficulty) || !IsValidAct(act)) return false;
        rememberedActs_[static_cast<std::size_t>(act)] = true;
        return true;
    }

    [[nodiscard]] auto HasAnyIntent(
            std::int32_t difficulty) const noexcept -> bool {
        if (!MatchesDifficulty(difficulty)) return false;
        if (revealAll_ || rememberedLevelCount_ != 0U) return true;
        for (const bool remembered : rememberedActs_) {
            if (remembered) return true;
        }
        return false;
    }

    [[nodiscard]] auto HasAnyIntent() const noexcept -> bool {
        return difficulty_ != UnknownRevealDifficulty
            && HasAnyIntent(difficulty_);
    }

    [[nodiscard]] auto ShouldReplayLevel(
            std::int32_t difficulty,
            std::int32_t levelId) const noexcept -> bool {
        return sessionGeneration_ != 0U
            && MatchesDifficulty(difficulty)
            && Contains(
                rememberedLevels_,
                rememberedLevelCount_,
                levelId)
            && !Contains(
                acceptedLevels_,
                acceptedLevelCount_,
                levelId);
    }

    // Reveal Act and Reveal All are process-lifetime intents. Whole-act work is
    // de-duplicated per session independently from single-level requests.
    [[nodiscard]] auto HasReplayIntentForLevel(
            std::int32_t difficulty,
            std::int32_t act,
            std::int32_t levelId) const noexcept -> bool {
        if (sessionGeneration_ == 0U
            || !MatchesDifficulty(difficulty) || levelId <= 0) {
            return false;
        }
        const bool rememberedLevel = Contains(
            rememberedLevels_,
            rememberedLevelCount_,
            levelId);
        const bool rememberedAct = IsValidAct(act)
            && rememberedActs_[static_cast<std::size_t>(act)];
        return rememberedLevel || rememberedAct || revealAll_;
    }

    [[nodiscard]] auto ShouldReplayCurrentLevel(
            std::int32_t difficulty,
            std::int32_t act,
            std::int32_t levelId) const noexcept -> bool {
        if (!HasReplayIntentForLevel(difficulty, act, levelId)) return false;
        if (ShouldRevealWholeAct(difficulty, act)) {
            return !acceptedActs_[static_cast<std::size_t>(act)]
                || !Contains(
                    acceptedLevels_,
                    acceptedLevelCount_,
                    levelId);
        }
        return !Contains(acceptedLevels_, acceptedLevelCount_, levelId);
    }

    [[nodiscard]] auto ShouldRevealWholeAct(
            std::int32_t difficulty,
            std::int32_t act) const noexcept -> bool {
        if (!MatchesDifficulty(difficulty) || !IsValidAct(act)) return false;
        return revealAll_
            || rememberedActs_[static_cast<std::size_t>(act)];
    }

    [[nodiscard]] auto IsLevelAccepted(
            std::int32_t difficulty,
            std::int32_t levelId) const noexcept -> bool {
        return MatchesDifficulty(difficulty) && levelId > 0
            && Contains(
                acceptedLevels_,
                acceptedLevelCount_,
                levelId);
    }

    [[nodiscard]] auto IsActAccepted(
            std::int32_t difficulty,
            std::int32_t act) const noexcept -> bool {
        return MatchesDifficulty(difficulty) && IsValidAct(act)
            && acceptedActs_[static_cast<std::size_t>(act)];
    }

    [[nodiscard]] auto MarkLevelAccepted(
            std::int32_t difficulty,
            std::int32_t levelId) noexcept -> bool {
        if (sessionGeneration_ == 0U
            || !MatchesDifficulty(difficulty) || levelId <= 0) {
            return false;
        }
        return AddUnique(acceptedLevels_, acceptedLevelCount_, levelId);
    }

    [[nodiscard]] auto MarkActAccepted(
            std::int32_t difficulty,
            std::int32_t act) noexcept -> bool {
        if (sessionGeneration_ == 0U
            || !MatchesDifficulty(difficulty) || !IsValidAct(act)) {
            return false;
        }
        acceptedActs_[static_cast<std::size_t>(act)] = true;
        return true;
    }

private:
    static constexpr auto IsValidDifficulty(
            std::int32_t difficulty) noexcept -> bool {
        return difficulty >= 0
            && difficulty < static_cast<std::int32_t>(RevealDifficultyCount);
    }

    static constexpr auto IsValidAct(std::int32_t act) noexcept -> bool {
        return act >= 0
            && act < static_cast<std::int32_t>(RevealPersistenceActCount);
    }

    [[nodiscard]] auto MatchesDifficulty(
            std::int32_t difficulty) const noexcept -> bool {
        return IsValidDifficulty(difficulty) && difficulty_ == difficulty;
    }

    static auto Contains(
            const std::array<std::int32_t,
                RevealPersistenceLevelCapacity>& values,
            std::size_t count,
            std::int32_t value) noexcept -> bool {
        for (std::size_t index = 0U; index < count; ++index) {
            if (values[index] == value) return true;
        }
        return false;
    }

    static auto AddUnique(
            std::array<std::int32_t,
                RevealPersistenceLevelCapacity>& values,
            std::size_t& count,
            std::int32_t value) noexcept -> bool {
        if (Contains(values, count, value)) return true;
        if (count >= values.size()) return false;
        values[count++] = value;
        return true;
    }

    void ResetSessionAcceptance() noexcept {
        acceptedActs_.fill(false);
        acceptedLevelCount_ = 0U;
    }

    std::int32_t difficulty_{UnknownRevealDifficulty};
    bool revealAll_{};
    std::array<bool, RevealPersistenceActCount> rememberedActs_{};
    std::array<std::int32_t,
        RevealPersistenceLevelCapacity> rememberedLevels_{};
    std::size_t rememberedLevelCount_{};
    std::uint64_t sessionGeneration_{};
    std::array<bool, RevealPersistenceActCount> acceptedActs_{};
    std::array<std::int32_t,
        RevealPersistenceLevelCapacity> acceptedLevels_{};
    std::size_t acceptedLevelCount_{};
};

// Borrowed native view resolved from the client DRLG captured by the existing
// InitLevel hook. Pointers are valid only during the synchronous UI-thread
// operation that requested the view; callers must never retain them.
struct ClientLevelView final {
    std::uint8_t dataContext{};
    // Validated client DRLG difficulty: 0 Normal, 1 Nightmare, 2 Hell.
    std::uint8_t difficulty{};
    std::int32_t levelId{};
    // The original game seed stored by DRLG allocation, and the one-step LCG
    // value D2R uses as the base for each generated Level. Resolve succeeds
    // only when this pair matches the governed native seed contract.
    std::uint32_t mapSeed{};
    std::uint32_t drlgStartSeed{};
    void* activeRoom{};
    void* drlg{};
    void* level{};
};

inline constexpr std::uint64_t DrlgSeedMultiplier = UINT64_C(0x6AC690C5);
inline constexpr std::uint32_t DrlgSeedHighWord = 0x29AU;

[[nodiscard]] constexpr auto DeriveDrlgStartSeed(
        std::uint32_t mapSeed) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(mapSeed) * DrlgSeedMultiplier
        + DrlgSeedHighWord);
}

using RevealLevelInitializedCallback = void(*)(
    std::uint8_t dataContext,
    void* level,
    void* userData) noexcept;

auto InitializeRevealEngine(
    const D2RL::PluginContext* context,
    bool diagnostics) noexcept -> bool;
void ShutdownRevealEngine() noexcept;
// Observes the level only for the duration of MapSense's existing InitLevel
// hook. The callback must not retain the borrowed native pointer.
void SetRevealLevelInitializedCallback(
    RevealLevelInitializedCallback callback,
    void* userData) noexcept;
// Session resets clear native DRLG references and diagnostics but intentionally
// preserve process-lifetime Reveal Level, Reveal Act, and Reveal All intents.
// The plugin coordinator invalidates them on a validated difficulty change.
void BeginRevealSession() noexcept;
void ResetRevealSession() noexcept;

auto RevealCurrentZone() noexcept -> RevealOutcome;
// Legacy diagnostic actions retain D2RCore's public revealmap command. The
// product-facing Reveal Map toggle does not use this path; it publishes the
// generated atlas directly into D2R's native cell trees without rooms.
auto RevealCurrentAct() noexcept -> RevealOutcome;
auto ArmRevealAll() noexcept -> RevealOutcome;
auto ToggleRevealAll() noexcept -> RevealOutcome;
auto DisableRevealAll() noexcept -> RevealOutcome;

// Navigation and Reveal must inspect the same client-side generated map.
// These helpers centralize the governed DRLG_GetLevel/DRLG_InitLevel and
// DRLGROOM_CreateActiveRoom calls already owned and fingerprinted here.
[[nodiscard]] auto ResolveCurrentClientLevelView(
    ClientLevelView& output) noexcept -> bool;
[[nodiscard]] auto ResolveClientLevelById(
    const ClientLevelView& current,
    std::int32_t levelId,
    void*& outputLevel) noexcept -> bool;

// Accepts one validated, seed-scoped helper artifact on D2R's UI thread. Pump
// reads and revalidates D2R's already-active owner on every callback, then
// inserts only levels whose authoritative Levels.Layer equals that owner. It
// never creates, switches, retains or restores an automap owner. No ActiveRoom,
// Unit or monster table is touched.
[[nodiscard]] auto BeginNativeAutomapAtlasPublication(
    std::uint64_t sessionGeneration,
    std::shared_ptr<const ExternalAtlasGeometrySnapshot> snapshot,
    const ClientLevelView& current,
    std::int32_t resolvedAct) noexcept
    -> NativeAutomapAtlasPublicationStatus;
[[nodiscard]] auto PumpNativeAutomapAtlasPublication(
    const ClientLevelView& current,
    std::uint32_t maximumCells) noexcept
    -> NativeAutomapAtlasPublicationStatus;
[[nodiscard]] auto QueryNativeAutomapAtlasPublication(
    std::uint64_t sessionGeneration,
    const ClientLevelView& current,
    std::int32_t resolvedAct) noexcept
    -> NativeAutomapAtlasPublicationStatus;
// Called only from MapSense's existing native automap-pass observer. It
// atomically resumes a matching publisher that was sleeping for D2R's owner;
// no native pointer is retained or dereferenced here.
[[nodiscard]] auto TryWakeNativeAutomapAtlasPublication(
    std::uint64_t sessionGeneration,
    std::int32_t observedLevelId) noexcept -> bool;
// Called after D2R's original local-player automap render pass. A generated
// layer is not credited as ready until bounded exact keys are still present in
// the same native floor/wall owner that D2R just rendered.
[[nodiscard]] auto ObserveNativeAutomapAtlasPublication(
    std::uint64_t sessionGeneration,
    std::int32_t observedLevelId) noexcept
    -> NativeAutomapAtlasPublicationStatus;
void ResetNativeAutomapAtlasPublication(
    std::uint64_t sessionGeneration) noexcept;
[[nodiscard]] auto AcquireNativeAutomapLayerCatalog() noexcept
    -> std::shared_ptr<const NativeAutomapLayerCatalog>;
[[nodiscard]] auto GetNativeAutomapAtlasCounters() noexcept
    -> NativeAutomapAtlasCounters;
[[nodiscard]] auto MaterializeClientRoom(
    std::uint8_t dataContext,
    void* drlgRoom) noexcept -> void*;
// Builds only DrlgRoom static tile/preset descriptors. It deliberately stops
// before D2R's ActiveRoom allocator and returns an owned lease that must be
// released immediately after the descriptors have been copied.
[[nodiscard]] auto PrepareStaticClientRoom(
    std::uint8_t dataContext,
    void* drlgRoom,
    StaticClientRoomLease& lease) noexcept -> bool;
[[nodiscard]] auto ReleaseStaticClientRoom(
    StaticClientRoomLease& lease) noexcept -> bool;
auto IsRevealEngineActive() noexcept -> bool;
auto IsRevealAllArmed() noexcept -> bool;
auto GetRevealCounters() noexcept -> RevealCounters;

} // namespace RuffnecKk::MapSense
