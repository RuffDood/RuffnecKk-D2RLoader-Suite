#pragma once

#include "isc12_codec_patch.hpp"
#include "isc12_publication_coordinator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ruffneckk::isc12 {

struct PublicationAdapterTargets {
    std::uintptr_t g0DescriptionRelayRva{};
    std::uintptr_t g10ReaderRelayRva{};
    std::uintptr_t g10WriterRelayRva{};
    CodecPatchActivationTargets codec{};
};

using PatchPublicationRel32Fn = bool (*)(
    void* context,
    std::uintptr_t rva,
    std::span<const std::uint8_t> expected,
    std::uintptr_t targetRva,
    std::size_t overwriteSize) noexcept;
using PatchPublicationU32Fn = bool (*)(
    void* context,
    std::uintptr_t rva,
    std::span<const std::uint8_t> expected,
    std::uint32_t replacement) noexcept;
using PublicationStateTransitionFn = void (*)(void* context) noexcept;

// Native operations used by the production-neutral adapters. The callback
// object is copied at Bind time, so later mutation of the caller's descriptor
// cannot alter a prepared transaction. The pointed-to native authority remains
// owned by the future loader transaction.
struct PublicationAdapterNativeCallbacks {
    void* context{};
    VerifyCodecPatternFn verifyPattern{};
    PatchPublicationRel32Fn patchRel32{};
    PatchPublicationU32Fn patchU32{};
    WriteCodecByteFn writeCodecByte{};
    FlushCodecInstructionCacheFn flushInstructionCache{};
    PublicationStateTransitionFn markG0TailCommitted{};
    PublicationStateTransitionFn activateG0Guard{};
    PublicationStateTransitionFn markG0CapCommitted{};
    PublicationStateTransitionFn markG10ReaderCommitted{};
    PublicationStateTransitionFn markG10WriterCommitted{};
    PublicationStateTransitionFn reserveProcessLifetime{};
    PublicationStateTransitionFn publishReadiness{};
    PublicationStateTransitionFn markPoisoned{};
};

// Binds the actual G0, G10 and codec publishers to the global coordinator
// contract. Bind only copies targets and callbacks; it does not inspect or
// mutate native memory. Every preflight produces an owned plan that is consumed
// by the later commit without re-resolving targets or re-reading fingerprints.
class PublicationAdapterSet final {
public:
    [[nodiscard]] auto Bind(
        const PublicationAdapterTargets& targets,
        const PublicationAdapterNativeCallbacks& callbacks) noexcept -> bool;

    auto Reset() noexcept -> void;

    [[nodiscard]] auto CoordinatorCallbacks() noexcept
        -> PublicationCoordinatorCallbacks;

    [[nodiscard]] auto IsBound() const noexcept -> bool { return bound_; }

private:
    struct PreparedG0Plan {
        std::uintptr_t descriptionRelayRva{};
    };
    struct PreparedG10Plan {
        std::uintptr_t readerRelayRva{};
        std::uintptr_t writerRelayRva{};
    };

    [[nodiscard]] auto PreflightG0(
        const NativePublicationLeaseView& lease) noexcept -> bool;
    [[nodiscard]] auto PreflightG10(
        const NativePublicationLeaseView& lease) noexcept -> bool;
    [[nodiscard]] auto PreflightCodec(
        const NativePublicationLeaseView& lease) noexcept -> bool;
    [[nodiscard]] auto CommitG0(
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome;
    [[nodiscard]] auto CommitG10(
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome;
    [[nodiscard]] auto CommitCodec(
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome;

    static auto PreflightG0Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool;
    static auto PreflightG10Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool;
    static auto PreflightCodecThunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool;
    static auto CommitG0Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome;
    static auto CommitG10Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome;
    static auto CommitCodecThunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome;
    static auto ReserveProcessLifetimeThunk(void* context) noexcept -> void;
    static auto PublishReadinessThunk(void* context) noexcept -> void;
    static auto MarkPoisonedThunk(void* context) noexcept -> void;

    PublicationAdapterTargets targets_{};
    PublicationAdapterNativeCallbacks callbacks_{};
    std::optional<PreparedG0Plan> g0Plan_{};
    std::optional<PreparedG10Plan> g10Plan_{};
    std::optional<PreparedCodecPatchPlan> codecPlan_{};
    bool bound_{};
};

} // namespace ruffneckk::isc12
