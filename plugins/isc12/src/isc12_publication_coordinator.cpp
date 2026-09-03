#include "isc12_publication_coordinator.hpp"

#include <array>

namespace ruffneckk::isc12 {
namespace {

[[nodiscard]] auto HasCompleteCallbackSet(
        const PublicationCoordinatorCallbacks& callbacks) noexcept -> bool {
    return callbacks.preflightG0
        && callbacks.preflightG10
        && callbacks.preflightCodec
        && callbacks.reserveProcessLifetime
        && callbacks.commitG0
        && callbacks.commitG10
        && callbacks.commitCodec
        && callbacks.publishReadiness
        && callbacks.markPoisoned;
}

} // namespace

auto PublicationCoordinator::Publish(
        const NativePublicationLeaseView& quiescence,
        const PublicationCoordinatorCallbacks& callbacks) noexcept
        -> PublicationCoordinatorStatus {
    if (attemptInProgress_) {
        return PublicationCoordinatorStatus::RejectedBeforeMutation;
    }
    if (state_ == PublicationCoordinatorState::Active) {
        return PublicationCoordinatorStatus::Active;
    }
    if (state_
            == PublicationCoordinatorState::CommittedPendingReadiness) {
        return PublicationCoordinatorStatus::CommittedPendingReadiness;
    }
    if (state_ == PublicationCoordinatorState::Reserved) {
        return PublicationCoordinatorStatus::ReservedWithoutMutation;
    }
    if (state_ == PublicationCoordinatorState::Poisoned) {
        return PublicationCoordinatorStatus::Poisoned;
    }
    if (!quiescence.IsHeld()) {
        return PublicationCoordinatorStatus::QuiescenceRequired;
    }
    if (!HasCompleteCallbackSet(callbacks)) {
        return PublicationCoordinatorStatus::RejectedBeforeMutation;
    }

    attemptInProgress_ = true;
    const auto finishFresh = [this](
            PublicationCoordinatorStatus status) noexcept {
        attemptInProgress_ = false;
        return status;
    };

    // The fixed array is the canonical preflight order. No reservation or
    // native write callback is reachable until all three stages pass.
    const std::array<PreflightPublicationStageFn, 3> preflights{
        callbacks.preflightG0,
        callbacks.preflightG10,
        callbacks.preflightCodec,
    };
    for (const auto preflight : preflights) {
        if (!quiescence.IsHeld()) {
            return finishFresh(
                PublicationCoordinatorStatus::QuiescenceRequired);
        }
        const bool accepted = preflight(callbacks.context, quiescence);
        if (!quiescence.IsHeld()) {
            return finishFresh(
                PublicationCoordinatorStatus::QuiescenceRequired);
        }
        if (!accepted) {
            return finishFresh(
                PublicationCoordinatorStatus::RejectedBeforeMutation);
        }
    }
    if (!quiescence.IsHeld()) {
        return finishFresh(PublicationCoordinatorStatus::QuiescenceRequired);
    }

    // Enter the terminal reserved phase before invoking the callback. This
    // rejects re-entrance and guarantees that the reservation is attempted at
    // most once. From here onward the prepared relay/state lifetime can never
    // be released before process exit, even when no native byte was changed.
    state_ = PublicationCoordinatorState::Reserved;
    processLifetimeReserved_ = true;
    callbacks.reserveProcessLifetime(callbacks.context);

    const auto finishReservedWithoutMutation = [this]() noexcept {
        attemptInProgress_ = false;
        readinessPublished_ = false;
        return PublicationCoordinatorStatus::ReservedWithoutMutation;
    };
    const auto poison = [this, &callbacks]() noexcept {
        state_ = PublicationCoordinatorState::Poisoned;
        attemptInProgress_ = false;
        readinessPublished_ = false;
        callbacks.markPoisoned(callbacks.context);
        return PublicationCoordinatorStatus::Poisoned;
    };

    if (!quiescence.IsHeld()) {
        return finishReservedWithoutMutation();
    }

    const std::array<CommitPublicationStageFn, 3> commits{
        callbacks.commitG0,
        callbacks.commitG10,
        callbacks.commitCodec,
    };
    std::size_t committedStages = 0U;
    for (const auto commit : commits) {
        if (!quiescence.IsHeld()) {
            return committedStages == 0U
                ? finishReservedWithoutMutation()
                : poison();
        }

        const auto outcome = commit(callbacks.context, quiescence);
        if (outcome == PublicationCommitOutcome::Uncertain) {
            return poison();
        }
        if (outcome
                == PublicationCommitOutcome::RejectedWithoutMutation) {
            return committedStages == 0U
                ? finishReservedWithoutMutation()
                : poison();
        }
        if (outcome != PublicationCommitOutcome::Committed) {
            return poison();
        }

        ++committedStages;
        if (!quiescence.IsHeld()) {
            return poison();
        }
    }

    if (!quiescence.IsHeld()) {
        return poison();
    }

    // Native publication is complete, but plugin-private readiness remains a
    // separate final step. The caller performs that infallible, non-native
    // step before leaving the initial D2RLoaderLoadPlugin publication window.
    readinessContext_ = callbacks.context;
    pendingReadiness_ = callbacks.publishReadiness;
    pendingPoison_ = callbacks.markPoisoned;
    state_ = PublicationCoordinatorState::CommittedPendingReadiness;
    attemptInProgress_ = false;
    return PublicationCoordinatorStatus::CommittedPendingReadiness;
}

auto PublicationCoordinator::PublishReadinessAfterStartupCommit() noexcept
        -> PublicationCoordinatorStatus {
    if (attemptInProgress_) {
        return PublicationCoordinatorStatus::RejectedBeforeMutation;
    }
    if (state_ == PublicationCoordinatorState::Active) {
        return PublicationCoordinatorStatus::Active;
    }
    if (state_ == PublicationCoordinatorState::Poisoned) {
        return PublicationCoordinatorStatus::Poisoned;
    }
    if (state_ == PublicationCoordinatorState::Reserved) {
        return PublicationCoordinatorStatus::ReservedWithoutMutation;
    }
    if (state_
            != PublicationCoordinatorState::CommittedPendingReadiness
            || !pendingReadiness_) {
        return PublicationCoordinatorStatus::RejectedBeforeMutation;
    }

    // Block callback reentry before invoking the infallible readiness step.
    // The callback is cleared first so it remains exactly-once even if local
    // code accidentally tries to activate the coordinator recursively.
    attemptInProgress_ = true;
    const auto publishReadiness = pendingReadiness_;
    void* const context = readinessContext_;
    pendingReadiness_ = nullptr;
    pendingPoison_ = nullptr;
    readinessContext_ = nullptr;
    publishReadiness(context);
    readinessPublished_ = true;
    state_ = PublicationCoordinatorState::Active;
    attemptInProgress_ = false;
    return PublicationCoordinatorStatus::Active;
}

auto PublicationCoordinator::PoisonBeforeStartupReadiness() noexcept
        -> PublicationCoordinatorStatus {
    if (state_ == PublicationCoordinatorState::Poisoned) {
        return PublicationCoordinatorStatus::Poisoned;
    }
    if (state_ == PublicationCoordinatorState::Active) {
        return PublicationCoordinatorStatus::Active;
    }
    if (state_ == PublicationCoordinatorState::Reserved) {
        return PublicationCoordinatorStatus::ReservedWithoutMutation;
    }
    if (state_
            != PublicationCoordinatorState::CommittedPendingReadiness
            || !pendingPoison_) {
        return PublicationCoordinatorStatus::RejectedBeforeMutation;
    }

    state_ = PublicationCoordinatorState::Poisoned;
    readinessPublished_ = false;
    pendingReadiness_ = nullptr;
    const auto poison = pendingPoison_;
    pendingPoison_ = nullptr;
    void* const context = readinessContext_;
    readinessContext_ = nullptr;
    poison(context);
    return PublicationCoordinatorStatus::Poisoned;
}

} // namespace ruffneckk::isc12
