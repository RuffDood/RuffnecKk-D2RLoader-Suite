#include "gps_route_artifact.hpp"
#include "gps_route_provider.hpp"
#include "gps_route_provider_policy.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

int Failures{};

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++Failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

template <typename T>
void Append(std::vector<std::uint8_t>& bytes, T value) {
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(bits >> (index * 8U)));
    }
}

template <typename T>
void Write(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(T); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            bits >> (index * 8U));
    }
}

auto Artifact(
        RuffnecKk::MapSense::GpsRouteMode mode,
        std::uint8_t middleKind) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> bytes{'M', 'S', 'R', '1'};
    Append<std::uint16_t>(bytes, 1U);
    Append<std::uint8_t>(bytes, static_cast<std::uint8_t>(mode));
    Append<std::uint8_t>(bytes, 0U);
    Append<std::uint32_t>(bytes, 77U);
    Append<std::uint8_t>(bytes, 2U);
    Append<std::uint8_t>(bytes, 0U);
    Append<std::uint8_t>(bytes, 0U);
    Append<std::uint8_t>(bytes, 0U);
    Append<std::int32_t>(bytes, 109);
    Append<std::int32_t>(bytes, 5001);
    Append<std::int32_t>(bytes, 5078);
    Append<std::int32_t>(bytes, 5156);
    Append<std::int32_t>(bytes, 5131);
    Append<std::uint64_t>(bytes, UINT64_C(0x40AD28CD63CEA943));
    Append<std::uint32_t>(bytes, 3U);
    Append<std::uint32_t>(bytes, 0U);
    for (const auto move : {
            std::array<std::int32_t, 3U>{5001, 5078, 0},
            std::array<std::int32_t, 3U>{5070, 5100, middleKind},
            std::array<std::int32_t, 3U>{5156, 5131, 0}}) {
        Append<std::int32_t>(bytes, move[0]);
        Append<std::int32_t>(bytes, move[1]);
        Append<std::uint8_t>(bytes, static_cast<std::uint8_t>(move[2]));
        Append<std::uint8_t>(bytes, 0U);
        Append<std::uint8_t>(bytes, 0U);
        Append<std::uint8_t>(bytes, 0U);
    }
    return bytes;
}

auto Artifact2(
        RuffnecKk::MapSense::GpsRouteMode mode,
        std::uint8_t middleKind,
        std::int32_t originX,
        std::int32_t originY,
        std::uint32_t width,
        std::uint32_t height,
        const std::vector<std::uint8_t>& bits) -> std::vector<std::uint8_t> {
    auto bytes = Artifact(mode, middleKind);
    bytes[3] = '2';
    Write<std::uint16_t>(bytes, 4U, 2U);
    Append<std::int32_t>(bytes, originX);
    Append<std::int32_t>(bytes, originY);
    Append<std::uint32_t>(bytes, width);
    Append<std::uint32_t>(bytes, height);
    Append<std::uint32_t>(bytes, static_cast<std::uint32_t>(bits.size()));
    bytes.insert(bytes.end(), bits.begin(), bits.end());
    return bytes;
}

void WriteMove(
        std::vector<std::uint8_t>& bytes,
        std::size_t index,
        std::int32_t subtileX,
        std::int32_t subtileY) {
    const auto offset = RuffnecKk::MapSense::GpsRouteArtifactHeaderSize
        + index * RuffnecKk::MapSense::GpsRouteArtifactMoveSize;
    Write<std::int32_t>(bytes, offset, subtileX);
    Write<std::int32_t>(bytes, offset + sizeof(std::int32_t), subtileY);
}

auto FingerprintBytes(std::uint64_t fingerprint, std::string_view bytes) noexcept
    -> std::uint64_t {
    for (const auto value : bytes) {
        fingerprint ^= static_cast<std::uint8_t>(value);
        fingerprint *= UINT64_C(1099511628211);
    }
    return fingerprint;
}

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::MapSense;

    // Integration fixtures provide the independently expected request fields:
    // artifact seed difficulty level fromX fromY toX toY fingerprint(hex).
    if (argc == 10) {
        GpsRouteRequestIdentity expected{};
        std::uint32_t difficulty{};
        const auto read = [](const char* argument, auto& value, int base = 10) {
            const std::string_view text{argument};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        };
        CHECK(read(argv[2], expected.seed) && read(argv[3], difficulty) && difficulty <= 2U
            && read(argv[4], expected.levelId)
            && read(argv[5], expected.fromSubtileX) && read(argv[6], expected.fromSubtileY)
            && read(argv[7], expected.toSubtileX) && read(argv[8], expected.toSubtileY)
            && read(argv[9], expected.dataFingerprint, 16));
        expected.difficulty = static_cast<std::uint8_t>(difficulty);
        std::error_code error;
        const std::filesystem::path path{argv[1]};
        const auto size = std::filesystem::file_size(path, error);
        CHECK(!error && size <= MaximumGpsRouteArtifactBytes);
        if (Failures != 0) return EXIT_FAILURE;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        CHECK(input && input.gcount() == static_cast<std::streamsize>(bytes.size()));
        GpsRouteArtifact artifact{};
        CHECK(ParseGpsRouteArtifact(bytes, expected, artifact));
        CHECK(artifact.walkGrid != nullptr && artifact.moves.size() >= 2U);
        if (Failures != 0) return EXIT_FAILURE;
        for (std::size_t index = 1U; index < artifact.moves.size(); ++index) {
            auto previous = artifact.moves[index - 1U];
            const auto next = artifact.moves[index];
            const auto dx = next.subtileX - previous.subtileX;
            const auto dy = next.subtileY - previous.subtileY;
            CHECK(dx == 0 || dy == 0 || std::abs(dx) == std::abs(dy));
            const auto steps = (std::max)(std::abs(dx), std::abs(dy));
            for (int step = 0; step <= steps; ++step) {
                CHECK(artifact.walkGrid->Passable(previous.subtileX, previous.subtileY));
                previous.subtileX += (dx > 0) - (dx < 0);
                previous.subtileY += (dy > 0) - (dy < 0);
            }
        }
        std::cout << "Helper MSR2 request identity and continuous walking corridor checked\n";
        return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 2) {
        std::error_code artifactError;
        const std::filesystem::path artifactPath{argv[1]};
        const auto artifactSize = std::filesystem::file_size(
            artifactPath, artifactError);
        CHECK(!artifactError && artifactSize == 20'480U);
        if (!artifactError && artifactSize <= MaximumGpsRouteArtifactBytes) {
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(artifactSize));
            std::ifstream input(artifactPath, std::ios::binary);
            input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            CHECK(input && input.gcount()
                == static_cast<std::streamsize>(bytes.size()));
            CHECK(bytes.size() >= 6U
                && std::equal(bytes.begin(), bytes.begin() + 4, "MSR2")
                && bytes[4] == 2U && bytes[5] == 0U);
            const GpsRouteRequestIdentity helperWalkIdentity{
                .seed = 2'055'134'277U,
                .difficulty = 2U,
                .levelId = 4,
                .fromSubtileX = 5153,
                .fromSubtileY = 5027,
                .toSubtileX = 5373,
                .toSubtileY = 5373,
                .dataFingerprint = UINT64_C(0x2D5B4117AFF0EDE6),
                .mode = GpsRouteMode::Walk,
            };
            GpsRouteArtifact helperArtifact{};
            CHECK(ParseGpsRouteArtifact(bytes, helperWalkIdentity, helperArtifact));
            CHECK(helperArtifact.moves.size() == 34U);
            CHECK(helperArtifact.walkGrid != nullptr
                && helperArtifact.walkGrid->originX == 5000
                && helperArtifact.walkGrid->originY == 5000
                && helperArtifact.walkGrid->width == 400U
                && helperArtifact.walkGrid->height == 400U
                && helperArtifact.walkGrid->bits.size() == 20'000U);
            CHECK(helperArtifact.walkGrid != nullptr && std::all_of(
                helperArtifact.moves.begin(), helperArtifact.moves.end(),
                [&helperArtifact](const GpsRouteMove& move) noexcept {
                    return helperArtifact.walkGrid->Passable(
                        move.subtileX, move.subtileY);
                }));
        }
    }

    const GpsRouteRequestIdentity walkIdentity{
        .seed = 77U,
        .difficulty = 2U,
        .levelId = 109,
        .fromSubtileX = 5001,
        .fromSubtileY = 5078,
        .toSubtileX = 5156,
        .toSubtileY = 5131,
        .dataFingerprint = UINT64_C(0x40AD28CD63CEA943),
        .mode = GpsRouteMode::Walk,
    };
    GpsRouteArtifact parsed;
    const auto walk = Artifact(GpsRouteMode::Walk, 0U);
    CHECK(ParseGpsRouteArtifact(walk, walkIdentity, parsed));
    CHECK(parsed.moves.size() == 3U);
    CHECK(parsed.walkGrid == nullptr);
    CHECK(parsed.moves.front().subtileX == walkIdentity.fromSubtileX);
    CHECK(parsed.moves.back().subtileY == walkIdentity.toSubtileY);

    auto mismatched = walkIdentity;
    mismatched.levelId = 110;
    CHECK(!ParseGpsRouteArtifact(walk, mismatched, parsed));
    CHECK(parsed.moves.empty());

    auto walkWithCast = Artifact(GpsRouteMode::Walk, 1U);
    CHECK(!ParseGpsRouteArtifact(walkWithCast, walkIdentity, parsed));

    auto teleportIdentity = walkIdentity;
    teleportIdentity.mode = GpsRouteMode::Teleport;
    const auto teleport = Artifact(GpsRouteMode::Teleport, 1U);
    CHECK(ParseGpsRouteArtifact(teleport, teleportIdentity, parsed));
    CHECK(parsed.moves[1].kind == GpsRouteMoveKind::Teleport);
    CHECK(parsed.walkGrid == nullptr);

    constexpr std::uint32_t WalkGridWidth = 200U;
    constexpr std::uint32_t WalkGridHeight = 200U;
    const std::vector<std::uint8_t> walkBits(
        (WalkGridWidth * WalkGridHeight + 7U) / 8U, std::uint8_t{0xFFU});
    const auto walk2 = Artifact2(GpsRouteMode::Walk, 0U,
        5000, 5000, WalkGridWidth, WalkGridHeight, walkBits);
    CHECK(ParseGpsRouteArtifact(walk2, walkIdentity, parsed));
    CHECK(parsed.walkGrid != nullptr);
    CHECK(parsed.walkGrid != nullptr && parsed.walkGrid->originX == 5000
        && parsed.walkGrid->originY == 5000
        && parsed.walkGrid->width == WalkGridWidth
        && parsed.walkGrid->height == WalkGridHeight);
    CHECK(parsed.walkGrid != nullptr && parsed.walkGrid->Passable(5001, 5078));
    CHECK(parsed.walkGrid != nullptr && !parsed.walkGrid->Passable(4999, 5078));
    CHECK(parsed.walkGrid != nullptr && !parsed.walkGrid->Passable(5200, 5078));
    const GpsRouteProviderPath providerPath{
        .identity = {}, .moves = parsed.moves, .walkGrid = parsed.walkGrid};
    CHECK(providerPath.walkGrid == parsed.walkGrid);

    auto msr2WrongVersion = walk2;
    Write<std::uint16_t>(msr2WrongVersion, 4U, 1U);
    CHECK(!ParseGpsRouteArtifact(msr2WrongVersion, walkIdentity, parsed));
    auto msr2WrongMagic = walk2;
    msr2WrongMagic[3] = '1';
    CHECK(!ParseGpsRouteArtifact(msr2WrongMagic, walkIdentity, parsed));

    const auto teleport2 = Artifact2(GpsRouteMode::Teleport, 1U,
        0, 0, 0U, 0U, {});
    CHECK(ParseGpsRouteArtifact(teleport2, teleportIdentity, parsed));
    CHECK(parsed.walkGrid == nullptr);

    constexpr auto GridOffset = GpsRouteArtifactHeaderSize
        + 3U * GpsRouteArtifactMoveSize;
    auto missingGrid = walk2;
    missingGrid.resize(GridOffset + GpsRouteArtifactWalkGridHeaderSize - 1U);
    CHECK(!ParseGpsRouteArtifact(missingGrid, walkIdentity, parsed));

    auto trailingGrid = walk2;
    trailingGrid.push_back(0U);
    CHECK(!ParseGpsRouteArtifact(trailingGrid, walkIdentity, parsed));

    auto invalidGridBytes = walk2;
    Write<std::uint32_t>(invalidGridBytes,
        GridOffset + 16U, 1U);
    CHECK(!ParseGpsRouteArtifact(invalidGridBytes, walkIdentity, parsed));

    auto invalidGridOrigin = walk2;
    Write<std::int32_t>(invalidGridOrigin, GridOffset, -1);
    CHECK(!ParseGpsRouteArtifact(invalidGridOrigin, walkIdentity, parsed));

    auto invalidGridExtent = walk2;
    Write<std::int32_t>(invalidGridExtent, GridOffset, 65'536);
    CHECK(!ParseGpsRouteArtifact(invalidGridExtent, walkIdentity, parsed));

    auto invalidGridCells = walk2;
    Write<std::uint32_t>(invalidGridCells, GridOffset + 8U, 65'536U);
    Write<std::uint32_t>(invalidGridCells, GridOffset + 12U, 17U);
    CHECK(!ParseGpsRouteArtifact(invalidGridCells, walkIdentity, parsed));

    auto invalidGridWidth = walk2;
    Write<std::uint32_t>(invalidGridWidth, GridOffset + 8U, 65'537U);
    CHECK(!ParseGpsRouteArtifact(invalidGridWidth, walkIdentity, parsed));

    auto smallIdentity = walkIdentity;
    smallIdentity.fromSubtileX = 0;
    smallIdentity.fromSubtileY = 0;
    smallIdentity.toSubtileX = 2;
    smallIdentity.toSubtileY = 2;
    auto paddedGrid = Artifact2(GpsRouteMode::Walk, 0U,
        0, 0, 3U, 3U, {0x11U, 0x01U});
    Write<std::int32_t>(paddedGrid, 20U, smallIdentity.fromSubtileX);
    Write<std::int32_t>(paddedGrid, 24U, smallIdentity.fromSubtileY);
    Write<std::int32_t>(paddedGrid, 28U, smallIdentity.toSubtileX);
    Write<std::int32_t>(paddedGrid, 32U, smallIdentity.toSubtileY);
    WriteMove(paddedGrid, 0U, 0, 0);
    WriteMove(paddedGrid, 1U, 1, 1);
    WriteMove(paddedGrid, 2U, 2, 2);
    CHECK(ParseGpsRouteArtifact(paddedGrid, smallIdentity, parsed));
    CHECK(parsed.walkGrid != nullptr && parsed.walkGrid->Passable(0, 0));
    CHECK(parsed.walkGrid != nullptr && !parsed.walkGrid->Passable(1, 0));
    auto badPadding = paddedGrid;
    badPadding.back() = 0x81U;
    CHECK(!ParseGpsRouteArtifact(badPadding, smallIdentity, parsed));

    auto teleportGrid = teleport2;
    Write<std::uint32_t>(teleportGrid, GridOffset + 8U, 1U);
    CHECK(!ParseGpsRouteArtifact(teleportGrid, teleportIdentity, parsed));

    auto blockedWalkMove = walk2;
    constexpr auto WalkGridBitsOffset = GridOffset
        + GpsRouteArtifactWalkGridHeaderSize;
    constexpr std::size_t MiddleMoveGridCell = 100U * WalkGridWidth + 70U;
    blockedWalkMove[WalkGridBitsOffset + MiddleMoveGridCell / 8U] &=
        static_cast<std::uint8_t>(~(1U << (MiddleMoveGridCell % 8U)));
    CHECK(!ParseGpsRouteArtifact(blockedWalkMove, walkIdentity, parsed));

    auto outsideWalkMove = walk2;
    WriteMove(outsideWalkMove, 1U, 5200, 5100);
    CHECK(!ParseGpsRouteArtifact(outsideWalkMove, walkIdentity, parsed));

    auto pad = Artifact(GpsRouteMode::Teleport, 2U);
    CHECK(!ParseGpsRouteArtifact(pad, teleportIdentity, parsed));

    auto reserved = walk;
    reserved[7] = 1U;
    CHECK(!ParseGpsRouteArtifact(reserved, walkIdentity, parsed));

    auto truncated = walk;
    truncated.pop_back();
    CHECK(!ParseGpsRouteArtifact(truncated, walkIdentity, parsed));

    auto wrongStart = walk;
    wrongStart[GpsRouteArtifactHeaderSize] ^= 1U;
    CHECK(!ParseGpsRouteArtifact(wrongStart, walkIdentity, parsed));

    auto snappedGoal = walk;
    constexpr auto LastMoveOffset = GpsRouteArtifactHeaderSize
        + 2U * GpsRouteArtifactMoveSize;
    Write<std::int32_t>(snappedGoal, LastMoveOffset,
        walkIdentity.toSubtileX - MaximumGpsRouteGoalSnapDistance);
    CHECK(ParseGpsRouteArtifact(snappedGoal, walkIdentity, parsed));
    CHECK(parsed.moves.back().subtileX
        == walkIdentity.toSubtileX - MaximumGpsRouteGoalSnapDistance);

    auto distantGoal = snappedGoal;
    Write<std::int32_t>(distantGoal, LastMoveOffset,
        walkIdentity.toSubtileX - MaximumGpsRouteGoalSnapDistance - 1);
    CHECK(!ParseGpsRouteArtifact(distantGoal, walkIdentity, parsed));

    const GpsRoutePublicationIdentity published{
        .sessionGeneration = 7U,
        .destinationRevision = 11U,
        .policyRevision = 13U,
        .terrainRevision = UINT64_C(0xC0FFEE),
        .destinationId = 42U,
        .destinationKind = 1U,
        .artifact = walkIdentity,
    };
    CHECK(IsGpsRoutePublicationCurrent(published, published));
    auto staleSession = published;
    ++staleSession.sessionGeneration;
    CHECK(!IsGpsRoutePublicationCurrent(published, staleSession));
    auto staleDestination = published;
    ++staleDestination.destinationRevision;
    CHECK(!IsGpsRoutePublicationCurrent(published, staleDestination));
    auto stalePolicy = published;
    ++stalePolicy.policyRevision;
    CHECK(!IsGpsRoutePublicationCurrent(published, stalePolicy));
    auto otherKind = published;
    ++otherKind.destinationKind;
    CHECK(!IsGpsRoutePublicationCurrent(published, otherKind));
    auto staleTerrain = published;
    ++staleTerrain.terrainRevision;
    CHECK(!IsGpsRoutePublicationCurrent(published, staleTerrain));
    auto teleportPublication = published;
    teleportPublication.artifact.mode = GpsRouteMode::Teleport;
    CHECK(!IsGpsRoutePublicationCurrent(published, teleportPublication));
    auto staleEndpoint = published;
    ++staleEndpoint.artifact.toSubtileX;
    CHECK(!IsGpsRoutePublicationCurrent(published, staleEndpoint));

    auto helperFingerprint = published;
    helperFingerprint.artifact.dataFingerprint = UINT64_C(0xDEADBEEF);
    CHECK(!IsGpsRoutePublicationCurrent(published, helperFingerprint));
    // The worker stamps the MSR artifact identity after it reproduces Mapgen's input
    // fingerprint. The live resolver scope still identifies that response
    // without trusting a broader terrain/cache digest.
    CHECK(SameGpsRoutePublicationScope(published, helperFingerprint));

    auto secondPublished = published;
    secondPublished.destinationId = 43U;
    auto pending = published;
    pending.destinationId = 44U;
    const std::array aggregateRequests{
        GpsRouteProviderRequest{.identity = published, .requestEpoch = 73U},
        GpsRouteProviderRequest{.identity = secondPublished, .requestEpoch = 73U},
        GpsRouteProviderRequest{.identity = pending, .requestEpoch = 73U},
    };
    const auto aggregateStatus = [&aggregateRequests](
            std::span<const std::uint64_t> publishedIds,
            std::span<const std::uint64_t> failedIds) {
        return EvaluateGpsRouteProviderAggregateStatus(aggregateRequests,
            [publishedIds](const GpsRouteProviderRequest& request) noexcept {
                return std::find(publishedIds.begin(), publishedIds.end(),
                    request.identity.destinationId) != publishedIds.end();
            },
            [failedIds](const GpsRouteProviderRequest& request) noexcept {
                return std::find(failedIds.begin(), failedIds.end(),
                    request.identity.destinationId) != failedIds.end();
            });
    };
    const std::array twoPublished{
        published.destinationId, secondPublished.destinationId};
    CHECK(aggregateStatus(std::span<const std::uint64_t>{twoPublished},
            std::span<const std::uint64_t>{})
        == GpsRouteProviderStatus::Calculating);
    const std::array pendingFailed{pending.destinationId};
    CHECK(aggregateStatus(std::span<const std::uint64_t>{twoPublished},
            std::span<const std::uint64_t>{pendingFailed})
        == GpsRouteProviderStatus::RouteReady);
    const std::array allFailed{
        published.destinationId, secondPublished.destinationId, pending.destinationId};
    CHECK(aggregateStatus(std::span<const std::uint64_t>{},
            std::span<const std::uint64_t>{allFailed})
        == GpsRouteProviderStatus::NoRoute);
    CHECK(EvaluateGpsRouteProviderAggregateStatus(
        std::span<const GpsRouteProviderRequest>{},
        [](const GpsRouteProviderRequest&) noexcept { return false; },
        [](const GpsRouteProviderRequest&) noexcept { return false; })
        == GpsRouteProviderStatus::Calculating);

    // A reset request is lock-free for Present and remains pending until the
    // worker-side locked boundary consumes it. This models a reset arriving
    // while the worker owns its mutex, followed by a new-session submission.
    GpsRouteResetMailbox resetMailbox;
    GpsRouteSessionGate sessionGate;
    std::atomic_flag simulatedWorkerMutex = ATOMIC_FLAG_INIT;
    CHECK(!simulatedWorkerMutex.test_and_set(std::memory_order_acquire));
    resetMailbox.Request(91U);
    CHECK(resetMailbox.Pending());
    simulatedWorkerMutex.clear(std::memory_order_release);
    CHECK(sessionGate.ConsumeReset(resetMailbox));
    CHECK(sessionGate.Accepts(91U));
    CHECK(!sessionGate.Accepts(90U));
    CHECK(!resetMailbox.Pending());

    CHECK(IsValidGpsRouteRequestCount(64U, 64U));
    CHECK(!IsValidGpsRouteRequestCount(65U, 64U));
    CHECK(CanAccumulateGpsRoutePoints(65'534U, 2U, 65'536U));
    CHECK(!CanAccumulateGpsRoutePoints(65'534U, 3U, 65'536U));
    CHECK(MaximumGpsRouteArtifactBytes == GpsRouteArtifactHeaderSize
        + MaximumGpsRouteMoves * GpsRouteArtifactMoveSize
        + GpsRouteArtifactWalkGridHeaderSize + MaximumGpsRouteWalkGridBytes);

    const auto fixture = std::filesystem::temp_directory_path()
        / "ruffneckk-mapsense-gps-route-fingerprint";
    std::error_code error;
    std::filesystem::remove_all(fixture, error);
    std::filesystem::create_directories(fixture / "first", error);
    std::filesystem::create_directories(fixture / "second", error);
    {
        std::ofstream input(fixture / "first" / "levels.txt", std::ios::binary);
        input << "first-root";
    }
    {
        std::ofstream input(fixture / "second" / "levels.txt", std::ios::binary);
        input << "must-not-win";
    }
    const std::array roots{fixture / "first", fixture / "second"};
    std::uint64_t actualFingerprint{};
    CHECK(ComputeGpsRouteInputFingerprint(
        roots, actualFingerprint));
    std::uint64_t expectedFingerprint = UINT64_C(14695981039346656037);
    for (const auto name : std::array<std::string_view, 7U>{
            "levels.txt", "lvlprest.txt", "lvltypes.txt", "lvlmaze.txt",
            "lvlsub.txt", "lvlwarp.txt", "objects.txt"}) {
        expectedFingerprint = FingerprintBytes(expectedFingerprint, name);
        expectedFingerprint = FingerprintBytes(expectedFingerprint,
            name == "levels.txt" ? std::string_view{"first-root"}
                : std::string_view{"\0", 1U});
    }
    CHECK(actualFingerprint == expectedFingerprint);
    {
        std::ofstream empty(fixture / "first" / "lvlprest.txt", std::ios::binary);
    }
    CHECK(!ComputeGpsRouteInputFingerprint(
        roots, actualFingerprint));
    std::filesystem::remove_all(fixture, error);

    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
