#include "isc12_native_schema_adapter.hpp"

#include "isc12_contract.hpp"

#include <limits>
#include <string>
#include <utility>

namespace ruffneckk::isc12 {
namespace {

auto ReadRecordId(
        std::span<const std::uint8_t> recordBytes,
        std::size_t recordOffset) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(recordBytes[recordOffset])
        | static_cast<std::uint16_t>(recordBytes[recordOffset + 1]) << 8U;
}

} // namespace

auto NativeSchemaCandidateSet::Reset() noexcept -> void {
    for (auto& candidate : candidates_) candidate = {};
    pendingCount_ = 0;
    lastObservedRevision_ = 0;
}

auto NativeSchemaCandidateSet::Stage(
        const void* sourceDataTables,
        const void* sourceRecords,
        std::size_t rowCount,
        NativeItemStatCostSchemaSnapshot&& snapshot) noexcept
        -> NativeSchemaStageResult {
    if (!sourceDataTables || !sourceRecords || rowCount == 0
            || rowCount > MaximumRecordCount
            || snapshot.rows.size() != rowCount) {
        return NativeSchemaStageResult::InvalidArgument;
    }

    std::size_t target = candidates_.size();
    for (std::size_t index{}; index < pendingCount_; ++index) {
        // A bank may compile more than one ItemStatCost source during the
        // same load (for example Classic/base followed by the expansion
        // table). Replace only the capture for the same compiled records
        // allocation; retain distinct arrays until DataTableService names
        // the authoritative active one.
        if (candidates_[index].sourceRecords == sourceRecords) {
            target = index;
            break;
        }
    }
    if (target == candidates_.size()) {
        if (pendingCount_ == candidates_.size()) {
            return NativeSchemaStageResult::CapacityExceeded;
        }
        target = pendingCount_++;
    }

    candidates_[target] = Candidate{
        .sourceDataTables = sourceDataTables,
        .sourceRecords = sourceRecords,
        .rowCount = rowCount,
        .snapshot = std::move(snapshot),
    };
    return NativeSchemaStageResult::Staged;
}

auto NativeSchemaCandidateSet::Finalize(
        const void* activeRecords,
        std::size_t activeRowCount,
        std::size_t activeRowSize,
        std::uint64_t revision,
        bool hasPublishedSnapshot,
        const Sha256Digest& publishedHash,
        NativeItemStatCostSchemaSnapshot& publishedSnapshot) noexcept
        -> NativeSchemaFinalizeResult {
    if (!activeRecords || activeRowCount == 0
            || activeRowCount > MaximumRecordCount
            || activeRowSize != CompiledItemStatRecordStride) {
        return NativeSchemaFinalizeResult::InvalidTableView;
    }
    // PluginSDK promises a non-zero revision that changes after each completed
    // load; it does not promise numeric monotonicity or reserve wraparound.
    if (revision == 0 || revision == lastObservedRevision_) {
        return NativeSchemaFinalizeResult::InvalidOrDuplicateRevision;
    }

    Candidate* selected{};
    for (std::size_t index{}; index < pendingCount_; ++index) {
        auto& candidate = candidates_[index];
        if (candidate.sourceRecords == activeRecords
                && candidate.rowCount == activeRowCount) {
            if (selected) {
                return NativeSchemaFinalizeResult::MissingCandidate;
            }
            selected = &candidate;
        }
    }
    if (!selected) return NativeSchemaFinalizeResult::MissingCandidate;

    NativeItemStatCostSchemaSnapshot authoritative;
    try {
        std::vector<std::string_view> copiedNameViews;
        copiedNameViews.reserve(selected->snapshot.rows.size());
        for (const auto& row : selected->snapshot.rows) {
            copiedNameViews.emplace_back(row.statName);
        }

        const auto recordBytes = std::span<const std::uint8_t>{
            static_cast<const std::uint8_t*>(activeRecords),
            activeRowCount * activeRowSize,
        };
        const auto decode = DecodeCompiledItemStatRecords(
            recordBytes,
            activeRowCount,
            copiedNameViews,
            authoritative.rows,
            authoritative.effectiveStuff);
        if (decode != SchemaError::None) {
            return NativeSchemaFinalizeResult::InvalidAuthoritativeSnapshot;
        }
        const auto hash = CalculateSchemaHash(
            authoritative.rows,
            authoritative.effectiveStuff,
            authoritative.schemaHash);
        if (hash != SchemaError::None) {
            return NativeSchemaFinalizeResult::InvalidAuthoritativeSnapshot;
        }
    } catch (...) {
        return NativeSchemaFinalizeResult::InvalidAuthoritativeSnapshot;
    }

    const auto decision = DecideNativeSchemaGate(
        SchemaError::None,
        hasPublishedSnapshot,
        publishedHash,
        authoritative.schemaHash);
    if (decision == NativeSchemaGateDecision::FailClosed) {
        return NativeSchemaFinalizeResult::Diverged;
    }
    if (decision == NativeSchemaGateDecision::Publish) {
        publishedSnapshot = std::move(authoritative);
    }

    for (auto& candidate : candidates_) candidate = {};
    pendingCount_ = 0;
    lastObservedRevision_ = revision;
    return decision == NativeSchemaGateDecision::Publish
        ? NativeSchemaFinalizeResult::Published
        : NativeSchemaFinalizeResult::AcceptedExisting;
}

auto BuildNativeItemStatCostSchemaSnapshot(
        const void* linker,
        std::span<const std::uint8_t> recordBytes,
        std::size_t rowCount,
        NativeItemStatCostLinkerCallbacks callbacks,
        NativeItemStatCostSchemaSnapshot& output) noexcept -> SchemaError {
    if (!linker || !callbacks.getLinkNameCount || !callbacks.getLinkName) {
        return SchemaError::InvalidArgument;
    }
    if (rowCount == 0) return SchemaError::InvalidArgument;
    if (rowCount > MaximumRecordCount) return SchemaError::TooManyRows;
    if (rowCount > (std::numeric_limits<std::size_t>::max)()
            / CompiledItemStatRecordStride
            || recordBytes.size()
                != rowCount * CompiledItemStatRecordStride) {
        return SchemaError::SizeMismatch;
    }
    for (std::size_t ordinal{}; ordinal < rowCount; ++ordinal) {
        const auto offset = ordinal * CompiledItemStatRecordStride;
        if (ReadRecordId(recordBytes, offset) != ordinal) {
            return SchemaError::RecordIdMismatch;
        }
    }
    if (callbacks.getLinkNameCount(linker) != rowCount) {
        return SchemaError::SizeMismatch;
    }

    try {
        std::vector<std::string> copiedNames;
        copiedNames.reserve(rowCount);
        for (std::size_t ordinal{}; ordinal < rowCount; ++ordinal) {
            const auto name = callbacks.getLinkName(
                linker, static_cast<std::int32_t>(ordinal));
            if (name.empty() || name.size() > MaximumNativeStatNameLength) {
                return SchemaError::InvalidUtf8;
            }
            copiedNames.emplace_back(name);
        }

        std::vector<std::string_view> nameViews;
        nameViews.reserve(copiedNames.size());
        for (const auto& name : copiedNames) nameViews.emplace_back(name);

        NativeItemStatCostSchemaSnapshot staged;
        const auto decode = DecodeCompiledItemStatRecords(
            recordBytes,
            rowCount,
            nameViews,
            staged.rows,
            staged.effectiveStuff);
        if (decode != SchemaError::None) return decode;
        const auto hash = CalculateSchemaHash(
            staged.rows, staged.effectiveStuff, staged.schemaHash);
        if (hash != SchemaError::None) return hash;

        output = std::move(staged);
        return SchemaError::None;
    } catch (...) {
        return SchemaError::Allocation;
    }
}

} // namespace ruffneckk::isc12
