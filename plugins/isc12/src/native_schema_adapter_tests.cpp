#include "isc12_native_schema_adapter.hpp"

#include "isc12_contract.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int Failures{};

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "CHECK failed at line " << __LINE__ \
                      << ": " #expression "\n"; \
            ++Failures; \
        } \
    } while (false)

struct LinkerFixture {
    std::uint64_t reportedCount{};
    std::vector<std::string> names;
    mutable std::string sharedBuffer;
    mutable std::size_t countCalls{};
    mutable std::size_t nameCalls{};
};

auto GetFixtureCount(const void* linker) noexcept -> std::uint64_t {
    const auto* fixture = static_cast<const LinkerFixture*>(linker);
    ++fixture->countCalls;
    return fixture->reportedCount;
}

auto GetFixtureName(
        const void* linker,
        std::int32_t ordinal) noexcept -> std::string_view {
    const auto* fixture = static_cast<const LinkerFixture*>(linker);
    ++fixture->nameCalls;
    if (ordinal < 0
            || static_cast<std::size_t>(ordinal) >= fixture->names.size()) {
        return {};
    }
    fixture->sharedBuffer = fixture->names[static_cast<std::size_t>(ordinal)];
    return fixture->sharedBuffer;
}

auto WriteU16(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint16_t value) -> void {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

auto WriteU32(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint32_t value) -> void {
    for (std::size_t index{}; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> (index * 8U));
    }
}

auto MakeRecords(std::size_t count) -> std::vector<std::uint8_t> {
    using namespace ruffneckk::isc12;
    std::vector<std::uint8_t> records(
        count * CompiledItemStatRecordStride);
    for (std::size_t ordinal{}; ordinal < count; ++ordinal) {
        WriteU16(
            records,
            ordinal * CompiledItemStatRecordStride,
            static_cast<std::uint16_t>(ordinal));
    }
    if (!records.empty()) {
        WriteU32(records, 0x13C, 7);
        records[0x15] = 12;
    }
    return records;
}

auto MakeSentinelSnapshot()
        -> ruffneckk::isc12::NativeItemStatCostSchemaSnapshot {
    using namespace ruffneckk::isc12;
    NativeItemStatCostSchemaSnapshot snapshot;
    snapshot.rows.resize(1);
    snapshot.rows[0].statName = "unchanged";
    snapshot.rows[0].sendBits = 0xA5;
    snapshot.effectiveStuff = 8;
    snapshot.schemaHash.fill(0x5A);
    return snapshot;
}

auto MakeCandidateSnapshot(
        std::size_t rowCount,
        std::uint8_t marker)
        -> ruffneckk::isc12::NativeItemStatCostSchemaSnapshot {
    using namespace ruffneckk::isc12;
    NativeItemStatCostSchemaSnapshot snapshot;
    snapshot.rows.resize(rowCount);
    for (std::size_t ordinal{}; ordinal < rowCount; ++ordinal) {
        snapshot.rows[ordinal].statName = "candidate-"
            + std::to_string(static_cast<unsigned int>(marker))
            + "-" + std::to_string(ordinal);
    }
    snapshot.effectiveStuff = marker;
    snapshot.schemaHash.fill(marker);
    return snapshot;
}

auto MakeCapturedSnapshot(
        std::span<const std::uint8_t> records,
        std::size_t rowCount,
        std::uint8_t marker)
        -> ruffneckk::isc12::NativeItemStatCostSchemaSnapshot {
    using namespace ruffneckk::isc12;
    auto snapshot = MakeCandidateSnapshot(rowCount, marker);
    std::vector<std::string_view> nameViews;
    nameViews.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        nameViews.emplace_back(row.statName);
    }
    CHECK(DecodeCompiledItemStatRecords(
        records,
        rowCount,
        nameViews,
        snapshot.rows,
        snapshot.effectiveStuff) == SchemaError::None);
    CHECK(CalculateSchemaHash(
        snapshot.rows,
        snapshot.effectiveStuff,
        snapshot.schemaHash) == SchemaError::None);
    return snapshot;
}

auto HasAuthoritativeCandidate(
        const ruffneckk::isc12::NativeItemStatCostSchemaSnapshot& snapshot,
        std::span<const std::uint8_t> records,
        std::size_t rowCount,
        std::uint8_t marker) -> bool {
    using namespace ruffneckk::isc12;
    std::vector<std::string> copiedNames;
    std::vector<std::string_view> nameViews;
    copiedNames.reserve(rowCount);
    nameViews.reserve(rowCount);
    for (std::size_t ordinal{}; ordinal < rowCount; ++ordinal) {
        copiedNames.emplace_back(
            "candidate-"
            + std::to_string(static_cast<unsigned int>(marker))
            + "-" + std::to_string(ordinal));
    }
    for (const auto& name : copiedNames) nameViews.emplace_back(name);

    std::vector<ItemStatSemanticRow> expectedRows;
    std::uint8_t expectedStuff{};
    Sha256Digest expectedHash{};
    if (DecodeCompiledItemStatRecords(
            records, rowCount, nameViews, expectedRows, expectedStuff)
            != SchemaError::None
            || CalculateSchemaHash(
                expectedRows, expectedStuff, expectedHash)
                != SchemaError::None) {
        return false;
    }
    if (snapshot.rows.size() != rowCount
            || snapshot.effectiveStuff != expectedStuff
            || snapshot.schemaHash != expectedHash) {
        return false;
    }
    for (std::size_t ordinal{}; ordinal < rowCount; ++ordinal) {
        if (snapshot.rows[ordinal].statName != copiedNames[ordinal]) {
            return false;
        }
    }
    return true;
}

auto IsSentinel(
        const ruffneckk::isc12::NativeItemStatCostSchemaSnapshot& snapshot)
        -> bool {
    return snapshot.rows.size() == 1
        && snapshot.rows[0].statName == "unchanged"
        && snapshot.rows[0].sendBits == 0xA5
        && snapshot.effectiveStuff == 8
        && snapshot.schemaHash.front() == 0x5A
        && snapshot.schemaHash.back() == 0x5A;
}

} // namespace

int main() {
    using namespace ruffneckk::isc12;

    static_assert(NativeItemStatCostLinkerOffset == 0x1270);
    static_assert(NativeGetLinkNameCountRva == 0xA12400);
    static_assert(NativeGetLinkNameRva == 0xA12420);
    static_assert(NativeGetLinkNameMode == 0);
    static_assert(CompiledItemStatRecordStride == 0x144);
    static_assert(MaximumSerializedCsvBits == 32);
    static_assert(MaximumSerializedCsvParamBits == 16);
    static_assert(MaximumNativeStatNameLength == 0xFFFF);
    static_assert(NativeSchemaCandidateCapacity == 3);

    Sha256Digest publishedHash{};
    Sha256Digest candidateHash{};
    CHECK(DecideNativeSchemaGate(
        SchemaError::None, false, publishedHash, candidateHash)
        == NativeSchemaGateDecision::Publish);
    CHECK(DecideNativeSchemaGate(
        SchemaError::None, true, publishedHash, candidateHash)
        == NativeSchemaGateDecision::AcceptExisting);
    candidateHash[17] = 1;
    CHECK(DecideNativeSchemaGate(
        SchemaError::None, true, publishedHash, candidateHash)
        == NativeSchemaGateDecision::FailClosed);
    CHECK(DecideNativeSchemaGate(
        SchemaError::InvalidArgument, false, publishedHash, publishedHash)
        == NativeSchemaGateDecision::FailClosed);
    CHECK(DecideNativeSchemaGate(
        SchemaError::InvalidUtf8, true, publishedHash, publishedHash)
        == NativeSchemaGateDecision::FailClosed);

    constexpr NativeItemStatCostLinkerCallbacks Callbacks{
        &GetFixtureCount,
        &GetFixtureName,
    };

    LinkerFixture zeroRowLinker{
        .reportedCount = 0,
        .names = {},
    };
    auto zeroRowSnapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &zeroRowLinker, {}, 0, Callbacks, zeroRowSnapshot)
        == SchemaError::InvalidArgument);
    CHECK(zeroRowLinker.countCalls == 0);
    CHECK(zeroRowLinker.nameCalls == 0);
    CHECK(IsSentinel(zeroRowSnapshot));

    auto records = MakeRecords(2);
    LinkerFixture linker{
        .reportedCount = 2,
        .names = {"strength", "energy"},
    };
    auto snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, records, 2, Callbacks, snapshot) == SchemaError::None);
    CHECK(snapshot.rows.size() == 2);
    CHECK(snapshot.rows[0].statName == "strength");
    CHECK(snapshot.rows[1].statName == "energy");
    CHECK(snapshot.effectiveStuff == 7);
    CHECK(linker.countCalls == 1);
    CHECK(linker.nameCalls == 2);

    constexpr std::array<std::string_view, 2> ExpectedNames{
        "strength", "energy",
    };
    std::vector<ItemStatSemanticRow> expectedRows;
    std::uint8_t expectedStuff{};
    CHECK(DecodeCompiledItemStatRecords(
        records, 2, ExpectedNames, expectedRows, expectedStuff)
        == SchemaError::None);
    Sha256Digest expectedHash{};
    CHECK(CalculateSchemaHash(expectedRows, expectedStuff, expectedHash)
        == SchemaError::None);
    CHECK(snapshot.schemaHash == expectedHash);

    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        nullptr, records, 2, Callbacks, snapshot)
        == SchemaError::InvalidArgument);
    CHECK(IsSentinel(snapshot));

    auto unsafeCodecRecords = records;
    unsafeCodecRecords[0x0B] = static_cast<std::uint8_t>(
        MaximumSerializedCsvParamBits + 1U);
    linker = {
        .reportedCount = 2,
        .names = {"strength", "energy"},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, unsafeCodecRecords, 2, Callbacks, snapshot)
        == SchemaError::UnsafeCodecWidth);
    CHECK(IsSentinel(snapshot));

    unsafeCodecRecords = records;
    unsafeCodecRecords[0x0A] = static_cast<std::uint8_t>(
        MaximumSerializedCsvBits + 1U);
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, unsafeCodecRecords, 2, Callbacks, snapshot)
        == SchemaError::UnsafeCodecWidth);
    CHECK(IsSentinel(snapshot));

    linker = {
        .reportedCount = 3,
        .names = {"strength", "energy"},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, records, 2, Callbacks, snapshot)
        == SchemaError::SizeMismatch);
    CHECK(linker.nameCalls == 0);
    CHECK(IsSentinel(snapshot));

    linker = {
        .reportedCount = 2,
        .names = {"strength", "energy"},
    };
    auto badIds = records;
    WriteU16(badIds, CompiledItemStatRecordStride, 0);
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, badIds, 2, Callbacks, snapshot)
        == SchemaError::RecordIdMismatch);
    CHECK(linker.countCalls == 0);
    CHECK(linker.nameCalls == 0);
    CHECK(IsSentinel(snapshot));

    auto shortRecords = records;
    shortRecords.pop_back();
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, shortRecords, 2, Callbacks, snapshot)
        == SchemaError::SizeMismatch);
    CHECK(linker.countCalls == 0);
    CHECK(linker.nameCalls == 0);
    CHECK(IsSentinel(snapshot));

    linker = {
        .reportedCount = 2,
        .names = {"strength", ""},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, records, 2, Callbacks, snapshot)
        == SchemaError::InvalidUtf8);
    CHECK(IsSentinel(snapshot));

    linker = {
        .reportedCount = 2,
        .names = {"strength", std::string(MaximumNativeStatNameLength + 1, 'x')},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, records, 2, Callbacks, snapshot)
        == SchemaError::InvalidUtf8);
    CHECK(IsSentinel(snapshot));

    linker = {
        .reportedCount = 2,
        .names = {"strength", std::string{"a\0b", 3}},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, records, 2, Callbacks, snapshot)
        == SchemaError::InvalidUtf8);
    CHECK(IsSentinel(snapshot));

    linker = {
        .reportedCount = 2,
        .names = {"strength", std::string{"\xC0\xAF", 2}},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, records, 2, Callbacks, snapshot)
        == SchemaError::InvalidUtf8);
    CHECK(IsSentinel(snapshot));

    auto maximumNameRecords = MakeRecords(1);
    linker = {
        .reportedCount = 1,
        .names = {std::string(MaximumNativeStatNameLength, 'n')},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, maximumNameRecords, 1, Callbacks, snapshot)
        == SchemaError::None);
    CHECK(snapshot.rows.size() == 1);
    CHECK(snapshot.rows[0].statName.size() == MaximumNativeStatNameLength);

    linker = {
        .reportedCount = MaximumRecordCount + 1,
        .names = {},
    };
    snapshot = MakeSentinelSnapshot();
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, {}, MaximumRecordCount + 1, Callbacks, snapshot)
        == SchemaError::TooManyRows);
    CHECK(linker.countCalls == 0);
    CHECK(linker.nameCalls == 0);
    CHECK(IsSentinel(snapshot));

    snapshot = MakeSentinelSnapshot();
    auto missingCount = Callbacks;
    missingCount.getLinkNameCount = nullptr;
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, {}, 0, missingCount, snapshot)
        == SchemaError::InvalidArgument);
    CHECK(IsSentinel(snapshot));

    snapshot = MakeSentinelSnapshot();
    auto missingName = Callbacks;
    missingName.getLinkName = nullptr;
    CHECK(BuildNativeItemStatCostSchemaSnapshot(
        &linker, {}, 0, missingName, snapshot)
        == SchemaError::InvalidArgument);
    CHECK(IsSentinel(snapshot));

    {
        int sourceDataTables{};
        auto exactRecords = MakeRecords(2);
        NativeSchemaCandidateSet candidates;
        CHECK(candidates.Stage(
            &sourceDataTables,
            exactRecords.data(),
            2,
            MakeCandidateSnapshot(2, 0x11))
            == NativeSchemaStageResult::Staged);
        CHECK(candidates.PendingCount() == 1);

        auto published = MakeSentinelSnapshot();
        const Sha256Digest noPublishedHash{};
        CHECK(candidates.Finalize(
            exactRecords.data(),
            2,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::Published);
        CHECK(HasAuthoritativeCandidate(
            published, exactRecords, 2, 0x11));
        CHECK(candidates.PendingCount() == 0);
        CHECK(candidates.LastObservedRevision() == 1);
    }

    {
        int sourceDataTables{};
        auto sharedRecords = MakeRecords(2);
        NativeSchemaCandidateSet candidates;
        CHECK(candidates.Stage(
            &sourceDataTables,
            sharedRecords.data(),
            2,
            MakeCandidateSnapshot(2, 0x21))
            == NativeSchemaStageResult::Staged);
        auto replacement = MakeCandidateSnapshot(2, 0x22);
        replacement.rows[0].statName = "replacement";
        CHECK(candidates.Stage(
            &sourceDataTables,
            sharedRecords.data(),
            2,
            std::move(replacement))
            == NativeSchemaStageResult::Staged);
        CHECK(candidates.PendingCount() == 1);

        NativeItemStatCostSchemaSnapshot published;
        const Sha256Digest noPublishedHash{};
        CHECK(candidates.Finalize(
            sharedRecords.data(),
            2,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::Published);
        CHECK(published.rows.size() == 2);
        CHECK(published.effectiveStuff == 7);
        CHECK(published.rows[0].statName == "replacement");
    }

    {
        int sourceDataTables{};
        auto postProcessedRecords = MakeRecords(1);
        auto captured = MakeCapturedSnapshot(
            postProcessedRecords, 1, 0x28);
        CHECK(captured.rows[0].saveBits == 12);
        NativeSchemaCandidateSet candidates;
        CHECK(candidates.Stage(
            &sourceDataTables,
            postProcessedRecords.data(),
            1,
            std::move(captured)) == NativeSchemaStageResult::Staged);

        // DataTablesLoaded runs after loader post-processing. The same
        // allocation may therefore contain newer authoritative semantic bytes
        // than the compiler-hook capture.
        postProcessedRecords[0x15] = 13;
        NativeItemStatCostSchemaSnapshot published;
        const Sha256Digest noPublishedHash{};
        CHECK(candidates.Finalize(
            postProcessedRecords.data(),
            1,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::Published);
        CHECK(published.rows.size() == 1);
        CHECK(published.rows[0].saveBits == 13);
        CHECK(HasAuthoritativeCandidate(
            published, postProcessedRecords, 1, 0x28));
    }

    {
        int sourceDataTables{};
        auto invalidAuthoritativeRecords = MakeRecords(2);
        NativeSchemaCandidateSet candidates;
        CHECK(candidates.Stage(
            &sourceDataTables,
            invalidAuthoritativeRecords.data(),
            2,
            MakeCapturedSnapshot(
                invalidAuthoritativeRecords, 2, 0x29))
            == NativeSchemaStageResult::Staged);
        WriteU16(
            invalidAuthoritativeRecords,
            CompiledItemStatRecordStride,
            0);
        auto published = MakeSentinelSnapshot();
        const Sha256Digest noPublishedHash{};
        CHECK(candidates.Finalize(
            invalidAuthoritativeRecords.data(),
            2,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published)
            == NativeSchemaFinalizeResult::InvalidAuthoritativeSnapshot);
        CHECK(IsSentinel(published));
        CHECK(candidates.PendingCount() == 1);
        CHECK(candidates.LastObservedRevision() == 0);
    }

    {
        std::array<int, NativeSchemaCandidateCapacity + 1> sourceDataTables{};
        std::array<std::vector<std::uint8_t>,
            NativeSchemaCandidateCapacity + 1> bankRecords{
                MakeRecords(1),
                MakeRecords(1),
                MakeRecords(1),
                MakeRecords(1),
            };
        NativeSchemaCandidateSet candidates;
        for (std::size_t index{};
                index < NativeSchemaCandidateCapacity;
                ++index) {
            CHECK(candidates.Stage(
                &sourceDataTables[index],
                bankRecords[index].data(),
                1,
                MakeCandidateSnapshot(
                    1,
                    static_cast<std::uint8_t>(0x30U + index)))
                == NativeSchemaStageResult::Staged);
        }
        CHECK(candidates.PendingCount() == NativeSchemaCandidateCapacity);
        CHECK(candidates.Stage(
            &sourceDataTables.back(),
            bankRecords.back().data(),
            1,
            MakeCandidateSnapshot(1, 0x3F))
            == NativeSchemaStageResult::CapacityExceeded);
        CHECK(candidates.PendingCount() == NativeSchemaCandidateCapacity);

        NativeItemStatCostSchemaSnapshot published;
        const Sha256Digest noPublishedHash{};
        CHECK(candidates.Finalize(
            bankRecords[1].data(),
            1,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::Published);
        CHECK(HasAuthoritativeCandidate(
            published, bankRecords[1], 1, 0x31));
    }

    {
        int sourceDataTables{};
        auto validRecords = MakeRecords(1);
        NativeSchemaCandidateSet candidates;
        CHECK(candidates.Stage(
            nullptr,
            validRecords.data(),
            1,
            MakeCandidateSnapshot(1, 0x40))
            == NativeSchemaStageResult::InvalidArgument);
        CHECK(candidates.Stage(
            &sourceDataTables,
            nullptr,
            1,
            MakeCandidateSnapshot(1, 0x41))
            == NativeSchemaStageResult::InvalidArgument);
        CHECK(candidates.Stage(
            &sourceDataTables,
            validRecords.data(),
            0,
            MakeCandidateSnapshot(0, 0x42))
            == NativeSchemaStageResult::InvalidArgument);
        CHECK(candidates.Stage(
            &sourceDataTables,
            validRecords.data(),
            MaximumRecordCount + 1,
            MakeCandidateSnapshot(1, 0x43))
            == NativeSchemaStageResult::InvalidArgument);
        CHECK(candidates.Stage(
            &sourceDataTables,
            validRecords.data(),
            2,
            MakeCandidateSnapshot(1, 0x44))
            == NativeSchemaStageResult::InvalidArgument);
        CHECK(candidates.PendingCount() == 0);
    }

    {
        int sourceDataTables{};
        int unrelatedRecords{};
        auto firstRecords = MakeRecords(1);
        NativeSchemaCandidateSet candidates;
        CHECK(candidates.Stage(
            &sourceDataTables,
            firstRecords.data(),
            1,
            MakeCandidateSnapshot(1, 0x71))
            == NativeSchemaStageResult::Staged);

        auto published = MakeSentinelSnapshot();
        const Sha256Digest noPublishedHash{};
        CHECK(candidates.Finalize(
            nullptr,
            1,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::InvalidTableView);
        CHECK(candidates.Finalize(
            firstRecords.data(),
            0,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::InvalidTableView);
        CHECK(candidates.Finalize(
            firstRecords.data(),
            MaximumRecordCount + 1,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::InvalidTableView);
        CHECK(candidates.Finalize(
            firstRecords.data(),
            1,
            CompiledItemStatRecordStride - 1,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::InvalidTableView);
        CHECK(candidates.Finalize(
            &unrelatedRecords,
            1,
            CompiledItemStatRecordStride,
            1,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::MissingCandidate);
        CHECK(candidates.Finalize(
            firstRecords.data(),
            1,
            CompiledItemStatRecordStride,
            0,
            false,
            noPublishedHash,
            published)
            == NativeSchemaFinalizeResult::InvalidOrDuplicateRevision);
        CHECK(IsSentinel(published));
        CHECK(candidates.PendingCount() == 1);
        CHECK(candidates.LastObservedRevision() == 0);

        CHECK(candidates.Finalize(
            firstRecords.data(),
            1,
            CompiledItemStatRecordStride,
            5,
            false,
            noPublishedHash,
            published) == NativeSchemaFinalizeResult::Published);
        CHECK(HasAuthoritativeCandidate(
            published, firstRecords, 1, 0x71));
        const auto publishedName = published.rows[0].statName;
        const auto publishedHashAfterFirstRevision = published.schemaHash;

        auto secondRecords = MakeRecords(1);
        auto matching = MakeCandidateSnapshot(1, 0x71);
        CHECK(candidates.Stage(
            &sourceDataTables,
            secondRecords.data(),
            1,
            std::move(matching)) == NativeSchemaStageResult::Staged);
        CHECK(candidates.Finalize(
            secondRecords.data(),
            1,
            CompiledItemStatRecordStride,
            5,
            true,
            publishedHashAfterFirstRevision,
            published)
            == NativeSchemaFinalizeResult::InvalidOrDuplicateRevision);
        CHECK(candidates.PendingCount() == 1);
        CHECK(candidates.Finalize(
            secondRecords.data(),
            1,
            CompiledItemStatRecordStride,
            6,
            true,
            publishedHashAfterFirstRevision,
            published) == NativeSchemaFinalizeResult::AcceptedExisting);
        CHECK(published.rows[0].statName == publishedName);
        CHECK(published.schemaHash == publishedHashAfterFirstRevision);
        CHECK(candidates.PendingCount() == 0);
        CHECK(candidates.LastObservedRevision() == 6);

        auto wrappedRevisionRecords = MakeRecords(1);
        CHECK(candidates.Stage(
            &sourceDataTables,
            wrappedRevisionRecords.data(),
            1,
            MakeCandidateSnapshot(1, 0x71))
            == NativeSchemaStageResult::Staged);
        CHECK(candidates.Finalize(
            wrappedRevisionRecords.data(),
            1,
            CompiledItemStatRecordStride,
            2,
            true,
            publishedHashAfterFirstRevision,
            published) == NativeSchemaFinalizeResult::AcceptedExisting);
        CHECK(candidates.LastObservedRevision() == 2);

        auto divergentRecords = MakeRecords(1);
        divergentRecords[0x15] = 13;
        CHECK(candidates.Stage(
            &sourceDataTables,
            divergentRecords.data(),
            1,
            MakeCandidateSnapshot(1, 0x71))
            == NativeSchemaStageResult::Staged);
        CHECK(candidates.Finalize(
            divergentRecords.data(),
            1,
            CompiledItemStatRecordStride,
            7,
            true,
            publishedHashAfterFirstRevision,
            published) == NativeSchemaFinalizeResult::Diverged);
        CHECK(published.rows[0].statName == publishedName);
        CHECK(published.schemaHash == publishedHashAfterFirstRevision);
        CHECK(candidates.PendingCount() == 1);
        CHECK(candidates.LastObservedRevision() == 2);

        candidates.Reset();
        CHECK(candidates.PendingCount() == 0);
        CHECK(candidates.LastObservedRevision() == 0);
        CHECK(candidates.Finalize(
            divergentRecords.data(),
            1,
            CompiledItemStatRecordStride,
            1,
            true,
            publishedHashAfterFirstRevision,
            published) == NativeSchemaFinalizeResult::MissingCandidate);
        CHECK(published.rows[0].statName == publishedName);
        CHECK(published.schemaHash == publishedHashAfterFirstRevision);
    }

    if (Failures != 0) {
        std::cerr << Failures << " native schema adapter test(s) failed\n";
        return 1;
    }
    std::cout << "ISC12 native schema adapter tests passed\n";
    return 0;
}
