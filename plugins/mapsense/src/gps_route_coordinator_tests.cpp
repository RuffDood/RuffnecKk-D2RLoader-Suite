#include "gps_route_coordinator.hpp"
#include "gps_route_readiness.hpp"
#include "native_automap_marker.hpp"
#include "navigation_policy.hpp"
#include "navigation_routing.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

using namespace RuffnecKk::MapSense;

namespace {
std::vector<GpsRouteProviderRequest> Requests;
std::vector<GpsRouteProviderPath> Completed;
std::vector<NavigationNativePoint> ProjectedClients;
std::uint64_t Now{100U};
std::size_t Submissions{};
std::size_t PathAcquisitions{};
std::size_t Published{};
std::size_t LastPreSubmissionCount{};
GpsRouteReadiness LastPreReadiness{GpsRouteReadiness::SourceInactive};
GpsRouteReadiness LastPostReadiness{GpsRouteReadiness::SourceInactive};

struct ControlledGpsPrerequisites final {
    bool hasGeometry{true};
    bool hasGeometryPayload{true};
    bool hasCatalog{true};
    std::uint64_t currentSession{41U};
    std::uint64_t currentPolicy{};
    std::uint64_t geometrySession{41U};
    std::uint64_t geometryDigest{1U};
    std::int32_t geometryLevel{4};
    std::uint8_t geometryDifficulty{};
};

ControlledGpsPrerequisites Prerequisites{};

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

auto Project(void*, std::int32_t x, std::int32_t y,
        NavigationNativePoint& output) noexcept -> bool {
    ProjectedClients.push_back({x, y});
    output = {x, y};
    return true;
}

auto EvaluatePumpReadiness(
        const NavigationGpsRouteSourceSnapshot& source,
        const NavigationGpsSourceDiagnostics& sourceDiagnostics) noexcept
        -> GpsRouteReadiness {
    const auto currentPolicy = Prerequisites.currentPolicy == 0U
        ? source.policy.revision
        : Prerequisites.currentPolicy;
    return EvaluateGpsRouteReadiness({
        .sourceReadiness = sourceDiagnostics.readiness,
        .hasGeometry = Prerequisites.hasGeometry,
        .hasGeometryPayload = Prerequisites.hasGeometryPayload,
        .hasCatalog = Prerequisites.hasCatalog,
        .sourceSession = sourceDiagnostics.sessionGeneration,
        .currentSession = Prerequisites.currentSession,
        .sourcePolicy = source.policy.revision,
        .currentPolicy = currentPolicy,
        .geometrySession = Prerequisites.geometrySession,
        .geometryDigest = Prerequisites.geometryDigest,
        .sourceLevel = sourceDiagnostics.levelId,
        .geometryLevel = Prerequisites.geometryLevel,
        .geometryDifficulty = Prerequisites.geometryDifficulty,
    });
}

void Pump(std::int32_t, bool, NativeAutomapObservationPhase phase, void*) noexcept {
    NavigationGpsSourceDiagnostics sourceDiagnostics{};
    GpsRouteCoordinatorInput input{
        .seed = 123U, .difficulty = 0U, .terrainRevision = 1U,
        .nowMilliseconds = Now,
        .allowRequestSubmission =
            phase == NativeAutomapObservationPhase::AfterObservation,
    };
    (void)AcquireNavigationGpsRouteSourceSnapshot(
        input.source, &sourceDiagnostics);
    const auto readiness = EvaluatePumpReadiness(input.source, sourceDiagnostics);
    if (phase == NativeAutomapObservationPhase::BeforeProjection) {
        LastPreReadiness = readiness;
    } else {
        LastPostReadiness = readiness;
    }
    if (readiness == GpsRouteReadiness::Ready) {
        GpsRouteCoordinatorResult result{};
        Check(TickGpsRouteCoordinator(input, result), "coordinator tick failed");
        if (result.published) Published = result.publishedPathCount;
    }
    if (phase == NativeAutomapObservationPhase::BeforeProjection) {
        LastPreSubmissionCount = Submissions;
    }
}

void CheckReadinessClassifier() {
    const GpsRouteReadinessInput ready{
        .sourceReadiness = NavigationGpsSourceReadiness::Ready,
        .hasGeometry = true,
        .hasGeometryPayload = true,
        .hasCatalog = true,
        .sourceSession = 41U,
        .currentSession = 41U,
        .sourcePolicy = 7U,
        .currentPolicy = 7U,
        .geometrySession = 41U,
        .geometryDigest = 1U,
        .sourceLevel = 4,
        .geometryLevel = 4,
        .geometryDifficulty = 0U,
    };
    struct ReadinessCase final {
        GpsRouteReadinessInput input;
        GpsRouteReadiness expected;
    };
    const std::array sourceCases{
        ReadinessCase{{.sourceReadiness = NavigationGpsSourceReadiness::Inactive},
            GpsRouteReadiness::SourceInactive},
        ReadinessCase{{.sourceReadiness = NavigationGpsSourceReadiness::Contended},
            GpsRouteReadiness::SourceContended},
        ReadinessCase{{.sourceReadiness = NavigationGpsSourceReadiness::UnknownLevel},
            GpsRouteReadiness::SourceUnknownLevel},
        ReadinessCase{{.sourceReadiness = NavigationGpsSourceReadiness::NoDestinations},
            GpsRouteReadiness::NoDestinations},
        ReadinessCase{{.sourceReadiness = NavigationGpsSourceReadiness::NoPlayer},
            GpsRouteReadiness::NoPlayer},
        ReadinessCase{{.sourceReadiness = NavigationGpsSourceReadiness::Exception},
            GpsRouteReadiness::SourceException},
    };
    for (const auto& test : sourceCases) {
        Check(EvaluateGpsRouteReadiness(test.input) == test.expected,
            "source readiness classification changed");
        Check(GpsRouteReadinessName(test.expected)[0] != '\0',
            "source readiness classification lost its diagnostic name");
    }

    const std::array readinessCases{
        ReadinessCase{[&] { auto input = ready; input.hasGeometry = false; return input; }(),
            GpsRouteReadiness::NoGeometry},
        ReadinessCase{[&] { auto input = ready; input.hasGeometryPayload = false; return input; }(),
            GpsRouteReadiness::NoGeometryPayload},
        ReadinessCase{[&] { auto input = ready; input.hasCatalog = false; return input; }(),
            GpsRouteReadiness::NoCatalog},
        ReadinessCase{[&] { auto input = ready; input.currentPolicy = 8U; return input; }(),
            GpsRouteReadiness::PolicyMismatch},
        ReadinessCase{[&] { auto input = ready; input.currentSession = 42U; return input; }(),
            GpsRouteReadiness::SessionMismatch},
        ReadinessCase{[&] { auto input = ready; input.geometryLevel = 5; return input; }(),
            GpsRouteReadiness::LevelMismatch},
        ReadinessCase{[&] { auto input = ready; input.geometrySession = 42U; return input; }(),
            GpsRouteReadiness::GeometrySessionMismatch},
        ReadinessCase{[&] { auto input = ready; input.geometryDifficulty = 3U; return input; }(),
            GpsRouteReadiness::InvalidDifficulty},
        ReadinessCase{[&] { auto input = ready; input.geometryDigest = 0U; return input; }(),
            GpsRouteReadiness::InvalidDigest},
    };
    for (const auto& test : readinessCases) {
        Check(EvaluateGpsRouteReadiness(test.input) == test.expected,
            "GPS prerequisite readiness classification changed");
        Check(GpsRouteReadinessName(test.expected)[0] != '\0',
            "GPS prerequisite readiness lost its diagnostic name");
    }
    Check(EvaluateGpsRouteReadiness(ready) == GpsRouteReadiness::Ready,
        "ready GPS prerequisites were rejected");
}

void CompleteHelperRequest(std::size_t requestIndex,
        std::size_t moveCount = 2U) {
    Check(moveCount >= 2U, "completed route fixture requires two points");
    Check(requestIndex < Requests.size(), "completed route fixture index is invalid");
    auto identity = Requests[requestIndex].identity;
    identity.artifact.dataFingerprint = 123U;
    GpsRouteProviderPath path{};
    path.identity = identity;
    path.moves.reserve(moveCount);
    path.moves.push_back({
        identity.artifact.fromSubtileX,
        identity.artifact.fromSubtileY});
    for (std::size_t index = 2U; index < moveCount; ++index) {
        path.moves.push_back({
            identity.artifact.fromSubtileX,
            identity.artifact.fromSubtileY});
    }
    path.moves.push_back({
        identity.artifact.toSubtileX,
        identity.artifact.toSubtileY});
    Completed.push_back(std::move(path));
}

void CompleteHelperBatch(std::size_t moveCount = 2U) {
    Completed.clear();
    for (std::size_t index = 0U; index < Requests.size(); ++index) {
        CompleteHelperRequest(index, moveCount);
    }
}

void CheckQolWalkingShortcuts() {
    struct Shortcut final {
        std::int32_t from;
        std::int32_t to;
        NavigationLineKind kind;
        bool town{};
    };
    const std::array shortcuts{
        Shortcut{4, 5, NavigationLineKind::Progression},
        Shortcut{20, 25, NavigationLineKind::Quest},
        Shortcut{28, 32, NavigationLineKind::Progression},
        Shortcut{33, 35, NavigationLineKind::Progression},
        Shortcut{35, 37, NavigationLineKind::Progression},
        Shortcut{40, 49, NavigationLineKind::Quest, true},
        Shortcut{43, 64, NavigationLineKind::Quest},
        Shortcut{40, 74, NavigationLineKind::Progression, true},
        Shortcut{78, 91, NavigationLineKind::Quest},
        Shortcut{75, 83, NavigationLineKind::Progression, true},
        Shortcut{83, 101, NavigationLineKind::Progression},
        Shortcut{103, 107, NavigationLineKind::Progression, true},
        Shortcut{113, 118, NavigationLineKind::Progression},
        Shortcut{120, 129, NavigationLineKind::Progression},
        Shortcut{129, 131, NavigationLineKind::Progression},
    };
    for (const auto shortcut : shortcuts) {
        ResetGpsRouteCoordinator(41U);
        ResetNavigationSession(41U);
        ResetNavigationLevel(41U, shortcut.from);
        NavigationRoutingOptions options{};
        options.townShortcuts = shortcut.town;
        SetNavigationTownShortcutsEnabled(shortcut.town);
        const auto plan = BuildAdaptiveNavigationRoutePlan(
            shortcut.from, std::array{shortcut.to}, options, shortcut.town);
        const std::array exits{NavigationExitCandidate{
            .destinationId = 101U, .targetLevelId = shortcut.to,
            .subtileX = 40, .subtileY = 10}};
        std::array<NavigationSubtileDestination, 8U> destinations{};
        const auto count = BuildNavigationDestinations({
            .currentLevelId = shortcut.from, .inTown = shortcut.town,
            .exits = exits, .routePlan = &plan}, destinations);
        Check(count == 1U && destinations[0].kind == shortcut.kind,
            "QoL shortcut lost its adapted destination or line family");
        auto policy = AcquireNavigationLinePolicySnapshot();
        for (auto& family : policy.families) family.enabled = false;
        policy.families[NavigationLineKindIndex(shortcut.kind)] = {
            true, NavigationLineMode::GpsWalk};
        (void)PublishNavigationLinePolicy(policy.families);
        Check(AcquireNavigationLinePolicySnapshot().families == policy.families,
            "QoL GPS Walk policy failed");
        Check(PublishNavigationDestinations(41U, shortcut.from,
            destinations.data(), count, plan.allowTownShortcut), "QoL destination publication failed");
        Prerequisites = {};
        Prerequisites.geometryLevel = shortcut.from;
        NavigationNativePoint client{};
        Check(ConvertNavigationSubtileToClientCoordinates(10, 10, client), "QoL player conversion failed");
        NavigationAutomapPass pass{
            .currentLevelId = shortcut.from, .inTown = shortcut.town,
            .playerClientX = client.x, .playerClientY = client.y,
            .hasPlayerSubtile = true, .playerSubtile = {10, 10},
            .nativeWidth = 4000, .nativeHeight = 4000,
            .clipLeft = -2000, .clipTop = -2000, .clipWidth = 4000, .clipHeight = 4000,
            .projectClient = &Project, .borrowedAutomapContext = &client};
        (void)ObserveNavigationAutomapPass(pass);
        Pump(shortcut.from, shortcut.town, NativeAutomapObservationPhase::AfterObservation, nullptr);
        Check(Requests.size() == 1U
            && Requests[0].identity.artifact.mode == GpsRouteMode::Walk
            && Requests[0].identity.artifact.levelId == shortcut.from
            && Requests[0].identity.artifact.toSubtileX == 40,
            "QoL GPS request did not retain the observed entrance in the current level");
        CompleteHelperBatch();
        // The helper's bounded standable approach can end just before a marker.
        Completed[0].moves.back().subtileX = 38;
        auto grid = std::make_shared<GpsRouteWalkGrid>();
        grid->width = grid->height = 128U;
        grid->bits.assign(128U * 128U / 8U, 0xffU);
        Completed[0].walkGrid = grid;
        Pump(shortcut.from, shortcut.town, NativeAutomapObservationPhase::BeforeProjection, nullptr);
        (void)ObserveNavigationAutomapPass(pass);
        std::vector<NavigationGpsRouteSegmentSnapshot> segments;
        Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U
            && segments[0].kind == shortcut.kind
            && segments[0].mode == NavigationGpsRouteMode::Walk,
            "QoL GPS walking path did not reach projection with the correct family");
        pass.playerSubtile = {12, 10};
        Check(ConvertNavigationSubtileToClientCoordinates(12, 10, client), "QoL moved player conversion failed");
        pass.playerClientX = client.x;
        pass.playerClientY = client.y;
        (void)ObserveNavigationAutomapPass(pass);
        Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U,
            "QoL walking guidance disappeared after moving along the route");
        ResetNavigationLevel(41U, shortcut.to);
        Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
            "QoL walking route survived a transition into its destination area");
    }
    std::cout << "QoL GPS Walk/Run: 15 shortcut destination/request/follow/projection cases passed\n";
}
} // namespace

// Controlled passive provider: only the test completes work. Coordinator and
// navigation publication/projection are the production implementations.
namespace RuffnecKk::MapSense {
auto InitializeGpsRouteProvider(const D2RL::PluginContext*) noexcept -> bool {
    Requests.clear();
    Completed.clear();
    PathAcquisitions = 0U;
    return true;
}
void ShutdownGpsRouteProvider() noexcept {}
void ResetGpsRouteProviderSession(std::uint64_t) noexcept {
    Requests.clear();
    Completed.clear();
}
auto SubmitGpsRouteProviderRequests(
        std::span<const GpsRouteProviderRequest> requests) noexcept -> bool {
    Requests.assign(requests.begin(), requests.end());
    Completed.clear();
    ++Submissions;
    return true;
}
auto AcquireGpsRouteProviderPaths(
        std::vector<GpsRouteProviderPath>& paths) noexcept -> bool {
    ++PathAcquisitions;
    paths = Completed;
    return !paths.empty();
}
auto GetGpsRouteProviderStatus() noexcept -> GpsRouteProviderStatus {
    return EvaluateGpsRouteProviderAggregateStatus(
        Requests,
        [](const GpsRouteProviderRequest& request) noexcept {
            return std::any_of(Completed.begin(), Completed.end(),
                [&request](const GpsRouteProviderPath& path) noexcept {
                    return SameGpsRoutePublicationScope(
                        request.identity, path.identity);
                });
        },
        [](const GpsRouteProviderRequest&) noexcept { return false; });
}
auto AcquireGpsRouteProviderStatuses(
        std::span<GpsRouteProviderStatus> statuses) noexcept -> bool {
    std::fill(statuses.begin(), statuses.end(), GetGpsRouteProviderStatus());
    return true;
}
} // namespace RuffnecKk::MapSense

int main() {
    InitializeNavigationEngine();
    Check(InitializeGpsRouteCoordinator(nullptr), "initialization failed");
    ResetNavigationSession(41U);
    ResetNavigationLevel(41U, 4);
    CheckReadinessClassifier();
    auto policy = AcquireNavigationLinePolicySnapshot();
    policy.families[NavigationLineKindIndex(NavigationLineKind::Waypoint)] = {
        true, NavigationLineMode::GpsWalk};
    Check(PublishNavigationLinePolicy(policy.families), "policy failed");

    // Match a completed resolver refresh that has no eligible destination.
    Check(PublishNavigationDestinations(41U, 4, nullptr, 0U),
        "empty completed navigation batch was rejected");
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::NoDestinations
        && Requests.empty() && Submissions == 0U,
        "empty completed navigation batch must not submit GPS work");
    NavigationGpsRouteSourceSnapshot source{};
    NavigationGpsSourceDiagnostics sourceDiagnostics{};
    Check(!AcquireNavigationGpsRouteSourceSnapshot(source, &sourceDiagnostics)
        && !AcquireNavigationGpsRouteSourceSnapshot(source)
        && sourceDiagnostics.readiness
            == NavigationGpsSourceReadiness::NoDestinations,
        "legacy source acquisition must preserve no-destination failure");

    const NavigationPointCandidate startupWaypoint{
        .destinationId = 99U, .subtileX = 12, .subtileY = 4,
    };
    std::array<NavigationSubtileDestination, MaximumNavigationDestinations>
        startupDestinations{};
    const auto startupDestinationCount = BuildNavigationDestinations(
        {.currentLevelId = 4, .waypoint = &startupWaypoint}, startupDestinations);
    Check(startupDestinationCount == 1U
        && startupDestinations.front().kind == NavigationLineKind::Waypoint,
        "production navigation policy did not build a startup destination");
    Check(PublishNavigationDestinations(41U, 4,
        startupDestinations.data(), startupDestinationCount),
        "startup navigation destination was not published");
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::NoPlayer
        && Requests.empty() && Submissions == 0U,
        "GPS must wait for a native player observation before submission");
    Check(!AcquireNavigationGpsRouteSourceSnapshot(source, &sourceDiagnostics)
        && !AcquireNavigationGpsRouteSourceSnapshot(source)
        && sourceDiagnostics.readiness == NavigationGpsSourceReadiness::NoPlayer,
        "legacy source acquisition must preserve no-player failure");

    std::uint8_t context{};
    NavigationAutomapPass pass{
        // The native automap projection uses raw client coordinates. GPS uses
        // only the explicit dynamic-path origin; it must never reverse this
        // deliberately off-lattice projection point.
        .currentLevelId = 4, .playerClientX = 161, .playerClientY = 240,
        .hasPlayerSubtile = true, .playerSubtile = {20, 10},
        .nativeWidth = 800, .nativeHeight = 600,
        .clipWidth = 800, .clipHeight = 600,
        .projectClient = Project, .borrowedAutomapContext = &context,
    };
    const auto observe = [&]() noexcept {
        return ObserveNavigationAutomapPass(pass);
    };
    Prerequisites.hasGeometry = false;
    ProjectedClients.clear();
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(LastPreReadiness == GpsRouteReadiness::NoPlayer
        && LastPostReadiness == GpsRouteReadiness::NoGeometry
        && Requests.empty() && Submissions == 0U,
        "explicit native origin must clear NoPlayer even when client coordinates are off-lattice");
    Check(std::any_of(ProjectedClients.begin(), ProjectedClients.end(),
        [](const NavigationNativePoint point) noexcept {
            return point.x == 161 && point.y == 240;
        }), "native projection did not receive the raw off-lattice client point");
    Check(AcquireNavigationGpsRouteSourceSnapshot(source, &sourceDiagnostics)
        && AcquireNavigationGpsRouteSourceSnapshot(source)
        && sourceDiagnostics.readiness == NavigationGpsSourceReadiness::Ready,
        "legacy source acquisition must remain ready after a player pass");

    Prerequisites.hasGeometry = true;
    Prerequisites.hasGeometryPayload = false;
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::NoGeometryPayload
        && Submissions == 0U,
        "missing geometry payload must suppress GPS requests");
    Prerequisites.hasGeometryPayload = true;
    Prerequisites.hasCatalog = false;
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::NoCatalog && Submissions == 0U,
        "missing catalog must suppress GPS requests");
    Prerequisites.hasCatalog = true;
    Prerequisites.currentPolicy = AcquireNavigationLinePolicySnapshot().revision + 1U;
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::PolicyMismatch
        && Submissions == 0U,
        "policy mismatch must suppress GPS requests");
    Prerequisites.currentPolicy = 0U;
    Prerequisites.geometrySession = 42U;
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::GeometrySessionMismatch
        && Submissions == 0U,
        "geometry session mismatch must suppress GPS requests");
    Prerequisites.geometrySession = 41U;

    const std::array destinations{
        NavigationSubtileDestination{.destinationId = 1U,
            .subtileX = 20, .subtileY = 0, .kind = NavigationLineKind::Waypoint},
        NavigationSubtileDestination{.destinationId = 2U,
            .subtileX = 25, .subtileY = 0, .kind = NavigationLineKind::Waypoint},
        NavigationSubtileDestination{.destinationId = 3U,
            .subtileX = 30, .subtileY = 0, .kind = NavigationLineKind::Waypoint},
    };
    Check(PublishNavigationDestinations(41U, 4,
        destinations.data(), destinations.size()), "destinations failed");
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(LastPreReadiness == GpsRouteReadiness::Ready
        && LastPostReadiness == GpsRouteReadiness::Ready
        && LastPreSubmissionCount == 0U
        && Requests.size() == 3U && Submissions == 1U,
        "only the first ready post-observation phase may submit the initial batch");
    Check(Requests.front().identity.artifact.fromSubtileX == 20
        && Requests.front().identity.artifact.fromSubtileY == 10,
        "initial request did not use the explicit native player origin");
    std::vector<NavigationGpsRouteSegmentSnapshot> segments;
    Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "pending batch must not draw");

    // A partially completed three-route batch remains in flight after the
    // movement threshold and interval. Replanning here would discard useful
    // serial helper work and can starve the final route indefinitely.
    CompleteHelperRequest(0U);
    const auto historicalAnyPathStatus = Completed.empty()
        ? GpsRouteProviderStatus::Calculating
        : GpsRouteProviderStatus::RouteReady;
    Check(historicalAnyPathStatus == GpsRouteProviderStatus::RouteReady
        && GetGpsRouteProviderStatus() == GpsRouteProviderStatus::Calculating,
        "one completed route must leave the aggregate provider calculating");
    Now = 900U;
    pass.playerClientX = 352;
    pass.playerClientY = 336;
    pass.playerSubtile = {32, 10};
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(Submissions == 1U && Completed.size() == 1U
        && Requests.front().identity.artifact.fromSubtileX == 20,
        "due movement must not reset an incomplete helper batch");
    Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "partial helper batch must not draw");

    CompleteHelperRequest(1U);
    CompleteHelperRequest(2U);
    Check(GetGpsRouteProviderStatus() == GpsRouteProviderStatus::RouteReady,
        "three completed routes must make the aggregate provider terminal");
    Now = 1'000U;
    pass.playerClientX = 368;
    pass.playerClientY = 344;
    pass.playerSubtile = {33, 10};
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(Published == 3U && Submissions == 2U
        && Requests.front().identity.artifact.fromSubtileX == 33
        && Completed.empty(),
        "complete batch must publish before replanning from the latest player origin");
    Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 3U,
        "completed batch must become drawable in the very next native pass");

    // Return to the accepted batch origin to retain the raw off-lattice anchor
    // assertion independently of the movement/replan regression above.
    pass.playerClientX = 161;
    pass.playerClientY = 240;
    pass.playerSubtile = {20, 10};
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 3U,
        "return to the accepted origin did not refresh the projected routes");
    // Route identity remains the exact world subtile (20,10), while the visible
    // anchor reuses the raw native player projection for subcell responsiveness.
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        Check(segments[index].destinationId == index + 1U
            && segments[index].startX == 161
            && segments[index].startY == 240
            && segments[index].endX == destinations[index].subtileX * 16,
            "projected route coordinates or identity are incorrect");
    }

    // Losing an already observed origin is a hard GPS-source revocation. The
    // Direct projection still runs, but neither phase may submit another batch.
    pass.hasPlayerSubtile = false;
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(LastPostReadiness == GpsRouteReadiness::NoPlayer
        && !AcquireNavigationGpsRouteSourceSnapshot(source, &sourceDiagnostics)
        && sourceDiagnostics.readiness == NavigationGpsSourceReadiness::NoPlayer
        && Submissions == 2U,
        "missing native origin must revoke the GPS source without a submission");
    pass.hasPlayerSubtile = true;
    pass.playerSubtile = {20, 10};

    CompleteHelperBatch();
    Now = 1'400U;
    pass.playerClientX = 384; // Explicit subtile (34,10): replan waits.
    pass.playerClientY = 352;
    pass.playerSubtile = {34, 10};
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(Submissions == 2U, "replan must wait for its interval");
    Now = 2'000U;
    pass.playerClientX = 496; // Explicit subtile (41,10), newer than the pass.
    pass.playerClientY = 408;
    pass.playerSubtile = {41, 10};
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(Submissions == 3U
        && Requests.front().identity.artifact.fromSubtileX == 41,
        "post-observation replan must use the latest player position");
    Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 3U,
        "movement replan must retain compatible drawable routes");

    // A completed large batch is copied and converted once. Stable native
    // passes use the accepted navigation-content epoch and never reacquire it.
    CompleteHelperBatch(4'096U);
    const auto acquisitionsBeforeLargeBatch = PathAcquisitions;
    Pump(4, false, NativeAutomapObservationPhase::BeforeProjection, nullptr);
    Check(PathAcquisitions == acquisitionsBeforeLargeBatch + 1U
        && Published == 3U,
        "large completed batch was not acquired exactly once");
    const auto acceptedLargeBatchEpoch = GetNavigationGpsRouteContentEpoch();
    const auto acquisitionsAfterLargeBatch = PathAcquisitions;
    for (std::size_t passIndex = 0U; passIndex < 120U; ++passIndex) {
        Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    }
    Check(PathAcquisitions == acquisitionsAfterLargeBatch
        && GetNavigationGpsRouteContentEpoch() == acceptedLargeBatchEpoch,
        "stable native passes reacquired or republished the accepted large batch");

    // A real navigation clear changes the content epoch. The next coordinator
    // pass reacquires the still-complete provider batch once and relatches the
    // exact epoch returned by publication.
    InvalidateNavigationGpsRoutes();
    (void)ObserveNavigationAutomapPass(pass);
    const auto clearedContentEpoch = GetNavigationGpsRouteContentEpoch();
    Check(clearedContentEpoch != acceptedLargeBatchEpoch,
        "navigation invalidation did not change the route-content epoch");
    Pump(4, false, NativeAutomapObservationPhase::BeforeProjection, nullptr);
    Check(PathAcquisitions == acquisitionsAfterLargeBatch + 1U
        && Published == 3U
        && GetNavigationGpsRouteContentEpoch() != clearedContentEpoch,
        "coordinator did not recover the invalidated accepted batch");
    const auto recoveredContentEpoch = GetNavigationGpsRouteContentEpoch();
    Pump(4, false, NativeAutomapObservationPhase::AfterObservation, nullptr);
    Check(PathAcquisitions == acquisitionsAfterLargeBatch + 1U
        && GetNavigationGpsRouteContentEpoch() == recoveredContentEpoch,
        "recovered batch did not relatch its exact navigation-content epoch");

    CompleteHelperBatch();
    pass.currentLevelId = 5;
    RunNativeAutomapObservation(pass, observe, Pump, nullptr);
    Check(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "previous-level completion must never draw in the new level");
    CheckQolWalkingShortcuts();
    ShutdownGpsRouteCoordinator();
    ShutdownNavigationEngine();
    std::cout << "GPS native-pass publication regression passed\n";
}
