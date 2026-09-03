#include "isc12_publication_adapters.hpp"
#include "isc12_contract.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using namespace ruffneckk::isc12;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ \
                  << ": " #condition "\n"; \
        std::exit(1); \
    } \
} while (false)

enum class Event : std::uint8_t {
    Verify,
    Reserve,
    PatchRel32,
    PatchU32,
    CodecWrite,
    Flush,
    TailCommitted,
    Guard,
    CapCommitted,
    ReaderCommitted,
    WriterCommitted,
    Readiness,
    Poison,
};

struct Fixture {
    bool leaseHeld{true};
    std::size_t validationCalls{};
    std::size_t failVerifyAttempt{
        (std::numeric_limits<std::size_t>::max)()};
    std::size_t failPatchAttempt{
        (std::numeric_limits<std::size_t>::max)()};
    std::size_t failCodecWriteAttempt{
        (std::numeric_limits<std::size_t>::max)()};
    std::size_t failFlushAttempt{
        (std::numeric_limits<std::size_t>::max)()};
    std::size_t verifyCalls{};
    std::size_t patchCalls{};
    std::size_t codecWriteCalls{};
    std::size_t flushCalls{};
    std::size_t reserveCalls{};
    std::size_t readinessCalls{};
    std::size_t poisonCalls{};
    bool tailCommitted{};
    bool guardActive{};
    bool capCommitted{};
    bool readerCommitted{};
    bool writerCommitted{};
    bool codecReady{};
    bool itemTransportReady{};
    bool persistenceOperational{};
    bool globalOperational{};
    std::vector<Event> events{};
    std::vector<std::string_view> verifiedIds{};
};

auto ValidateLease(void* context) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    ++fixture.validationCalls;
    return fixture.leaseHeld;
}

auto VerifyPattern(
        void* context,
        const NativePattern& pattern) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::Verify);
    fixture.verifiedIds.emplace_back(pattern.id ? pattern.id : "");
    const auto attempt = fixture.verifyCalls++;
    return attempt != fixture.failVerifyAttempt;
}

auto PatchRel32(
        void* context,
        std::uintptr_t,
        std::span<const std::uint8_t> expected,
        std::uintptr_t targetRva,
        std::size_t overwriteSize) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::PatchRel32);
    const auto attempt = fixture.patchCalls++;
    CHECK(!expected.empty());
    CHECK(targetRva != 0U);
    CHECK(overwriteSize >= 5U);
    // The event models a write attempt before the API result is known. A false
    // result is therefore deliberately the mutate-then-false case.
    return attempt != fixture.failPatchAttempt;
}

auto PatchU32(
        void* context,
        std::uintptr_t,
        std::span<const std::uint8_t> expected,
        std::uint32_t replacement) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::PatchU32);
    const auto attempt = fixture.patchCalls++;
    CHECK(expected.size() == sizeof(std::uint32_t));
    CHECK(replacement == SerializedSentinel);
    return attempt != fixture.failPatchAttempt;
}

auto WriteCodecByte(
        void* context,
        std::uintptr_t,
        std::uint8_t,
        std::uint8_t) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::CodecWrite);
    const auto attempt = fixture.codecWriteCalls++;
    return attempt != fixture.failCodecWriteAttempt;
}

auto FlushCodec(
        void* context,
        std::uintptr_t,
        std::size_t size) noexcept -> bool {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::Flush);
    const auto attempt = fixture.flushCalls++;
    CHECK(size != 0U);
    return attempt != fixture.failFlushAttempt;
}

auto MarkTail(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::TailCommitted);
    fixture.tailCommitted = true;
}

auto ActivateGuard(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::Guard);
    fixture.guardActive = true;
    CHECK(!fixture.globalOperational);
}

auto MarkCap(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::CapCommitted);
    fixture.capCommitted = true;
}

auto MarkReader(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::ReaderCommitted);
    fixture.readerCommitted = true;
}

auto MarkWriter(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::WriterCommitted);
    fixture.writerCommitted = true;
}

auto Reserve(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::Reserve);
    ++fixture.reserveCalls;
    CHECK(!fixture.tailCommitted);
    CHECK(!fixture.readerCommitted);
    CHECK(fixture.codecWriteCalls == 0U);
}

auto PublishReadiness(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::Readiness);
    ++fixture.readinessCalls;
    CHECK(fixture.tailCommitted && fixture.capCommitted);
    CHECK(fixture.readerCommitted && fixture.writerCommitted);
    CHECK(fixture.flushCalls == PreparedCodecMutableSiteCount);
    fixture.codecReady = true;
    fixture.itemTransportReady = true;
    fixture.persistenceOperational = true;
    fixture.globalOperational = true;
}

auto MarkPoisoned(void* context) noexcept -> void {
    auto& fixture = *static_cast<Fixture*>(context);
    fixture.events.push_back(Event::Poison);
    ++fixture.poisonCalls;
    fixture.codecReady = false;
    fixture.itemTransportReady = false;
    fixture.persistenceOperational = false;
    fixture.globalOperational = false;
}

auto MakeTargets() noexcept -> PublicationAdapterTargets {
    return PublicationAdapterTargets{
        .g0DescriptionRelayRva = 0x01F00000,
        .g10ReaderRelayRva = 0x01F01000,
        .g10WriterRelayRva = 0x01F02000,
        .codec = CodecPatchActivationTargets::ForTesting(
            0x01F03000,
            0x01F04000,
            0x01F05000,
            0x01F06000,
            0x01F07000,
            0x01F08000,
            0x01F09000,
            0x01F0A000),
    };
}

auto MakeNativeCallbacks(Fixture& fixture) noexcept
        -> PublicationAdapterNativeCallbacks {
    return PublicationAdapterNativeCallbacks{
        .context = &fixture,
        .verifyPattern = &VerifyPattern,
        .patchRel32 = &PatchRel32,
        .patchU32 = &PatchU32,
        .writeCodecByte = &WriteCodecByte,
        .flushInstructionCache = &FlushCodec,
        .markG0TailCommitted = &MarkTail,
        .activateG0Guard = &ActivateGuard,
        .markG0CapCommitted = &MarkCap,
        .markG10ReaderCommitted = &MarkReader,
        .markG10WriterCommitted = &MarkWriter,
        .reserveProcessLifetime = &Reserve,
        .publishReadiness = &PublishReadiness,
        .markPoisoned = &MarkPoisoned,
    };
}

auto PublishFixture(Fixture& fixture) -> PublicationCoordinatorStatus {
    PublicationAdapterSet adapters;
    const auto targets = MakeTargets();
    const auto nativeCallbacks = MakeNativeCallbacks(fixture);
    CHECK(adapters.Bind(targets, nativeCallbacks));

    // Bind owns copies; invalidating the caller-side descriptors cannot alter
    // the targets or callbacks later consumed by preflight and commit.
    auto overwrittenTargets = targets;
    overwrittenTargets = {};
    auto overwrittenCallbacks = nativeCallbacks;
    overwrittenCallbacks = {};
    CHECK(overwrittenTargets.g0DescriptionRelayRva == 0U);
    CHECK(overwrittenCallbacks.context == nullptr);

    const auto lease = NativePublicationLeaseView::ForTesting(
        &fixture, &ValidateLease);
    PublicationCoordinator coordinator;
    const auto status = coordinator.Publish(
        lease, adapters.CoordinatorCallbacks());
    if (status
            != PublicationCoordinatorStatus::CommittedPendingReadiness) {
        return status;
    }

    // This boundary models the local startup commit completing while the
    // initial-load publication window is still active.
    // Native commit is complete, but readiness is still false until the
    // initial plugin-load caller performs the final startup-readiness step.
    CHECK(fixture.readinessCalls == 0U);
    CHECK(!fixture.codecReady && !fixture.itemTransportReady);
    CHECK(!fixture.persistenceOperational && !fixture.globalOperational);
    CHECK(!coordinator.IsReadinessPublished());
    return coordinator.PublishReadinessAfterStartupCommit();
}

auto TestSuccessfulRealBinding() -> void {
    Fixture fixture;
    CHECK(PublishFixture(fixture) == PublicationCoordinatorStatus::Active);
    CHECK(fixture.reserveCalls == 1U);
    CHECK(fixture.readinessCalls == 1U);
    CHECK(fixture.poisonCalls == 0U);
    CHECK(fixture.patchCalls == 4U);
    CHECK(fixture.flushCalls == PreparedCodecMutableSiteCount);
    CHECK(fixture.verifyCalls
        == 13U + 25U + PreparedCodecMutableSiteCount
            + PreparedCodecWitnessCount);
    CHECK(fixture.verifiedIds.front().starts_with("loader."));
    CHECK(fixture.verifiedIds[13].starts_with("save."));
    CHECK(fixture.verifiedIds[38].starts_with("transport.")
        || fixture.verifiedIds[38].starts_with("codec."));
    CHECK(fixture.guardActive);
    CHECK(fixture.codecReady && fixture.itemTransportReady);
    CHECK(fixture.persistenceOperational && fixture.globalOperational);

    const auto reserve = std::find(
        fixture.events.begin(), fixture.events.end(), Event::Reserve);
    const auto firstWrite = std::find_if(
        fixture.events.begin(),
        fixture.events.end(),
        [](Event event) {
            return event == Event::PatchRel32
                || event == Event::PatchU32
                || event == Event::CodecWrite;
        });
    const auto readiness = std::find(
        fixture.events.begin(), fixture.events.end(), Event::Readiness);
    CHECK(reserve != fixture.events.end() && reserve < firstWrite);
    CHECK(readiness == fixture.events.end() - 1);
    CHECK(std::find(reserve, fixture.events.end(), Event::Verify)
        == fixture.events.end());
}

auto TestEveryDomainPreflightRejectsBeforeReservation() -> void {
    constexpr std::array failAttempts{0U, 13U, 38U};
    for (const auto failAttempt : failAttempts) {
        Fixture fixture{.failVerifyAttempt = failAttempt};
        CHECK(PublishFixture(fixture)
            == PublicationCoordinatorStatus::RejectedBeforeMutation);
        CHECK(fixture.reserveCalls == 0U);
        CHECK(fixture.patchCalls == 0U);
        CHECK(fixture.codecWriteCalls == 0U);
        CHECK(fixture.flushCalls == 0U);
        CHECK(fixture.readinessCalls == 0U);
        CHECK(fixture.poisonCalls == 0U);
    }
}

auto TestEveryNativePatchFailurePoisons() -> void {
    for (std::size_t failAttempt{}; failAttempt < 4U; ++failAttempt) {
        Fixture fixture{.failPatchAttempt = failAttempt};
        CHECK(PublishFixture(fixture)
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(fixture.reserveCalls == 1U);
        CHECK(fixture.poisonCalls == 1U);
        CHECK(fixture.readinessCalls == 0U);
        CHECK(!fixture.globalOperational);
        CHECK(!fixture.codecReady);
    }
}

auto TestCodecWriteAndFlushFailuresPoison() -> void {
    {
        Fixture fixture{.failCodecWriteAttempt = 0U};
        CHECK(PublishFixture(fixture)
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(fixture.poisonCalls == 1U);
        CHECK(fixture.readinessCalls == 0U);
    }
    {
        Fixture fixture{.failFlushAttempt = 0U};
        CHECK(PublishFixture(fixture)
            == PublicationCoordinatorStatus::Poisoned);
        CHECK(fixture.poisonCalls == 1U);
        CHECK(fixture.readinessCalls == 0U);
    }
}

auto TestStartupAbortAfterNativeCommitPoisonsWithoutReadiness() -> void {
    Fixture fixture;
    PublicationAdapterSet adapters;
    CHECK(adapters.Bind(MakeTargets(), MakeNativeCallbacks(fixture)));

    const auto lease = NativePublicationLeaseView::ForTesting(
        &fixture, &ValidateLease);
    PublicationCoordinator coordinator;
    CHECK(coordinator.Publish(lease, adapters.CoordinatorCallbacks())
        == PublicationCoordinatorStatus::CommittedPendingReadiness);
    CHECK(fixture.readinessCalls == 0U);
    CHECK(fixture.poisonCalls == 0U);
    CHECK(!fixture.codecReady);
    CHECK(!fixture.itemTransportReady);
    CHECK(!fixture.persistenceOperational);
    CHECK(!fixture.globalOperational);

    CHECK(coordinator.PoisonBeforeStartupReadiness()
        == PublicationCoordinatorStatus::Poisoned);
    CHECK(fixture.readinessCalls == 0U);
    CHECK(fixture.poisonCalls == 1U);
    CHECK(!fixture.codecReady);
    CHECK(!fixture.itemTransportReady);
    CHECK(!fixture.persistenceOperational);
    CHECK(!fixture.globalOperational);
}

auto TestIncompleteBindingIsRejected() -> void {
    Fixture fixture;
    auto callbacks = MakeNativeCallbacks(fixture);
    PublicationAdapterSet adapters;
    callbacks.markPoisoned = nullptr;
    CHECK(!adapters.Bind(MakeTargets(), callbacks));
    CHECK(!adapters.IsBound());
}

} // namespace

int main() {
    TestSuccessfulRealBinding();
    TestEveryDomainPreflightRejectsBeforeReservation();
    TestEveryNativePatchFailurePoisons();
    TestCodecWriteAndFlushFailuresPoison();
    TestStartupAbortAfterNativeCommitPoisonsWithoutReadiness();
    TestIncompleteBindingIsRejected();
    return 0;
}
