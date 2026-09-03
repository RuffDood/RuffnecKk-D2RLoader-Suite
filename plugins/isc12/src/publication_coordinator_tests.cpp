#include "isc12_publication_coordinator.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using ruffneckk::isc12::NativePublicationLeaseView;
using ruffneckk::isc12::PublicationCommitOutcome;
using ruffneckk::isc12::PublicationCoordinator;
using ruffneckk::isc12::PublicationCoordinatorCallbacks;
using ruffneckk::isc12::PublicationCoordinatorState;
using ruffneckk::isc12::PublicationCoordinatorStatus;

enum class Event : std::uint8_t {
    None,
    PreflightG0,
    PreflightG10,
    PreflightCodec,
    Reserve,
    CommitG0,
    CommitG10,
    CommitCodec,
    PublishReadiness,
    MarkPoisoned,
};

struct Fixture {
    bool leaseHeld{true};
    Event revokeAt{Event::None};
    Event mutateAt{Event::None};
    Event reenterAt{Event::None};
    bool reenterReadinessActivation{};
    bool preflightG0{true};
    bool preflightG10{true};
    bool preflightCodec{true};
    PublicationCommitOutcome commitG0{PublicationCommitOutcome::Committed};
    PublicationCommitOutcome commitG10{PublicationCommitOutcome::Committed};
    PublicationCommitOutcome commitCodec{
        PublicationCommitOutcome::Committed};
    PublicationCoordinator* coordinator{};
    const NativePublicationLeaseView* activeLease{};
    const PublicationCoordinatorCallbacks* activeCallbacks{};
    std::array<Event, 16> events{};
    std::size_t eventCount{};
    std::size_t readinessPublishCount{};
    std::size_t poisonCount{};
    std::size_t nativeMutationCount{};
    std::size_t reentryCount{};
    std::size_t readinessReentryCount{};
    std::size_t stageLeaseCount{};
    bool everyStageReceivedActiveLease{true};
    bool readinessWasFalseAtPublish{};
    bool readinessWasFalseAtPoison{};
    PublicationCoordinatorStatus reentryStatus{
        PublicationCoordinatorStatus::Active};
    PublicationCoordinatorStatus readinessReentryStatus{
        PublicationCoordinatorStatus::Active};
};

auto Check(bool condition, const char* expression, int line) -> void {
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression
              << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

auto Record(Fixture& fixture, Event event) noexcept -> void {
    if (fixture.eventCount < fixture.events.size()) {
        fixture.events[fixture.eventCount] = event;
        ++fixture.eventCount;
    }
    if (fixture.revokeAt == event) fixture.leaseHeld = false;
    if (fixture.mutateAt == event) ++fixture.nativeMutationCount;
    if (fixture.reenterAt == event
            && fixture.coordinator
            && fixture.activeLease
            && fixture.activeCallbacks) {
        ++fixture.reentryCount;
        fixture.reentryStatus = fixture.coordinator->Publish(
            *fixture.activeLease,
            *fixture.activeCallbacks);
    }
}

auto ValidateLease(void* context) noexcept -> bool {
    return static_cast<Fixture*>(context)->leaseHeld;
}

auto RecordStage(
        Fixture& fixture,
        Event event,
        const NativePublicationLeaseView& lease) noexcept -> void {
    ++fixture.stageLeaseCount;
    fixture.everyStageReceivedActiveLease =
        fixture.everyStageReceivedActiveLease
        && fixture.activeLease == &lease;
    Record(fixture, event);
}

auto PreflightG0(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    RecordStage(fixture, Event::PreflightG0, lease);
    return fixture.preflightG0;
}

auto PreflightG10(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    RecordStage(fixture, Event::PreflightG10, lease);
    return fixture.preflightG10;
}

auto PreflightCodec(
        void* context,
        const NativePublicationLeaseView& lease) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    RecordStage(fixture, Event::PreflightCodec, lease);
    return fixture.preflightCodec;
}

auto Reserve(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    Record(fixture, Event::Reserve);
}

auto CommitG0(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    auto& fixture = *static_cast<Fixture*>(context);
    RecordStage(fixture, Event::CommitG0, lease);
    return fixture.commitG0;
}

auto CommitG10(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    auto& fixture = *static_cast<Fixture*>(context);
    RecordStage(fixture, Event::CommitG10, lease);
    return fixture.commitG10;
}

auto CommitCodec(
        void* context,
        const NativePublicationLeaseView& lease) noexcept
        -> PublicationCommitOutcome {
    auto& fixture = *static_cast<Fixture*>(context);
    RecordStage(fixture, Event::CommitCodec, lease);
    return fixture.commitCodec;
}

auto PublishReadiness(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    Record(fixture, Event::PublishReadiness);
    ++fixture.readinessPublishCount;
    if (fixture.reenterReadinessActivation && fixture.coordinator) {
        ++fixture.readinessReentryCount;
        fixture.readinessReentryStatus =
            fixture.coordinator->PublishReadinessAfterStartupCommit();
    }
    fixture.readinessWasFalseAtPublish = fixture.coordinator
        && !fixture.coordinator->IsReadinessPublished();
}

auto MarkPoisoned(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    Record(fixture, Event::MarkPoisoned);
    ++fixture.poisonCount;
    fixture.readinessWasFalseAtPoison = fixture.coordinator
        && !fixture.coordinator->IsReadinessPublished()
        && fixture.coordinator->State()
            == PublicationCoordinatorState::Poisoned;
}

auto Callbacks(Fixture& fixture) noexcept
        -> PublicationCoordinatorCallbacks {
    return PublicationCoordinatorCallbacks{
        .context = &fixture,
        .preflightG0 = &PreflightG0,
        .preflightG10 = &PreflightG10,
        .preflightCodec = &PreflightCodec,
        .reserveProcessLifetime = &Reserve,
        .commitG0 = &CommitG0,
        .commitG10 = &CommitG10,
        .commitCodec = &CommitCodec,
        .publishReadiness = &PublishReadiness,
        .markPoisoned = &MarkPoisoned,
    };
}

auto Lease(Fixture& fixture) noexcept
        -> NativePublicationLeaseView {
    return NativePublicationLeaseView::ForTesting(
        &fixture,
        &ValidateLease);
}

auto Invoke(
        PublicationCoordinator& coordinator,
        Fixture& fixture,
        const NativePublicationLeaseView& lease,
        const PublicationCoordinatorCallbacks& callbacks) noexcept
        -> PublicationCoordinatorStatus {
    fixture.activeLease = &lease;
    fixture.activeCallbacks = &callbacks;
    const auto status = coordinator.Publish(lease, callbacks);
    fixture.activeLease = nullptr;
    fixture.activeCallbacks = nullptr;
    return status;
}

template <std::size_t Size>
auto ExpectEvents(
        const Fixture& fixture,
        const std::array<Event, Size>& expected) -> void {
    CHECK(fixture.eventCount == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(fixture.events[index] == expected[index]);
    }
}

auto TestAbsentLeaseRequiresQuiescence() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{.coordinator = &coordinator};
    NativePublicationLeaseView absent;

    CHECK(Invoke(coordinator, fixture, absent, Callbacks(fixture))
        == PublicationCoordinatorStatus::QuiescenceRequired);
    CHECK(coordinator.State() == PublicationCoordinatorState::Fresh);
    CHECK(!coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.eventCount == 0U);
    CHECK(fixture.poisonCount == 0U);
}

auto TestInitialLoadLeaseFactoryTracksItsWindow() -> void {
    Fixture fixture;
    auto lease = NativePublicationLeaseView::ForInitialLoad(
        &fixture, &ValidateLease);
    CHECK(lease.IsHeld());
    fixture.leaseHeld = false;
    CHECK(!lease.IsHeld());
}

auto TestEveryPreflightFailureRejectsBeforeReservation() -> void {
    for (std::size_t rejectedIndex = 0U; rejectedIndex < 3U;
            ++rejectedIndex) {
        PublicationCoordinator coordinator;
        Fixture fixture{.coordinator = &coordinator};
        if (rejectedIndex == 0U) fixture.preflightG0 = false;
        if (rejectedIndex == 1U) fixture.preflightG10 = false;
        if (rejectedIndex == 2U) fixture.preflightCodec = false;
        auto lease = Lease(fixture);

        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::RejectedBeforeMutation);
        CHECK(fixture.eventCount == rejectedIndex + 1U);
        CHECK(fixture.events[0] == Event::PreflightG0);
        if (rejectedIndex >= 1U) {
            CHECK(fixture.events[1] == Event::PreflightG10);
        }
        if (rejectedIndex >= 2U) {
            CHECK(fixture.events[2] == Event::PreflightCodec);
        }
        CHECK(coordinator.State() == PublicationCoordinatorState::Fresh);
        CHECK(!coordinator.IsProcessLifetimeReserved());
        CHECK(!coordinator.IsReadinessPublished());
        CHECK(fixture.readinessPublishCount == 0U);
        CHECK(fixture.poisonCount == 0U);
    }
}

auto TestPreflightOrderAndRetry() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .preflightG10 = false,
        .coordinator = &coordinator,
    };
    {
        auto lease = Lease(fixture);
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::RejectedBeforeMutation);
    }
    ExpectEvents(fixture, std::array{
        Event::PreflightG0,
        Event::PreflightG10,
    });
    CHECK(coordinator.State() == PublicationCoordinatorState::Fresh);
    CHECK(!coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 0U);
    CHECK(fixture.poisonCount == 0U);

    fixture.preflightG10 = true;
    fixture.eventCount = 0U;
    fixture.stageLeaseCount = 0U;
    {
        auto lease = Lease(fixture);
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::CommittedPendingReadiness);
    }
    ExpectEvents(fixture, std::array{
        Event::PreflightG0,
        Event::PreflightG10,
        Event::PreflightCodec,
        Event::Reserve,
        Event::CommitG0,
        Event::CommitG10,
        Event::CommitCodec,
    });
    CHECK(coordinator.State()
        == PublicationCoordinatorState::CommittedPendingReadiness);
    CHECK(coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 0U);

    CHECK(coordinator.PublishReadinessAfterStartupCommit()
        == PublicationCoordinatorStatus::Active);
    CHECK(fixture.events[fixture.eventCount - 1U]
        == Event::PublishReadiness);
    CHECK(coordinator.State() == PublicationCoordinatorState::Active);
    CHECK(coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 1U);
    CHECK(fixture.readinessWasFalseAtPublish);
    CHECK(fixture.poisonCount == 0U);
    CHECK(fixture.stageLeaseCount == 6U);
    CHECK(fixture.everyStageReceivedActiveLease);

    const auto previousEventCount = fixture.eventCount;
    auto activeLease = Lease(fixture);
    CHECK(Invoke(
        coordinator, fixture, activeLease, Callbacks(fixture))
        == PublicationCoordinatorStatus::Active);
    CHECK(fixture.eventCount == previousEventCount);
    CHECK(fixture.readinessPublishCount == 1U);
}

auto TestInitiallyRevokedLeaseRequiresQuiescence() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .leaseHeld = false,
        .coordinator = &coordinator,
    };
    auto lease = Lease(fixture);

    CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
        == PublicationCoordinatorStatus::QuiescenceRequired);
    CHECK(coordinator.State() == PublicationCoordinatorState::Fresh);
    CHECK(!coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.eventCount == 0U);
    CHECK(fixture.poisonCount == 0U);
}

auto TestLeaseLossAtEveryPreflightBoundaryRequiresQuiescence() -> void {
    constexpr std::array preflightEvents{
        Event::PreflightG0,
        Event::PreflightG10,
        Event::PreflightCodec,
    };
    for (std::size_t revokedIndex = 0U;
            revokedIndex < preflightEvents.size(); ++revokedIndex) {
        PublicationCoordinator coordinator;
        Fixture fixture{
            .revokeAt = preflightEvents[revokedIndex],
            .coordinator = &coordinator,
        };
        auto lease = Lease(fixture);

        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::QuiescenceRequired);
        CHECK(fixture.eventCount == revokedIndex + 1U);
        CHECK(fixture.events[0] == Event::PreflightG0);
        if (revokedIndex >= 1U) {
            CHECK(fixture.events[1] == Event::PreflightG10);
        }
        if (revokedIndex >= 2U) {
            CHECK(fixture.events[2] == Event::PreflightCodec);
        }
        CHECK(coordinator.State() == PublicationCoordinatorState::Fresh);
        CHECK(!coordinator.IsProcessLifetimeReserved());
        CHECK(!coordinator.IsReadinessPublished());
        CHECK(fixture.readinessPublishCount == 0U);
        CHECK(fixture.poisonCount == 0U);
    }
}

auto TestRevokedAfterReservationIsTerminalWithoutMutation() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .revokeAt = Event::Reserve,
        .coordinator = &coordinator,
    };
    {
        auto lease = Lease(fixture);
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::ReservedWithoutMutation);
    }
    ExpectEvents(fixture, std::array{
        Event::PreflightG0,
        Event::PreflightG10,
        Event::PreflightCodec,
        Event::Reserve,
    });
    CHECK(coordinator.State() == PublicationCoordinatorState::Reserved);
    CHECK(coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 0U);

    fixture.leaseHeld = true;
    fixture.revokeAt = Event::None;
    const auto previousEventCount = fixture.eventCount;
    auto secondLease = Lease(fixture);
    CHECK(Invoke(
        coordinator, fixture, secondLease, Callbacks(fixture))
        == PublicationCoordinatorStatus::ReservedWithoutMutation);
    CHECK(fixture.eventCount == previousEventCount);
    CHECK(fixture.poisonCount == 0U);
}

auto TestFirstCommitCanProveNoMutation() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .commitG0 = PublicationCommitOutcome::RejectedWithoutMutation,
        .coordinator = &coordinator,
    };
    auto lease = Lease(fixture);

    CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
        == PublicationCoordinatorStatus::ReservedWithoutMutation);
    ExpectEvents(fixture, std::array{
        Event::PreflightG0,
        Event::PreflightG10,
        Event::PreflightCodec,
        Event::Reserve,
        Event::CommitG0,
    });
    CHECK(coordinator.State() == PublicationCoordinatorState::Reserved);
    CHECK(coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 0U);
    CHECK(fixture.poisonCount == 0U);
}

auto TestUncertainOrPostCommitFailurePoisons() -> void {
    {
        PublicationCoordinator coordinator;
        Fixture fixture{
            .commitG0 = PublicationCommitOutcome::Uncertain,
            .coordinator = &coordinator,
        };
        auto lease = Lease(fixture);
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(coordinator.State() == PublicationCoordinatorState::Poisoned);
        CHECK(coordinator.IsProcessLifetimeReserved());
        CHECK(!coordinator.IsReadinessPublished());
        CHECK(fixture.readinessPublishCount == 0U);
        CHECK(fixture.poisonCount == 1U);
        CHECK(fixture.readinessWasFalseAtPoison);
    }
    {
        PublicationCoordinator coordinator;
        Fixture fixture{
            .commitG10 = PublicationCommitOutcome::RejectedWithoutMutation,
            .coordinator = &coordinator,
        };
        auto lease = Lease(fixture);
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::Poisoned);
        ExpectEvents(fixture, std::array{
            Event::PreflightG0,
            Event::PreflightG10,
            Event::PreflightCodec,
            Event::Reserve,
            Event::CommitG0,
            Event::CommitG10,
            Event::MarkPoisoned,
        });
        CHECK(coordinator.State() == PublicationCoordinatorState::Poisoned);
        CHECK(!coordinator.IsReadinessPublished());
        CHECK(fixture.readinessPublishCount == 0U);
        CHECK(fixture.poisonCount == 1U);
        CHECK(fixture.readinessWasFalseAtPoison);

        const auto previousEventCount = fixture.eventCount;
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(fixture.eventCount == previousEventCount);
        CHECK(fixture.poisonCount == 1U);
    }
}

auto TestLeaseLossAfterNativeCommitPoisons() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .revokeAt = Event::CommitCodec,
        .coordinator = &coordinator,
    };
    auto lease = Lease(fixture);

    CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
        == PublicationCoordinatorStatus::Poisoned);
    ExpectEvents(fixture, std::array{
        Event::PreflightG0,
        Event::PreflightG10,
        Event::PreflightCodec,
        Event::Reserve,
        Event::CommitG0,
        Event::CommitG10,
        Event::CommitCodec,
        Event::MarkPoisoned,
    });
    CHECK(coordinator.State() == PublicationCoordinatorState::Poisoned);
    CHECK(coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 0U);
    CHECK(fixture.poisonCount == 1U);
    CHECK(fixture.readinessWasFalseAtPoison);
}

auto TestLeaseLossAtEveryCommitBoundaryPoisonsOnce() -> void {
    constexpr std::array commitEvents{
        Event::CommitG0,
        Event::CommitG10,
        Event::CommitCodec,
    };
    for (const auto revokeAt : commitEvents) {
        PublicationCoordinator coordinator;
        Fixture fixture{
            .revokeAt = revokeAt,
            .coordinator = &coordinator,
        };
        auto lease = Lease(fixture);

        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(coordinator.State() == PublicationCoordinatorState::Poisoned);
        CHECK(coordinator.IsProcessLifetimeReserved());
        CHECK(!coordinator.IsReadinessPublished());
        CHECK(fixture.readinessPublishCount == 0U);
        CHECK(fixture.poisonCount == 1U);
        CHECK(fixture.events[fixture.eventCount - 1U]
            == Event::MarkPoisoned);
        CHECK(fixture.readinessWasFalseAtPoison);

        fixture.leaseHeld = true;
        const auto previousEventCount = fixture.eventCount;
        CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(fixture.eventCount == previousEventCount);
        CHECK(fixture.poisonCount == 1U);
    }
}

auto TestReadinessRequiresStartupCommitAndIsIdempotent() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .reenterReadinessActivation = true,
        .coordinator = &coordinator,
    };
    auto lease = Lease(fixture);

    CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
        == PublicationCoordinatorStatus::CommittedPendingReadiness);
    ExpectEvents(fixture, std::array{
        Event::PreflightG0,
        Event::PreflightG10,
        Event::PreflightCodec,
        Event::Reserve,
        Event::CommitG0,
        Event::CommitG10,
        Event::CommitCodec,
    });
    CHECK(coordinator.State()
        == PublicationCoordinatorState::CommittedPendingReadiness);
    CHECK(coordinator.IsProcessLifetimeReserved());
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 0U);
    CHECK(fixture.poisonCount == 0U);

    CHECK(coordinator.PublishReadinessAfterStartupCommit()
        == PublicationCoordinatorStatus::Active);
    CHECK(coordinator.State() == PublicationCoordinatorState::Active);
    CHECK(coordinator.IsReadinessPublished());
    CHECK(fixture.readinessPublishCount == 1U);
    CHECK(fixture.readinessWasFalseAtPublish);
    CHECK(fixture.readinessReentryCount == 1U);
    CHECK(fixture.readinessReentryStatus
        == PublicationCoordinatorStatus::RejectedBeforeMutation);

    const auto eventCount = fixture.eventCount;
    CHECK(coordinator.PublishReadinessAfterStartupCommit()
        == PublicationCoordinatorStatus::Active);
    CHECK(fixture.eventCount == eventCount);
    CHECK(fixture.readinessPublishCount == 1U);

    PublicationCoordinator failedCoordinator;
    Fixture failedFixture{.coordinator = &failedCoordinator};
    auto failedLease = Lease(failedFixture);
    CHECK(Invoke(
        failedCoordinator,
        failedFixture,
        failedLease,
        Callbacks(failedFixture))
        == PublicationCoordinatorStatus::CommittedPendingReadiness);
    CHECK(failedCoordinator.PoisonBeforeStartupReadiness()
        == PublicationCoordinatorStatus::Poisoned);
    CHECK(failedCoordinator.State()
        == PublicationCoordinatorState::Poisoned);
    CHECK(!failedCoordinator.IsReadinessPublished());
    CHECK(failedFixture.readinessPublishCount == 0U);
    CHECK(failedFixture.poisonCount == 1U);
    CHECK(failedFixture.readinessWasFalseAtPoison);
    CHECK(failedCoordinator.PoisonBeforeStartupReadiness()
        == PublicationCoordinatorStatus::Poisoned);
    CHECK(failedFixture.poisonCount == 1U);
}

auto TestMutateThenReturnUncertainPoisons() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .mutateAt = Event::CommitG0,
        .commitG0 = PublicationCommitOutcome::Uncertain,
        .coordinator = &coordinator,
    };
    auto lease = Lease(fixture);

    CHECK(Invoke(coordinator, fixture, lease, Callbacks(fixture))
        == PublicationCoordinatorStatus::Poisoned);
    CHECK(fixture.nativeMutationCount == 1U);
    CHECK(fixture.poisonCount == 1U);
    CHECK(fixture.readinessPublishCount == 0U);
    CHECK(!coordinator.IsReadinessPublished());
    CHECK(fixture.readinessWasFalseAtPoison);
}

auto TestReentryAfterReservationIsRejected() -> void {
    PublicationCoordinator coordinator;
    Fixture fixture{
        .reenterAt = Event::Reserve,
        .coordinator = &coordinator,
    };
    auto callbacks = Callbacks(fixture);
    auto lease = Lease(fixture);

    CHECK(Invoke(coordinator, fixture, lease, callbacks)
        == PublicationCoordinatorStatus::CommittedPendingReadiness);
    CHECK(fixture.reentryCount == 1U);
    CHECK(fixture.reentryStatus
        == PublicationCoordinatorStatus::RejectedBeforeMutation);
    CHECK(fixture.readinessPublishCount == 0U);
    CHECK(fixture.poisonCount == 0U);
    CHECK(coordinator.PublishReadinessAfterStartupCommit()
        == PublicationCoordinatorStatus::Active);
    CHECK(fixture.readinessPublishCount == 1U);
}

auto TestEveryMissingCallbackRejectsBeforeMutation() -> void {
    for (std::size_t missingIndex = 0U; missingIndex < 9U;
            ++missingIndex) {
        PublicationCoordinator coordinator;
        Fixture fixture{.coordinator = &coordinator};
        auto callbacks = Callbacks(fixture);
        if (missingIndex == 0U) callbacks.preflightG0 = nullptr;
        if (missingIndex == 1U) callbacks.preflightG10 = nullptr;
        if (missingIndex == 2U) callbacks.preflightCodec = nullptr;
        if (missingIndex == 3U) {
            callbacks.reserveProcessLifetime = nullptr;
        }
        if (missingIndex == 4U) callbacks.commitG0 = nullptr;
        if (missingIndex == 5U) callbacks.commitG10 = nullptr;
        if (missingIndex == 6U) callbacks.commitCodec = nullptr;
        if (missingIndex == 7U) callbacks.publishReadiness = nullptr;
        if (missingIndex == 8U) callbacks.markPoisoned = nullptr;
        auto lease = Lease(fixture);

        CHECK(Invoke(coordinator, fixture, lease, callbacks)
            == PublicationCoordinatorStatus::RejectedBeforeMutation);
        CHECK(coordinator.State() == PublicationCoordinatorState::Fresh);
        CHECK(!coordinator.IsProcessLifetimeReserved());
        CHECK(!coordinator.IsReadinessPublished());
        CHECK(fixture.eventCount == 0U);
        CHECK(fixture.stageLeaseCount == 0U);
        CHECK(fixture.readinessPublishCount == 0U);
        CHECK(fixture.poisonCount == 0U);
    }
}

} // namespace

int main() {
    TestAbsentLeaseRequiresQuiescence();
    TestInitialLoadLeaseFactoryTracksItsWindow();
    TestEveryPreflightFailureRejectsBeforeReservation();
    TestPreflightOrderAndRetry();
    TestInitiallyRevokedLeaseRequiresQuiescence();
    TestLeaseLossAtEveryPreflightBoundaryRequiresQuiescence();
    TestRevokedAfterReservationIsTerminalWithoutMutation();
    TestFirstCommitCanProveNoMutation();
    TestUncertainOrPostCommitFailurePoisons();
    TestLeaseLossAfterNativeCommitPoisons();
    TestLeaseLossAtEveryCommitBoundaryPoisonsOnce();
    TestReadinessRequiresStartupCommitAndIsIdempotent();
    TestMutateThenReturnUncertainPoisons();
    TestReentryAfterReservationIsRejected();
    TestEveryMissingCallbackRejectsBeforeMutation();
    return EXIT_SUCCESS;
}
