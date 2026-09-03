#include "isc12_publication_adapters.hpp"

#include "isc12_contract.hpp"
#include "isc12_native_sites.hpp"

#include <limits>
#include <utility>

namespace ruffneckk::isc12 {
namespace {

constexpr std::size_t G0PatternCount = 13U;
constexpr std::size_t G10PatternCount = 25U;
constexpr std::uintptr_t G0TailPatchRva = 0x31F0AB;
constexpr std::uintptr_t G0CountImmediateRva = 0x31ED38;
constexpr std::uintptr_t G10ReaderPatchRva = 0x9FC654;
constexpr std::uintptr_t G10WriterPatchRva = 0x9F95A2;

static_assert(FoundationPatterns.size()
    >= G0PatternCount + G10PatternCount);

[[nodiscard]] auto G0Patterns() noexcept -> std::span<const NativePattern> {
    return std::span<const NativePattern>{FoundationPatterns}
        .first<G0PatternCount>();
}

[[nodiscard]] auto G10Patterns() noexcept -> std::span<const NativePattern> {
    return std::span<const NativePattern>{FoundationPatterns}
        .last<G10PatternCount>();
}

[[nodiscard]] auto CanEncodeRel32Target(
        std::uintptr_t instructionRva,
        std::uintptr_t targetRva) noexcept -> bool {
    if (instructionRva
            > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
        return false;
    }
    const auto next = instructionRva + 5U;
    if (targetRva >= next) {
        return targetRva - next
            <= static_cast<std::uintptr_t>(
                (std::numeric_limits<std::int32_t>::max)());
    }
    return next - targetRva
        <= static_cast<std::uintptr_t>(
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::int32_t>::max)())
            + 1ULL);
}

[[nodiscard]] auto VerifyPatterns(
        const NativePublicationLeaseView& lease,
        const PublicationAdapterNativeCallbacks& callbacks,
        std::span<const NativePattern> patterns) noexcept -> bool {
    for (const auto& pattern : patterns) {
        if (!lease.IsHeld()) return false;
        if (!callbacks.verifyPattern(callbacks.context, pattern)) return false;
        if (!lease.IsHeld()) return false;
    }
    return true;
}

[[nodiscard]] auto HasCompleteCallbacks(
        const PublicationAdapterNativeCallbacks& callbacks) noexcept -> bool {
    return callbacks.context
        && callbacks.verifyPattern
        && callbacks.patchRel32
        && callbacks.patchU32
        && callbacks.writeCodecByte
        && callbacks.flushInstructionCache
        && callbacks.markG0TailCommitted
        && callbacks.activateG0Guard
        && callbacks.markG0CapCommitted
        && callbacks.markG10ReaderCommitted
        && callbacks.markG10WriterCommitted
        && callbacks.reserveProcessLifetime
        && callbacks.publishReadiness
        && callbacks.markPoisoned;
}

} // namespace

auto PublicationAdapterSet::Bind(
        const PublicationAdapterTargets& targets,
        const PublicationAdapterNativeCallbacks& callbacks) noexcept -> bool {
    Reset();
    if (!HasCompleteCallbacks(callbacks)
            || targets.g0DescriptionRelayRva == 0
            || targets.g10ReaderRelayRva == 0
            || targets.g10WriterRelayRva == 0) {
        return false;
    }
    targets_ = targets;
    callbacks_ = callbacks;
    bound_ = true;
    return true;
}

auto PublicationAdapterSet::Reset() noexcept -> void {
    targets_ = {};
    callbacks_ = {};
    g0Plan_.reset();
    g10Plan_.reset();
    codecPlan_.reset();
    bound_ = false;
}

auto PublicationAdapterSet::CoordinatorCallbacks() noexcept
        -> PublicationCoordinatorCallbacks {
    return PublicationCoordinatorCallbacks{
        .context = this,
        .preflightG0 = &PreflightG0Thunk,
        .preflightG10 = &PreflightG10Thunk,
        .preflightCodec = &PreflightCodecThunk,
        .reserveProcessLifetime = &ReserveProcessLifetimeThunk,
        .commitG0 = &CommitG0Thunk,
        .commitG10 = &CommitG10Thunk,
        .commitCodec = &CommitCodecThunk,
        .publishReadiness = &PublishReadinessThunk,
        .markPoisoned = &MarkPoisonedThunk,
    };
}

auto PublicationAdapterSet::PreflightG0(
        const NativePublicationLeaseView& lease) noexcept -> bool {
    g0Plan_.reset();
    if (!bound_
            || !CanEncodeRel32Target(
                G0TailPatchRva, targets_.g0DescriptionRelayRva)
            || !VerifyPatterns(lease, callbacks_, G0Patterns())) {
        return false;
    }
    g0Plan_.emplace(PreparedG0Plan{
        .descriptionRelayRva = targets_.g0DescriptionRelayRva,
    });
    return lease.IsHeld();
}

auto PublicationAdapterSet::PreflightG10(
        const NativePublicationLeaseView& lease) noexcept -> bool {
    g10Plan_.reset();
    if (!bound_
            || !CanEncodeRel32Target(
                G10ReaderPatchRva, targets_.g10ReaderRelayRva)
            || !CanEncodeRel32Target(
                G10WriterPatchRva, targets_.g10WriterRelayRva)
            || !VerifyPatterns(lease, callbacks_, G10Patterns())) {
        return false;
    }
    g10Plan_.emplace(PreparedG10Plan{
        .readerRelayRva = targets_.g10ReaderRelayRva,
        .writerRelayRva = targets_.g10WriterRelayRva,
    });
    return lease.IsHeld();
}

auto PublicationAdapterSet::PreflightCodec(
        const NativePublicationLeaseView& lease) noexcept -> bool {
    codecPlan_.reset();
    if (!bound_) return false;
    auto result = PreflightPreparedCodecPatchSet(
        lease,
        targets_.codec,
        CodecPatchPreflightCallbacks{
            .context = callbacks_.context,
            .verifyPattern = callbacks_.verifyPattern,
        });
    if (result.status != CodecPatchPreflightStatus::Prepared
            || !result.plan.has_value()
            || !lease.IsHeld()) {
        return false;
    }
    codecPlan_.emplace(std::move(*result.plan));
    return true;
}

auto PublicationAdapterSet::CommitG0(
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    if (!bound_ || !g0Plan_.has_value() || !lease.IsHeld()) {
        return PublicationCommitOutcome::RejectedWithoutMutation;
    }

    constexpr auto tailExpected = std::span<const std::uint8_t>{
        DescriptionTailBytes}.first<8>();
    if (!callbacks_.patchRel32(
            callbacks_.context,
            G0TailPatchRva,
            tailExpected,
            g0Plan_->descriptionRelayRva,
            tailExpected.size())) {
        return PublicationCommitOutcome::Uncertain;
    }
    callbacks_.markG0TailCommitted(callbacks_.context);
    if (!lease.IsHeld()) return PublicationCommitOutcome::Uncertain;

    // The dormant relay must conservatively tolerate an extended cap before
    // the cap write is attempted. Global operational readiness remains zero.
    callbacks_.activateG0Guard(callbacks_.context);
    if (!lease.IsHeld()) return PublicationCommitOutcome::Uncertain;

    constexpr auto capExpected = std::span<const std::uint8_t>{
        LoaderCountBytes}.subspan<7, 4>();
    if (!callbacks_.patchU32(
            callbacks_.context,
            G0CountImmediateRva,
            capExpected,
            SerializedSentinel)) {
        return PublicationCommitOutcome::Uncertain;
    }
    callbacks_.markG0CapCommitted(callbacks_.context);
    return lease.IsHeld()
        ? PublicationCommitOutcome::Committed
        : PublicationCommitOutcome::Uncertain;
}

auto PublicationAdapterSet::CommitG10(
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    if (!bound_ || !g10Plan_.has_value() || !lease.IsHeld()) {
        return PublicationCommitOutcome::RejectedWithoutMutation;
    }

    constexpr auto readerExpected = std::span<const std::uint8_t>{
        SaveObjectReaderEnvelopeSeamBytes}.first<5>();
    if (!callbacks_.patchRel32(
            callbacks_.context,
            G10ReaderPatchRva,
            readerExpected,
            g10Plan_->readerRelayRva,
            readerExpected.size())) {
        return PublicationCommitOutcome::Uncertain;
    }
    callbacks_.markG10ReaderCommitted(callbacks_.context);
    if (!lease.IsHeld()) return PublicationCommitOutcome::Uncertain;

    constexpr auto writerExpected = std::span<const std::uint8_t>{
        SaveObjectWriterTransactionSeamBytes}.first<5>();
    if (!callbacks_.patchRel32(
            callbacks_.context,
            G10WriterPatchRva,
            writerExpected,
            g10Plan_->writerRelayRva,
            writerExpected.size())) {
        return PublicationCommitOutcome::Uncertain;
    }
    callbacks_.markG10WriterCommitted(callbacks_.context);
    return lease.IsHeld()
        ? PublicationCommitOutcome::Committed
        : PublicationCommitOutcome::Uncertain;
}

auto PublicationAdapterSet::CommitCodec(
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    if (!bound_ || !codecPlan_.has_value() || !lease.IsHeld()) {
        return PublicationCommitOutcome::RejectedWithoutMutation;
    }
    const auto result = CommitPreflightedCodecPatchSet(
        lease,
        *codecPlan_,
        CodecPatchCommitCallbacks{
            .context = callbacks_.context,
            .writeByte = callbacks_.writeCodecByte,
            .flushInstructionCache = callbacks_.flushInstructionCache,
        });
    if (result.status == CodecPatchCommitStatus::Active) {
        return PublicationCommitOutcome::Committed;
    }
    if (result.status == CodecPatchCommitStatus::QuiescenceRequired
            && !result.mutationAttempted) {
        return PublicationCommitOutcome::RejectedWithoutMutation;
    }
    return PublicationCommitOutcome::Uncertain;
}

auto PublicationAdapterSet::PreflightG0Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool {
    return context
        && static_cast<PublicationAdapterSet*>(context)->PreflightG0(lease);
}

auto PublicationAdapterSet::PreflightG10Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool {
    return context
        && static_cast<PublicationAdapterSet*>(context)->PreflightG10(lease);
}

auto PublicationAdapterSet::PreflightCodecThunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool {
    return context
        && static_cast<PublicationAdapterSet*>(context)->PreflightCodec(lease);
}

auto PublicationAdapterSet::CommitG0Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    return context
        ? static_cast<PublicationAdapterSet*>(context)->CommitG0(lease)
        : PublicationCommitOutcome::RejectedWithoutMutation;
}

auto PublicationAdapterSet::CommitG10Thunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    return context
        ? static_cast<PublicationAdapterSet*>(context)->CommitG10(lease)
        : PublicationCommitOutcome::RejectedWithoutMutation;
}

auto PublicationAdapterSet::CommitCodecThunk(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    return context
        ? static_cast<PublicationAdapterSet*>(context)->CommitCodec(lease)
        : PublicationCommitOutcome::RejectedWithoutMutation;
}

auto PublicationAdapterSet::ReserveProcessLifetimeThunk(
        void* context) noexcept -> void {
    auto& self = *static_cast<PublicationAdapterSet*>(context);
    self.callbacks_.reserveProcessLifetime(self.callbacks_.context);
}

auto PublicationAdapterSet::PublishReadinessThunk(
        void* context) noexcept -> void {
    auto& self = *static_cast<PublicationAdapterSet*>(context);
    self.callbacks_.publishReadiness(self.callbacks_.context);
}

auto PublicationAdapterSet::MarkPoisonedThunk(
        void* context) noexcept -> void {
    auto& self = *static_cast<PublicationAdapterSet*>(context);
    self.callbacks_.markPoisoned(self.callbacks_.context);
}

} // namespace ruffneckk::isc12
