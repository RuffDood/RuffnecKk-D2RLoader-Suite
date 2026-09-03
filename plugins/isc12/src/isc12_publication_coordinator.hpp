#pragma once

#include "isc12_publication_lease.hpp"

#include <cstdint>

namespace ruffneckk::isc12 {

enum class PublicationCoordinatorStatus : std::uint8_t {
    Active,
    CommittedPendingReadiness,
    RejectedBeforeMutation,
    QuiescenceRequired,
    ReservedWithoutMutation,
    Poisoned,
};

enum class PublicationCoordinatorState : std::uint8_t {
    Fresh,
    Reserved,
    CommittedPendingReadiness,
    Active,
    Poisoned,
};

// A commit callback may report RejectedWithoutMutation only when it can prove
// that it did not attempt or publish any native mutation. Every ambiguous
// result is Uncertain and permanently poisons the coordinator.
enum class PublicationCommitOutcome : std::uint8_t {
    Committed,
    RejectedWithoutMutation,
    Uncertain,
};

using PreflightPublicationStageFn = bool (*)(
    void* context,
    const NativePublicationLeaseView& quiescence) noexcept;
using ReservePublicationLifetimeFn = void (*)(void* context) noexcept;
using CommitPublicationStageFn = PublicationCommitOutcome (*)(
    void* context,
    const NativePublicationLeaseView& quiescence) noexcept;
using PublishPublicationReadinessFn = void (*)(void* context) noexcept;
using MarkPublicationPoisonedFn = void (*)(void* context) noexcept;

struct PublicationCoordinatorCallbacks {
    void* context{};
    PreflightPublicationStageFn preflightG0{};
    PreflightPublicationStageFn preflightG10{};
    PreflightPublicationStageFn preflightCodec{};
    // Infallibly makes every prepared relay/state allocation process-bound.
    // It runs exactly once and strictly before the first commit callback.
    ReservePublicationLifetimeFn reserveProcessLifetime{};
    CommitPublicationStageFn commitG0{};
    CommitPublicationStageFn commitG10{};
    CommitPublicationStageFn commitCodec{};
    // Infallibly publishes readiness only after every canonical commit has
    // succeeded inside the still-active publication window.
    PublishPublicationReadinessFn publishReadiness{};
    // Infallibly keeps readiness/operational false and sets the
    // cold-restart-required state. Production terminates the process from this
    // callback when an attempted publication first becomes poisoned.
    MarkPublicationPoisonedFn markPoisoned{};
};

// Production-neutral orchestration for the one-shot ISC12 native publication.
// The caller owns the publication window and supplies its borrowed lease. This
// class deliberately owns no native mutation implementation and exposes no
// rollback path.
class PublicationCoordinator final {
public:
    [[nodiscard]] auto Publish(
        const NativePublicationLeaseView& quiescence,
        const PublicationCoordinatorCallbacks& callbacks) noexcept
        -> PublicationCoordinatorStatus;

    // Called only after every local commit returned Committed, while the
    // initial D2RLoaderLoadPlugin publication window is still active. This
    // step performs no native write.
    [[nodiscard]] auto PublishReadinessAfterStartupCommit() noexcept
        -> PublicationCoordinatorStatus;

    // Defensive terminal path for any caller-side failure after all local
    // commit stages reported success. It never publishes readiness and
    // preserves the cold-restart-required policy.
    [[nodiscard]] auto PoisonBeforeStartupReadiness() noexcept
        -> PublicationCoordinatorStatus;

    [[nodiscard]] auto State() const noexcept
        -> PublicationCoordinatorState {
        return state_;
    }

    [[nodiscard]] auto IsProcessLifetimeReserved() const noexcept -> bool {
        return processLifetimeReserved_;
    }

    [[nodiscard]] auto IsReadinessPublished() const noexcept -> bool {
        return readinessPublished_;
    }

private:
    PublicationCoordinatorState state_{PublicationCoordinatorState::Fresh};
    bool attemptInProgress_{};
    bool processLifetimeReserved_{};
    bool readinessPublished_{};
    void* readinessContext_{};
    PublishPublicationReadinessFn pendingReadiness_{};
    MarkPublicationPoisonedFn pendingPoison_{};
};

} // namespace ruffneckk::isc12
