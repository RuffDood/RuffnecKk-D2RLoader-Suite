#pragma once

#include "isc12_schema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ruffneckk::isc12 {

inline constexpr std::size_t NativeItemStatCostLinkerOffset = 0x1270;
inline constexpr std::uintptr_t NativeGetLinkNameCountRva = 0xA12400;
inline constexpr std::uintptr_t NativeGetLinkNameRva = 0xA12420;
inline constexpr std::int32_t NativeGetLinkNameMode = 0;
inline constexpr std::size_t MaximumNativeStatNameLength = 0xFFFF;

using GetLinkNameCountCallback = std::uint64_t (*)(
    const void* linker) noexcept;
using GetLinkNameCallback = std::string_view (*)(
    const void* linker,
    std::int32_t ordinal) noexcept;

struct NativeItemStatCostLinkerCallbacks {
    GetLinkNameCountCallback getLinkNameCount{};
    GetLinkNameCallback getLinkName{};
};

struct NativeItemStatCostSchemaSnapshot {
    std::vector<ItemStatSemanticRow> rows;
    std::uint8_t effectiveStuff{};
    Sha256Digest schemaHash{};
};

enum class NativeSchemaGateDecision : std::uint8_t {
    Publish,
    AcceptExisting,
    FailClosed,
};

[[nodiscard]] constexpr auto DecideNativeSchemaGate(
        SchemaError captureResult,
        bool hasPublishedSnapshot,
        const Sha256Digest& publishedHash,
        const Sha256Digest& candidateHash) noexcept
        -> NativeSchemaGateDecision {
    if (captureResult != SchemaError::None) {
        return NativeSchemaGateDecision::FailClosed;
    }
    if (!hasPublishedSnapshot) {
        return NativeSchemaGateDecision::Publish;
    }
    return publishedHash == candidateHash
        ? NativeSchemaGateDecision::AcceptExisting
        : NativeSchemaGateDecision::FailClosed;
}

inline constexpr std::size_t NativeSchemaCandidateCapacity = 3;

enum class NativeSchemaStageResult : std::uint8_t {
    Staged,
    InvalidArgument,
    CapacityExceeded,
};

enum class NativeSchemaFinalizeResult : std::uint8_t {
    Published,
    AcceptedExisting,
    InvalidState,
    InvalidTableView,
    InvalidAuthoritativeSnapshot,
    MissingCandidate,
    InvalidOrDuplicateRevision,
    Diverged,
};

// ItemStatCost is compiled once per D2R data bank before D2RLoader announces
// the authoritative DataTablesLoaded revision. Keep those bank-local captures
// bounded and unpublished until the active RotW TableView identifies the one
    // that may bind persistence and codec validation. Finalization decodes the
    // authoritative bytes again with the copied linker names; pointer identity
    // is selection evidence, never proof that post-processing left the records
    // unchanged. Revisions must be non-zero and distinct from the last
    // finalized load; the public SDK does not promise numeric monotonicity.
class NativeSchemaCandidateSet final {
public:
    auto Reset() noexcept -> void;

    [[nodiscard]] auto Stage(
        const void* sourceDataTables,
        const void* sourceRecords,
        std::size_t rowCount,
        NativeItemStatCostSchemaSnapshot&& snapshot) noexcept
        -> NativeSchemaStageResult;

    [[nodiscard]] auto Finalize(
        const void* activeRecords,
        std::size_t activeRowCount,
        std::size_t activeRowSize,
        std::uint64_t revision,
        bool hasPublishedSnapshot,
        const Sha256Digest& publishedHash,
        NativeItemStatCostSchemaSnapshot& publishedSnapshot) noexcept
        -> NativeSchemaFinalizeResult;

    [[nodiscard]] auto PendingCount() const noexcept -> std::size_t {
        return pendingCount_;
    }

    [[nodiscard]] auto LastObservedRevision() const noexcept
            -> std::uint64_t {
        return lastObservedRevision_;
    }

private:
    struct Candidate {
        const void* sourceDataTables{};
        const void* sourceRecords{};
        std::size_t rowCount{};
        NativeItemStatCostSchemaSnapshot snapshot{};
    };

    std::array<Candidate, NativeSchemaCandidateCapacity> candidates_{};
    std::size_t pendingCount_{};
    std::uint64_t lastObservedRevision_{};
};

// The linker argument is the non-owning pointer stored at
// DataTables+NativeItemStatCostLinkerOffset. The callback RVAs above document
// the governed native providers, including GetLinkName mode 0. This adapter
// deliberately calls only injected wrappers. Runtime integration must validate
// the linker and native
// executable ranges, contain native faults, and scan each returned C string
// across readable memory for a terminator within 65,536 bytes before returning
// its string_view. The view must remain valid after getLinkName returns until
// this adapter copies it immediately, before invoking the next callback.
//
// On failure, output is left exactly unchanged.
auto BuildNativeItemStatCostSchemaSnapshot(
    const void* linker,
    std::span<const std::uint8_t> recordBytes,
    std::size_t rowCount,
    NativeItemStatCostLinkerCallbacks callbacks,
    NativeItemStatCostSchemaSnapshot& output) noexcept -> SchemaError;

} // namespace ruffneckk::isc12
