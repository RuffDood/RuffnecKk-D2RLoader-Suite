#pragma once

#include "external_atlas_geometry.hpp"
#include "reveal_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

class MapSenseDataCatalog;

// Pure coordinator policy shared by the runtime provider and its unit tests.
// No D2R pointer, Win32 handle, process or renderer state is owned here.
enum class ExternalLabelProviderOperation : std::uint8_t {
    None,
    Labels,
    PrimaryGeometry,
    PrewarmGeometry,
};

[[nodiscard]] constexpr auto ExternalLabelProviderOperationName(
        ExternalLabelProviderOperation operation) noexcept -> const char* {
    switch (operation) {
        case ExternalLabelProviderOperation::None:
            return "none";
        case ExternalLabelProviderOperation::Labels:
            return "labels";
        case ExternalLabelProviderOperation::PrimaryGeometry:
            return "primary-geometry";
        case ExternalLabelProviderOperation::PrewarmGeometry:
            return "prewarm-geometry";
    }
    return "unknown";
}

inline constexpr std::uint32_t ExternalLabelHelperTimeoutMilliseconds = 5'000U;
inline constexpr std::uint32_t ExternalGeometryHelperTimeoutMilliseconds =
    30'000U;

[[nodiscard]] constexpr auto ExternalLabelProviderOperationTimeoutMilliseconds(
        ExternalLabelProviderOperation operation) noexcept -> std::uint32_t {
    switch (operation) {
        case ExternalLabelProviderOperation::Labels:
            return ExternalLabelHelperTimeoutMilliseconds;
        case ExternalLabelProviderOperation::PrimaryGeometry:
        case ExternalLabelProviderOperation::PrewarmGeometry:
            return ExternalGeometryHelperTimeoutMilliseconds;
        case ExternalLabelProviderOperation::None:
            return 0U;
    }
    return 0U;
}

struct ExternalLabelProviderRequestIdentity final {
    std::uint64_t sessionGeneration{};
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::int32_t act{};
    std::int32_t currentLevelId{};
    std::uint64_t dataFingerprint{};

    [[nodiscard]] constexpr auto operator==(
        const ExternalLabelProviderRequestIdentity&) const noexcept
        -> bool = default;
};

enum class ExternalLabelProviderSubmission : std::uint8_t {
    Duplicate,
    Queue,
    QueueAndCancel,
};

[[nodiscard]] constexpr auto DecideExternalLabelProviderSubmission(
        const ExternalLabelProviderRequestIdentity& incoming,
        const std::optional<ExternalLabelProviderRequestIdentity>& published,
        const std::optional<ExternalLabelProviderRequestIdentity>& inFlight,
        const std::optional<ExternalLabelProviderRequestIdentity>& pending,
        bool failed,
        ExternalLabelProviderOperation activeOperation) noexcept
        -> ExternalLabelProviderSubmission {
    if ((published && *published == incoming)
        || (inFlight && *inFlight == incoming)
        || (pending && *pending == incoming)
        || failed) {
        return ExternalLabelProviderSubmission::Duplicate;
    }
    return activeOperation == ExternalLabelProviderOperation::None
        ? ExternalLabelProviderSubmission::Queue
        : ExternalLabelProviderSubmission::QueueAndCancel;
}

[[nodiscard]] constexpr auto ShouldContinueExternalAtlasPrewarm(
        bool stopRequested,
        bool hasPendingRequest,
        std::uint64_t requestSerial,
        std::uint64_t latestSerial,
        std::uint64_t requestSessionGeneration,
        std::uint64_t activeSessionGeneration) noexcept -> bool {
    return !stopRequested && !hasPendingRequest
        && requestSerial == latestSerial
        && requestSessionGeneration == activeSessionGeneration;
}

enum class ExternalLabelProviderCompletion : std::uint8_t {
    Stale,
    Failed,
    Publish,
};

[[nodiscard]] constexpr auto DecideExternalLabelProviderCompletion(
        bool requestIsCurrent,
        bool labelsReady,
        bool geometryReady,
        bool snapshotReady) noexcept -> ExternalLabelProviderCompletion {
    if (!requestIsCurrent) return ExternalLabelProviderCompletion::Stale;
    return labelsReady && geometryReady && snapshotReady
        ? ExternalLabelProviderCompletion::Publish
        : ExternalLabelProviderCompletion::Failed;
}

struct ExternalAtlasTopologyEdge final {
    std::int32_t sourceLevelId{};
    std::int32_t targetLevelId{};
    // 0 is a warp/entrance into another map space. 1 is a continuous outdoor
    // seam whose two levels share one automap coordinate space.
    std::int32_t kind{};
};

inline constexpr std::size_t ExternalAtlasLevelCapacity = 512U;
inline constexpr std::size_t ExternalAtlasEdgeCapacity = 16'384U;

struct ExternalPhysicalSeamAnchor final {
    std::int32_t subtileX{};
    std::int32_t subtileY{};
};

// The helper response itself is the authoritative membership list for the
// generated act. A warp may deliberately target a disconnected level outside
// that list (including a mod-defined or cross-act level), while a continuous
// seam may not: both sides of a seam must share this atlas coordinate space.
// `sortedActLevelIds` must be sorted and contain no duplicates.
[[nodiscard]] inline auto IsExternalAtlasTopologyEdgeValid(
        std::span<const std::int32_t> sortedActLevelIds,
        const ExternalAtlasTopologyEdge& edge) noexcept -> bool {
    if (edge.sourceLevelId <= 0 || edge.targetLevelId <= 0
        || (edge.kind != 0 && edge.kind != 1)) {
        return false;
    }
    const auto owns = [sortedActLevelIds](std::int32_t levelId) noexcept {
        return std::binary_search(
            sortedActLevelIds.begin(),
            sortedActLevelIds.end(),
            levelId);
    };
    return owns(edge.sourceLevelId)
        && (edge.kind == 0 || owns(edge.targetLevelId));
}

// MS1 v3 retains the v2 collision contract and adds exact generated red-portal
// anchors. It is deliberately not backward-compatible with the former
// room-centre sample stream. One directed level pair must carry exactly one collision-
// proven opening; multiple samples are rejected instead of averaged.
[[nodiscard]] inline auto SelectUniqueExternalPhysicalSeamAnchor(
        std::span<const std::pair<std::int32_t, std::int32_t>> anchors,
        ExternalPhysicalSeamAnchor& output) noexcept -> bool {
    output = {};
    if (anchors.size() != 1U) return false;
    output = {
        .subtileX = anchors.front().first,
        .subtileY = anchors.front().second,
    };
    return output.subtileX >= 0 && output.subtileY >= 0;
}

// Returns only the levels that share the current level's continuous automap
// space. Warp targets deliberately remain outside this set and are presented
// at their source entrance instead of importing their unrelated coordinates.
[[nodiscard]] inline auto CollectExternalAtlasVisibleLevels(
    std::int32_t currentLevelId,
    const ExternalAtlasTopologyEdge* edges,
    std::size_t edgeCount,
    std::vector<std::int32_t>& output) -> bool {
    output.clear();
    if (currentLevelId <= 0
        || (edgeCount != 0U && edges == nullptr)
        || edgeCount > ExternalAtlasEdgeCapacity) {
        return false;
    }
    output.push_back(currentLevelId);
    for (;;) {
        const auto before = output.size();
        for (std::size_t index = 0U; index < edgeCount; ++index) {
            const auto& edge = edges[index];
            if (edge.kind != 1 || edge.sourceLevelId <= 0
                || edge.targetLevelId <= 0) {
                continue;
            }
            const bool sourceVisible = std::find(
                output.begin(), output.end(), edge.sourceLevelId)
                != output.end();
            const bool targetVisible = std::find(
                output.begin(), output.end(), edge.targetLevelId)
                != output.end();
            if (sourceVisible && !targetVisible) {
                output.push_back(edge.targetLevelId);
            } else if (targetVisible && !sourceVisible) {
                output.push_back(edge.sourceLevelId);
            }
            if (output.size() > ExternalAtlasLevelCapacity) {
                output.clear();
                return false;
            }
        }
        if (output.size() == before) break;
    }
    std::sort(output.begin(), output.end());
    output.erase(
        std::unique(output.begin(), output.end()),
        output.end());
    return true;
}

struct ExternalLabelProviderCounters final {
    std::uint64_t requests{};
    std::uint64_t processesStarted{};
    std::uint64_t atlasesPublished{};
    std::uint64_t staleResponses{};
    std::uint64_t failedResponses{};
    std::uint64_t timeouts{};
    std::uint64_t labelTimeouts{};
    std::uint64_t geometryTimeouts{};
    std::uint64_t primaryCancellations{};
    std::uint64_t prewarmCancellations{};
    std::uint64_t geometryCacheHits{};
    std::uint64_t geometryCacheMisses{};
    std::uint64_t geometryCacheInvalid{};
    std::uint64_t geometryAtlasesGenerated{};
    std::uint64_t geometryFailures{};
    std::uint64_t geometrySnapshotsPublished{};
    std::uint64_t publishedGeometryCells{};
    ExternalLabelProviderOperation activeOperation{
        ExternalLabelProviderOperation::None};
    std::uint32_t activeSeed{};
    std::int32_t activeAct{-1};
};

// Immutable seed-scoped geometry for one generated act. The worker owns
// generation and validation; the UI-thread native publisher consumes this
// value in bounded batches and resolves every level's authoritative
// Levels.Layer before touching D2R's cell trees.
struct ExternalAtlasGeometrySnapshot final {
    std::uint64_t sessionGeneration{};
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::uint8_t act{};
    std::int32_t currentLevelId{};
    // Active Levels.txt membership used to resolve native Layers for both
    // helper-generated and mod-added labels. A member need not have external
    // terrain geometry; exact runtime entrances remain independently valid.
    std::vector<std::int32_t> visibleLevelIds;
    std::shared_ptr<const ExternalAtlasGeometry> geometry;
};

using ExternalLabelAtlasResultCallback = void(*)(
    std::uint64_t sessionGeneration,
    std::uint8_t difficulty,
    std::int32_t act,
    std::int32_t currentLevelId,
    bool published,
    void* userData) noexcept;

// Starts one private worker. The helper is launched hidden and only from that
// worker; D2R's gameplay, UI and render threads never wait for map generation.
[[nodiscard]] auto InitializeExternalLabelProvider(
    const D2RL::PluginContext* context,
    bool diagnostics,
    ExternalLabelAtlasResultCallback resultCallback,
    void* resultUserData) noexcept -> bool;
void ShutdownExternalLabelProvider() noexcept;

// Invalidates pending work and its cache. A completed response can publish
// only when both this generation and the most recent request serial still
// match.
void ResetExternalLabelProviderSession(
    std::uint64_t sessionGeneration) noexcept;

// Copies only the current Level's already-generated room rectangles, then
// queues a seed-scoped act atlas. No ActiveRoom, collision, unit or monster is
// created by this operation.
[[nodiscard]] auto RequestExternalLabelAtlas(
    std::uint64_t sessionGeneration,
    const ClientLevelView& current,
    std::int32_t resolvedAct,
    std::shared_ptr<const MapSenseDataCatalog> dataCatalog) noexcept -> bool;

[[nodiscard]] auto IsExternalLabelProviderActive() noexcept -> bool;
[[nodiscard]] auto AcquireExternalAtlasGeometrySnapshot() noexcept
    -> std::shared_ptr<const ExternalAtlasGeometrySnapshot>;
[[nodiscard]] auto WantsExternalAtlasGeometryFrame() noexcept -> bool;
[[nodiscard]] auto HasPersistedExternalRevealMapIntent(
    std::uint32_t seed,
    std::uint8_t difficulty) noexcept -> bool;
[[nodiscard]] auto SetPersistedExternalRevealMapIntent(
    std::uint32_t seed,
    std::uint8_t difficulty,
    bool enabled) noexcept -> bool;
[[nodiscard]] auto GetExternalLabelProviderCounters() noexcept
    -> ExternalLabelProviderCounters;

} // namespace RuffnecKk::MapSense
