#pragma once

#include "isc12_native_sites.hpp"
#include "isc12_publication_lease.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ruffneckk::isc12 {

enum class CodecPatchGroupId : std::uint8_t {
    FullItemTransport,
    Packet3E,
    PacketA8,
    PacketAA,
    PacketAC,
    GenericItem,
    AuxiliaryPlayer,
    PlayerSave,
    PlayerPreview,
};

struct CodecByteMutation {
    std::size_t patternOffset{};
    std::uint8_t expected{};
    std::uint8_t replacement{};
    enum class ReplacementSource : std::uint8_t {
        Literal,
        AuxiliaryReaderRelayRel32,
        PlayerReaderRelayRel32,
        PlayerPreviewRelayRel32,
        PlayerSaveFinalizeRel32,
        Packet9CQueueRelayRel32,
        Packet9DQueueRelayRel32,
        Packet9CEntryRelayRel32,
        Packet9DEntryRelayRel32,
    } source{ReplacementSource::Literal};
    std::uint8_t sourceByteIndex{};
};

class LoaderCodecPatchAuthority;

class CodecPatchActivationTargets {
public:
    constexpr CodecPatchActivationTargets() noexcept = default;

    [[nodiscard]] constexpr auto PlayerSaveFinalizeRelayRva()
            const noexcept -> std::uintptr_t {
        return playerSaveFinalizeRelayRva_;
    }
    [[nodiscard]] constexpr auto AuxiliaryReaderRelayRva()
            const noexcept -> std::uintptr_t {
        return auxiliaryReaderRelayRva_;
    }
    [[nodiscard]] constexpr auto PlayerReaderRelayRva()
            const noexcept -> std::uintptr_t {
        return playerReaderRelayRva_;
    }
    [[nodiscard]] constexpr auto PlayerPreviewRelayRva()
            const noexcept -> std::uintptr_t {
        return playerPreviewRelayRva_;
    }
    [[nodiscard]] constexpr auto Packet9CQueueRelayRva()
            const noexcept -> std::uintptr_t {
        return packet9CQueueRelayRva_;
    }
    [[nodiscard]] constexpr auto Packet9DQueueRelayRva()
            const noexcept -> std::uintptr_t {
        return packet9DQueueRelayRva_;
    }
    [[nodiscard]] constexpr auto Packet9CEntryRelayRva()
            const noexcept -> std::uintptr_t {
        return packet9CEntryRelayRva_;
    }
    [[nodiscard]] constexpr auto Packet9DEntryRelayRva()
            const noexcept -> std::uintptr_t {
        return packet9DEntryRelayRva_;
    }

#if defined(ISC12_CODEC_PATCH_TESTING)
    [[nodiscard]] static constexpr auto ForTesting(
            std::uintptr_t auxiliaryReaderRelayRva,
            std::uintptr_t playerReaderRelayRva,
            std::uintptr_t playerPreviewRelayRva,
            std::uintptr_t playerSaveFinalizeRelayRva,
            std::uintptr_t packet9CQueueRelayRva,
            std::uintptr_t packet9DQueueRelayRva,
            std::uintptr_t packet9CEntryRelayRva,
            std::uintptr_t packet9DEntryRelayRva) noexcept
            -> CodecPatchActivationTargets {
        return CodecPatchActivationTargets{
            auxiliaryReaderRelayRva,
            playerReaderRelayRva,
            playerPreviewRelayRva,
            playerSaveFinalizeRelayRva,
            packet9CQueueRelayRva,
            packet9DQueueRelayRva,
            packet9CEntryRelayRva,
            packet9DEntryRelayRva};
    }
#endif

private:
    // Only the loader authority may bind this contract to the copied,
    // process-lifetime RX entries prepared from the governed relay template.
    explicit constexpr CodecPatchActivationTargets(
            std::uintptr_t auxiliaryReaderRelayRva,
            std::uintptr_t playerReaderRelayRva,
            std::uintptr_t playerPreviewRelayRva,
            std::uintptr_t playerSaveFinalizeRelayRva,
            std::uintptr_t packet9CQueueRelayRva,
            std::uintptr_t packet9DQueueRelayRva,
            std::uintptr_t packet9CEntryRelayRva,
            std::uintptr_t packet9DEntryRelayRva) noexcept
        : auxiliaryReaderRelayRva_{auxiliaryReaderRelayRva},
          playerReaderRelayRva_{playerReaderRelayRva},
          playerPreviewRelayRva_{playerPreviewRelayRva},
          playerSaveFinalizeRelayRva_{playerSaveFinalizeRelayRva},
          packet9CQueueRelayRva_{packet9CQueueRelayRva},
          packet9DQueueRelayRva_{packet9DQueueRelayRva},
          packet9CEntryRelayRva_{packet9CEntryRelayRva},
          packet9DEntryRelayRva_{packet9DEntryRelayRva} {}

    std::uintptr_t auxiliaryReaderRelayRva_{};
    std::uintptr_t playerReaderRelayRva_{};
    std::uintptr_t playerPreviewRelayRva_{};
    std::uintptr_t playerSaveFinalizeRelayRva_{};
    std::uintptr_t packet9CQueueRelayRva_{};
    std::uintptr_t packet9DQueueRelayRva_{};
    std::uintptr_t packet9CEntryRelayRva_{};
    std::uintptr_t packet9DEntryRelayRva_{};

    friend class LoaderCodecPatchAuthority;
};

struct CodecPatchSite {
    NativePattern pattern{};
    std::span<const CodecByteMutation> mutations{};
};

struct CodecPatchGroup {
    CodecPatchGroupId id{};
    const char* name{};
    std::span<const CodecPatchSite> sites{};
    // Unchanged owner/capacity/error-path fingerprints that must match before
    // any byte in this group may be published.
    std::span<const NativePattern> witnesses{};
};

enum class CodecPatchPlanError : std::uint8_t {
    None,
    EmptyGroup,
    InvalidPattern,
    InvalidMutation,
    DuplicateMutation,
    WitnessOverlapsMutation,
    InvalidActivationTarget,
};

enum class CodecPatchCommitStatus : std::uint8_t {
    Active,
    InvalidPlan,
    QuiescenceRequired,
    PreflightFailed,
    PartialCommitColdRestartRequired,
};

struct CodecPatchCommitResult {
    CodecPatchCommitStatus status{CodecPatchCommitStatus::InvalidPlan};
    CodecPatchPlanError planError{CodecPatchPlanError::None};
    // Becomes true immediately before the first native write callback. Merely
    // processing an already-equal resolved byte does not count as mutation.
    bool mutationAttempted{};
    std::size_t attemptedMutations{};
    std::size_t confirmedMutations{};
    std::size_t confirmedNoOpMutations{};
    std::size_t confirmedFlushes{};
};

enum class CodecPatchPreflightStatus : std::uint8_t {
    Prepared,
    InvalidPlan,
    QuiescenceRequired,
    PreflightFailed,
};

using VerifyCodecPatternFn = bool (*)(
    void* context,
    const NativePattern& pattern) noexcept;
using WriteCodecByteFn = bool (*)(
    void* context,
    std::uintptr_t rva,
    std::uint8_t expected,
    std::uint8_t replacement) noexcept;
using FlushCodecInstructionCacheFn = bool (*)(
    void* context,
    std::uintptr_t firstRva,
    std::size_t size) noexcept;

struct CodecPatchPreflightCallbacks {
    void* context{};
    VerifyCodecPatternFn verifyPattern{};
};

struct CodecPatchCommitCallbacks {
    void* context{};
    WriteCodecByteFn writeByte{};
    FlushCodecInstructionCacheFn flushInstructionCache{};
};

struct CodecPatchPreflightResult;

static_assert(PreparedCodecMutationCount == 129U);
static_assert(PreparedCodecMutableSiteCount == 43U);
static_assert(PreparedCodecWitnessCount == 85U);

// Heap-free immutable publication input produced only by the complete codec
// preflight. It owns all 102 resolved replacement bytes and every mutation/
// flush coordinate needed by commit, so commit performs no plan validation,
// target resolution, fingerprinting or lifetime reservation.
class PreparedCodecPatchPlan final {
public:
    PreparedCodecPatchPlan(const PreparedCodecPatchPlan&) = default;
    PreparedCodecPatchPlan(PreparedCodecPatchPlan&&) noexcept = default;
    auto operator=(const PreparedCodecPatchPlan&)
        -> PreparedCodecPatchPlan& = delete;
    auto operator=(PreparedCodecPatchPlan&&) noexcept
        -> PreparedCodecPatchPlan& = delete;

    [[nodiscard]] auto ResolvedBytes() const noexcept
            -> std::span<
                const std::uint8_t,
                PreparedCodecMutationCount> {
        return resolvedBytes_;
    }

    [[nodiscard]] auto FlushRangeCount() const noexcept -> std::size_t {
        return flushRanges_.size();
    }

private:
    struct FlushRange {
        std::size_t firstMutationIndex{};
        std::size_t mutationCount{};
        std::uintptr_t firstRva{};
        std::size_t size{};
    };

    constexpr PreparedCodecPatchPlan(
            std::array<std::uintptr_t, PreparedCodecMutationCount>
                mutationRvas,
            std::array<std::uint8_t, PreparedCodecMutationCount>
                expectedBytes,
            std::array<std::uint8_t, PreparedCodecMutationCount>
                resolvedBytes,
            std::array<FlushRange, PreparedCodecMutableSiteCount>
                flushRanges) noexcept
        : mutationRvas_{mutationRvas},
          expectedBytes_{expectedBytes},
          resolvedBytes_{resolvedBytes},
          flushRanges_{flushRanges} {}

    std::array<std::uintptr_t, PreparedCodecMutationCount> mutationRvas_{};
    std::array<std::uint8_t, PreparedCodecMutationCount> expectedBytes_{};
    std::array<std::uint8_t, PreparedCodecMutationCount> resolvedBytes_{};
    std::array<FlushRange, PreparedCodecMutableSiteCount> flushRanges_{};

    friend auto PreflightPreparedCodecPatchSet(
        const NativePublicationLeaseView& quiescence,
        const CodecPatchActivationTargets& activationTargets,
        const CodecPatchPreflightCallbacks& callbacks) noexcept
        -> CodecPatchPreflightResult;
    friend auto CommitPreflightedCodecPatchSet(
        const NativePublicationLeaseView& quiescence,
        const PreparedCodecPatchPlan& plan,
        const CodecPatchCommitCallbacks& callbacks) noexcept
        -> CodecPatchCommitResult;
};

struct CodecPatchPreflightResult {
    CodecPatchPreflightStatus status{CodecPatchPreflightStatus::InvalidPlan};
    CodecPatchPlanError planError{CodecPatchPlanError::None};
    std::optional<PreparedCodecPatchPlan> plan{};
};

auto PreparedCodecPatchGroups() noexcept -> std::span<const CodecPatchGroup>;
auto ValidateCodecPatchGroup(const CodecPatchGroup& group) noexcept
    -> CodecPatchPlanError;

// Validates the complete canonical G9/G2/G4/G1/G3 set, resolves every dynamic
// rel32 byte and fingerprints all 24 mutable sites plus 77 witnesses while the
// borrowed loader lease remains held. This function never reserves process
// lifetime and never writes native memory.
auto PreflightPreparedCodecPatchSet(
    const NativePublicationLeaseView& quiescence,
    const CodecPatchActivationTargets& activationTargets,
    const CodecPatchPreflightCallbacks& callbacks) noexcept
    -> CodecPatchPreflightResult;

// Commits only a successful immutable preflight plan. The caller must reserve
// all relay/state/unwind storage process-lifetime before calling this function.
// A false write/flush or lease loss after the first attempted write is
// uncertain and requires a cold restart; rollback is intentionally absent.
auto CommitPreflightedCodecPatchSet(
    const NativePublicationLeaseView& quiescence,
    const PreparedCodecPatchPlan& plan,
    const CodecPatchCommitCallbacks& callbacks) noexcept
    -> CodecPatchCommitResult;

#if defined(ISC12_CODEC_PATCH_TESTING)
using ReserveCodecMutationLifetimeFn = void (*)(void* context) noexcept;

// Test-only compatibility surface. Production cannot combine preflight,
// reservation and commit and therefore cannot bypass the global coordinator.
struct CodecPatchCallbacks {
    void* context{};
    VerifyCodecPatternFn verifyPattern{};
    WriteCodecByteFn writeByte{};
    FlushCodecInstructionCacheFn flushInstructionCache{};
    ReserveCodecMutationLifetimeFn reserveMutationLifetime{};
};

auto CommitPreparedCodecPatchSet(
    const NativePublicationLeaseView& quiescence,
    const CodecPatchActivationTargets& activationTargets,
    const CodecPatchCallbacks& callbacks) noexcept -> CodecPatchCommitResult;
#endif

} // namespace ruffneckk::isc12
