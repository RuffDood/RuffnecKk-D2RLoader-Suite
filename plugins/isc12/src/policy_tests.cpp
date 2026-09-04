#include "isc12_contract.hpp"
#include "isc12_atomic_file.hpp"
#include "isc12_codec_patch.hpp"
#include "isc12_envelope.hpp"
#include "isc12_item_packet_budget.hpp"
#include "isc12_loader.hpp"
#include "isc12_native_sites.hpp"
#include "isc12_persistence_policy.hpp"
#include "isc12_player_stat_preflight.hpp"
#include "isc12_schema.hpp"
#include "isc12_store_kind.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
int Failures{};

auto ReadBytes(const std::filesystem::path& path) -> std::vector<std::uint8_t> {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

auto WriteBytes(
        const std::filesystem::path& path,
        std::span<const std::uint8_t> bytes) -> void {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

auto PartialWriteFile(
        HANDLE handle,
        LPCVOID bytes,
        DWORD size,
        LPDWORD written,
        LPOVERLAPPED overlapped) noexcept -> BOOL {
    return ::WriteFile(handle, bytes, std::min<DWORD>(size, 2), written, overlapped);
}

auto FailedWriteFile(
        HANDLE,
        LPCVOID,
        DWORD,
        LPDWORD written,
        LPOVERLAPPED) noexcept -> BOOL {
    if (written) *written = 0;
    ::SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
}

auto FailedFlushFileBuffers(HANDLE) noexcept -> BOOL {
    ::SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
}

auto FailedReplaceFileW(
        LPCWSTR,
        LPCWSTR,
        LPCWSTR,
        DWORD,
        LPVOID,
        LPVOID) noexcept -> BOOL {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

auto FailedReplaceAfterBackupFileW(
        LPCWSTR canonicalPath,
        LPCWSTR,
        LPCWSTR backupPath,
        DWORD,
        LPVOID,
        LPVOID) noexcept -> BOOL {
    if (!::MoveFileExW(
            canonicalPath, backupPath, MOVEFILE_WRITE_THROUGH)) {
        return FALSE;
    }
    ::SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2);
    return FALSE;
}

auto FailedMoveFileExW(LPCWSTR, LPCWSTR, DWORD) noexcept -> BOOL {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

auto HasAtomicSibling(const std::filesystem::path& directory) -> bool {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        if (name.find(L".isc12.tmp.") != std::wstring::npos
                || name.find(L".isc12.bak.") != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

auto WriteU16ForTest(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint16_t value) -> void {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

auto WriteU32ForTest(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint32_t value) -> void {
    for (std::size_t index{}; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> (index * 8U));
    }
}

struct CodecFixtureSite {
    std::uintptr_t rva{};
    std::vector<std::uint8_t> bytes{};
};

struct CodecPatchFixture {
    std::vector<CodecFixtureSite> sites{};
    std::size_t verifyCalls{};
    std::size_t reserveLifetimeCalls{};
    std::size_t writeCalls{};
    std::size_t flushCalls{};
    std::size_t failWriteAttempt{(std::numeric_limits<std::size_t>::max)()};
    std::size_t failFlushAttempt{(std::numeric_limits<std::size_t>::max)()};
    std::array<std::size_t,
        ruffneckk::isc12::PreparedCodecMutableSiteCount> writesAtFlush{};
    std::array<std::uintptr_t,
        ruffneckk::isc12::PreparedCodecMutableSiteCount> flushFirstRvas{};
    std::array<std::size_t,
        ruffneckk::isc12::PreparedCodecMutableSiteCount> flushSizes{};
};

struct CodecQuiescenceFixture {
    bool held{};
    std::size_t validationCalls{};
    std::size_t revokeOnValidation{
        (std::numeric_limits<std::size_t>::max)()};
    const CodecPatchFixture* observedCodec{};
    std::size_t revokeAfterWriteCalls{
        (std::numeric_limits<std::size_t>::max)()};
};

auto ValidateCodecQuiescence(void* context) noexcept -> bool {
    auto& fixture = *static_cast<CodecQuiescenceFixture*>(context);
    const auto call = fixture.validationCalls++;
    if (call == fixture.revokeOnValidation) fixture.held = false;
    if (fixture.observedCodec
            && fixture.observedCodec->writeCalls
                >= fixture.revokeAfterWriteCalls) {
        fixture.held = false;
    }
    return fixture.held;
}

auto MakeCodecPatchSetFixture(
        std::span<const ruffneckk::isc12::CodecPatchGroup> groups)
        -> CodecPatchFixture {
    CodecPatchFixture fixture;
    std::size_t siteCount{};
    for (const auto& group : groups) {
        siteCount += group.sites.size() + group.witnesses.size();
    }
    fixture.sites.reserve(siteCount);
    for (const auto& group : groups) {
        for (const auto& site : group.sites) {
            fixture.sites.push_back({
                .rva = site.pattern.rva,
                .bytes = std::vector<std::uint8_t>(
                    site.pattern.bytes.begin(), site.pattern.bytes.end()),
            });
        }
        for (const auto& witness : group.witnesses) {
            fixture.sites.push_back({
                .rva = witness.rva,
                .bytes = std::vector<std::uint8_t>(
                    witness.bytes.begin(), witness.bytes.end()),
            });
        }
    }
    return fixture;
}

auto FindCodecFixtureSite(
        CodecPatchFixture& fixture,
        std::uintptr_t rva) noexcept -> CodecFixtureSite* {
    for (auto& site : fixture.sites) {
        if (site.rva == rva) return &site;
    }
    return nullptr;
}

auto CodecFixtureHasRel32(
        CodecPatchFixture& fixture,
        std::uintptr_t patternRva,
        std::size_t displacementOffset,
        std::uintptr_t targetRva) noexcept -> bool {
    auto* const site = FindCodecFixtureSite(fixture, patternRva);
    if (!site || displacementOffset > site->bytes.size()
            || site->bytes.size() - displacementOffset < 4U
            || patternRva > (std::numeric_limits<std::uintptr_t>::max)()
                - displacementOffset - 4U) {
        return false;
    }
    const auto nextRva = patternRva + displacementOffset + 4U;
    std::int64_t displacement{};
    if (targetRva >= nextRva) {
        const auto distance = targetRva - nextRva;
        if (distance > static_cast<std::uintptr_t>(
                (std::numeric_limits<std::int32_t>::max)())) {
            return false;
        }
        displacement = static_cast<std::int64_t>(distance);
    } else {
        const auto distance = nextRva - targetRva;
        constexpr auto MaximumNegativeDistance =
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::int32_t>::max)()) + 1ULL;
        if (distance > MaximumNegativeDistance) return false;
        displacement = -static_cast<std::int64_t>(distance);
    }
    const auto encoded = std::bit_cast<std::uint32_t>(
        static_cast<std::int32_t>(displacement));
    for (std::size_t index{}; index < 4U; ++index) {
        if (site->bytes[displacementOffset + index]
                != static_cast<std::uint8_t>(
                    encoded >> (index * 8U))) {
            return false;
        }
    }
    return true;
}

auto VerifyCodecFixturePattern(
        void* context,
        const ruffneckk::isc12::NativePattern& pattern) noexcept -> bool {
    auto& fixture = *static_cast<CodecPatchFixture*>(context);
    ++fixture.verifyCalls;
    for (const auto& site : fixture.sites) {
        if (site.rva != pattern.rva
                || site.bytes.size() != pattern.bytes.size()) {
            continue;
        }
        for (std::size_t index{}; index < site.bytes.size(); ++index) {
            if ((site.bytes[index] & pattern.mask[index])
                    != (pattern.bytes[index] & pattern.mask[index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

auto WriteCodecFixtureByte(
        void* context,
        std::uintptr_t rva,
        std::uint8_t expected,
        std::uint8_t replacement) noexcept -> bool {
    auto& fixture = *static_cast<CodecPatchFixture*>(context);
    const auto attempt = fixture.writeCalls++;
    if (attempt == fixture.failWriteAttempt) return false;
    for (auto& site : fixture.sites) {
        if (rva < site.rva) continue;
        const auto offset = rva - site.rva;
        if (offset >= site.bytes.size()) continue;
        if (site.bytes[offset] != expected) return false;
        site.bytes[offset] = replacement;
        return true;
    }
    return false;
}

auto ReserveCodecFixtureMutationLifetime(void* context) noexcept -> void {
    auto& fixture = *static_cast<CodecPatchFixture*>(context);
    ++fixture.reserveLifetimeCalls;
}

auto FlushCodecFixtureInstructionCache(
        void* context,
        std::uintptr_t firstRva,
        std::size_t size) noexcept -> bool {
    auto& fixture = *static_cast<CodecPatchFixture*>(context);
    const auto attempt = fixture.flushCalls++;
    if (attempt < fixture.writesAtFlush.size()) {
        fixture.writesAtFlush[attempt] = fixture.writeCalls;
        fixture.flushFirstRvas[attempt] = firstRva;
        fixture.flushSizes[attempt] = size;
    }
    return attempt != fixture.failFlushAttempt;
}

auto EncodeTwelveBitFixture(
        std::span<const std::uint16_t> ids,
        std::vector<std::uint8_t>& output) -> bool {
    using namespace ruffneckk::isc12;
    for (const auto id : ids) {
        if (!IsValidStatId(id)) return false;
    }
    const auto fieldCount = ids.size() + 1U;
    if (fieldCount > (std::numeric_limits<std::size_t>::max)()
            / SerializedBitWidth) {
        return false;
    }
    const auto bitCount = fieldCount * SerializedBitWidth;
    std::vector<std::uint8_t> staged((bitCount + 7U) / 8U, 0);
    auto writeField = [&](std::size_t fieldIndex, std::uint16_t value) {
        const auto bitBase = fieldIndex * SerializedBitWidth;
        for (std::size_t bit{}; bit < SerializedBitWidth; ++bit) {
            if ((value & (1U << bit)) == 0) continue;
            const auto destinationBit = bitBase + bit;
            staged[destinationBit / 8U] |= static_cast<std::uint8_t>(
                1U << (destinationBit % 8U));
        }
    };
    for (std::size_t index{}; index < ids.size(); ++index) {
        writeField(index, ids[index]);
    }
    writeField(ids.size(), SerializedSentinel);
    output = std::move(staged);
    return true;
}

auto DecodeTwelveBitFixture(
        std::span<const std::uint8_t> bytes,
        std::vector<std::uint16_t>& output) -> bool {
    using namespace ruffneckk::isc12;
    std::vector<std::uint16_t> staged;
    const auto availableFields = bytes.size() * 8U / SerializedBitWidth;
    for (std::size_t field{}; field < availableFields; ++field) {
        std::uint16_t value{};
        const auto bitBase = field * SerializedBitWidth;
        for (std::size_t bit{}; bit < SerializedBitWidth; ++bit) {
            const auto sourceBit = bitBase + bit;
            if ((bytes[sourceBit / 8U]
                    & static_cast<std::uint8_t>(1U << (sourceBit % 8U))) != 0) {
                value |= static_cast<std::uint16_t>(1U << bit);
            }
        }
        if (value == SerializedSentinel) {
            output = std::move(staged);
            return true;
        }
        if (!IsValidStatId(value)) return false;
        staged.push_back(value);
    }
    return false;
}

auto EncodePlayerStatPreflightFixture(
        std::span<const std::uint16_t> ids,
        std::span<const ruffneckk::isc12::ItemStatSemanticRow> schema,
        ruffneckk::isc12::PlayerStatStreamKind kind,
        bool includeSentinel) -> std::vector<std::uint8_t> {
    using namespace ruffneckk::isc12;
    std::vector<std::uint8_t> bytes{0x67, 0x66};
    std::size_t bitPosition = 16;
    auto appendBits = [&](std::uint16_t value, std::size_t width) {
        const auto requiredBits = bitPosition + width;
        bytes.resize((requiredBits + 7U) / 8U, 0);
        for (std::size_t bit{}; bit < width; ++bit) {
            if ((value & static_cast<std::uint16_t>(1U << bit)) == 0) {
                continue;
            }
            const auto destination = bitPosition + bit;
            bytes[destination / 8U] |= static_cast<std::uint8_t>(
                1U << (destination % 8U));
        }
        bitPosition += width;
    };
    for (const auto id : ids) {
        appendBits(id, SerializedBitWidth);
        if (id < schema.size()) {
            appendBits(0, schema[id].csvParamBits);
            appendBits(0, kind == PlayerStatStreamKind::Auxiliary
                ? 32U
                : schema[id].csvBits);
        }
    }
    if (includeSentinel) appendBits(SerializedSentinel, SerializedBitWidth);
    return bytes;
}

struct FullItemPacketVisitProbe {
    std::array<std::size_t, 16> nodeIndices{};
    std::array<ruffneckk::isc12::FullItemPacketKind, 16> packetKinds{};
    std::size_t callCount{};
};

auto RecordFullItemPacketVisit(
        void* context,
        std::size_t nodeIndex,
        ruffneckk::isc12::FullItemPacketKind packet) noexcept -> void {
    auto& probe = *static_cast<FullItemPacketVisitProbe*>(context);
    if (probe.callCount < probe.nodeIndices.size()) {
        probe.nodeIndices[probe.callCount] = nodeIndex;
        probe.packetKinds[probe.callCount] = packet;
    }
    ++probe.callCount;
}

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "CHECK failed at line " << __LINE__ \
                      << ": " #expression "\n"; \
            ++Failures; \
        } \
    } while (false)

struct FullItemStagingQueueProbe {
    std::array<std::array<std::uint8_t,
        ruffneckk::isc12::MaximumStagedItemPacketBytes>,
        ruffneckk::isc12::MaximumStagedItemPacketCount> packets{};
    std::array<std::size_t,
        ruffneckk::isc12::MaximumStagedItemPacketCount> lengths{};
    std::array<void*,
        ruffneckk::isc12::MaximumStagedItemPacketCount> clients{};
    std::size_t callCount{};
    bool rootReturned{};
    bool observedBeforeRootReturn{};
    ruffneckk::isc12::FullItemPacketStagingContext* reentryTransaction{};
    ruffneckk::isc12::FullItemProducerDisposition reentryDisposition{
        ruffneckk::isc12::FullItemProducerDisposition::InvokeOriginal};
    int reentryClient{};
    int reentryItem{};
};

auto RecordStagedFullItemPacket(
        void* context,
        void* client,
        const std::uint8_t* bytes,
        std::size_t length) noexcept -> void {
    auto& probe = *static_cast<FullItemStagingQueueProbe*>(context);
    if (!probe.rootReturned) probe.observedBeforeRootReturn = true;
    if (probe.callCount < probe.packets.size()
            && length <= probe.packets[probe.callCount].size()) {
        probe.lengths[probe.callCount] = length;
        probe.clients[probe.callCount] = client;
        std::copy_n(
            bytes,
            length,
            probe.packets[probe.callCount].begin());
    }
    ++probe.callCount;

    if (probe.reentryTransaction != nullptr && probe.callCount == 1) {
        const auto admission = BeginFullItemPacketProducer(
            *probe.reentryTransaction,
            {
                .kind = ruffneckk::isc12::FullItemPacketKind::ItemAction9C,
                .client = &probe.reentryClient,
                .item = &probe.reentryItem,
                .action = 0x10,
            });
        probe.reentryDisposition = admission.disposition;
    }
}

auto MakeStagedFullItemPacket(
        ruffneckk::isc12::FullItemPacketKind kind,
        std::uint8_t action,
        std::size_t length,
        std::uint8_t seed = 0x40)
        -> std::array<std::uint8_t, 256> {
    std::array<std::uint8_t, 256> packet{};
    for (std::size_t index{}; index < packet.size(); ++index) {
        packet[index] = static_cast<std::uint8_t>(seed + index);
    }
    packet[0] = kind
            == ruffneckk::isc12::FullItemPacketKind::ItemAction9C
        ? 0x9C
        : 0x9D;
    packet[1] = action;
    packet[2] = static_cast<std::uint8_t>(length);
    return packet;
}

auto RunFullItemPacketStagingTests() -> void {
    using namespace ruffneckk::isc12;

    static_assert(MaximumStagedItemPacketBytes == 0xFC);
    static_assert(MaximumStagedItemImmediateChildren == 7);
    static_assert(MaximumStagedItemPacketCount == 64);
    static_assert(MaximumStagedItemTreeDepth == 16);
    static_assert(MaximumStagedItemTransactionBytes == 0x4000);
    static_assert(NestedFullItemTemporaryFlagsMask == 0x08);
    static_assert(noexcept(BeginFullItemPacketProducer(
        std::declval<FullItemPacketStagingContext&>(),
        std::declval<const FullItemProducerDescriptor&>())));
    static_assert(noexcept(CaptureFullItemPacketQueueCall(
        std::declval<FullItemPacketStagingContext&>(),
        FullItemPacketKind::ItemAction9C,
        nullptr,
        nullptr,
        0)));
    static_assert(noexcept(AbortFullItemPacketProducer(
        std::declval<FullItemPacketStagingContext&>(),
        std::declval<const FullItemProducerToken&>())));

    int client{};
    std::array<int, MaximumStagedItemPacketCount + 2U> items{};
    const auto root9C = FullItemProducerDescriptor{
        .kind = FullItemPacketKind::ItemAction9C,
        .client = &client,
        .item = &items[0],
        .action = 0x10,
        .temporaryFlags = 0xA5A5,
        .gamble = 1,
    };
    const auto child9D = [&](std::size_t parent, std::size_t item) {
        return FullItemProducerDescriptor{
            .kind = FullItemPacketKind::ItemAction9D,
            .client = &client,
            .parentItem = &items[parent],
            .item = &items[item],
            .action = 0x12,
            .temporaryFlags =
                root9C.temporaryFlags | NestedFullItemTemporaryFlagsMask,
            .gamble = 0,
        };
    };

    const auto expectInvalidRoot = [&](FullItemProducerDescriptor descriptor) {
        FullItemPacketStagingContext transaction{};
        const auto admission = BeginFullItemPacketProducer(
            transaction, descriptor);
        CHECK(admission.disposition
            == FullItemProducerDisposition::SkipOriginal);
        CHECK(admission.error == FullItemPacketStagingError::InvalidArgument);
        CHECK(admission.token.ownsRoot);
        CHECK(EndFullItemPacketProducer(transaction, admission.token)
            == FullItemProducerCompletion::RootRejected);
        FullItemStagingQueueProbe probe{.rootReturned = true};
        const auto result = FlushOrDiscardFullItemPacketTransaction(
            transaction,
            &RecordStagedFullItemPacket,
            &probe);
        CHECK(result.error == FullItemPacketStagingError::InvalidArgument);
        CHECK(probe.callCount == 0);
        CHECK(transaction.state == FullItemPacketStagingState::Idle);
    };
    auto nullRootClient = root9C;
    nullRootClient.client = nullptr;
    expectInvalidRoot(nullRootClient);
    auto nullRootItem = root9C;
    nullRootItem.item = nullptr;
    expectInvalidRoot(nullRootItem);
    auto nullRoot9DParent = root9C;
    nullRoot9DParent.kind = FullItemPacketKind::ItemAction9D;
    nullRoot9DParent.parentItem = nullptr;
    expectInvalidRoot(nullRoot9DParent);
    auto invalidRootKind = root9C;
    invalidRootKind.kind = static_cast<FullItemPacketKind>(0xFF);
    expectInvalidRoot(invalidRootKind);

    // A copied root packet is not exposed until the root producer returns.
    FullItemPacketStagingContext copiedRootTransaction{};
    const auto copiedRootAdmission = BeginFullItemPacketProducer(
        copiedRootTransaction,
        root9C);
    CHECK(copiedRootAdmission.disposition
        == FullItemProducerDisposition::InvokeOriginal);
    CHECK(copiedRootAdmission.token.ownsRoot);
    auto copiedRootPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C,
        root9C.action,
        MaximumStagedItemPacketBytes,
        0x21);
    const auto copiedRootExpected = copiedRootPacket;
    CHECK(CaptureFullItemPacketQueueCall(
        copiedRootTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        copiedRootPacket.data(),
        MaximumStagedItemPacketBytes)
        == FullItemPacketStagingError::None);
    copiedRootPacket.fill(0xEE);
    CHECK(EndFullItemPacketProducer(
        copiedRootTransaction,
        copiedRootAdmission.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe copiedRootProbe{};
    CHECK(copiedRootProbe.callCount == 0);
    copiedRootProbe.rootReturned = true;
    const auto copiedRootFlush = FlushOrDiscardFullItemPacketTransaction(
        copiedRootTransaction,
        &RecordStagedFullItemPacket,
        &copiedRootProbe);
    CHECK(copiedRootFlush.completed);
    CHECK(copiedRootFlush.queuedPacketCount == 1);
    CHECK(copiedRootProbe.callCount == 1);
    CHECK(!copiedRootProbe.observedBeforeRootReturn);
    CHECK(copiedRootProbe.clients[0] == &client);
    CHECK(std::equal(
        copiedRootExpected.begin(),
        copiedRootExpected.begin() + MaximumStagedItemPacketBytes,
        copiedRootProbe.packets[0].begin()));
    CHECK(copiedRootTransaction.state == FullItemPacketStagingState::Idle);

    // An SEH unwind after capture poisons the batch before producer balance;
    // the already-copied root packet can therefore never be published.
    FullItemPacketStagingContext exceptionalRootTransaction{};
    const auto exceptionalRootAdmission = BeginFullItemPacketProducer(
        exceptionalRootTransaction,
        root9C);
    auto exceptionalRootPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32, 0x2A);
    CHECK(CaptureFullItemPacketQueueCall(
        exceptionalRootTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        exceptionalRootPacket.data(),
        32) == FullItemPacketStagingError::None);
    CHECK(AbortFullItemPacketProducer(
        exceptionalRootTransaction,
        exceptionalRootAdmission.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe exceptionalRootProbe{.rootReturned = true};
    const auto exceptionalRootFlush =
        FlushOrDiscardFullItemPacketTransaction(
            exceptionalRootTransaction,
            &RecordStagedFullItemPacket,
            &exceptionalRootProbe);
    CHECK(exceptionalRootFlush.error
        == FullItemPacketStagingError::NativeProducerException);
    CHECK(exceptionalRootFlush.queuedPacketCount == 0);
    CHECK(exceptionalRootProbe.callCount == 0);
    CHECK(exceptionalRootTransaction.state
        == FullItemPacketStagingState::Idle);

    // A 0x9D entry outside another transaction is a valid autonomous root.
    FullItemPacketStagingContext root9DTransaction{};
    const auto root9D = FullItemProducerDescriptor{
        .kind = FullItemPacketKind::ItemAction9D,
        .client = &client,
        .parentItem = &items[1],
        .item = &items[0],
        .action = 0x22,
        .temporaryFlags = 0x55,
        .gamble = 1,
    };
    const auto root9DAdmission = BeginFullItemPacketProducer(
        root9DTransaction,
        root9D);
    CHECK(root9DAdmission.token.ownsRoot);
    auto root9DPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D,
        root9D.action,
        MaximumStagedItemPacketBytes);
    CHECK(CaptureFullItemPacketQueueCall(
        root9DTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        root9DPacket.data(),
        MaximumStagedItemPacketBytes)
        == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        root9DTransaction,
        root9DAdmission.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe root9DProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        root9DTransaction,
        &RecordStagedFullItemPacket,
        &root9DProbe).completed);
    CHECK(root9DProbe.callCount == 1);
    CHECK(root9DProbe.packets[0][0] == 0x9D);

    FullItemPacketStagingContext root9DTreeTransaction{};
    const auto root9DTreeRoot = BeginFullItemPacketProducer(
        root9DTreeTransaction,
        root9D);
    CHECK(CaptureFullItemPacketQueueCall(
        root9DTreeTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        root9DPacket.data(),
        MaximumStagedItemPacketBytes)
        == FullItemPacketStagingError::None);
    const auto root9DTreeChild = BeginFullItemPacketProducer(
        root9DTreeTransaction,
        {
            .kind = FullItemPacketKind::ItemAction9D,
            .client = &client,
            .parentItem = &items[0],
            .item = &items[2],
            .action = 0x12,
            .temporaryFlags = root9D.temporaryFlags
                | NestedFullItemTemporaryFlagsMask,
        });
    auto root9DTreeChildPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D,
        0x12,
        32);
    CHECK(CaptureFullItemPacketQueueCall(
        root9DTreeTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        root9DTreeChildPacket.data(), 32)
        == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        root9DTreeTransaction, root9DTreeChild.token)
        == FullItemProducerCompletion::NestedComplete);
    CHECK(EndFullItemPacketProducer(
        root9DTreeTransaction, root9DTreeRoot.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe root9DTreeProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        root9DTreeTransaction,
        &RecordStagedFullItemPacket,
        &root9DTreeProbe).completed);
    CHECK(root9DTreeProbe.callCount == 2);
    CHECK(root9DTreeProbe.packets[0][0] == 0x9D);
    CHECK(root9DTreeProbe.packets[1][0] == 0x9D);

    // Expected recursive 9D calls preserve native depth-first preorder.
    FullItemPacketStagingContext preorderTransaction{};
    const auto preorderRoot = BeginFullItemPacketProducer(
        preorderTransaction,
        root9C);
    auto preorderRootPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32, 0x10);
    CHECK(CaptureFullItemPacketQueueCall(
        preorderTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        preorderRootPacket.data(), 32) == FullItemPacketStagingError::None);
    const auto childA = BeginFullItemPacketProducer(
        preorderTransaction, child9D(0, 1));
    auto childAPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D, 0x12, 33, 0x20);
    CHECK(CaptureFullItemPacketQueueCall(
        preorderTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        childAPacket.data(), 33) == FullItemPacketStagingError::None);
    const auto grandchild = BeginFullItemPacketProducer(
        preorderTransaction, child9D(1, 2));
    auto grandchildPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D, 0x12, 34, 0x30);
    CHECK(CaptureFullItemPacketQueueCall(
        preorderTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        grandchildPacket.data(), 34) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        preorderTransaction, grandchild.token)
        == FullItemProducerCompletion::NestedComplete);
    CHECK(EndFullItemPacketProducer(preorderTransaction, childA.token)
        == FullItemProducerCompletion::NestedComplete);
    const auto childB = BeginFullItemPacketProducer(
        preorderTransaction, child9D(0, 3));
    auto childBPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D, 0x12, 35, 0x40);
    CHECK(CaptureFullItemPacketQueueCall(
        preorderTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        childBPacket.data(), 35) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(preorderTransaction, childB.token)
        == FullItemProducerCompletion::NestedComplete);
    CHECK(EndFullItemPacketProducer(preorderTransaction, preorderRoot.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe preorderProbe{.rootReturned = true};
    const auto preorderFlush = FlushOrDiscardFullItemPacketTransaction(
        preorderTransaction,
        &RecordStagedFullItemPacket,
        &preorderProbe);
    CHECK(preorderFlush.completed);
    CHECK(preorderProbe.callCount == 4);
    CHECK(preorderProbe.packets[0][0] == 0x9C);
    CHECK(preorderProbe.packets[1][0] == 0x9D);
    CHECK(preorderProbe.packets[2][0] == 0x9D);
    CHECK(preorderProbe.packets[3][0] == 0x9D);
    CHECK(preorderProbe.packets[0][3] == preorderRootPacket[3]);
    CHECK(preorderProbe.packets[1][3] == childAPacket[3]);
    CHECK(preorderProbe.packets[2][3] == grandchildPacket[3]);
    CHECK(preorderProbe.packets[3][3] == childBPacket[3]);

    const auto expectCaptureRejection = [&](FullItemPacketKind rootKind,
                                             FullItemPacketKind relayKind,
                                             const std::uint8_t* packetBytes,
                                             std::size_t packetLength,
                                             void* relayClient,
                                             FullItemPacketStagingError expected) {
        auto descriptor = root9C;
        descriptor.kind = rootKind;
        descriptor.parentItem = rootKind == FullItemPacketKind::ItemAction9D
            ? static_cast<void*>(&items[1])
            : nullptr;
        FullItemPacketStagingContext transaction{};
        const auto admission = BeginFullItemPacketProducer(
            transaction, descriptor);
        CHECK(admission.disposition
            == FullItemProducerDisposition::InvokeOriginal);
        CHECK(CaptureFullItemPacketQueueCall(
            transaction,
            relayKind,
            relayClient,
            packetBytes,
            packetLength) == expected);
        CHECK(EndFullItemPacketProducer(transaction, admission.token)
            == FullItemProducerCompletion::RootRejected);
        FullItemStagingQueueProbe probe{.rootReturned = true};
        const auto result = FlushOrDiscardFullItemPacketTransaction(
            transaction,
            &RecordStagedFullItemPacket,
            &probe);
        CHECK(result.error == expected);
        CHECK(result.queuedPacketCount == 0);
        CHECK(probe.callCount == 0);
        CHECK(transaction.state == FullItemPacketStagingState::Idle);
    };

    const auto expectAcceptedRootLength = [&](FullItemPacketKind kind,
                                              std::size_t length) {
        auto descriptor = root9C;
        descriptor.kind = kind;
        descriptor.parentItem = kind == FullItemPacketKind::ItemAction9D
            ? static_cast<void*>(&items[1])
            : nullptr;
        FullItemPacketStagingContext transaction{};
        const auto admission = BeginFullItemPacketProducer(
            transaction, descriptor);
        auto packet = MakeStagedFullItemPacket(
            kind, descriptor.action, length);
        CHECK(CaptureFullItemPacketQueueCall(
            transaction,
            kind,
            &client,
            packet.data(),
            length) == FullItemPacketStagingError::None);
        CHECK(EndFullItemPacketProducer(transaction, admission.token)
            == FullItemProducerCompletion::RootReady);
        FullItemStagingQueueProbe probe{.rootReturned = true};
        const auto result = FlushOrDiscardFullItemPacketTransaction(
            transaction,
            &RecordStagedFullItemPacket,
            &probe);
        CHECK(result.completed);
        CHECK(probe.callCount == 1);
        CHECK(probe.lengths[0] == length);
    };
    expectAcceptedRootLength(
        FullItemPacketKind::ItemAction9C,
        Packet9CHeaderBytes + 1U);
    expectAcceptedRootLength(
        FullItemPacketKind::ItemAction9D,
        Packet9DHeaderBytes + 1U);

    auto packet9CHeaderOnly = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 8);
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9C,
        packet9CHeaderOnly.data(), 8, &client,
        FullItemPacketStagingError::InvalidPacketLength);
    auto packet9DHeaderOnly = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D, root9C.action, 13);
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9D,
        FullItemPacketKind::ItemAction9D,
        packet9DHeaderOnly.data(), 13, &client,
        FullItemPacketStagingError::InvalidPacketLength);
    for (const auto invalidLength : {
            std::size_t{253}, std::size_t{254}, std::size_t{255},
            std::size_t{0}, std::size_t{1}}) {
        auto packet = MakeStagedFullItemPacket(
            FullItemPacketKind::ItemAction9D,
            root9C.action,
            invalidLength);
        expectCaptureRejection(
            FullItemPacketKind::ItemAction9D,
            FullItemPacketKind::ItemAction9D,
            packet.data(), invalidLength, &client,
            FullItemPacketStagingError::InvalidPacketLength);
    }
    auto invalidOpcodePacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    invalidOpcodePacket[0] = 0x9D;
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9C,
        invalidOpcodePacket.data(), 32, &client,
        FullItemPacketStagingError::InvalidPacketHeader);
    auto invalidActionPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    invalidActionPacket[1] ^= 1;
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9C,
        invalidActionPacket.data(), 32, &client,
        FullItemPacketStagingError::InvalidPacketHeader);
    auto invalidLengthHeaderPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    invalidLengthHeaderPacket[2] = 31;
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9C,
        invalidLengthHeaderPacket.data(), 32, &client,
        FullItemPacketStagingError::InvalidPacketHeader);
    auto validFailurePacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9D,
        validFailurePacket.data(), 32, &client,
        FullItemPacketStagingError::RelayKindMismatch);
    int wrongClient{};
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9C,
        validFailurePacket.data(), 32, &wrongClient,
        FullItemPacketStagingError::ClientMismatch);
    expectCaptureRejection(
        FullItemPacketKind::ItemAction9C,
        FullItemPacketKind::ItemAction9C,
        nullptr, 32, &client,
        FullItemPacketStagingError::InvalidPacketPointer);

    // The first relay error is sticky, and a second relay cannot publish it.
    FullItemPacketStagingContext stickyTransaction{};
    const auto stickyRoot = BeginFullItemPacketProducer(
        stickyTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        stickyTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        validFailurePacket.data(), 32)
        == FullItemPacketStagingError::RelayKindMismatch);
    CHECK(CaptureFullItemPacketQueueCall(
        stickyTransaction,
        FullItemPacketKind::ItemAction9C,
        &wrongClient,
        validFailurePacket.data(), 32)
        == FullItemPacketStagingError::RelayKindMismatch);
    CHECK(EndFullItemPacketProducer(stickyTransaction, stickyRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe stickyProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        stickyTransaction,
        &RecordStagedFullItemPacket,
        &stickyProbe).error
        == FullItemPacketStagingError::RelayKindMismatch);
    CHECK(stickyProbe.callCount == 0);

    // Duplicate relays and missing relays reject the complete root.
    FullItemPacketStagingContext duplicateRelayTransaction{};
    const auto duplicateRelayRoot = BeginFullItemPacketProducer(
        duplicateRelayTransaction, root9C);
    auto duplicateRelayPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    CHECK(CaptureFullItemPacketQueueCall(
        duplicateRelayTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        duplicateRelayPacket.data(), 32)
        == FullItemPacketStagingError::None);
    CHECK(CaptureFullItemPacketQueueCall(
        duplicateRelayTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        duplicateRelayPacket.data(), 32)
        == FullItemPacketStagingError::DuplicatePacket);
    CHECK(EndFullItemPacketProducer(
        duplicateRelayTransaction, duplicateRelayRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe duplicateRelayProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        duplicateRelayTransaction,
        &RecordStagedFullItemPacket,
        &duplicateRelayProbe).error
        == FullItemPacketStagingError::DuplicatePacket);
    CHECK(duplicateRelayProbe.callCount == 0);

    FullItemPacketStagingContext missingRelayTransaction{};
    const auto missingRelayRoot = BeginFullItemPacketProducer(
        missingRelayTransaction, root9C);
    CHECK(EndFullItemPacketProducer(
        missingRelayTransaction, missingRelayRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe missingRelayProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        missingRelayTransaction,
        &RecordStagedFullItemPacket,
        &missingRelayProbe).error
        == FullItemPacketStagingError::MissingPacket);
    CHECK(missingRelayProbe.callCount == 0);

    // A relay outside a wrapped producer is a process-fatal invariant breach.
    FullItemPacketStagingContext idleRelayTransaction{};
    CHECK(CaptureFullItemPacketQueueCall(
        idleRelayTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        validFailurePacket.data(), 32)
        == FullItemPacketStagingError::RelayWithoutTransaction);
    CHECK(idleRelayTransaction.state == FullItemPacketStagingState::Fatal);

    // Root, middle and final-sibling errors all precede the first real queue.
    const auto expectLateTreeRejection = [&](std::size_t corruptNode) {
        FullItemPacketStagingContext transaction{};
        const auto root = BeginFullItemPacketProducer(transaction, root9C);
        auto rootPacket = MakeStagedFullItemPacket(
            FullItemPacketKind::ItemAction9C, root9C.action, 32);
        if (corruptNode == 0) rootPacket[0] = 0;
        CHECK(CaptureFullItemPacketQueueCall(
            transaction,
            FullItemPacketKind::ItemAction9C,
            &client,
            rootPacket.data(), 32)
            == (corruptNode == 0
                ? FullItemPacketStagingError::InvalidPacketHeader
                : FullItemPacketStagingError::None));
        for (std::size_t node = 1; node <= 3; ++node) {
            const auto child = BeginFullItemPacketProducer(
                transaction, child9D(0, node));
            if (child.disposition
                    == FullItemProducerDisposition::InvokeOriginal) {
                auto packet = MakeStagedFullItemPacket(
                    FullItemPacketKind::ItemAction9D, 0x12, 32);
                if (node == corruptNode) packet[2] = 31;
                CHECK(CaptureFullItemPacketQueueCall(
                    transaction,
                    FullItemPacketKind::ItemAction9D,
                    &client,
                    packet.data(), 32)
                    == (node == corruptNode
                        ? FullItemPacketStagingError::InvalidPacketHeader
                        : FullItemPacketStagingError::None));
                CHECK(EndFullItemPacketProducer(transaction, child.token)
                    == FullItemProducerCompletion::NestedComplete);
            }
        }
        CHECK(EndFullItemPacketProducer(transaction, root.token)
            == FullItemProducerCompletion::RootRejected);
        FullItemStagingQueueProbe probe{.rootReturned = true};
        const auto result = FlushOrDiscardFullItemPacketTransaction(
            transaction,
            &RecordStagedFullItemPacket,
            &probe);
        CHECK(result.error == FullItemPacketStagingError::InvalidPacketHeader);
        CHECK(result.queuedPacketCount == 0);
        CHECK(probe.callCount == 0);
    };
    expectLateTreeRejection(0);
    expectLateTreeRejection(2);
    expectLateTreeRejection(3);

    const auto expectNestedAdmissionRejection = [&](FullItemProducerDescriptor nested,
                                                     FullItemPacketStagingError expected) {
        FullItemPacketStagingContext transaction{};
        const auto root = BeginFullItemPacketProducer(transaction, root9C);
        auto packet = MakeStagedFullItemPacket(
            FullItemPacketKind::ItemAction9C, root9C.action, 32);
        CHECK(CaptureFullItemPacketQueueCall(
            transaction,
            FullItemPacketKind::ItemAction9C,
            &client,
            packet.data(), 32) == FullItemPacketStagingError::None);
        const auto rejected = BeginFullItemPacketProducer(transaction, nested);
        CHECK(rejected.disposition == FullItemProducerDisposition::SkipOriginal);
        CHECK(rejected.error == expected);
        if (expected == FullItemPacketStagingError::NestedFlagsMismatch) {
            CHECK(transaction.rejectedProducerTemporaryFlags
                == nested.temporaryFlags);
            CHECK(transaction.rejectedParentTemporaryFlags
                == root9C.temporaryFlags);
        }
        CHECK(EndFullItemPacketProducer(transaction, root.token)
            == FullItemProducerCompletion::RootRejected);
        FullItemStagingQueueProbe probe{.rootReturned = true};
        const auto result = FlushOrDiscardFullItemPacketTransaction(
            transaction,
            &RecordStagedFullItemPacket,
            &probe);
        CHECK(result.error == expected);
        CHECK(result.queuedPacketCount == 0);
        CHECK(probe.callCount == 0);
    };

    auto nested9C = root9C;
    nested9C.item = &items[1];
    nested9C.parentItem = &items[0];
    expectNestedAdmissionRejection(
        nested9C,
        FullItemPacketStagingError::UnexpectedNested9C);
    auto wrongNestedClient = child9D(0, 1);
    wrongNestedClient.client = &wrongClient;
    expectNestedAdmissionRejection(
        wrongNestedClient,
        FullItemPacketStagingError::ClientMismatch);
    auto nullNestedItem = child9D(0, 1);
    nullNestedItem.item = nullptr;
    expectNestedAdmissionRejection(
        nullNestedItem,
        FullItemPacketStagingError::InvalidArgument);
    auto wrongNestedParent = child9D(0, 1);
    wrongNestedParent.parentItem = &items[2];
    expectNestedAdmissionRejection(
        wrongNestedParent,
        FullItemPacketStagingError::ParentMismatch);
    auto wrongNestedAction = child9D(0, 1);
    wrongNestedAction.action = 0x11;
    expectNestedAdmissionRejection(
        wrongNestedAction,
        FullItemPacketStagingError::NestedActionMismatch);
    auto wrongNestedFlags = child9D(0, 1);
    wrongNestedFlags.temporaryFlags ^= 1;
    expectNestedAdmissionRejection(
        wrongNestedFlags,
        FullItemPacketStagingError::NestedFlagsMismatch);
    auto wrongNestedGamble = child9D(0, 1);
    wrongNestedGamble.gamble = 1;
    expectNestedAdmissionRejection(
        wrongNestedGamble,
        FullItemPacketStagingError::NestedGambleMismatch);
    expectNestedAdmissionRejection(
        child9D(0, 0),
        FullItemPacketStagingError::DuplicateOrCycle);

    FullItemPacketStagingContext parentMissingTransaction{};
    const auto parentMissingRoot = BeginFullItemPacketProducer(
        parentMissingTransaction, root9C);
    const auto parentMissingChild = BeginFullItemPacketProducer(
        parentMissingTransaction, child9D(0, 1));
    CHECK(parentMissingChild.error
        == FullItemPacketStagingError::ParentPacketMissing);
    CHECK(EndFullItemPacketProducer(
        parentMissingTransaction, parentMissingRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe parentMissingProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        parentMissingTransaction,
        &RecordStagedFullItemPacket,
        &parentMissingProbe).error
        == FullItemPacketStagingError::ParentPacketMissing);
    CHECK(parentMissingProbe.callCount == 0);

    // The same child cannot appear twice, even after its first subtree returns.
    FullItemPacketStagingContext sharedChildTransaction{};
    const auto sharedRoot = BeginFullItemPacketProducer(
        sharedChildTransaction, root9C);
    auto sharedRootPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    CHECK(CaptureFullItemPacketQueueCall(
        sharedChildTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        sharedRootPacket.data(), 32) == FullItemPacketStagingError::None);
    const auto sharedFirst = BeginFullItemPacketProducer(
        sharedChildTransaction, child9D(0, 1));
    auto sharedPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D, 0x12, 32);
    CHECK(CaptureFullItemPacketQueueCall(
        sharedChildTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        sharedPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        sharedChildTransaction, sharedFirst.token)
        == FullItemProducerCompletion::NestedComplete);
    CHECK(BeginFullItemPacketProducer(
        sharedChildTransaction, child9D(0, 1)).error
        == FullItemPacketStagingError::DuplicateOrCycle);
    CHECK(EndFullItemPacketProducer(sharedChildTransaction, sharedRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe sharedProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        sharedChildTransaction,
        &RecordStagedFullItemPacket,
        &sharedProbe).error
        == FullItemPacketStagingError::DuplicateOrCycle);
    CHECK(sharedProbe.callCount == 0);

    // Seven direct children are accepted; the eighth poisons the whole batch.
    FullItemPacketStagingContext childLimitTransaction{};
    const auto childLimitRoot = BeginFullItemPacketProducer(
        childLimitTransaction, root9C);
    auto limitRootPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C, root9C.action, 32);
    auto limitChildPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9D, 0x12, 32);
    CHECK(CaptureFullItemPacketQueueCall(
        childLimitTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    for (std::size_t child = 1;
            child <= MaximumStagedItemImmediateChildren; ++child) {
        const auto admission = BeginFullItemPacketProducer(
            childLimitTransaction, child9D(0, child));
        CHECK(admission.disposition
            == FullItemProducerDisposition::InvokeOriginal);
        CHECK(CaptureFullItemPacketQueueCall(
            childLimitTransaction,
            FullItemPacketKind::ItemAction9D,
            &client,
            limitChildPacket.data(), 32) == FullItemPacketStagingError::None);
        CHECK(EndFullItemPacketProducer(
            childLimitTransaction, admission.token)
            == FullItemProducerCompletion::NestedComplete);
    }
    CHECK(BeginFullItemPacketProducer(
        childLimitTransaction,
        child9D(0, MaximumStagedItemImmediateChildren + 1U)).error
        == FullItemPacketStagingError::ImmediateChildLimitExceeded);
    CHECK(EndFullItemPacketProducer(
        childLimitTransaction, childLimitRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe childLimitProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        childLimitTransaction,
        &RecordStagedFullItemPacket,
        &childLimitProbe).error
        == FullItemPacketStagingError::ImmediateChildLimitExceeded);
    CHECK(childLimitProbe.callCount == 0);

    // A depth-16 chain is admissible; attempting depth 17 rejects everything.
    FullItemPacketStagingContext depthLimitTransaction{};
    const auto depthRoot = BeginFullItemPacketProducer(
        depthLimitTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        depthLimitTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    std::array<FullItemProducerToken, MaximumStagedItemTreeDepth - 1U>
        depthTokens{};
    for (std::size_t depth = 2;
            depth <= MaximumStagedItemTreeDepth; ++depth) {
        const auto admission = BeginFullItemPacketProducer(
            depthLimitTransaction,
            child9D(depth - 2U, depth - 1U));
        CHECK(admission.disposition
            == FullItemProducerDisposition::InvokeOriginal);
        depthTokens[depth - 2U] = admission.token;
        CHECK(CaptureFullItemPacketQueueCall(
            depthLimitTransaction,
            FullItemPacketKind::ItemAction9D,
            &client,
            limitChildPacket.data(), 32) == FullItemPacketStagingError::None);
    }
    CHECK(BeginFullItemPacketProducer(
        depthLimitTransaction,
        child9D(
            MaximumStagedItemTreeDepth - 1U,
            MaximumStagedItemTreeDepth)).error
        == FullItemPacketStagingError::DepthLimitExceeded);
    for (std::size_t remaining = depthTokens.size(); remaining != 0; --remaining) {
        CHECK(EndFullItemPacketProducer(
            depthLimitTransaction, depthTokens[remaining - 1U])
            == FullItemProducerCompletion::NestedComplete);
    }
    CHECK(EndFullItemPacketProducer(depthLimitTransaction, depthRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe depthLimitProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        depthLimitTransaction,
        &RecordStagedFullItemPacket,
        &depthLimitProbe).error
        == FullItemPacketStagingError::DepthLimitExceeded);
    CHECK(depthLimitProbe.callCount == 0);

    FullItemPacketStagingContext maximumDepthTransaction{};
    const auto maximumDepthRoot = BeginFullItemPacketProducer(
        maximumDepthTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        maximumDepthTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    std::array<FullItemProducerToken, MaximumStagedItemTreeDepth - 1U>
        maximumDepthTokens{};
    for (std::size_t depth = 2;
            depth <= MaximumStagedItemTreeDepth; ++depth) {
        const auto admission = BeginFullItemPacketProducer(
            maximumDepthTransaction,
            child9D(depth - 2U, depth - 1U));
        maximumDepthTokens[depth - 2U] = admission.token;
        CHECK(CaptureFullItemPacketQueueCall(
            maximumDepthTransaction,
            FullItemPacketKind::ItemAction9D,
            &client,
            limitChildPacket.data(), 32) == FullItemPacketStagingError::None);
    }
    for (std::size_t remaining = maximumDepthTokens.size();
            remaining != 0; --remaining) {
        CHECK(EndFullItemPacketProducer(
            maximumDepthTransaction,
            maximumDepthTokens[remaining - 1U])
            == FullItemProducerCompletion::NestedComplete);
    }
    CHECK(EndFullItemPacketProducer(
        maximumDepthTransaction, maximumDepthRoot.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe maximumDepthProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        maximumDepthTransaction,
        &RecordStagedFullItemPacket,
        &maximumDepthProbe).completed);
    CHECK(maximumDepthProbe.callCount == MaximumStagedItemTreeDepth);

    const auto stageMaximumTree = [&](FullItemPacketStagingContext& transaction,
                                      bool attemptSixtyFifth,
                                      FullItemProducerToken& rootToken) {
        const auto root = BeginFullItemPacketProducer(transaction, root9C);
        rootToken = root.token;
        auto rootPacket = MakeStagedFullItemPacket(
            FullItemPacketKind::ItemAction9C,
            root9C.action,
            MaximumStagedItemPacketBytes,
            0x11);
        auto descendantPacket = MakeStagedFullItemPacket(
            FullItemPacketKind::ItemAction9D,
            0x12,
            MaximumStagedItemPacketBytes,
            0x33);
        CHECK(CaptureFullItemPacketQueueCall(
            transaction,
            FullItemPacketKind::ItemAction9C,
            &client,
            rootPacket.data(),
            MaximumStagedItemPacketBytes)
            == FullItemPacketStagingError::None);
        std::size_t nextItem{1};
        for (std::size_t childNumber{};
                childNumber < MaximumStagedItemImmediateChildren;
                ++childNumber) {
            const auto childItem = nextItem++;
            const auto child = BeginFullItemPacketProducer(
                transaction, child9D(0, childItem));
            CHECK(CaptureFullItemPacketQueueCall(
                transaction,
                FullItemPacketKind::ItemAction9D,
                &client,
                descendantPacket.data(),
                MaximumStagedItemPacketBytes)
                == FullItemPacketStagingError::None);
            for (std::size_t grandchildNumber{};
                    grandchildNumber < MaximumStagedItemImmediateChildren;
                    ++grandchildNumber) {
                const auto grandchildItem = nextItem++;
                const auto grandchildAdmission = BeginFullItemPacketProducer(
                    transaction, child9D(childItem, grandchildItem));
                CHECK(CaptureFullItemPacketQueueCall(
                    transaction,
                    FullItemPacketKind::ItemAction9D,
                    &client,
                    descendantPacket.data(),
                    MaximumStagedItemPacketBytes)
                    == FullItemPacketStagingError::None);
                if (childNumber == 0 && grandchildNumber == 0) {
                    for (std::size_t greatGrandchildNumber{};
                            greatGrandchildNumber
                                < MaximumStagedItemImmediateChildren;
                            ++greatGrandchildNumber) {
                        const auto greatGrandchildItem = nextItem++;
                        const auto greatGrandchild =
                            BeginFullItemPacketProducer(
                                transaction,
                                child9D(
                                    grandchildItem,
                                    greatGrandchildItem));
                        CHECK(CaptureFullItemPacketQueueCall(
                            transaction,
                            FullItemPacketKind::ItemAction9D,
                            &client,
                            descendantPacket.data(),
                            MaximumStagedItemPacketBytes)
                            == FullItemPacketStagingError::None);
                        CHECK(EndFullItemPacketProducer(
                            transaction, greatGrandchild.token)
                            == FullItemProducerCompletion::NestedComplete);
                    }
                }
                CHECK(EndFullItemPacketProducer(
                    transaction, grandchildAdmission.token)
                    == FullItemProducerCompletion::NestedComplete);
            }
            CHECK(EndFullItemPacketProducer(transaction, child.token)
                == FullItemProducerCompletion::NestedComplete);
        }
        CHECK(nextItem == MaximumStagedItemPacketCount);
        CHECK(transaction.nodeCount == MaximumStagedItemPacketCount);
        CHECK(transaction.packetCount == MaximumStagedItemPacketCount);
        CHECK(transaction.totalBytes
            == MaximumStagedItemPacketCount
                * MaximumStagedItemPacketBytes);
        if (attemptSixtyFifth) {
            return BeginFullItemPacketProducer(
                transaction,
                child9D(0, MaximumStagedItemPacketCount)).error;
        }
        return FullItemPacketStagingError::None;
    };

    FullItemPacketStagingContext maximumTreeTransaction{};
    FullItemProducerToken maximumTreeRoot{};
    CHECK(stageMaximumTree(
        maximumTreeTransaction,
        false,
        maximumTreeRoot) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        maximumTreeTransaction, maximumTreeRoot)
        == FullItemProducerCompletion::RootReady);
    CHECK(ValidateCapturedFullItemPacketTransaction(maximumTreeTransaction)
        == FullItemPacketStagingError::None);
    FullItemStagingQueueProbe maximumTreeProbe{.rootReturned = true};
    const auto maximumTreeFlush = FlushOrDiscardFullItemPacketTransaction(
        maximumTreeTransaction,
        &RecordStagedFullItemPacket,
        &maximumTreeProbe);
    CHECK(maximumTreeFlush.completed);
    CHECK(maximumTreeFlush.queuedPacketCount
        == MaximumStagedItemPacketCount);
    CHECK(maximumTreeProbe.callCount == MaximumStagedItemPacketCount);

    FullItemPacketStagingContext nodeLimitTransaction{};
    FullItemProducerToken nodeLimitRoot{};
    CHECK(stageMaximumTree(
        nodeLimitTransaction,
        true,
        nodeLimitRoot) == FullItemPacketStagingError::NodeLimitExceeded);
    CHECK(EndFullItemPacketProducer(nodeLimitTransaction, nodeLimitRoot)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe nodeLimitProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        nodeLimitTransaction,
        &RecordStagedFullItemPacket,
        &nodeLimitProbe).error
        == FullItemPacketStagingError::NodeLimitExceeded);
    CHECK(nodeLimitProbe.callCount == 0);

    FullItemPacketStagingContext byteLimitTransaction{};
    const auto byteLimitRoot = BeginFullItemPacketProducer(
        byteLimitTransaction, root9C);
    byteLimitTransaction.totalBytes =
        MaximumStagedItemTransactionBytes - Packet9CHeaderBytes;
    auto minimum9CPacket = MakeStagedFullItemPacket(
        FullItemPacketKind::ItemAction9C,
        root9C.action,
        Packet9CHeaderBytes + 1U);
    CHECK(CaptureFullItemPacketQueueCall(
        byteLimitTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        minimum9CPacket.data(),
        Packet9CHeaderBytes + 1U)
        == FullItemPacketStagingError::ByteLimitExceeded);
    CHECK(EndFullItemPacketProducer(
        byteLimitTransaction, byteLimitRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe byteLimitProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        byteLimitTransaction,
        &RecordStagedFullItemPacket,
        &byteLimitProbe).error
        == FullItemPacketStagingError::ByteLimitExceeded);
    CHECK(byteLimitProbe.callCount == 0);

    // Out-of-order exits cannot accidentally make a partially captured tree ready.
    FullItemPacketStagingContext unbalancedTransaction{};
    const auto unbalancedRoot = BeginFullItemPacketProducer(
        unbalancedTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        unbalancedTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    const auto unbalancedChild = BeginFullItemPacketProducer(
        unbalancedTransaction, child9D(0, 1));
    CHECK(CaptureFullItemPacketQueueCall(
        unbalancedTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        limitChildPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        unbalancedTransaction, unbalancedRoot.token)
        == FullItemProducerCompletion::RootRejected);
    CHECK(EndFullItemPacketProducer(
        unbalancedTransaction, unbalancedChild.token)
        == FullItemProducerCompletion::NestedComplete);
    CHECK(EndFullItemPacketProducer(
        unbalancedTransaction, unbalancedRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe unbalancedProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        unbalancedTransaction,
        &RecordStagedFullItemPacket,
        &unbalancedProbe).error
        == FullItemPacketStagingError::UnbalancedProducerExit);
    CHECK(unbalancedProbe.callCount == 0);

    // A stale token poisons only the current transaction and cannot pop it.
    FullItemPacketStagingContext staleTokenTransaction{};
    const auto staleFirst = BeginFullItemPacketProducer(
        staleTokenTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        staleTokenTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(staleTokenTransaction, staleFirst.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe staleFirstProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        staleTokenTransaction,
        &RecordStagedFullItemPacket,
        &staleFirstProbe).completed);
    const auto staleSecond = BeginFullItemPacketProducer(
        staleTokenTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        staleTokenTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(staleTokenTransaction, staleFirst.token)
        == FullItemProducerCompletion::RootRejected);
    CHECK(EndFullItemPacketProducer(staleTokenTransaction, staleSecond.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe staleSecondProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        staleTokenTransaction,
        &RecordStagedFullItemPacket,
        &staleSecondProbe).error
        == FullItemPacketStagingError::StaleToken);
    CHECK(staleSecondProbe.callCount == 0);

    // The final byte scan catches metadata or copied-byte corruption before flush.
    FullItemPacketStagingContext corruptFinalTransaction{};
    const auto corruptFinalRoot = BeginFullItemPacketProducer(
        corruptFinalTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        corruptFinalTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        corruptFinalTransaction, corruptFinalRoot.token)
        == FullItemProducerCompletion::RootReady);
    corruptFinalTransaction.bytes[0] = 0;
    CHECK(ValidateCapturedFullItemPacketTransaction(corruptFinalTransaction)
        == FullItemPacketStagingError::InvalidPacketHeader);
    FullItemStagingQueueProbe corruptFinalProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        corruptFinalTransaction,
        &RecordStagedFullItemPacket,
        &corruptFinalProbe).error
        == FullItemPacketStagingError::InvalidPacketHeader);
    CHECK(corruptFinalProbe.callCount == 0);
    CHECK(corruptFinalTransaction.state == FullItemPacketStagingState::Idle);

    FullItemPacketStagingContext corruptOffsetTransaction{};
    const auto corruptOffsetRoot = BeginFullItemPacketProducer(
        corruptOffsetTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        corruptOffsetTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        corruptOffsetTransaction, corruptOffsetRoot.token)
        == FullItemProducerCompletion::RootReady);
    corruptOffsetTransaction.packets[0].byteOffset = 1;
    CHECK(ValidateCapturedFullItemPacketTransaction(corruptOffsetTransaction)
        == FullItemPacketStagingError::FinalBatchInvalid);
    FullItemStagingQueueProbe corruptOffsetProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        corruptOffsetTransaction,
        &RecordStagedFullItemPacket,
        &corruptOffsetProbe).error
        == FullItemPacketStagingError::FinalBatchInvalid);
    CHECK(corruptOffsetProbe.callCount == 0);

    FullItemPacketStagingContext nullCallbackTransaction{};
    const auto nullCallbackRoot = BeginFullItemPacketProducer(
        nullCallbackTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        nullCallbackTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        nullCallbackTransaction, nullCallbackRoot.token)
        == FullItemProducerCompletion::RootReady);
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        nullCallbackTransaction, nullptr, nullptr).error
        == FullItemPacketStagingError::InvalidArgument);
    CHECK(nullCallbackTransaction.state == FullItemPacketStagingState::Idle);

    // A rejected context resets cleanly and accepts the following transaction.
    FullItemPacketStagingContext resetTransaction{};
    const auto resetRejectedRoot = BeginFullItemPacketProducer(
        resetTransaction, root9C);
    auto resetBadPacket = limitRootPacket;
    resetBadPacket[1] ^= 1;
    CHECK(CaptureFullItemPacketQueueCall(
        resetTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        resetBadPacket.data(), 32)
        == FullItemPacketStagingError::InvalidPacketHeader);
    CHECK(EndFullItemPacketProducer(
        resetTransaction, resetRejectedRoot.token)
        == FullItemProducerCompletion::RootRejected);
    FullItemStagingQueueProbe resetRejectedProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        resetTransaction,
        &RecordStagedFullItemPacket,
        &resetRejectedProbe).error
        == FullItemPacketStagingError::InvalidPacketHeader);
    const auto resetValidRoot = BeginFullItemPacketProducer(
        resetTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        resetTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(resetTransaction, resetValidRoot.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe resetValidProbe{.rootReturned = true};
    CHECK(FlushOrDiscardFullItemPacketTransaction(
        resetTransaction,
        &RecordStagedFullItemPacket,
        &resetValidProbe).completed);
    CHECK(resetValidProbe.callCount == 1);

    // A flush-time producer reentry is post-commit fatal and stops the batch.
    FullItemPacketStagingContext reentrantFlushTransaction{};
    const auto reentrantRoot = BeginFullItemPacketProducer(
        reentrantFlushTransaction, root9C);
    CHECK(CaptureFullItemPacketQueueCall(
        reentrantFlushTransaction,
        FullItemPacketKind::ItemAction9C,
        &client,
        limitRootPacket.data(), 32) == FullItemPacketStagingError::None);
    const auto reentrantChild = BeginFullItemPacketProducer(
        reentrantFlushTransaction, child9D(0, 1));
    CHECK(CaptureFullItemPacketQueueCall(
        reentrantFlushTransaction,
        FullItemPacketKind::ItemAction9D,
        &client,
        limitChildPacket.data(), 32) == FullItemPacketStagingError::None);
    CHECK(EndFullItemPacketProducer(
        reentrantFlushTransaction, reentrantChild.token)
        == FullItemProducerCompletion::NestedComplete);
    CHECK(EndFullItemPacketProducer(
        reentrantFlushTransaction, reentrantRoot.token)
        == FullItemProducerCompletion::RootReady);
    FullItemStagingQueueProbe reentrantProbe{
        .rootReturned = true,
        .reentryTransaction = &reentrantFlushTransaction,
    };
    const auto reentrantFlush = FlushOrDiscardFullItemPacketTransaction(
        reentrantFlushTransaction,
        &RecordStagedFullItemPacket,
        &reentrantProbe);
    CHECK(!reentrantFlush.completed);
    CHECK(reentrantFlush.error
        == FullItemPacketStagingError::ReenteredDuringFlush);
    CHECK(reentrantFlush.queuedPacketCount == 1);
    CHECK(reentrantProbe.callCount == 1);
    CHECK(reentrantProbe.reentryDisposition
        == FullItemProducerDisposition::SkipOriginal);
    CHECK(reentrantFlushTransaction.state
        == FullItemPacketStagingState::Fatal);

    FullItemPacketStagingContext generationTransaction{};
    generationTransaction.generation =
        (std::numeric_limits<std::uint64_t>::max)();
    CHECK(BeginFullItemPacketProducer(
        generationTransaction, root9C).error
        == FullItemPacketStagingError::GenerationExhausted);
    CHECK(generationTransaction.state == FullItemPacketStagingState::Fatal);
}
} // namespace

int main() {
    using namespace ruffneckk::isc12;

    static_assert(SerializedBitWidth == 12);
    static_assert(SerializedSentinel == 0x0FFF);
    static_assert(MaximumStatId == 4094);
    static_assert(MaximumRecordCount == 4095);
    static_assert(InternalWordSentinel == 0xFFFF);
    static_assert(CanonicalSchemaDescriptorVersion == 1);
    static_assert(MaximumSerializedCsvBits == 32);
    static_assert(MaximumSerializedCsvParamBits == 16);
    static_assert(PreparedCodecMutableSiteCount == 43);
    static_assert(PreparedCodecMutationCount == 129);
    static_assert(PreparedCodecWitnessCount == 85);
    static_assert(MaximumPlayerStatSectionBytes == 3844);
    static_assert(PacketACMaximumBitstreamBits == 1289);
    static_assert(PacketACMaximumBitstreamBytes == 162);
    static_assert(PacketACMaximumPacketBytes == 175);
    static_assert(PacketACPacketHeadroomBytes == 69);

    CHECK(ClassifyFullItemTransportProvider(true, {}, {}, {})
        == FullItemTransportProvider::NativeG9);
    std::array<
        FullItemTransportHookObservation,
        FullItemTransportProviderSurfaceCount> externalTransportHooks{};
    for (auto& hook : externalTransportHooks) {
        hook = {
            .querySucceeded = true,
            .trackedInlineHook = true,
            .ownerCount = 1,
            .ownerPluginId = ExtendedItemStatsProviderId,
        };
    }
    CHECK(ClassifyFullItemTransportProvider(
        false,
        externalTransportHooks,
        ExtendedItemStatsProviderId,
        ExtendedItemStatsProviderVersion)
        == FullItemTransportProvider::ExtendedItemStatsV1);
    CHECK(ClassifyFullItemTransportProvider(
        false,
        externalTransportHooks,
        ExtendedItemStatsProviderId,
        "0.3.15") == FullItemTransportProvider::Invalid);
    auto partialTransportHooks = externalTransportHooks;
    partialTransportHooks[2].querySucceeded = false;
    CHECK(ClassifyFullItemTransportProvider(
        false,
        partialTransportHooks,
        ExtendedItemStatsProviderId,
        ExtendedItemStatsProviderVersion)
        == FullItemTransportProvider::Invalid);
    auto multiOwnerTransportHooks = externalTransportHooks;
    multiOwnerTransportHooks[4].ownerCount = 2;
    CHECK(ClassifyFullItemTransportProvider(
        false,
        multiOwnerTransportHooks,
        ExtendedItemStatsProviderId,
        ExtendedItemStatsProviderVersion)
        == FullItemTransportProvider::Invalid);
    auto wrongOwnerTransportHooks = externalTransportHooks;
    wrongOwnerTransportHooks[5].ownerPluginId = "another-plugin";
    CHECK(ClassifyFullItemTransportProvider(
        false,
        wrongOwnerTransportHooks,
        ExtendedItemStatsProviderId,
        ExtendedItemStatsProviderVersion)
        == FullItemTransportProvider::Invalid);

    constexpr auto packet9CBudget =
        FullItemPacketBudgetFor(FullItemPacketKind::ItemAction9C);
    constexpr auto packet9DBudget =
        FullItemPacketBudgetFor(FullItemPacketKind::ItemAction9D);
    CHECK(packet9CBudget.headerBytes == 8);
    CHECK(packet9CBudget.payloadCapacityBytes == 244);
    CHECK(packet9DBudget.headerBytes == 13);
    CHECK(packet9DBudget.payloadCapacityBytes == 239);
    CHECK(FullItemPacketBudgetFor(
        static_cast<FullItemPacketKind>(0xFF)).packetLimitBytes == 0);
    for (const auto bytes : {std::size_t{243}, std::size_t{244}}) {
        CHECK(ClassifyFullItemPayload(
            bytes,
            FullItemPacketKind::ItemAction9C)
            == FullItemPayloadDisposition::NativePacket);
    }
    CHECK(ClassifyFullItemPayload(
        245,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);
    for (const auto bytes : {std::size_t{238}, std::size_t{239}}) {
        CHECK(ClassifyFullItemPayload(
            bytes,
            FullItemPacketKind::ItemAction9D)
            == FullItemPayloadDisposition::NativePacket);
    }
    for (const auto bytes : {
            std::size_t{240}, std::size_t{242},
            std::size_t{243}, std::size_t{244}}) {
        CHECK(ClassifyFullItemPayload(
            bytes,
            FullItemPacketKind::ItemAction9D)
            == FullItemPayloadDisposition::ExceedsNativePacketCapacity);
    }

    FullItemNodePayload itemNode{1000, 76};
    auto fullItemEstimate = EstimateItemPacketPayload(itemNode);
    CHECK(fullItemEstimate.valid);
    CHECK(fullItemEstimate.totalBits == 1912);
    CHECK(fullItemEstimate.totalBytes == 239);
    CHECK(ClassifyFullItemPayload(
        fullItemEstimate.totalBytes,
        FullItemPacketKind::ItemAction9D)
        == FullItemPayloadDisposition::NativePacket);

    itemNode.directNonStatIdBits = 1001;
    fullItemEstimate = EstimateItemPacketPayload(itemNode);
    CHECK(fullItemEstimate.valid);
    CHECK(fullItemEstimate.totalBytes == 240);
    CHECK(ClassifyFullItemPayload(
        fullItemEstimate.totalBytes,
        FullItemPacketKind::ItemAction9D)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);
    CHECK(ClassifyFullItemPayload(
        fullItemEstimate.totalBytes,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::NativePacket);

    // Arithmetic reference only: 4,095 table rows in one emitted list would
    // require one token per record plus the terminating 0xFFF token. This is
    // not a claim that the native item builder permits that list shape.
    itemNode = {
        .directNonStatIdBits = 0,
        .statIdTokens = MaximumRecordCount + 1U,
    };
    const auto allTableRowsOneListEstimate = EstimateItemPacketPayload(itemNode);
    CHECK(allTableRowsOneListEstimate.valid);
    CHECK(allTableRowsOneListEstimate.totalBytes == 6144);
    CHECK(ClassifyFullItemPayload(
        allTableRowsOneListEstimate.totalBytes,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);

    itemNode = {
        .directNonStatIdBits = (std::numeric_limits<std::size_t>::max)(),
        .statIdTokens = 1,
    };
    CHECK(!EstimateItemPacketPayload(itemNode).valid);
    itemNode = {
        .directNonStatIdBits = 0,
        .statIdTokens =
            (std::numeric_limits<std::size_t>::max)() / 12U + 1U,
    };
    CHECK(!EstimateItemPacketPayload(itemNode).valid);

    LegacyFullItemNodePayload legacyNode{1684, 76};
    const auto expandedLegacyEstimate =
        ExpandLegacyItemPacketPayload(legacyNode);
    CHECK(expandedLegacyEstimate.valid);
    CHECK(expandedLegacyEstimate.totalBits == 1912);
    CHECK(expandedLegacyEstimate.totalBytes == 239);
    legacyNode = {
        .directLegacyBits = (std::numeric_limits<std::size_t>::max)(),
        .statIdTokens = 1,
    };
    CHECK(!ExpandLegacyItemPacketPayload(legacyNode).valid);

    const auto packetTree = std::to_array<FullItemNodePayload>({
        FullItemNodePayload{.directNonStatIdBits = 1952},
        FullItemNodePayload{.directNonStatIdBits = 1912},
        FullItemNodePayload{.directNonStatIdBits = 1912},
    });
    CHECK(ClassifyFullItemPacketSequence(
        packetTree,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::NativePacket);
    auto oversizedChildTree = packetTree;
    oversizedChildTree[1].directNonStatIdBits = 1913;
    CHECK(ClassifyFullItemPacketSequence(
        oversizedChildTree,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);
    auto oversizedRootTree = packetTree;
    oversizedRootTree[0].directNonStatIdBits = 1960;
    CHECK(ClassifyFullItemPacketSequence(
        oversizedRootTree,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);
    auto packet9DRootTree = packetTree;
    packet9DRootTree[0].directNonStatIdBits = 1912;
    CHECK(ClassifyFullItemPacketSequence(
        packet9DRootTree,
        FullItemPacketKind::ItemAction9D)
        == FullItemPayloadDisposition::NativePacket);
    packet9DRootTree[0].directNonStatIdBits = 1913;
    CHECK(ClassifyFullItemPacketSequence(
        packet9DRootTree,
        FullItemPacketKind::ItemAction9D)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);
    auto invalidDescendantTree = packetTree;
    invalidDescendantTree[2] = {};
    CHECK(ClassifyFullItemPacketSequence(
        invalidDescendantTree,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::InvalidEncoding);
    CHECK(ClassifyFullItemPacketSequence(
        std::span<const FullItemNodePayload>{},
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::InvalidEncoding);
    CHECK(ClassifyFullItemPayload(
        0,
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::InvalidEncoding);
    CHECK(ClassifyFullItemPayload(
        1,
        static_cast<FullItemPacketKind>(0xFF))
        == FullItemPayloadDisposition::InvalidEncoding);
    CHECK(ClassifyFullItemPayload(
        (std::numeric_limits<std::size_t>::max)(),
        FullItemPacketKind::ItemAction9C)
        == FullItemPayloadDisposition::ExceedsNativePacketCapacity);

    static_assert(noexcept(PreflightAndVisitFullItemPacketTree(
        FullItemPacketTreeView{},
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeScratch{},
        nullptr,
        nullptr)));
    const auto makePacketTreeNode = [](
            std::size_t directBits,
            std::size_t firstChildListOffset,
            std::size_t listedChildCount,
            std::size_t declaredChildCount,
            std::size_t socketCapacity) noexcept {
        return FullItemPacketTreeNode{
            .payload = {.directNonStatIdBits = directBits},
            .firstChildListOffset = firstChildListOffset,
            .listedChildCount = listedChildCount,
            .declaredChildCount = declaredChildCount,
            .socketCapacity = socketCapacity,
        };
    };
    std::array<std::uint8_t, 16> packetTreeMarks{};
    std::array<std::size_t, 16> packetTreeOrder{};
    std::array<std::size_t, 16> packetTreeStack{};
    FullItemPacketVisitProbe packetTreeProbe{};
    const auto runPacketTree = [&packetTreeMarks,
                                &packetTreeOrder,
                                &packetTreeStack,
                                &packetTreeProbe](
            std::span<const FullItemPacketTreeNode> nodes,
            std::span<const std::size_t> childNodeIndices,
            std::size_t rootNodeIndex,
            std::size_t declaredNodeCount,
            FullItemPacketKind rootPacket) noexcept {
        packetTreeProbe = {};
        return PreflightAndVisitFullItemPacketTree(
            {
                .nodes = nodes,
                .childNodeIndices = childNodeIndices,
                .rootNodeIndex = rootNodeIndex,
                .declaredNodeCount = declaredNodeCount,
            },
            rootPacket,
            {
                .nodeMarks = packetTreeMarks,
                .traversalOrder = packetTreeOrder,
                .nodeStack = packetTreeStack,
            },
            &RecordFullItemPacketVisit,
            &packetTreeProbe);
    };
    const auto expectPacketTreeFailure = [&runPacketTree, &packetTreeProbe](
            std::span<const FullItemPacketTreeNode> nodes,
            std::span<const std::size_t> childNodeIndices,
            std::size_t rootNodeIndex,
            std::size_t declaredNodeCount,
            FullItemPacketKind rootPacket,
            FullItemPacketTreeError expected) {
        CHECK(runPacketTree(
            nodes,
            childNodeIndices,
            rootNodeIndex,
            declaredNodeCount,
            rootPacket) == expected);
        CHECK(packetTreeProbe.callCount == 0);
    };

    const auto noChildIndices = std::array<std::size_t, 0>{};
    auto rootOnlyPacketTree = std::to_array<FullItemPacketTreeNode>({
        makePacketTreeNode(1952, 0, 0, 0, 0),
    });
    CHECK(runPacketTree(
        rootOnlyPacketTree,
        noChildIndices,
        0,
        1,
        FullItemPacketKind::ItemAction9C)
        == FullItemPacketTreeError::None);
    CHECK(packetTreeProbe.callCount == 1);
    CHECK(packetTreeProbe.nodeIndices[0] == 0);
    CHECK(packetTreeProbe.packetKinds[0]
        == FullItemPacketKind::ItemAction9C);

    rootOnlyPacketTree[0].payload.directNonStatIdBits = 1912;
    CHECK(runPacketTree(
        rootOnlyPacketTree,
        noChildIndices,
        0,
        1,
        FullItemPacketKind::ItemAction9D)
        == FullItemPacketTreeError::None);
    CHECK(packetTreeProbe.callCount == 1);
    CHECK(packetTreeProbe.packetKinds[0]
        == FullItemPacketKind::ItemAction9D);

    const auto fullPacketTreeChildren =
        std::to_array<std::size_t>({3, 0, 1});
    const auto fullPacketTree = std::to_array<FullItemPacketTreeNode>({
        makePacketTreeNode(1912, 0, 1, 1, 2),
        makePacketTreeNode(1912, 1, 0, 0, 0),
        makePacketTreeNode(1952, 1, 2, 2, 3),
        makePacketTreeNode(1912, 3, 0, 0, 0),
    });
    CHECK(runPacketTree(
        fullPacketTree,
        fullPacketTreeChildren,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C)
        == FullItemPacketTreeError::None);
    CHECK(packetTreeProbe.callCount == fullPacketTree.size());
    constexpr auto ExpectedPacketTreeOrder =
        std::to_array<std::size_t>({2, 0, 3, 1});
    for (std::size_t index{}; index < ExpectedPacketTreeOrder.size(); ++index) {
        CHECK(packetTreeProbe.nodeIndices[index]
            == ExpectedPacketTreeOrder[index]);
        CHECK(packetTreeProbe.packetKinds[index]
            == (index == 0
                ? FullItemPacketKind::ItemAction9C
                : FullItemPacketKind::ItemAction9D));
    }

    const auto emptyPacketTree = std::array<FullItemPacketTreeNode, 0>{};
    expectPacketTreeFailure(
        emptyPacketTree,
        noChildIndices,
        0,
        0,
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::InvalidArgument);
    expectPacketTreeFailure(
        fullPacketTree,
        fullPacketTreeChildren,
        fullPacketTree.size(),
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::NodeIndexOutOfRange);
    expectPacketTreeFailure(
        fullPacketTree,
        fullPacketTreeChildren,
        2,
        fullPacketTree.size(),
        static_cast<FullItemPacketKind>(0xFF),
        FullItemPacketTreeError::InvalidArgument);
    expectPacketTreeFailure(
        fullPacketTree,
        fullPacketTreeChildren,
        2,
        fullPacketTree.size() - 1U,
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::CountMismatch);

    packetTreeProbe = {};
    CHECK(PreflightAndVisitFullItemPacketTree(
        {
            .nodes = fullPacketTree,
            .childNodeIndices = fullPacketTreeChildren,
            .rootNodeIndex = 2,
            .declaredNodeCount = fullPacketTree.size(),
        },
        FullItemPacketKind::ItemAction9C,
        {
            .nodeMarks = packetTreeMarks,
            .traversalOrder = packetTreeOrder,
            .nodeStack = packetTreeStack,
        },
        nullptr,
        &packetTreeProbe) == FullItemPacketTreeError::InvalidArgument);
    CHECK(packetTreeProbe.callCount == 0);

    packetTreeProbe = {};
    CHECK(PreflightAndVisitFullItemPacketTree(
        {
            .nodes = fullPacketTree,
            .childNodeIndices = fullPacketTreeChildren,
            .rootNodeIndex = 2,
            .declaredNodeCount = fullPacketTree.size(),
        },
        FullItemPacketKind::ItemAction9C,
        {
            .nodeMarks = std::span<std::uint8_t>{packetTreeMarks}.first(3),
            .traversalOrder =
                std::span<std::size_t>{packetTreeOrder}.first(3),
            .nodeStack = packetTreeStack,
        },
        &RecordFullItemPacketVisit,
        &packetTreeProbe) == FullItemPacketTreeError::InsufficientScratch);
    CHECK(packetTreeProbe.callCount == 0);

    auto mismatchedChildCountTree = fullPacketTree;
    mismatchedChildCountTree[2].declaredChildCount = 1;
    expectPacketTreeFailure(
        mismatchedChildCountTree,
        fullPacketTreeChildren,
        2,
        mismatchedChildCountTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::CountMismatch);

    auto overSocketCapacityTree = fullPacketTree;
    overSocketCapacityTree[2].socketCapacity = 1;
    expectPacketTreeFailure(
        overSocketCapacityTree,
        fullPacketTreeChildren,
        2,
        overSocketCapacityTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::SocketCapacityExceeded);

    auto mismatchedStatTokenTree = fullPacketTree;
    mismatchedStatTokenTree[3].payload.statIdTokens = 2;
    mismatchedStatTokenTree[3].emittedStatRecordCount = 1;
    expectPacketTreeFailure(
        mismatchedStatTokenTree,
        fullPacketTreeChildren,
        2,
        mismatchedStatTokenTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::CountMismatch);

    auto overflowingStatCountTree = fullPacketTree;
    overflowingStatCountTree[3].payload.statIdTokens =
        (std::numeric_limits<std::size_t>::max)();
    overflowingStatCountTree[3].emittedStatRecordCount =
        (std::numeric_limits<std::size_t>::max)();
    overflowingStatCountTree[3].emittedStatListTerminatorCount = 1;
    expectPacketTreeFailure(
        overflowingStatCountTree,
        fullPacketTreeChildren,
        2,
        overflowingStatCountTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::CountMismatch);

    auto childListGapTree = fullPacketTree;
    childListGapTree[0].firstChildListOffset = 1;
    expectPacketTreeFailure(
        childListGapTree,
        fullPacketTreeChildren,
        2,
        childListGapTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::ChildListMismatch);

    auto childListOverlapTree = fullPacketTree;
    childListOverlapTree[2].firstChildListOffset = 0;
    expectPacketTreeFailure(
        childListOverlapTree,
        fullPacketTreeChildren,
        2,
        childListOverlapTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::ChildListMismatch);

    const auto trailingChildIndexList =
        std::to_array<std::size_t>({3, 0, 1, 0});
    expectPacketTreeFailure(
        fullPacketTree,
        trailingChildIndexList,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::ChildListMismatch);

    auto overflowingChildRangeTree = fullPacketTree;
    overflowingChildRangeTree[1].listedChildCount =
        (std::numeric_limits<std::size_t>::max)();
    overflowingChildRangeTree[1].declaredChildCount =
        (std::numeric_limits<std::size_t>::max)();
    overflowingChildRangeTree[1].socketCapacity =
        (std::numeric_limits<std::size_t>::max)();
    expectPacketTreeFailure(
        overflowingChildRangeTree,
        fullPacketTreeChildren,
        2,
        overflowingChildRangeTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::ChildListMismatch);

    auto outOfRangeChildIndices = fullPacketTreeChildren;
    outOfRangeChildIndices[0] = fullPacketTree.size();
    expectPacketTreeFailure(
        fullPacketTree,
        outOfRangeChildIndices,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::NodeIndexOutOfRange);
    outOfRangeChildIndices[0] =
        (std::numeric_limits<std::size_t>::max)();
    expectPacketTreeFailure(
        fullPacketTree,
        outOfRangeChildIndices,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::NodeIndexOutOfRange);

    const auto duplicateSiblingIndices =
        std::to_array<std::size_t>({3, 0, 0});
    expectPacketTreeFailure(
        fullPacketTree,
        duplicateSiblingIndices,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::DuplicateOrCycle);
    const auto sharedChildIndices =
        std::to_array<std::size_t>({3, 0, 3});
    expectPacketTreeFailure(
        fullPacketTree,
        sharedChildIndices,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::DuplicateOrCycle);
    const auto rootBackEdgeIndices =
        std::to_array<std::size_t>({2, 0, 1});
    expectPacketTreeFailure(
        fullPacketTree,
        rootBackEdgeIndices,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::DuplicateOrCycle);

    auto ancestorCycleTree = fullPacketTree;
    ancestorCycleTree[3].listedChildCount = 1;
    ancestorCycleTree[3].declaredChildCount = 1;
    ancestorCycleTree[3].socketCapacity = 1;
    const auto ancestorCycleIndices =
        std::to_array<std::size_t>({3, 0, 1, 0});
    expectPacketTreeFailure(
        ancestorCycleTree,
        ancestorCycleIndices,
        2,
        ancestorCycleTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::DuplicateOrCycle);

    const auto isolatedNodeTree = std::to_array<FullItemPacketTreeNode>({
        makePacketTreeNode(1952, 0, 0, 0, 0),
        makePacketTreeNode(1912, 0, 0, 0, 0),
    });
    expectPacketTreeFailure(
        isolatedNodeTree,
        noChildIndices,
        0,
        isolatedNodeTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::UnreachableNode);

    const auto disconnectedCycleTree =
        std::to_array<FullItemPacketTreeNode>({
            makePacketTreeNode(1952, 0, 0, 0, 0),
            makePacketTreeNode(1912, 0, 1, 1, 1),
        });
    const auto disconnectedCycleIndices =
        std::to_array<std::size_t>({1});
    expectPacketTreeFailure(
        disconnectedCycleTree,
        disconnectedCycleIndices,
        0,
        disconnectedCycleTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::UnreachableNode);

    auto oversizedLastDescendantTree = fullPacketTree;
    oversizedLastDescendantTree[3].payload.directNonStatIdBits = 1913;
    expectPacketTreeFailure(
        oversizedLastDescendantTree,
        fullPacketTreeChildren,
        2,
        oversizedLastDescendantTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::ExceedsNativePacketCapacity);

    auto overflowingLastDescendantTree = fullPacketTree;
    overflowingLastDescendantTree[3].payload = {
        .directNonStatIdBits = (std::numeric_limits<std::size_t>::max)(),
        .statIdTokens = 1,
    };
    overflowingLastDescendantTree[3].emittedStatRecordCount = 1;
    expectPacketTreeFailure(
        overflowingLastDescendantTree,
        fullPacketTreeChildren,
        2,
        overflowingLastDescendantTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::InvalidEncoding);

    auto overflowingTokenBitsTree = fullPacketTree;
    overflowingTokenBitsTree[3].payload = {
        .directNonStatIdBits = 1,
        .statIdTokens =
            (std::numeric_limits<std::size_t>::max)() / 12U + 1U,
    };
    overflowingTokenBitsTree[3].emittedStatRecordCount =
        overflowingTokenBitsTree[3].payload.statIdTokens;
    expectPacketTreeFailure(
        overflowingTokenBitsTree,
        fullPacketTreeChildren,
        2,
        overflowingTokenBitsTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::InvalidEncoding);

    auto zeroPayloadDescendantTree = fullPacketTree;
    zeroPayloadDescendantTree[3].payload = {};
    expectPacketTreeFailure(
        zeroPayloadDescendantTree,
        fullPacketTreeChildren,
        2,
        zeroPayloadDescendantTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::InvalidEncoding);

    auto oversizedRootPacketTree = fullPacketTree;
    oversizedRootPacketTree[2].payload.directNonStatIdBits = 1960;
    expectPacketTreeFailure(
        oversizedRootPacketTree,
        fullPacketTreeChildren,
        2,
        oversizedRootPacketTree.size(),
        FullItemPacketKind::ItemAction9C,
        FullItemPacketTreeError::ExceedsNativePacketCapacity);
    expectPacketTreeFailure(
        fullPacketTree,
        fullPacketTreeChildren,
        2,
        fullPacketTree.size(),
        FullItemPacketKind::ItemAction9D,
        FullItemPacketTreeError::ExceedsNativePacketCapacity);

    RunFullItemPacketStagingTests();

    const auto codecGroups = PreparedCodecPatchGroups();
    CHECK(codecGroups.size() == 9);
    constexpr std::size_t G1GroupIndex = 7;
    std::size_t observedCodecMutationCount{};
    std::size_t observedCodecSiteCount{};
    for (const auto& group : codecGroups) {
        CHECK(ValidateCodecPatchGroup(group) == CodecPatchPlanError::None);
        CHECK(!group.witnesses.empty());
        observedCodecSiteCount += group.sites.size();
        for (const auto& site : group.sites) {
            observedCodecMutationCount += site.mutations.size();
        }
    }
    CHECK(observedCodecMutationCount == PreparedCodecMutationCount);
    CHECK(observedCodecSiteCount == PreparedCodecMutableSiteCount);
    CHECK(codecGroups[0].id == CodecPatchGroupId::FullItemTransport);
    CHECK(codecGroups[0].sites.size() == 4);
    CHECK(codecGroups[0].sites[0].pattern.rva == 0x479E10);
    CHECK(codecGroups[0].sites[1].pattern.rva == 0x47A001);
    CHECK(codecGroups[0].sites[2].pattern.rva == 0x479CD0);
    CHECK(codecGroups[0].sites[3].pattern.rva == 0x479EA0);
    CHECK(codecGroups[1].id == CodecPatchGroupId::Packet3E);
    CHECK(codecGroups[1].sites.size() == 3);
    CHECK(codecGroups[1].witnesses.size() == 1);
    CHECK(codecGroups[2].id == CodecPatchGroupId::PacketA8);
    CHECK(codecGroups[2].sites.size() == 5);
    CHECK(codecGroups[2].witnesses.size() == 3);
    CHECK(codecGroups[3].id == CodecPatchGroupId::PacketAA);
    CHECK(codecGroups[3].sites.size() == 7);
    CHECK(codecGroups[3].witnesses.size() == 1);
    CHECK(codecGroups[4].id == CodecPatchGroupId::PacketAC);
    CHECK(codecGroups[4].sites.size() == 4);
    CHECK(codecGroups[4].witnesses.size() == 3);
    CHECK(codecGroups[G1GroupIndex].id == CodecPatchGroupId::GenericItem);
    CHECK(codecGroups[G1GroupIndex].sites.size() == 4);
    std::size_t observedG1Mutations{};
    for (const auto& site : codecGroups[G1GroupIndex].sites) {
        observedG1Mutations += site.mutations.size();
    }
    CHECK(observedG1Mutations == 44);
    CHECK(codecGroups[G1GroupIndex].sites[0].pattern.rva == 0x37F174);
    CHECK(codecGroups[G1GroupIndex].sites[1].pattern.rva == 0x37AB2B);
    CHECK(codecGroups[G1GroupIndex].sites[2].pattern.rva == 0x37B7D4);
    CHECK(codecGroups[G1GroupIndex].sites[3].pattern.rva == 0x37F983);
    const auto& boundedWriterSite = codecGroups[G1GroupIndex].sites[0];
    CHECK(boundedWriterSite.pattern.bytes.size() == 50);
    CHECK(boundedWriterSite.mutations.size() == 37);
    for (std::size_t index{};
            index < boundedWriterSite.pattern.mask.size(); ++index) {
        CHECK(boundedWriterSite.pattern.mask[index] == 0xFF);
    }
    for (std::size_t index{};
            index < boundedWriterSite.mutations.size(); ++index) {
        const auto& mutation = boundedWriterSite.mutations[index];
        CHECK(mutation.patternOffset == index + 8U);
        CHECK(mutation.expected
            == GenericItemBoundedWriterBytes[mutation.patternOffset]);
        CHECK(mutation.replacement
            == GenericItemBoundedWriterReplacementBytes[
                mutation.patternOffset]);
        CHECK(mutation.source
            == CodecByteMutation::ReplacementSource::Literal);
    }
    for (std::size_t index{}; index < 8U; ++index) {
        CHECK(GenericItemBoundedWriterBytes[index]
            == GenericItemBoundedWriterReplacementBytes[index]);
    }
    for (std::size_t index = 45U;
            index < GenericItemBoundedWriterBytes.size(); ++index) {
        CHECK(GenericItemBoundedWriterBytes[index]
            == GenericItemBoundedWriterReplacementBytes[index]);
    }
    auto duplicateBoundedMutations =
        std::array<CodecByteMutation, 37>{};
    std::copy(
        boundedWriterSite.mutations.begin(),
        boundedWriterSite.mutations.end(),
        duplicateBoundedMutations.begin());
    duplicateBoundedMutations[1] = duplicateBoundedMutations[0];
    const std::array duplicateBoundedSites{
        CodecPatchSite{
            boundedWriterSite.pattern,
            duplicateBoundedMutations},
    };
    const CodecPatchGroup duplicateBoundedGroup{
        CodecPatchGroupId::GenericItem,
        "duplicate-bounded-writer",
        duplicateBoundedSites,
        codecGroups[G1GroupIndex].witnesses.first(1),
    };
    CHECK(ValidateCodecPatchGroup(duplicateBoundedGroup)
        == CodecPatchPlanError::DuplicateMutation);

    const std::array overlappingBoundedSites{
        CodecPatchSite{
            boundedWriterSite.pattern,
            boundedWriterSite.mutations},
    };
    const std::array overlappingBoundedWitnesses{
        boundedWriterSite.pattern,
    };
    const CodecPatchGroup overlappingBoundedGroup{
        CodecPatchGroupId::GenericItem,
        "overlapping-bounded-witness",
        overlappingBoundedSites,
        overlappingBoundedWitnesses,
    };
    CHECK(ValidateCodecPatchGroup(overlappingBoundedGroup)
        == CodecPatchPlanError::WitnessOverlapsMutation);

    constexpr auto expectedG1WitnessRvas =
        std::to_array<std::uintptr_t>({
        0x37D140, 0x3800E8, 0x37F08A, 0x37F09B, 0x37F0D0,
        0x2F6527,
        0x37F295, 0x37F36D, 0x37F445, 0x37F51D, 0x37F685,
        0x37F754, 0x37F832, 0x37F901,
    });
    CHECK(codecGroups[G1GroupIndex].witnesses.size()
        == expectedG1WitnessRvas.size());
    for (std::size_t index{}; index < expectedG1WitnessRvas.size(); ++index) {
        CHECK(codecGroups[G1GroupIndex].witnesses[index].rva
            == expectedG1WitnessRvas[index]);
        for (const auto mask :
                codecGroups[G1GroupIndex].witnesses[index].mask) {
            CHECK(mask == 0xFF);
        }
    }
    constexpr auto expectedG9WitnessRvas =
        std::to_array<std::uintptr_t>({
        0x479D85, 0x479E15, 0x479F76, 0x47A006,
        0x479E23, 0x47A019, 0x375F25, 0x12E2F0, 0x12E4B0,
        0x481BAD, 0x4817F0, 0x4818B6,
    });
    CHECK(codecGroups[0].witnesses.size()
        == expectedG9WitnessRvas.size());
    for (std::size_t index{}; index < expectedG9WitnessRvas.size(); ++index) {
        CHECK(codecGroups[0].witnesses[index].rva
            == expectedG9WitnessRvas[index]);
        for (const auto mask : codecGroups[0].witnesses[index].mask) {
            CHECK(mask == 0xFF);
        }
    }
    CHECK(codecGroups[0].witnesses[4].bytes.size() == 30);
    CHECK(codecGroups[0].witnesses[4].bytes.back() == 0xC3);
    CHECK(codecGroups[0].witnesses[5].bytes.size() == 30);
    CHECK(codecGroups[0].witnesses[5].bytes.back() == 0xC3);
    auto g9WitnessFixture = MakeCodecPatchSetFixture(codecGroups);
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x479D85, 52, 0x375EE0));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x479E10, 1, 0x4817F0));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x479E15, 10, 0x481B50));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x479F76, 40, 0x375EE0));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x47A001, 1, 0x4817F0));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x47A006, 15, 0x481B50));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x481BAD, 1, 0x388C10));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x481BAD, 30, 0x38AAB0));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x481BAD, 82, 0x479EA0));
    CHECK(CodecFixtureHasRel32(
        g9WitnessFixture, 0x481BAD, 90, 0x38ABA0));
    CHECK(codecGroups.back().id == CodecPatchGroupId::PlayerSave);

    constexpr std::uintptr_t AuxiliaryReaderRelayRva = 0x01F00000;
    constexpr std::uintptr_t PlayerReaderRelayRva = 0x01F01000;
    constexpr std::uintptr_t PlayerPreviewRelayRva = 0x01F02000;
    constexpr std::uintptr_t PlayerSaveFinalizeRelayRva = 0x01F03000;
    constexpr std::uintptr_t Packet9CQueueRelayRva = 0x01F04000;
    constexpr std::uintptr_t Packet9DQueueRelayRva = 0x01F05000;
    constexpr std::uintptr_t Packet9CEntryRelayRva = 0x01F06000;
    constexpr std::uintptr_t Packet9DEntryRelayRva = 0x01F07000;
    constexpr auto codecActivationTargets =
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva);

    NativePublicationLeaseView noCodecLease;
    CodecQuiescenceFixture heldCodecQuiescence{.held = true};
    auto heldCodecLease = NativePublicationLeaseView::ForTesting(
        &heldCodecQuiescence,
        &ValidateCodecQuiescence);
    {
        CodecQuiescenceFixture releasedLeaseFixture{.held = true};
        {
            auto releasedLease =
                NativePublicationLeaseView::ForTesting(
                    &releasedLeaseFixture,
                    &ValidateCodecQuiescence);
            CHECK(releasedLease.IsHeld());
        }
        CHECK(releasedLeaseFixture.held);
    }

    auto inactiveCodecSetFixture = MakeCodecPatchSetFixture(codecGroups);
    auto codecSetCallbacks = CodecPatchCallbacks{
        .context = &inactiveCodecSetFixture,
        .verifyPattern = &VerifyCodecFixturePattern,
        .writeByte = &WriteCodecFixtureByte,
        .flushInstructionCache = &FlushCodecFixtureInstructionCache,
        .reserveMutationLifetime = &ReserveCodecFixtureMutationLifetime,
    };

    static_assert(std::is_trivially_copyable_v<NativePublicationLeaseView>);
    static_assert(std::is_copy_constructible_v<PreparedCodecPatchPlan>);
    static_assert(!std::is_copy_assignable_v<PreparedCodecPatchPlan>);
    static_assert(std::is_nothrow_move_constructible_v<
        PreparedCodecPatchPlan>);
    static_assert(!std::is_move_assignable_v<
        PreparedCodecPatchPlan>);
    auto splitCodecFixture = MakeCodecPatchSetFixture(codecGroups);
    auto splitPreflight = PreflightPreparedCodecPatchSet(
        heldCodecLease,
        codecActivationTargets,
        CodecPatchPreflightCallbacks{
            .context = &splitCodecFixture,
            .verifyPattern = &VerifyCodecFixturePattern,
        });
    CHECK(splitPreflight.status == CodecPatchPreflightStatus::Prepared);
    CHECK(splitPreflight.plan.has_value());
    CHECK(splitCodecFixture.verifyCalls
        == PreparedCodecMutableSiteCount + PreparedCodecWitnessCount);
    CHECK(splitCodecFixture.reserveLifetimeCalls == 0);
    CHECK(splitCodecFixture.writeCalls == 0);
    CHECK(splitCodecFixture.flushCalls == 0);
    if (splitPreflight.plan) {
        const auto copiedPlan = *splitPreflight.plan;
        CHECK(copiedPlan.ResolvedBytes().size()
            == PreparedCodecMutationCount);
        CHECK(copiedPlan.FlushRangeCount()
            == PreparedCodecMutableSiteCount);
        const auto preflightVerifyCalls = splitCodecFixture.verifyCalls;
        const auto splitCommit = CommitPreflightedCodecPatchSet(
            heldCodecLease,
            copiedPlan,
            CodecPatchCommitCallbacks{
                .context = &splitCodecFixture,
                .writeByte = &WriteCodecFixtureByte,
                .flushInstructionCache =
                    &FlushCodecFixtureInstructionCache,
            });
        CHECK(splitCommit.status == CodecPatchCommitStatus::Active);
        CHECK(splitCommit.mutationAttempted);
        CHECK(splitCommit.confirmedMutations
            == PreparedCodecMutationCount);
        CHECK(splitCommit.confirmedFlushes
            == PreparedCodecMutableSiteCount);
        CHECK(splitCodecFixture.verifyCalls == preflightVerifyCalls);
        CHECK(splitCodecFixture.reserveLifetimeCalls == 0);
    }

    auto codecSetResult = CommitPreparedCodecPatchSet(
        noCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::QuiescenceRequired);
    CHECK(!codecSetResult.mutationAttempted);
    CHECK(codecSetResult.attemptedMutations == 0);
    CHECK(inactiveCodecSetFixture.verifyCalls == 0);
    CHECK(inactiveCodecSetFixture.reserveLifetimeCalls == 0);
    CHECK(inactiveCodecSetFixture.writeCalls == 0);
    CHECK(inactiveCodecSetFixture.flushCalls == 0);

    auto revokedBeforePreflightFixture =
        MakeCodecPatchSetFixture(codecGroups);
    codecSetCallbacks.context = &revokedBeforePreflightFixture;
    CodecQuiescenceFixture revokedBeforePreflight{
        .held = true,
        .revokeOnValidation = 1,
    };
    {
        auto revokedLease = NativePublicationLeaseView::ForTesting(
            &revokedBeforePreflight,
            &ValidateCodecQuiescence);
        codecSetResult = CommitPreparedCodecPatchSet(
            revokedLease, codecActivationTargets, codecSetCallbacks);
    }
    CHECK(codecSetResult.status
        == CodecPatchCommitStatus::QuiescenceRequired);
    CHECK(!codecSetResult.mutationAttempted);
    CHECK(revokedBeforePreflightFixture.verifyCalls == 0);
    CHECK(revokedBeforePreflightFixture.reserveLifetimeCalls == 0);
    CHECK(revokedBeforePreflightFixture.writeCalls == 0);
    CHECK(revokedBeforePreflightFixture.flushCalls == 0);

    auto revokedAfterWriteFixture = MakeCodecPatchSetFixture(codecGroups);
    codecSetCallbacks.context = &revokedAfterWriteFixture;
    CodecQuiescenceFixture revokedAfterWrite{
        .held = true,
        .observedCodec = &revokedAfterWriteFixture,
        .revokeAfterWriteCalls = 1,
    };
    {
        auto revokedLease = NativePublicationLeaseView::ForTesting(
            &revokedAfterWrite,
            &ValidateCodecQuiescence);
        codecSetResult = CommitPreparedCodecPatchSet(
            revokedLease, codecActivationTargets, codecSetCallbacks);
    }
    CHECK(codecSetResult.status == CodecPatchCommitStatus::
        PartialCommitColdRestartRequired);
    CHECK(codecSetResult.mutationAttempted);
    CHECK(codecSetResult.attemptedMutations
        == codecSetResult.confirmedMutations);
    CHECK(codecSetResult.confirmedMutations >= 1);
    CHECK(revokedAfterWriteFixture.reserveLifetimeCalls == 1);
    CHECK(revokedAfterWriteFixture.writeCalls == 1);
    CHECK(revokedAfterWriteFixture.flushCalls == 0);

    auto callbacksWithoutFlush = codecSetCallbacks;
    callbacksWithoutFlush.flushInstructionCache = nullptr;
    CHECK(CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, callbacksWithoutFlush).status
        == CodecPatchCommitStatus::InvalidPlan);
    CHECK(inactiveCodecSetFixture.verifyCalls == 0);
    auto callbacksWithoutReservation = codecSetCallbacks;
    callbacksWithoutReservation.reserveMutationLifetime = nullptr;
    CHECK(CommitPreparedCodecPatchSet(
        heldCodecLease,
        codecActivationTargets,
        callbacksWithoutReservation).status
        == CodecPatchCommitStatus::InvalidPlan);

    auto codecSetFixture = MakeCodecPatchSetFixture(codecGroups);
    codecSetCallbacks.context = &codecSetFixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::Active);
    CHECK(codecSetResult.mutationAttempted);
    CHECK(codecSetResult.attemptedMutations == PreparedCodecMutationCount);
    CHECK(codecSetResult.confirmedMutations == PreparedCodecMutationCount);
    CHECK(codecSetResult.confirmedFlushes == PreparedCodecMutableSiteCount);
    CHECK(codecSetFixture.reserveLifetimeCalls == 1);
    CHECK(codecSetFixture.flushCalls == PreparedCodecMutableSiteCount);
    const auto activeCodecSetResult = codecSetResult;
    for (const auto& group : codecGroups) {
        for (const auto& site : group.sites) {
            const auto fixtureSite = FindCodecFixtureSite(
                codecSetFixture, site.pattern.rva);
            CHECK(fixtureSite != nullptr);
            if (!fixtureSite) continue;
            for (const auto& mutation : site.mutations) {
                if (mutation.source
                        == CodecByteMutation::ReplacementSource::Literal) {
                    CHECK(fixtureSite->bytes[mutation.patternOffset]
                        == mutation.replacement);
                }
            }
        }
    }
    const auto activeG1First = FindCodecFixtureSite(
        codecSetFixture, 0x37AB2B);
    const auto activeG1Next = FindCodecFixtureSite(
        codecSetFixture, 0x37B7D4);
    const auto activeG1Writer = FindCodecFixtureSite(
        codecSetFixture, 0x37F174);
    const auto activeG1Terminator = FindCodecFixtureSite(
        codecSetFixture, 0x37F983);
    CHECK(activeG1First != nullptr);
    CHECK(activeG1Next != nullptr);
    CHECK(activeG1Writer != nullptr);
    CHECK(activeG1Terminator != nullptr);
    if (activeG1First) {
        CHECK(activeG1First->bytes[1] == 0x0C);
        CHECK(activeG1First->bytes[11] == 0xF3);
        CHECK(activeG1First->bytes[21] == 0x0F);
    }
    if (activeG1Next) {
        CHECK(activeG1Next->bytes[1] == 0x0C);
        CHECK(activeG1Next->bytes[17] == 0x0F);
    }
    if (activeG1Writer) {
        CHECK(std::equal(
            activeG1Writer->bytes.begin(),
            activeG1Writer->bytes.end(),
            GenericItemBoundedWriterReplacementBytes.begin(),
            GenericItemBoundedWriterReplacementBytes.end()));
        CHECK(activeG1Writer->bytes[0] == 0x85);
        CHECK(activeG1Writer->bytes[1] == 0xF6);
        CHECK(activeG1Writer->bytes[8] == 0x81);
        CHECK(activeG1Writer->bytes[14] == 0x73);
        CHECK(activeG1Writer->bytes[16] == 0x3B);
        CHECK(activeG1Writer->bytes[26] == 0xBA);
        CHECK(activeG1Writer->bytes[28] == 0x0F);
        CHECK(activeG1Writer->bytes[36] == 0x41);
        CHECK(activeG1Writer->bytes[38] == 0x0C);
        CHECK(activeG1Writer->bytes[45] == 0xE8);
    }
    if (activeG1Terminator) {
        CHECK(activeG1Terminator->bytes[2] == 0x0F);
        CHECK(activeG1Terminator->bytes[7] == 0x0C);
    }

    CHECK(codecSetFixture.flushFirstRvas[0] == 0x479E11);
    CHECK(codecSetFixture.flushSizes[0] == 4);
    CHECK(codecSetFixture.flushFirstRvas[1] == 0x47A002);
    CHECK(codecSetFixture.flushSizes[1] == 4);
    CHECK(codecSetFixture.flushFirstRvas[2] == 0x479CD0);
    CHECK(codecSetFixture.flushSizes[2] == 5);
    CHECK(codecSetFixture.flushFirstRvas[3] == 0x479EA0);
    CHECK(codecSetFixture.flushSizes[3] == 5);

    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x479E10, 1, Packet9CQueueRelayRva));
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x47A001, 1, Packet9DQueueRelayRva));
    const auto active9CEntry = FindCodecFixtureSite(
        codecSetFixture, 0x479CD0);
    const auto active9DEntry = FindCodecFixtureSite(
        codecSetFixture, 0x479EA0);
    CHECK(active9CEntry != nullptr);
    CHECK(active9DEntry != nullptr);
    if (active9CEntry) CHECK(active9CEntry->bytes[0] == 0xE9);
    if (active9DEntry) CHECK(active9DEntry->bytes[0] == 0xE9);
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x479CD0, 1, Packet9CEntryRelayRva));
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x479EA0, 1, Packet9DEntryRelayRva));

    std::size_t g1FirstFlush{};
    for (std::size_t index{}; index < G1GroupIndex; ++index) {
        g1FirstFlush += codecGroups[index].sites.size();
    }
    CHECK(codecSetFixture.flushFirstRvas[g1FirstFlush] == 0x37F17C);
    CHECK(codecSetFixture.flushSizes[g1FirstFlush] == 37);

    const auto& g1NextPattern =
        codecGroups[G1GroupIndex].sites[2].pattern;
    for (std::size_t index{}; index < g1NextPattern.mask.size(); ++index) {
        CHECK(g1NextPattern.mask[index] == 0xFF);
    }
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x37B7D4, 9, 0xA1B6C0));
    auto retargetedG1CallFixture = MakeCodecPatchSetFixture(codecGroups);
    auto retargetedG1Call = FindCodecFixtureSite(
        retargetedG1CallFixture, 0x37B7D4);
    CHECK(retargetedG1Call != nullptr);
    if (retargetedG1Call) retargetedG1Call->bytes[9] ^= 0x5A;
    codecSetCallbacks.context = &retargetedG1CallFixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::PreflightFailed);
    CHECK(retargetedG1CallFixture.writeCalls == 0);
    CHECK(retargetedG1CallFixture.flushCalls == 0);

    auto corruptG1Fixture = MakeCodecPatchSetFixture(codecGroups);
    auto corruptG1Site = FindCodecFixtureSite(corruptG1Fixture, 0x37B7D4);
    CHECK(corruptG1Site != nullptr);
    if (corruptG1Site) corruptG1Site->bytes[8] ^= 0xFF;
    codecSetCallbacks.context = &corruptG1Fixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::PreflightFailed);
    CHECK(corruptG1Fixture.writeCalls == 0);
    CHECK(corruptG1Fixture.flushCalls == 0);

    for (const auto corruptOffset : {std::size_t{0}, std::size_t{49}}) {
        auto corruptBoundedWriterFixture =
            MakeCodecPatchSetFixture(codecGroups);
        auto corruptBoundedWriter = FindCodecFixtureSite(
            corruptBoundedWriterFixture, 0x37F174);
        CHECK(corruptBoundedWriter != nullptr);
        if (corruptBoundedWriter) {
            corruptBoundedWriter->bytes[corruptOffset] ^= 0xFF;
        }
        codecSetCallbacks.context = &corruptBoundedWriterFixture;
        codecSetResult = CommitPreparedCodecPatchSet(
            heldCodecLease, codecActivationTargets, codecSetCallbacks);
        CHECK(codecSetResult.status
            == CodecPatchCommitStatus::PreflightFailed);
        CHECK(corruptBoundedWriterFixture.writeCalls == 0);
        CHECK(corruptBoundedWriterFixture.flushCalls == 0);
    }

    for (std::size_t index{}; index < 14U; ++index) {
        auto corruptG1SafetyWitnessFixture =
            MakeCodecPatchSetFixture(codecGroups);
        const auto witnessRva =
            codecGroups[G1GroupIndex].witnesses[index].rva;
        auto corruptG1SafetyWitness = FindCodecFixtureSite(
            corruptG1SafetyWitnessFixture, witnessRva);
        CHECK(corruptG1SafetyWitness != nullptr);
        if (corruptG1SafetyWitness) {
            corruptG1SafetyWitness->bytes.back() ^= 0xFF;
        }
        codecSetCallbacks.context = &corruptG1SafetyWitnessFixture;
        codecSetResult = CommitPreparedCodecPatchSet(
            heldCodecLease, codecActivationTargets, codecSetCallbacks);
        CHECK(codecSetResult.status
            == CodecPatchCommitStatus::PreflightFailed);
        CHECK(corruptG1SafetyWitnessFixture.writeCalls == 0);
        CHECK(corruptG1SafetyWitnessFixture.flushCalls == 0);
    }

    auto corruptCodecSetFixture = MakeCodecPatchSetFixture(codecGroups);
    corruptCodecSetFixture.sites.front().bytes.front() ^= 0xFF;
    codecSetCallbacks.context = &corruptCodecSetFixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::PreflightFailed);
    CHECK(codecSetResult.attemptedMutations == 0);
    CHECK(corruptCodecSetFixture.writeCalls == 0);
    CHECK(corruptCodecSetFixture.flushCalls == 0);

    auto corruptWitnessFixture = MakeCodecPatchSetFixture(codecGroups);
    const auto corruptWitnessRva = codecGroups.front().witnesses.front().rva;
    const auto corruptWitness = FindCodecFixtureSite(
        corruptWitnessFixture, corruptWitnessRva);
    CHECK(corruptWitness != nullptr);
    if (corruptWitness) corruptWitness->bytes.front() ^= 0xFF;
    codecSetCallbacks.context = &corruptWitnessFixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::PreflightFailed);
    CHECK(corruptWitnessFixture.writeCalls == 0);
    CHECK(corruptWitnessFixture.flushCalls == 0);

    auto partialCodecSetFixture = MakeCodecPatchSetFixture(codecGroups);
    partialCodecSetFixture.failWriteAttempt = 10;
    codecSetCallbacks.context = &partialCodecSetFixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status
        == CodecPatchCommitStatus::PartialCommitColdRestartRequired);
    CHECK(partialCodecSetFixture.writeCalls == 11);
    CHECK(codecSetResult.attemptedMutations
        == codecSetResult.confirmedMutations + 1U);

    const auto& playerSaveGroup = codecGroups.back();
    CHECK(playerSaveGroup.sites.size() >= 2);
    CHECK(playerSaveGroup.sites[playerSaveGroup.sites.size() - 2U]
        .pattern.rva == 0x5353BD);
    CHECK(playerSaveGroup.sites.back().pattern.rva == 0x5353C7);
    constexpr std::uintptr_t playerSaveCallNextRva = 0x5353C7;
    constexpr auto expectedRelayDisplacement = static_cast<std::uint32_t>(
        PlayerSaveFinalizeRelayRva
        - playerSaveCallNextRva);
    const auto activeRelayCall = FindCodecFixtureSite(
        codecSetFixture, 0x5353BD);
    const auto activeStatus = FindCodecFixtureSite(
        codecSetFixture, 0x5353C7);
    CHECK(activeRelayCall != nullptr);
    CHECK(activeStatus != nullptr);
    if (activeRelayCall) {
        for (std::size_t index{}; index < 4; ++index) {
            CHECK(activeRelayCall->bytes[6U + index]
                == static_cast<std::uint8_t>(
                    expectedRelayDisplacement >> (index * 8U)));
        }
    }
    if (activeStatus) {
        CHECK(activeStatus->bytes[11] == 0x8B);
        CHECK(activeStatus->bytes[12] == 0xC2);
    }
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x531A54, 26, AuxiliaryReaderRelayRva));
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x52EC28, 35, PlayerReaderRelayRva));
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x530A24, 17, PlayerReaderRelayRva));
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x61CF81, 16, PlayerPreviewRelayRva));
    CHECK(CodecFixtureHasRel32(
        codecSetFixture, 0x5353BD, 6, PlayerSaveFinalizeRelayRva));
    CHECK(activeCodecSetResult.confirmedNoOpMutations == 1);
    CHECK(codecSetFixture.writeCalls
        == PreparedCodecMutationCount
            - activeCodecSetResult.confirmedNoOpMutations);

    constexpr auto mutationsBeforeRelayCall =
        PreparedCodecMutationCount - 6U;
    constexpr auto sitesBeforeRelayCall =
        PreparedCodecMutableSiteCount - 2U;
    CHECK(codecSetFixture.writesAtFlush[sitesBeforeRelayCall]
        == codecSetFixture.writeCalls - 2U);
    CHECK(codecSetFixture.flushFirstRvas[sitesBeforeRelayCall]
        == 0x5353C3);
    CHECK(codecSetFixture.flushSizes[sitesBeforeRelayCall] == 4);
    const auto statusFlush = codecSetFixture.flushCalls - 1U;
    CHECK(codecSetFixture.writesAtFlush[statusFlush]
        == codecSetFixture.writeCalls);
    CHECK(codecSetFixture.flushFirstRvas[statusFlush] == 0x5353D2);
    CHECK(codecSetFixture.flushSizes[statusFlush] == 2);

    auto corruptRelayCall = MakeCodecPatchSetFixture(codecGroups);
    const auto corruptCallSite = FindCodecFixtureSite(
        corruptRelayCall, 0x5353BD);
    CHECK(corruptCallSite != nullptr);
    if (corruptCallSite) corruptCallSite->bytes[6] ^= 0xFF;
    codecSetCallbacks.context = &corruptRelayCall;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::PreflightFailed);
    CHECK(corruptRelayCall.writeCalls == 0);

    auto corruptGuardStatus = MakeCodecPatchSetFixture(codecGroups);
    const auto corruptStatusSite = FindCodecFixtureSite(
        corruptGuardStatus, 0x5353C7);
    CHECK(corruptStatusSite != nullptr);
    if (corruptStatusSite) corruptStatusSite->bytes[11] ^= 0xFF;
    codecSetCallbacks.context = &corruptGuardStatus;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::PreflightFailed);
    CHECK(corruptGuardStatus.writeCalls == 0);

    auto invalidTargetFixture = MakeCodecPatchSetFixture(codecGroups);
    codecSetCallbacks.context = &invalidTargetFixture;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, CodecPatchActivationTargets{}, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::InvalidPlan);
    CHECK(codecSetResult.planError
        == CodecPatchPlanError::InvalidActivationTarget);
    CHECK(invalidTargetFixture.verifyCalls == 0);
    CHECK(invalidTargetFixture.writeCalls == 0);

    constexpr std::array nativeNoOpTargets{
        CodecPatchActivationTargets::ForTesting(
            0x530A00,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            0x533760,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            0xA1E110,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            0xA1B610,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            0x4817F0,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            0x4817F0,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            0x479CD5,
            Packet9DEntryRelayRva),
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            PlayerSaveFinalizeRelayRva,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            0x479EA5),
    };
    for (const auto& nativeNoOpTarget : nativeNoOpTargets) {
        codecSetResult = CommitPreparedCodecPatchSet(
            heldCodecLease, nativeNoOpTarget, codecSetCallbacks);
        CHECK(codecSetResult.planError
            == CodecPatchPlanError::InvalidActivationTarget);
        CHECK(invalidTargetFixture.verifyCalls == 0);
    }

    constexpr auto forwardRel32Overflow =
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            playerSaveCallNextRva
            + static_cast<std::uintptr_t>(
                (std::numeric_limits<std::int32_t>::max)())
            + 1U,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva);
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, forwardRel32Overflow, codecSetCallbacks);
    CHECK(codecSetResult.planError
        == CodecPatchPlanError::InvalidActivationTarget);
    CHECK(invalidTargetFixture.verifyCalls == 0);

    auto noOpRelayByteFixture = MakeCodecPatchSetFixture(codecGroups);
    codecSetCallbacks.context = &noOpRelayByteFixture;
    constexpr auto noOpRelayByteTarget =
        CodecPatchActivationTargets::ForTesting(
            AuxiliaryReaderRelayRva,
            PlayerReaderRelayRva,
            PlayerPreviewRelayRva,
            0x00600000,
            Packet9CQueueRelayRva,
            Packet9DQueueRelayRva,
            Packet9CEntryRelayRva,
            Packet9DEntryRelayRva);
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, noOpRelayByteTarget, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::Active);
    CHECK(codecSetResult.confirmedMutations == PreparedCodecMutationCount);
    CHECK(codecSetResult.confirmedNoOpMutations == 2);
    CHECK(noOpRelayByteFixture.writeCalls
        == PreparedCodecMutationCount - 2U);
    CHECK(noOpRelayByteFixture.flushCalls == PreparedCodecMutableSiteCount);

    auto failedCallWrite = MakeCodecPatchSetFixture(codecGroups);
    const auto writesBeforeRelayCall =
        codecSetFixture.writesAtFlush[sitesBeforeRelayCall - 1U];
    failedCallWrite.failWriteAttempt = writesBeforeRelayCall;
    codecSetCallbacks.context = &failedCallWrite;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::
        PartialCommitColdRestartRequired);
    CHECK(codecSetResult.confirmedMutations == mutationsBeforeRelayCall);
    const auto failedCallStatus = FindCodecFixtureSite(
        failedCallWrite, 0x5353C7);
    CHECK(failedCallStatus != nullptr);
    if (failedCallStatus) {
        CHECK(failedCallStatus->bytes[11] == 0x33);
        CHECK(failedCallStatus->bytes[12] == 0xC0);
    }

    auto failedCallFlush = MakeCodecPatchSetFixture(codecGroups);
    failedCallFlush.failFlushAttempt = sitesBeforeRelayCall;
    codecSetCallbacks.context = &failedCallFlush;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::
        PartialCommitColdRestartRequired);
    CHECK(codecSetResult.confirmedMutations
        == mutationsBeforeRelayCall + 4U);
    CHECK(codecSetResult.confirmedFlushes == sitesBeforeRelayCall);
    const auto failedCallFlushStatus = FindCodecFixtureSite(
        failedCallFlush, 0x5353C7);
    CHECK(failedCallFlushStatus != nullptr);
    if (failedCallFlushStatus) {
        CHECK(failedCallFlushStatus->bytes[11] == 0x33);
        CHECK(failedCallFlushStatus->bytes[12] == 0xC0);
    }

    auto failedFirstStatusByte = MakeCodecPatchSetFixture(codecGroups);
    const auto writesBeforeStatus =
        codecSetFixture.writesAtFlush[sitesBeforeRelayCall];
    failedFirstStatusByte.failWriteAttempt = writesBeforeStatus;
    codecSetCallbacks.context = &failedFirstStatusByte;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::
        PartialCommitColdRestartRequired);
    const auto failedFirstStatus = FindCodecFixtureSite(
        failedFirstStatusByte, 0x5353C7);
    CHECK(failedFirstStatus != nullptr);
    if (failedFirstStatus) {
        CHECK(failedFirstStatus->bytes[11] == 0x33);
        CHECK(failedFirstStatus->bytes[12] == 0xC0);
    }

    auto failedSecondStatusByte = MakeCodecPatchSetFixture(codecGroups);
    failedSecondStatusByte.failWriteAttempt = writesBeforeStatus + 1U;
    codecSetCallbacks.context = &failedSecondStatusByte;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::
        PartialCommitColdRestartRequired);
    const auto failedSecondStatus = FindCodecFixtureSite(
        failedSecondStatusByte, 0x5353C7);
    CHECK(failedSecondStatus != nullptr);
    if (failedSecondStatus) {
        CHECK(failedSecondStatus->bytes[11] == 0x8B);
        CHECK(failedSecondStatus->bytes[12] == 0xC0);
    }

    auto failedStatusFlush = MakeCodecPatchSetFixture(codecGroups);
    failedStatusFlush.failFlushAttempt =
        PreparedCodecMutableSiteCount - 1U;
    codecSetCallbacks.context = &failedStatusFlush;
    codecSetResult = CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, codecSetCallbacks);
    CHECK(codecSetResult.status == CodecPatchCommitStatus::
        PartialCommitColdRestartRequired);
    CHECK(codecSetResult.confirmedMutations == PreparedCodecMutationCount);
    CHECK(codecSetResult.confirmedFlushes
        == PreparedCodecMutableSiteCount - 1U);

    const auto invalidCodecCallbacks = CodecPatchCallbacks{};
    CHECK(CommitPreparedCodecPatchSet(
        heldCodecLease, codecActivationTargets, invalidCodecCallbacks).status
        == CodecPatchCommitStatus::InvalidPlan);

    constexpr std::array<std::uint16_t, 8> BoundaryStatIds{
        0, 510, 511, 512, 1022, 1023, 2047, 4094,
    };
    std::vector<std::uint8_t> encodedBoundaryIds{0xCC};
    CHECK(EncodeTwelveBitFixture(BoundaryStatIds, encodedBoundaryIds));
    CHECK(encodedBoundaryIds.size()
        == ((BoundaryStatIds.size() + 1U) * SerializedBitWidth + 7U) / 8U);
    std::vector<std::uint16_t> decodedBoundaryIds{0xFFFF};
    CHECK(DecodeTwelveBitFixture(encodedBoundaryIds, decodedBoundaryIds));
    CHECK(decodedBoundaryIds == std::vector<std::uint16_t>(
        BoundaryStatIds.begin(), BoundaryStatIds.end()));
    auto missingSentinelBytes = encodedBoundaryIds;
    missingSentinelBytes.resize(missingSentinelBytes.size() - 2U);
    const auto decodedBeforeMissingSentinel = decodedBoundaryIds;
    CHECK(!DecodeTwelveBitFixture(
        missingSentinelBytes, decodedBoundaryIds));
    CHECK(decodedBoundaryIds == decodedBeforeMissingSentinel);
    constexpr std::array<std::uint16_t, 1> InvalidStatIds{
        SerializedSentinel,
    };
    const auto encodedBeforeInvalid = encodedBoundaryIds;
    CHECK(!EncodeTwelveBitFixture(InvalidStatIds, encodedBoundaryIds));
    CHECK(encodedBoundaryIds == encodedBeforeInvalid);

    std::array<ItemStatSemanticRow, 2> preflightSchema{};
    preflightSchema[0].csvBits = 17;
    preflightSchema[0].csvParamBits = 5;
    preflightSchema[1].csvBits = 32;
    preflightSchema[1].csvParamBits = 16;
    constexpr std::array<std::uint16_t, 2> PreflightIds{0, 1};
    auto auxiliaryStream = EncodePlayerStatPreflightFixture(
        PreflightIds, preflightSchema, PlayerStatStreamKind::Auxiliary, true);
    PlayerStatPreflightResult preflightResult{0xFFFF, 0xFFFF};
    CHECK(PreflightPlayerStatStream(
        auxiliaryStream,
        preflightSchema,
        PlayerStatStreamKind::Auxiliary,
        preflightResult) == PlayerStatPreflightError::None);
    CHECK(preflightResult.entryCount == PreflightIds.size());
    CHECK(preflightResult.consumedBits
        == 16U + 12U + 5U + 32U + 12U + 16U + 32U + 12U);

    auto regularStream = EncodePlayerStatPreflightFixture(
        PreflightIds, preflightSchema, PlayerStatStreamKind::Regular, true);
    CHECK(PreflightPlayerStatStream(
        regularStream,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::None);
    CHECK(preflightResult.consumedBits
        == 16U + 12U + 5U + 17U + 12U + 16U + 32U + 12U);

    auto emptyStream = EncodePlayerStatPreflightFixture(
        std::span<const std::uint16_t>{},
        preflightSchema,
        PlayerStatStreamKind::Regular,
        true);
    CHECK(PreflightPlayerStatStream(
        emptyStream,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::None);
    CHECK(preflightResult.entryCount == 0);

    const auto preflightBeforeFailure = preflightResult;
    auto missingPreflightSentinel = EncodePlayerStatPreflightFixture(
        PreflightIds, preflightSchema, PlayerStatStreamKind::Regular, false);
    CHECK(PreflightPlayerStatStream(
        missingPreflightSentinel,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::Truncated);
    CHECK(preflightResult.consumedBits
        == preflightBeforeFailure.consumedBits);
    CHECK(preflightResult.entryCount == preflightBeforeFailure.entryCount);
    constexpr std::array<std::uint8_t, 2> MarkerWithoutSentinel{0x67, 0x66};
    CHECK(PreflightPlayerStatStream(
        MarkerWithoutSentinel,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::MissingSentinel);

    auto truncatedPreflight = regularStream;
    truncatedPreflight.pop_back();
    CHECK(PreflightPlayerStatStream(
        truncatedPreflight,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::Truncated);
    auto invalidMarkerStream = regularStream;
    invalidMarkerStream.front() ^= 0xFF;
    CHECK(PreflightPlayerStatStream(
        invalidMarkerStream,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::InvalidMarker);

    constexpr std::array<std::uint16_t, 1> InvalidPreflightId{2};
    auto invalidIdStream = EncodePlayerStatPreflightFixture(
        InvalidPreflightId,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        true);
    CHECK(PreflightPlayerStatStream(
        invalidIdStream,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::InvalidStatId);

    auto unserializedSchema = preflightSchema;
    unserializedSchema[0].csvBits = 0;
    auto unserializedRegularStream = EncodePlayerStatPreflightFixture(
        std::span<const std::uint16_t>{PreflightIds.data(), 1},
        unserializedSchema,
        PlayerStatStreamKind::Regular,
        true);
    CHECK(PreflightPlayerStatStream(
        unserializedRegularStream,
        unserializedSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::UnserializedStat);
    auto unserializedAuxiliaryStream = EncodePlayerStatPreflightFixture(
        std::span<const std::uint16_t>{PreflightIds.data(), 1},
        unserializedSchema,
        PlayerStatStreamKind::Auxiliary,
        true);
    CHECK(PreflightPlayerStatStream(
        unserializedAuxiliaryStream,
        unserializedSchema,
        PlayerStatStreamKind::Auxiliary,
        preflightResult) == PlayerStatPreflightError::UnserializedStat);

    std::vector<std::uint16_t> tooManyPreflightIds(
        MaximumPlayerStatEntries + 1U, 0);
    auto tooManyStream = EncodePlayerStatPreflightFixture(
        tooManyPreflightIds,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        true);
    CHECK(PreflightPlayerStatStream(
        tooManyStream,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::TooManyEntries);

    auto unsafePreflightSchema = preflightSchema;
    unsafePreflightSchema[0].csvBits = 33;
    CHECK(PreflightPlayerStatStream(
        regularStream,
        unsafePreflightSchema,
        PlayerStatStreamKind::Regular,
        preflightResult) == PlayerStatPreflightError::UnsafeSchema);
    unsafePreflightSchema = preflightSchema;
    unsafePreflightSchema[0].csvParamBits = 17;
    CHECK(PreflightPlayerStatStream(
        regularStream,
        unsafePreflightSchema,
        PlayerStatStreamKind::Auxiliary,
        preflightResult) == PlayerStatPreflightError::UnsafeSchema);

    const auto finalizePreviewD2S = [](std::vector<std::uint8_t>& bytes) {
        WriteU32ForTest(bytes, 0, 0xAA55AA55);
        WriteU32ForTest(bytes, 4, InnerFormatVersion);
        WriteU32ForTest(
            bytes, 8, static_cast<std::uint32_t>(bytes.size()));
        WriteU32ForTest(bytes, 12, 0);
        WriteU32ForTest(bytes, 12, CalculateD2SChecksum(bytes));
    };
    const auto refreshPreviewChecksum = [](
            std::vector<std::uint8_t>& bytes) {
        WriteU32ForTest(bytes, 12, 0);
        WriteU32ForTest(bytes, 12, CalculateD2SChecksum(bytes));
    };
    std::vector<std::uint8_t> previewD2S(
        PlayerPreviewRegularStatOffset + regularStream.size(), 0);
    std::copy(
        regularStream.begin(),
        regularStream.end(),
        previewD2S.begin() + PlayerPreviewRegularStatOffset);
    previewD2S[PlayerPreviewDataContextOffset] = 3;
    finalizePreviewD2S(previewD2S);
    CHECK(ValidateInnerStore(StoreKind::D2S, previewD2S));
    std::uint8_t structuralDataContext{0xFF};
    CHECK(PreflightPlayerPreviewContainer(
        previewD2S, structuralDataContext)
        == PlayerPreviewPreflightError::None);
    CHECK(structuralDataContext == 3);
    auto structurallyRejectedPreview = previewD2S;
    structurallyRejectedPreview[0] ^= 0xFF;
    refreshPreviewChecksum(structurallyRejectedPreview);
    structuralDataContext = 0xEE;
    CHECK(PreflightPlayerPreviewContainer(
        structurallyRejectedPreview, structuralDataContext)
        == PlayerPreviewPreflightError::InvalidContainer);
    CHECK(structuralDataContext == 0xEE);
    PlayerPreviewPreflightResult previewResult{
        .playerStats = {0xAAAA, 0xBBBB},
        .dataContext = 0xCC,
    };
    CHECK(PreflightPlayerPreviewD2S(
        previewD2S,
        preflightSchema,
        previewResult) == PlayerPreviewPreflightError::None);
    CHECK(previewResult.dataContext == 3);
    CHECK(previewResult.playerStats.entryCount == PreflightIds.size());
    CHECK(previewResult.playerStats.consumedBits
        == 16U + 12U + 5U + 17U + 12U + 16U + 32U + 12U);

    std::vector<std::uint8_t> headerOnlyPreview(
        PlayerPreviewHeaderOnlyLength, 0);
    headerOnlyPreview[PlayerPreviewDataContextOffset] = 3;
    finalizePreviewD2S(headerOnlyPreview);
    structuralDataContext = 0xFF;
    CHECK(PreflightPlayerPreviewContainer(
        headerOnlyPreview, structuralDataContext)
        == PlayerPreviewPreflightError::None);
    CHECK(structuralDataContext == 3);
    previewResult = {
        .playerStats = {0xAAAA, 0xBBBB},
        .dataContext = 0xCC,
    };
    CHECK(PreflightPlayerPreviewD2S(
        headerOnlyPreview,
        preflightSchema,
        previewResult) == PlayerPreviewPreflightError::None);
    CHECK(previewResult.dataContext == 3);
    CHECK(previewResult.playerStats.entryCount == 0);
    CHECK(previewResult.playerStats.consumedBits == 0);

    const PlayerPreviewPreflightResult unchangedPreviewResult{
        .playerStats = {0x1234, 0x5678},
        .dataContext = 0xAB,
    };
    const auto expectPreviewFailure = [&preflightSchema,
            &unchangedPreviewResult](
            std::span<const std::uint8_t> bytes,
            PlayerPreviewPreflightError expected) {
        auto observed = unchangedPreviewResult;
        CHECK(PreflightPlayerPreviewD2S(
            bytes, preflightSchema, observed) == expected);
        CHECK(observed.playerStats.consumedBits
            == unchangedPreviewResult.playerStats.consumedBits);
        CHECK(observed.playerStats.entryCount
            == unchangedPreviewResult.playerStats.entryCount);
        CHECK(observed.dataContext == unchangedPreviewResult.dataContext);
    };

    std::vector<std::uint8_t> underflowPreview(342, 0);
    finalizePreviewD2S(underflowPreview);
    expectPreviewFailure(
        underflowPreview, PlayerPreviewPreflightError::InvalidArgument);
    std::vector<std::uint8_t> oversizedPreview(
        PlayerPreviewBufferCapacity + 1U, 0);
    expectPreviewFailure(
        oversizedPreview, PlayerPreviewPreflightError::InvalidArgument);

    auto rejectedPreview = previewD2S;
    rejectedPreview[0] ^= 0xFF;
    refreshPreviewChecksum(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview, PlayerPreviewPreflightError::InvalidContainer);
    rejectedPreview = previewD2S;
    WriteU32ForTest(rejectedPreview, 4, InnerFormatVersion - 1U);
    refreshPreviewChecksum(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview, PlayerPreviewPreflightError::InvalidContainer);
    rejectedPreview = previewD2S;
    WriteU32ForTest(
        rejectedPreview,
        8,
        static_cast<std::uint32_t>(rejectedPreview.size() - 1U));
    refreshPreviewChecksum(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview, PlayerPreviewPreflightError::InvalidContainer);
    rejectedPreview = previewD2S;
    rejectedPreview.back() ^= 0x80;
    expectPreviewFailure(
        rejectedPreview, PlayerPreviewPreflightError::InvalidContainer);
    rejectedPreview = previewD2S;
    rejectedPreview[PlayerPreviewDataContextOffset] =
        PlayerPreviewDataContextCount;
    refreshPreviewChecksum(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview, PlayerPreviewPreflightError::InvalidDataContext);
    rejectedPreview = previewD2S;
    rejectedPreview[PlayerPreviewRegularStatOffset] ^= 0xFF;
    refreshPreviewChecksum(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview,
        PlayerPreviewPreflightError::InvalidPlayerStatStream);

    const auto missingPreviewSentinel = EncodePlayerStatPreflightFixture(
        PreflightIds,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        false);
    rejectedPreview.assign(
        PlayerPreviewRegularStatOffset + missingPreviewSentinel.size(), 0);
    std::copy(
        missingPreviewSentinel.begin(),
        missingPreviewSentinel.end(),
        rejectedPreview.begin() + PlayerPreviewRegularStatOffset);
    finalizePreviewD2S(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview,
        PlayerPreviewPreflightError::InvalidPlayerStatStream);

    constexpr std::array<std::uint16_t, 1> PreviewInvalidId{2};
    const auto invalidPreviewStream = EncodePlayerStatPreflightFixture(
        PreviewInvalidId,
        preflightSchema,
        PlayerStatStreamKind::Regular,
        true);
    rejectedPreview.assign(
        PlayerPreviewRegularStatOffset + invalidPreviewStream.size(), 0);
    std::copy(
        invalidPreviewStream.begin(),
        invalidPreviewStream.end(),
        rejectedPreview.begin() + PlayerPreviewRegularStatOffset);
    finalizePreviewD2S(rejectedPreview);
    expectPreviewFailure(
        rejectedPreview,
        PlayerPreviewPreflightError::InvalidPlayerStatStream);

    CHECK(ClassifyStoreName("Hero.d2s") == StoreKind::D2S);
    CHECK(ClassifyStoreName("Hero.With.Dots.d2s") == StoreKind::D2S);
    CHECK(ClassifyStoreName(".d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("Hero.D2S") == StoreKind::Other);
    CHECK(ClassifyStoreName("folder/Hero.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("folder\\Hero.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("C:Hero.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("Hero.d2s:stream.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("Hero?.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("CON.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("aux.any.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("LPT1.d2s") == StoreKind::Other);
    CHECK(ClassifyStoreName("Hero.d2s ") == StoreKind::Other);
    CHECK(ClassifyStoreName("arbitrary.d2i") == StoreKind::Other);
    CHECK(ClassifyStoreName("SharedStashSoftCoreV2.d2i") == StoreKind::D2I);
    CHECK(ClassifyStoreName("SharedStashHardCoreV2.d2i") == StoreKind::D2I);
    CHECK(ClassifyStoreName("ModernSharedStashSoftCoreV2.d2i")
        == StoreKind::D2I);
    CHECK(ClassifyStoreName("ModernSharedStashHardCoreV2.d2i")
        == StoreKind::D2I);
    CHECK(ClassifyStoreName("modernSharedStashSoftCoreV2.d2i")
        == StoreKind::Other);
    constexpr char EmbeddedNullName[]{'H','e','r','o','\0','.','d','2','s'};
    CHECK(ClassifyStoreName(std::string_view{
        EmbeddedNullName, sizeof(EmbeddedNullName)}) == StoreKind::Other);

    std::array<ItemStatSemanticRow, 2> schemaRows{};
    schemaRows[0] = {
        .statName = "strength",
        .sendOther = true,
        .isSigned = true,
        .sendBits = 11,
        .sendParamBits = 2,
        .updateAnimRate = true,
        .saved = true,
        .csvSigned = true,
        .csvBits = 20,
        .csvParamBits = 3,
        .callback = true,
        .hasMinimum = true,
        .minimumAccumulator = -7,
        .encode = 3,
        .add = -32,
        .multiply = 55,
        .valueShift = 8,
        .saveBits = 10,
        .saveAdd = 1024,
        .saveParamBits = 5,
        .keepZero = true,
        .operation = 13,
        .operationParam = 2,
        .operationBase = 1,
        .operationStat1 = 0,
        .operationStat2 = InvalidStatReference,
        .operationStat3 = 999,
        .direct = true,
        .maximumStat = 1,
        .damageRelated = true,
        .itemEvent1 = 7,
        .itemEventFunction1 = 21,
        .itemEvent2 = InvalidStatReference,
        .itemEventFunction2 = 0,
    };
    schemaRows[1].statName = "energy";
    std::vector<std::uint8_t> schemaDescriptor{0xCC};
    CHECK(BuildSchemaDescriptor(schemaRows, 0, schemaDescriptor)
        == SchemaError::None);
    constexpr std::size_t SchemaDomainSize = sizeof(SchemaDomainTag);
    CHECK(schemaDescriptor.size()
        == SchemaDomainSize + 10 + 2 * 54
            + schemaRows[0].statName.size() + schemaRows[1].statName.size());
    CHECK(std::equal(
        SchemaDomainTag, SchemaDomainTag + SchemaDomainSize,
        schemaDescriptor.begin()));
    CHECK(schemaDescriptor[SchemaDomainSize] == 1);
    CHECK(schemaDescriptor[SchemaDomainSize + 1] == 0);
    CHECK(schemaDescriptor[SchemaDomainSize + 2] == SerializedBitWidth);
    CHECK(schemaDescriptor[SchemaDomainSize + 3] == 0xFF);
    CHECK(schemaDescriptor[SchemaDomainSize + 4] == 0x0F);
    CHECK(schemaDescriptor[SchemaDomainSize + 5] == schemaRows.size());
    CHECK(schemaDescriptor[SchemaDomainSize + 9] == DefaultEffectiveStuff);
    auto normalizedSchemaDescriptor = schemaDescriptor;
    CHECK(BuildSchemaDescriptor(schemaRows, 6, normalizedSchemaDescriptor)
        == SchemaError::None);
    CHECK(normalizedSchemaDescriptor == schemaDescriptor);
    auto invalidReferenceRows = schemaRows;
    invalidReferenceRows[0].operationStat3 = InvalidStatReference;
    CHECK(BuildSchemaDescriptor(
        invalidReferenceRows, 6, normalizedSchemaDescriptor)
        == SchemaError::None);
    CHECK(normalizedSchemaDescriptor == schemaDescriptor);

    Sha256Digest schemaGoldenHash{};
    CHECK(CalculateSchemaHash(schemaRows, 6, schemaGoldenHash)
        == SchemaError::None);
    constexpr Sha256Digest ExpectedSchemaGoldenHash{
        0x37,0xEB,0x7F,0x26,0x18,0x66,0x7E,0x3D,
        0xD3,0x40,0x67,0x4E,0xDF,0x15,0xF3,0x25,
        0xB1,0x8B,0xD2,0x50,0x23,0x6E,0x50,0x22,
        0x3A,0xF3,0x91,0x5F,0x27,0x71,0x1D,0xE7,
    };
    CHECK(schemaGoldenHash == ExpectedSchemaGoldenHash);
    auto reorderedRows = schemaRows;
    std::swap(reorderedRows[0], reorderedRows[1]);
    Sha256Digest reorderedHash{};
    CHECK(CalculateSchemaHash(reorderedRows, 6, reorderedHash)
        == SchemaError::None);
    CHECK(reorderedHash != schemaGoldenHash);
    auto renamedRows = schemaRows;
    renamedRows[0].statName = "strength_renamed";
    CHECK(CalculateSchemaHash(renamedRows, 6, reorderedHash)
        == SchemaError::None);
    CHECK(reorderedHash != schemaGoldenHash);
    auto invalidUtf8Rows = schemaRows;
    invalidUtf8Rows[0].statName.assign("\xC0\xAF", 2);
    normalizedSchemaDescriptor = {0x44, 0x55};
    CHECK(BuildSchemaDescriptor(
        invalidUtf8Rows, 6, normalizedSchemaDescriptor)
        == SchemaError::InvalidUtf8);
    CHECK((normalizedSchemaDescriptor
        == std::vector<std::uint8_t>{0x44, 0x55}));
    auto unsafeCodecRows = schemaRows;
    unsafeCodecRows[0].csvParamBits =
        static_cast<std::uint8_t>(MaximumSerializedCsvParamBits + 1U);
    normalizedSchemaDescriptor = {0x66, 0x77};
    CHECK(BuildSchemaDescriptor(
        unsafeCodecRows, 6, normalizedSchemaDescriptor)
        == SchemaError::UnsafeCodecWidth);
    CHECK((normalizedSchemaDescriptor
        == std::vector<std::uint8_t>{0x66, 0x77}));
    unsafeCodecRows = schemaRows;
    unsafeCodecRows[0].csvBits =
        static_cast<std::uint8_t>(MaximumSerializedCsvBits + 1U);
    CHECK(BuildSchemaDescriptor(
        unsafeCodecRows, 6, normalizedSchemaDescriptor)
        == SchemaError::UnsafeCodecWidth);
    CHECK((normalizedSchemaDescriptor
        == std::vector<std::uint8_t>{0x66, 0x77}));

    std::vector<std::uint8_t> compiledRows(
        2 * CompiledItemStatRecordStride);
    WriteU32ForTest(compiledRows, 0x04,
        (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U)
        | (1U << 8U) | (1U << 9U) | (1U << 10U)
        | (1U << 11U) | (1U << 12U));
    compiledRows[0x08] = 11;
    compiledRows[0x09] = 2;
    compiledRows[0x0A] = 20;
    compiledRows[0x0B] = 3;
    WriteU32ForTest(compiledRows, 0x0C, 55);
    WriteU32ForTest(compiledRows, 0x10, static_cast<std::uint32_t>(-32));
    compiledRows[0x14] = 8;
    compiledRows[0x15] = 10;
    compiledRows[0x16] = 99;
    WriteU32ForTest(compiledRows, 0x18, 1024);
    WriteU32ForTest(compiledRows, 0x1C, 0xDEADBEEF);
    WriteU32ForTest(compiledRows, 0x20, 5);
    WriteU32ForTest(compiledRows, 0x24, 0x123);
    WriteU32ForTest(compiledRows, 0x28, static_cast<std::uint32_t>(-7));
    compiledRows[0x2C] = 3;
    WriteU16ForTest(compiledRows, 0x2E, 1);
    compiledRows[0x30] = 0xA5;
    WriteU16ForTest(compiledRows, 0x44, 7);
    WriteU16ForTest(compiledRows, 0x46, InvalidStatReference);
    WriteU16ForTest(compiledRows, 0x48, 21);
    WriteU16ForTest(compiledRows, 0x4A, 0);
    compiledRows[0x4C] = 1;
    compiledRows[0x50] = 13;
    compiledRows[0x51] = 2;
    WriteU16ForTest(compiledRows, 0x52, 1);
    WriteU16ForTest(compiledRows, 0x54, 0);
    WriteU16ForTest(compiledRows, 0x56, InvalidStatReference);
    WriteU16ForTest(compiledRows, 0x58, InvalidStatReference);
    WriteU32ForTest(compiledRows, 0x13C, 0);
    compiledRows[0x140] = 0x5A;
    WriteU16ForTest(compiledRows, CompiledItemStatRecordStride, 1);
    for (const auto rowOffset : {
            CompiledItemStatRecordStride + 0x2E,
            CompiledItemStatRecordStride + 0x44,
            CompiledItemStatRecordStride + 0x46,
            CompiledItemStatRecordStride + 0x52,
            CompiledItemStatRecordStride + 0x54,
            CompiledItemStatRecordStride + 0x56,
            CompiledItemStatRecordStride + 0x58}) {
        WriteU16ForTest(compiledRows, rowOffset, InvalidStatReference);
    }
    std::vector<ItemStatSemanticRow> decodedRows;
    std::uint8_t decodedStuff{};
    constexpr std::array<std::string_view, 2> CompiledStatNames{
        "strength", "energy",
    };
    CHECK(DecodeCompiledItemStatRecords(
        compiledRows, 2, CompiledStatNames, decodedRows, decodedStuff)
        == SchemaError::None);
    CHECK(decodedRows.size() == schemaRows.size());
    CHECK(decodedStuff == DefaultEffectiveStuff);
    Sha256Digest decodedHash{};
    CHECK(CalculateSchemaHash(decodedRows, decodedStuff, decodedHash)
        == SchemaError::None);
    CHECK(decodedHash == schemaGoldenHash);

    auto displayOnlyCompiledRows = compiledRows;
    std::fill(
        displayOnlyCompiledRows.begin() + 0x30,
        displayOnlyCompiledRows.begin() + 0x44,
        std::uint8_t{0x7C});
    displayOnlyCompiledRows[0x140] ^= 0xFF;
    CHECK(DecodeCompiledItemStatRecords(
        displayOnlyCompiledRows, 2, CompiledStatNames,
        decodedRows, decodedStuff)
        == SchemaError::None);
    CHECK(CalculateSchemaHash(decodedRows, decodedStuff, decodedHash)
        == SchemaError::None);
    CHECK(decodedHash == schemaGoldenHash);
    auto semanticCompiledRows = compiledRows;
    semanticCompiledRows[0x15] ^= 1;
    CHECK(DecodeCompiledItemStatRecords(
        semanticCompiledRows, 2, CompiledStatNames,
        decodedRows, decodedStuff)
        == SchemaError::None);
    CHECK(CalculateSchemaHash(decodedRows, decodedStuff, decodedHash)
        == SchemaError::None);
    CHECK(decodedHash != schemaGoldenHash);
    auto unsafeCodecCompiledRows = compiledRows;
    unsafeCodecCompiledRows[0x0B] =
        static_cast<std::uint8_t>(
            MaximumSerializedCsvParamBits + 1U);
    const auto rowsBeforeUnsafeCodec = decodedRows;
    const auto stuffBeforeUnsafeCodec = decodedStuff;
    CHECK(DecodeCompiledItemStatRecords(
        unsafeCodecCompiledRows, 2, CompiledStatNames,
        decodedRows, decodedStuff) == SchemaError::UnsafeCodecWidth);
    CHECK(decodedRows.size() == rowsBeforeUnsafeCodec.size());
    CHECK(decodedRows.front().statName
        == rowsBeforeUnsafeCodec.front().statName);
    CHECK(decodedStuff == stuffBeforeUnsafeCodec);
    unsafeCodecCompiledRows = compiledRows;
    unsafeCodecCompiledRows[0x0A] = static_cast<std::uint8_t>(
        MaximumSerializedCsvBits + 1U);
    CHECK(DecodeCompiledItemStatRecords(
        unsafeCodecCompiledRows, 2, CompiledStatNames,
        decodedRows, decodedStuff) == SchemaError::UnsafeCodecWidth);
    CHECK(decodedRows.size() == rowsBeforeUnsafeCodec.size());
    CHECK(decodedRows.front().statName
        == rowsBeforeUnsafeCodec.front().statName);
    CHECK(decodedStuff == stuffBeforeUnsafeCodec);
    for (const auto legacyOffset : {0x16U, 0x1CU, 0x24U}) {
        semanticCompiledRows = compiledRows;
        semanticCompiledRows[legacyOffset] ^= 1;
        CHECK(DecodeCompiledItemStatRecords(
            semanticCompiledRows, 2, CompiledStatNames,
            decodedRows, decodedStuff) == SchemaError::None);
        CHECK(CalculateSchemaHash(decodedRows, decodedStuff, decodedHash)
            == SchemaError::None);
        CHECK(decodedHash == schemaGoldenHash);
    }

    auto badRecordIds = compiledRows;
    WriteU16ForTest(badRecordIds, CompiledItemStatRecordStride, 0);
    const auto rowsBeforeBadId = decodedRows.size();
    const auto stuffBeforeBadId = decodedStuff;
    CHECK(DecodeCompiledItemStatRecords(
        badRecordIds, 2, CompiledStatNames,
        decodedRows, decodedStuff) == SchemaError::RecordIdMismatch);
    CHECK(decodedRows.size() == rowsBeforeBadId);
    CHECK(decodedStuff == stuffBeforeBadId);

    std::vector<ItemStatSemanticRow> unchangedRows(1);
    decodedStuff = 8;
    CHECK(DecodeCompiledItemStatRecords(
        compiledRows, 1, CompiledStatNames, unchangedRows, decodedStuff)
        == SchemaError::SizeMismatch);
    CHECK(unchangedRows.size() == 1);
    CHECK(decodedStuff == 8);

    std::array<std::uint8_t, 16> innerD2S{};
    WriteU32ForTest(innerD2S, 0, 0xAA55AA55);
    WriteU32ForTest(innerD2S, 4, InnerFormatVersion);
    WriteU32ForTest(
        innerD2S, 8, static_cast<std::uint32_t>(innerD2S.size()));
    WriteU32ForTest(innerD2S, 12, CalculateD2SChecksum(innerD2S));
    constexpr std::array<std::uint8_t, 16> GoldenInnerD2S{
        0x55,0xAA,0x55,0xAA,0x69,0x00,0x00,0x00,
        0x10,0x00,0x00,0x00,0x00,0x90,0x6D,0x00,
    };
    CHECK(innerD2S == GoldenInnerD2S);
    CHECK(ValidateInnerStore(StoreKind::D2S, innerD2S));

    const auto schemaHash = schemaGoldenHash;
    constexpr Sha256Digest GoldenD2SHash{
        0x61,0x44,0x61,0xA2,0x67,0xBA,0x54,0xE8,
        0x72,0xC5,0x74,0xA6,0x8B,0x54,0x46,0xBE,
        0x52,0xED,0xD2,0x3C,0x2B,0x82,0x58,0x5C,
        0x27,0xFA,0xB6,0x92,0x28,0x8E,0x4B,0x46,
    };
    Sha256Digest calculatedHash{};
    CHECK(CalculateSha256(innerD2S, calculatedHash));
    CHECK(calculatedHash == GoldenD2SHash);

    std::vector<std::uint8_t> envelope{0xCC, 0xDD};
    CHECK(BuildEnvelope(StoreKind::D2S, innerD2S, schemaHash, envelope)
        == EnvelopeError::None);
    CHECK(envelope.size() == EnvelopeHeaderSize + innerD2S.size());
    CHECK(std::equal(
        EnvelopeMagic.begin(), EnvelopeMagic.end(), envelope.begin()));
    constexpr std::array<std::uint8_t, 24> GoldenEnvelopePrefix{
        0x49,0x53,0x43,0x31,0x32,0x0D,0x0A,0x1A,
        0x01,0x00,0x60,0x00,0x01,0x0C,0xFF,0x0F,
        0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,
    };
    CHECK(std::equal(
        GoldenEnvelopePrefix.begin(), GoldenEnvelopePrefix.end(),
        envelope.begin()));
    CHECK(std::equal(
        schemaHash.begin(), schemaHash.end(),
        envelope.begin() + EnvelopeSchemaHashOffset));
    CHECK(std::equal(
        GoldenD2SHash.begin(), GoldenD2SHash.end(),
        envelope.begin() + EnvelopePayloadHashOffset));
    auto validation = ValidateEnvelope(StoreKind::D2S, envelope, schemaHash);
    CHECK(validation);
    CHECK(std::equal(
        validation.payload.begin(), validation.payload.end(),
        innerD2S.begin()));
    std::vector<std::uint8_t> secondEnvelope;
    CHECK(BuildEnvelope(
        StoreKind::D2S, validation.payload, schemaHash, secondEnvelope)
        == EnvelopeError::None);
    CHECK(secondEnvelope == envelope);
    CHECK(ValidateEnvelope(
        StoreKind::D2S, secondEnvelope, schemaHash));

    auto preparation = PrepareStoreRead(
        "opaque.manager.object", 99, envelope.size(), envelope.size(),
        envelope);
    CHECK(preparation.disposition == StorePreparation::PassThrough);
    preparation = PrepareStoreRead(
        "Hero.d2s", 1, envelope.size(), envelope.size(),
        envelope);
    CHECK(preparation.disposition == StorePreparation::Rejected);
    CHECK(preparation.error == PersistenceError::ReadFailure);
    preparation = PrepareStoreRead(
        "Hero.d2s", 0, envelope.size(), envelope.size() - 1,
        envelope);
    CHECK(preparation.disposition == StorePreparation::Rejected);
    CHECK(preparation.error == PersistenceError::ReadLength);
    preparation = PrepareStoreRead(
        "Hero.d2s", 0, innerD2S.size(), innerD2S.size(), innerD2S);
    CHECK(preparation.disposition == StorePreparation::Prepared);
    CHECK(preparation.error == PersistenceError::None);
    preparation = PrepareStoreRead(
        "Hero.d2s", 0, envelope.size(), envelope.size(), envelope);
    CHECK(preparation.disposition == StorePreparation::Rejected);
    CHECK(preparation.error == PersistenceError::InnerStore);

    preparation = PrepareStoreWrite(
        "opaque.manager.object", innerD2S);
    CHECK(preparation.disposition == StorePreparation::PassThrough);
    preparation = PrepareStoreWrite(
        "Hero.d2s", innerD2S);
    CHECK(preparation.disposition == StorePreparation::Prepared);
    CHECK(preparation.error == PersistenceError::None);
    preparation = PrepareStoreWrite("Hero.d2s", envelope);
    CHECK(preparation.disposition == StorePreparation::Rejected);
    CHECK(preparation.error == PersistenceError::InnerStore);

    auto legacyInnerD2S = innerD2S;
    WriteU32ForTest(legacyInnerD2S, 4, 91);
    WriteU32ForTest(legacyInnerD2S, 12, 0);
    WriteU32ForTest(
        legacyInnerD2S, 12, CalculateD2SChecksum(legacyInnerD2S));
    CHECK(!ValidateInnerStore(StoreKind::D2S, legacyInnerD2S));

    auto rejectedEnvelope = envelope;
    rejectedEnvelope[0] ^= 0xFF;
    CHECK(ValidateEnvelope(StoreKind::D2S, rejectedEnvelope, schemaHash).error
        == EnvelopeError::Magic);
    rejectedEnvelope = envelope;
    rejectedEnvelope[8] = 2;
    CHECK(ValidateEnvelope(StoreKind::D2S, rejectedEnvelope, schemaHash).error
        == EnvelopeError::Version);
    rejectedEnvelope = envelope;
    rejectedEnvelope[12] = static_cast<std::uint8_t>(StoreKind::D2I);
    CHECK(ValidateEnvelope(StoreKind::D2S, rejectedEnvelope, schemaHash).error
        == EnvelopeError::StoreKind);
    auto wrongSchema = schemaHash;
    wrongSchema[0] ^= 0xFF;
    CHECK(ValidateEnvelope(StoreKind::D2S, envelope, wrongSchema).error
        == EnvelopeError::SchemaHash);
    rejectedEnvelope = envelope;
    rejectedEnvelope[EnvelopePayloadHashOffset] ^= 0xFF;
    CHECK(ValidateEnvelope(StoreKind::D2S, rejectedEnvelope, schemaHash).error
        == EnvelopeError::PayloadHash);
    rejectedEnvelope = envelope;
    rejectedEnvelope.pop_back();
    CHECK(ValidateEnvelope(StoreKind::D2S, rejectedEnvelope, schemaHash).error
        == EnvelopeError::PayloadLength);
    rejectedEnvelope = envelope;
    rejectedEnvelope.push_back(0);
    CHECK(ValidateEnvelope(StoreKind::D2S, rejectedEnvelope, schemaHash).error
        == EnvelopeError::PayloadLength);
    CHECK(ValidateEnvelope(StoreKind::D2S, innerD2S, schemaHash).error
        == EnvelopeError::TooShort);

    std::array<std::uint8_t, 64> innerD2I{};
    WriteU32ForTest(innerD2I, 0, 0xAA55AA55);
    WriteU32ForTest(innerD2I, 4, 2);
    WriteU32ForTest(innerD2I, 8, InnerFormatVersion);
    WriteU16ForTest(
        innerD2I, 0x10, static_cast<std::uint16_t>(innerD2I.size()));
    WriteU16ForTest(innerD2I, 0x12, 0xB8);
    innerD2I[0x14] = 0;
    constexpr Sha256Digest GoldenD2IHash{
        0x56,0x80,0x18,0xBA,0x1B,0xC8,0x25,0x10,
        0xBC,0x7A,0x81,0x00,0x1B,0x0D,0x41,0xFA,
        0xEC,0x24,0x44,0x7A,0x06,0x9B,0xFE,0xCA,
        0x59,0x93,0x67,0xAD,0x9C,0x34,0xC4,0x39,
    };
    CHECK(CalculateSha256(innerD2I, calculatedHash));
    CHECK(calculatedHash == GoldenD2IHash);
    CHECK(ValidateInnerStore(StoreKind::D2I, innerD2I));
    CHECK(BuildEnvelope(StoreKind::D2I, innerD2I, schemaHash, envelope)
        == EnvelopeError::None);
    validation = ValidateEnvelope(StoreKind::D2I, envelope, schemaHash);
    CHECK(validation);
    CHECK(validation.payload.size() == innerD2I.size());
    CHECK(BuildEnvelope(
        StoreKind::D2I, validation.payload, schemaHash, secondEnvelope)
        == EnvelopeError::None);
    CHECK(secondEnvelope == envelope);
    CHECK(ValidateEnvelope(
        StoreKind::D2I, secondEnvelope, schemaHash));

    std::vector<std::uint8_t> mixedD2I;
    mixedD2I.insert(mixedD2I.end(), innerD2I.begin(), innerD2I.end());
    mixedD2I.insert(mixedD2I.end(), innerD2I.begin(), innerD2I.end());
    mixedD2I[64] ^= 0xFF;
    const auto unchangedEnvelope = envelope;
    CHECK(BuildEnvelope(StoreKind::D2I, mixedD2I, schemaHash, envelope)
        == EnvelopeError::InnerPayload);
    CHECK(envelope == unchangedEnvelope);

    const auto atomicDirectory = std::filesystem::temp_directory_path()
        / ("ruffneckk-isc12-atomic-tests-"
            + std::to_string(::GetCurrentProcessId()));
    std::error_code cleanupError;
    std::filesystem::remove_all(atomicDirectory, cleanupError);
    CHECK(std::filesystem::create_directories(atomicDirectory));
    const auto atomicPath = atomicDirectory / "fixture.d2s";
    const std::array<std::uint8_t, 4> originalBytes{1, 2, 3, 4};
    const std::array<std::uint8_t, 7> replacementBytes{9, 8, 7, 6, 5, 4, 3};
    WriteBytes(atomicPath, originalBytes);
    auto atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes);
    CHECK(atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::Committed);
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        replacementBytes.begin(), replacementBytes.end()));
    CHECK(!HasAtomicSibling(atomicDirectory));

    std::filesystem::remove(atomicPath, cleanupError);
    atomicResult = WriteFileAtomically(atomicPath.wstring(), originalBytes);
    CHECK(atomicResult.committed);
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        originalBytes.begin(), originalBytes.end()));

    const auto nulPath = atomicDirectory / L"nul-target.d2s";
    std::filesystem::remove(nulPath, cleanupError);
    auto embeddedNulPath = nulPath.wstring();
    embeddedNulPath.push_back(L'\0');
    embeddedNulPath.append(L".ignored");
    atomicResult = WriteFileAtomically(embeddedNulPath, replacementBytes);
    CHECK(!atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::InvalidArgument);
    CHECK(!std::filesystem::exists(nulPath));
    CHECK(!HasAtomicSibling(atomicDirectory));

    auto api = DefaultAtomicFileApi();
    api.writeFile = &PartialWriteFile;
    atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes, api);
    CHECK(atomicResult.committed);
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        replacementBytes.begin(), replacementBytes.end()));

    WriteBytes(atomicPath, originalBytes);
    api = DefaultAtomicFileApi();
    api.writeFile = &FailedWriteFile;
    atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes, api);
    CHECK(!atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::Write);
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        originalBytes.begin(), originalBytes.end()));
    CHECK(!HasAtomicSibling(atomicDirectory));

    api = DefaultAtomicFileApi();
    api.flushFileBuffers = &FailedFlushFileBuffers;
    atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes, api);
    CHECK(!atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::Flush);
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        originalBytes.begin(), originalBytes.end()));
    CHECK(!HasAtomicSibling(atomicDirectory));

    api = DefaultAtomicFileApi();
    api.replaceFileW = &FailedReplaceFileW;
    atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes, api);
    CHECK(!atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::Replace);
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        originalBytes.begin(), originalBytes.end()));
    CHECK(!HasAtomicSibling(atomicDirectory));

    WriteBytes(atomicPath, originalBytes);
    api = DefaultAtomicFileApi();
    api.replaceFileW = &FailedReplaceAfterBackupFileW;
    atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes, api);
    CHECK(!atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::Rollback);
    CHECK(atomicResult.rollbackAttempted);
    CHECK(atomicResult.rollbackSucceeded);
    CHECK(AtomicFailurePreservedDestination(atomicResult));
    CHECK(ReadBytes(atomicPath) == std::vector<std::uint8_t>(
        originalBytes.begin(), originalBytes.end()));
    CHECK(!HasAtomicSibling(atomicDirectory));

    WriteBytes(atomicPath, originalBytes);
    api = DefaultAtomicFileApi();
    api.replaceFileW = &FailedReplaceAfterBackupFileW;
    api.moveFileExW = &FailedMoveFileExW;
    atomicResult = WriteFileAtomically(
        atomicPath.wstring(), replacementBytes, api);
    CHECK(!atomicResult.committed);
    CHECK(atomicResult.stage == AtomicWriteStage::Rollback);
    CHECK(atomicResult.rollbackAttempted);
    CHECK(!atomicResult.rollbackSucceeded);
    CHECK(!AtomicFailurePreservedDestination(atomicResult));
    CHECK(!std::filesystem::exists(atomicPath));
    CHECK(HasAtomicSibling(atomicDirectory));

    std::filesystem::remove_all(atomicDirectory, cleanupError);

    CHECK(CanEncodeRel32(0x1000, 0x1005));
    CHECK(CanEncodeRel32(0x80001000, 0x1005));
    CHECK(!CanEncodeRel32(0x1000, UINT64_C(0x80001005)));
    static_assert(LoaderCompileCallRva == 0x31EC7B);
    static_assert(LoaderCompileCallInstructionOffset == 14U);
    static_assert(NativeGenericCompileRva == 0x2FF970);
    static_assert(PlayerSaveStatWriterCallRva == 0x5352F6);
    static_assert(PlayerSaveStatWriterCallInstructionOffset == 13U);
    static_assert(ItemSaveStatWriterCallRva == 0x37F174);
    static_assert(ItemSaveStatWriterCallInstructionOffset == 45U);
    static_assert(NativeBitWriterRva == 0xA1B710);
    static_assert(PlayerSaveDynamicCapacityRva == 0x41E138);
    static_assert(PlayerSaveDynamicCallRva == 0x41E1E9);
    static_assert(PlayerSaveDynamicCallInstructionOffset == 30U);
    static_assert(NativePlayerSaveRva == 0x52F090);
    static_assert(D2SContainerVersionForwardRva == 0x52EDFA);
    static_assert(D2SContainerVersionForwardCallOffset == 29U);
    static_assert(NativeReadItemsByVersionRva == 0x41F0B0);
    static_assert(D2SSaveWriterProviderCallRva == 0x9F95C6);
    static_assert(D2SSaveWriterProviderCallOffset == 30U);
    static_assert(NativeD2SSaveWriterRva == 0x122BFF0);
    static_assert(D2SSaveCloseProviderCallRva == 0x9F95E9);
    static_assert(D2SSaveCloseProviderCallOffset == 21U);
    static_assert(NativeD2SSaveCloseRva == 0x11C7E30);

    std::vector<std::string> commitEvents;
    auto commitResult = CommitLoaderMutation(
        noCodecLease,
        [&]() noexcept { commitEvents.emplace_back("reserve"); },
        [&]() noexcept {
            commitEvents.emplace_back("tail");
            return true;
        },
        [&]() noexcept { commitEvents.emplace_back("activate"); },
        [&]() noexcept {
            commitEvents.emplace_back("cap");
            return true;
        },
        [&]() noexcept { commitEvents.emplace_back("publish"); });
    CHECK(commitResult == LoaderInstallResult::QuiescenceRequired);
    CHECK(commitEvents.empty());

    CodecQuiescenceFixture loaderRevokedBeforeWrite{
        .held = true,
        .revokeOnValidation = 1,
    };
    {
        auto loaderLease = NativePublicationLeaseView::ForTesting(
            &loaderRevokedBeforeWrite,
            &ValidateCodecQuiescence);
        commitResult = CommitLoaderMutation(
            loaderLease,
            [&]() noexcept { commitEvents.emplace_back("reserve"); },
            [&]() noexcept {
                commitEvents.emplace_back("tail");
                return true;
            },
            [&]() noexcept { commitEvents.emplace_back("activate"); },
            [&]() noexcept {
                commitEvents.emplace_back("cap");
                return true;
            },
            [&]() noexcept { commitEvents.emplace_back("publish"); });
    }
    CHECK(commitResult == LoaderInstallResult::QuiescenceRequired);
    CHECK((commitEvents == std::vector<std::string>{"reserve"}));

    commitEvents.clear();
    CodecQuiescenceFixture loaderRevokedAfterTail{
        .held = true,
        .revokeOnValidation = 2,
    };
    {
        auto loaderLease = NativePublicationLeaseView::ForTesting(
            &loaderRevokedAfterTail,
            &ValidateCodecQuiescence);
        commitResult = CommitLoaderMutation(
            loaderLease,
            [&]() noexcept { commitEvents.emplace_back("reserve"); },
            [&]() noexcept {
                commitEvents.emplace_back("tail");
                return true;
            },
            [&]() noexcept { commitEvents.emplace_back("activate"); },
            [&]() noexcept {
                commitEvents.emplace_back("cap");
                return true;
            },
            [&]() noexcept { commitEvents.emplace_back("publish"); });
    }
    CHECK(commitResult
        == LoaderInstallResult::PartialCommitColdRestartRequired);
    CHECK((commitEvents == std::vector<std::string>{"reserve", "tail"}));

    commitEvents.clear();
    commitResult = CommitLoaderMutation(
        heldCodecLease,
        [&]() noexcept { commitEvents.emplace_back("reserve"); },
        [&]() noexcept {
            commitEvents.emplace_back("tail");
            return true;
        },
        [&]() noexcept { commitEvents.emplace_back("activate"); },
        [&]() noexcept {
            commitEvents.emplace_back("cap");
            return true;
        },
        [&]() noexcept { commitEvents.emplace_back("publish"); });
    CHECK(commitResult == LoaderInstallResult::Active);
    CHECK((commitEvents == std::vector<std::string>{
        "reserve", "tail", "activate", "cap", "publish",
    }));

    commitEvents.clear();
    commitResult = CommitLoaderMutation(
        heldCodecLease,
        [&]() noexcept { commitEvents.emplace_back("reserve"); },
        [&]() noexcept {
            commitEvents.emplace_back("tail");
            return false;
        },
        [&]() noexcept { commitEvents.emplace_back("activate"); },
        [&]() noexcept {
            commitEvents.emplace_back("cap");
            return true;
        },
        [&]() noexcept { commitEvents.emplace_back("publish"); });
    CHECK(commitResult
        == LoaderInstallResult::PartialCommitColdRestartRequired);
    CHECK((commitEvents == std::vector<std::string>{"reserve", "tail"}));

    commitEvents.clear();
    commitResult = CommitLoaderMutation(
        heldCodecLease,
        [&]() noexcept { commitEvents.emplace_back("reserve"); },
        [&]() noexcept {
            commitEvents.emplace_back("tail");
            return true;
        },
        [&]() noexcept { commitEvents.emplace_back("activate"); },
        [&]() noexcept {
            commitEvents.emplace_back("cap");
            return false;
        },
        [&]() noexcept { commitEvents.emplace_back("publish"); });
    CHECK(commitResult
        == LoaderInstallResult::PartialCommitColdRestartRequired);
    CHECK((commitEvents == std::vector<std::string>{
        "reserve", "tail", "activate", "cap",
    }));

    CHECK(IsValidStatId(0));
    CHECK(IsValidStatId(4094));
    CHECK(!IsValidStatId(4095));
    CHECK(IsValidRecordCount(0));
    CHECK(IsValidRecordCount(4095));
    CHECK(!IsValidRecordCount(4096));
    CHECK(AddedSerializedBits(16, 1) == 51);
    CHECK(AddedSerializedBits(500, 10) == 1530);

    std::vector<DescriptionEntry> descriptions{
        {4094, 1}, {511, 200}, {0, 200}, {1023, 15},
    };
    CHECK(SortDescriptionEntries(descriptions));
    CHECK(descriptions[0].statId == 4094);
    CHECK(descriptions[1].statId == 1023);
    CHECK(descriptions[2].priority == 200);
    CHECK(descriptions[3].priority == 200);
    descriptions.push_back({4095, 500});
    CHECK(!SortDescriptionEntries(descriptions));

    std::vector<DescriptionEntry> signedPriorities{
        {0, std::bit_cast<std::int16_t>(std::uint16_t{0x7FFF})},
        {1, std::bit_cast<std::int16_t>(std::uint16_t{0x8000})},
        {2, std::bit_cast<std::int16_t>(std::uint16_t{0xFFFF})},
        {3, 0},
    };
    CHECK(SortDescriptionEntries(signedPriorities));
    CHECK(signedPriorities[0].statId == 1);
    CHECK(signedPriorities[1].statId == 2);
    CHECK(signedPriorities[2].statId == 3);
    CHECK(signedPriorities[3].statId == 0);

    const auto checkDenseDescriptionBoundary = [](std::size_t count) {
        std::vector<DescriptionSource> rows(count);
        for (std::size_t id = 0; id < count; ++id) {
            rows[id] = {
                1,
                static_cast<std::int16_t>(count - id),
            };
        }
        std::vector<std::uint16_t> index{0xBEEF};
        CHECK(BuildDescriptionIndex(rows, index));
        CHECK(index.size() == count);
        if (!index.empty()) {
            CHECK(index.front() == count - 1);
            CHECK(index.back() == 0);
        }
    };
    checkDenseDescriptionBoundary(511);
    checkDenseDescriptionBoundary(512);
    checkDenseDescriptionBoundary(1023);
    checkDenseDescriptionBoundary(2047);
    checkDenseDescriptionBoundary(4095);

    std::vector<DescriptionSource> sparseRows(4095);
    sparseRows[0] = {1, 30};
    sparseRows[511] = {2, 20};
    sparseRows[1023] = {3, 10};
    sparseRows[2047] = {4, 40};
    sparseRows[4094] = {5, 0};
    std::vector<std::uint16_t> sparseIndex;
    CHECK(BuildDescriptionIndex(sparseRows, sparseIndex));
    CHECK((sparseIndex == std::vector<std::uint16_t>{
        4094, 1023, 511, 0, 2047,
    }));

    std::vector<DescriptionSource> tooManyRows(4096, {1, 1});
    std::vector<std::uint16_t> untouched{7, 8, 9};
    CHECK(!BuildDescriptionIndex(tooManyRows, untouched));
    CHECK((untouched == std::vector<std::uint16_t>{7, 8, 9}));

    CHECK(!FoundationPatterns.empty());
    struct OwnedNativeRange {
        std::uintptr_t begin;
        std::size_t size;
    };
    // Historical RuffDood transport-prototype entry ranges. They are not part
    // of eezstreet's official plugin-items. Every ISC12 surface remains
    // disjoint except the deliberate exact queue-entry witness: that overlap
    // makes the canonical preflight reject the old prototype fail-closed.
    constexpr auto historicalTransportPrototypeRanges =
        std::to_array<OwnedNativeRange>({
        {0x12E2C0, 32},
        {0x12E490, 32},
        {0x374BF0, 14},
        {0x374FF0, 27},
        {0x375EE0, 26},
        {0x4817F0, 22},
    });
    for (const auto& pattern : FoundationPatterns) {
        CHECK(pattern.id != nullptr);
        CHECK(pattern.rva != 0);
        CHECK(!pattern.bytes.empty());
        CHECK(pattern.bytes.size() == pattern.mask.size());
        CHECK(std::string_view{pattern.id} != "item.decode-entry");
        CHECK(std::string_view{pattern.id} != "item.serialize-entry");
        CHECK(pattern.rva <=
            (std::numeric_limits<std::uintptr_t>::max)()
                - pattern.bytes.size());
        const auto patternEnd = pattern.rva + pattern.bytes.size();
        for (const auto& owned : historicalTransportPrototypeRanges) {
            const auto ownedEnd = owned.begin + owned.size;
            const auto deliberateQueueWitness =
                std::string_view{pattern.id}
                    == "transport.g9-native-queue-entry"
                && pattern.rva == owned.begin
                && pattern.bytes.size() == owned.size
                && owned.begin == 0x4817F0;
            CHECK(deliberateQueueWitness
                || patternEnd <= owned.begin || pattern.rva >= ownedEnd);
        }
        for (const auto mask : pattern.mask) CHECK(mask == 0xFF);
    }

    std::ifstream pluginFile(
        std::filesystem::path{ISC12_PLUGIN_SOURCE_PATH}, std::ios::binary);
    const std::string pluginText{
        std::istreambuf_iterator<char>{pluginFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!pluginText.empty());
    CHECK(pluginText.find("D2RL::GetBuildName(context)")
        != std::string::npos);
    CHECK(pluginText.find("without a version allowlist")
        != std::string::npos);
    CHECK(pluginText.find("92777") == std::string::npos);
    CHECK(pluginText.find("93847") == std::string::npos);
    CHECK(pluginText.find("IsSupportedBuild") == std::string::npos);
    CHECK(pluginText.find("RuntimeBuild ==") == std::string::npos);
    CHECK(pluginText.find("RuntimeBuild !=") == std::string::npos);
    CHECK(pluginText.find("InstallInlineHook") == std::string::npos);
    CHECK(pluginText.find("InspectLoaderCompileProviderContract(")
        != std::string::npos);
    CHECK(pluginText.find("D2RCore!LoadExcelTable")
        != std::string::npos);
    CHECK(pluginText.find("D2RL::ServiceId::Lifecycle")
        != std::string::npos);
    CHECK(pluginText.find("D2RL::ServiceId::DataTable")
        != std::string::npos);
    CHECK(pluginText.find("registerDataTablesLoadedListener")
        != std::string::npos);
    CHECK(pluginText.find("unregisterDataTablesLoadedListener")
        != std::string::npos);
    CHECK(pluginText.find("D2RL::DataTables::Bank::Rotw")
        != std::string::npos);
    CHECK(pluginText.find("D2RL::DataTables::TableId::ItemStatCost")
        != std::string::npos);
    CHECK(pluginText.find("FinalizePublishedSchemaSnapshot(")
        != std::string::npos);
    CHECK(pluginText.find("InspectPlayerSaveStatWriterProviderContract(")
        != std::string::npos);
    CHECK(pluginText.find("D2RCore!WritePlayerSaveStatId")
        != std::string::npos);
    CHECK(pluginText.find("InspectItemSaveStatWriterProviderContract(")
        != std::string::npos);
    CHECK(pluginText.find("D2RCore!WriteItemSaveStatId")
        != std::string::npos);
    CHECK(pluginText.find("InspectPlayerSaveProviderContract(")
        != std::string::npos);
    CHECK(pluginText.find(
            "D2RCore!WritePlayerSaveWithEnvironmentCapture")
        != std::string::npos);
    CHECK(pluginText.find("InspectD2SItemReadProviderContract(")
        != std::string::npos);
    CHECK(pluginText.find("D2RCore!ReadItemsByVersion")
        != std::string::npos);
    CHECK(pluginText.find("InspectD2SSaveIoProviderContract(")
        != std::string::npos);
    CHECK(pluginText.find("WriteD2sFileWithEnvironment")
        != std::string::npos);
    CHECK(pluginText.find("CloseD2sFileWithEnvironment")
        != std::string::npos);
    CHECK(pluginText.find("VirtualProtect") == std::string::npos);
    CHECK(pluginText.find("WriteProcessMemory") == std::string::npos);
    CHECK(pluginText.find("ProcessMutexNameFormat") != std::string::npos);
    CHECK(pluginText.find("GetCurrentProcessId()") != std::string::npos);
    CHECK(pluginText.find("RuffnecKk.ISC12.%lu") != std::string::npos);
    constexpr std::string_view publicDescription =
        "Extends ItemStatCost.txt capacity to 4,095 rows. Requires ISC12-compatible save files.";
    CHECK(pluginText.find(publicDescription) != std::string::npos);

    std::ifstream resourceFile(
        std::filesystem::path{ISC12_RESOURCE_SOURCE_PATH}, std::ios::binary);
    const std::string resourceText{
        std::istreambuf_iterator<char>{resourceFile},
        std::istreambuf_iterator<char>{}};
    CHECK(resourceText.find(publicDescription) != std::string::npos);

    std::ifstream loaderFile(
        std::filesystem::path{ISC12_LOADER_SOURCE_PATH}, std::ios::binary);
    const std::string loaderText{
        std::istreambuf_iterator<char>{loaderFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!loaderText.empty());
    CHECK(pluginText.find("NativePublicationQuiescenceLease")
        == std::string::npos);
    CHECK(loaderText.find("NativePublicationQuiescenceLease")
        == std::string::npos);
    CHECK(pluginText.find("InitialLoadPublicationWindow publicationWindow")
        != std::string::npos);
    CHECK(pluginText.find("NativePublicationLeaseView publicationLease")
        == std::string::npos);
    CHECK(loaderText.find("PublicationCoordinator::Publish(")
        == std::string::npos);
    CHECK(pluginText.find("publicationCoordinator.Publish(")
        != std::string::npos);
    CHECK(pluginText.find("PublishReadinessAfterStartupCommit()")
        != std::string::npos);
    CHECK(pluginText.find("FailClosedNativePublication(")
        != std::string::npos);
    CHECK(pluginText.find("InstallLoaderExtension(publicationLease")
        == std::string::npos);
    CHECK(pluginText.find(
        "pinned D2RLoader SDK exposes no loader-owned")
        == std::string::npos);
    CHECK(pluginText.find("->Publish(") == std::string::npos);
    CHECK(loaderText.find(".Publish(") == std::string::npos);
    CHECK(loaderText.find("->Publish(") == std::string::npos);

    const auto publicationWindow = pluginText.find(
        "InitialLoadPublicationWindow publicationWindow");
    const auto prepareLoader = pluginText.find("PrepareLoaderExtension(");
    const auto registerSchemaLifecycle = pluginText.find(
        "if (!RegisterSchemaLifecycleListener())");
    const auto coordinatorPublish = pluginText.find(
        "publicationCoordinator.Publish(");
    const auto coordinatorPublishAgain = pluginText.find(
        "publicationCoordinator.Publish(", coordinatorPublish + 1U);
    const auto publishReadiness = pluginText.find(
        "PublishReadinessAfterStartupCommit()");
    const auto operationalPublish = pluginText.find(
        "SchemaLifecycle.compare_exchange_strong(");
    CHECK(pluginText.find("Settings.enabled") == std::string::npos);
    CHECK(pluginText.find("LoadConfig(") == std::string::npos);
    CHECK(pluginText.find("ruffneckk-isc12.toml") == std::string::npos);
    CHECK(pluginText.find("activation=presence") != std::string::npos);
    CHECK(publicationWindow != std::string::npos);
    CHECK(prepareLoader != std::string::npos);
    CHECK(registerSchemaLifecycle != std::string::npos);
    CHECK(coordinatorPublish != std::string::npos);
    CHECK(coordinatorPublishAgain == std::string::npos);
    CHECK(publishReadiness != std::string::npos);
    CHECK(operationalPublish != std::string::npos);
    CHECK(publicationWindow < prepareLoader);
    CHECK(prepareLoader < coordinatorPublish);
    CHECK(prepareLoader < registerSchemaLifecycle);
    CHECK(registerSchemaLifecycle < coordinatorPublish);
    CHECK(coordinatorPublish < publishReadiness);
    CHECK(publishReadiness < operationalPublish);

    const auto atomicListenerHandle = pluginText.find(
        "std::atomic<D2RL::Lifecycle::ListenerHandle>");
    const auto stoppingState = pluginText.find(
        "lifecycle == SchemaLifecycleState::Stopping");
    const auto benignShutdownReturn = pluginText.find(
        "return;", stoppingState);
    const auto lifecycleFailure = pluginText.find(
        "FailSchemaLifecycle(", benignShutdownReturn);
    const auto stableListenerUserData = pluginText.find(
        ".userData = &SchemaLifecycle");
    CHECK(atomicListenerHandle != std::string::npos);
    CHECK(stoppingState != std::string::npos);
    CHECK(benignShutdownReturn != std::string::npos);
    CHECK(lifecycleFailure != std::string::npos);
    CHECK(stoppingState < benignShutdownReturn);
    CHECK(benignShutdownReturn < lifecycleFailure);
    CHECK(stableListenerUserData != std::string::npos);

    const auto schemaLeaseClass = loaderText.find(
        "class PublishedSchemaReadLease final");
    const auto nonBlockingSchemaLease = loaderText.find(
        "TryAcquireSRWLockShared(&SchemaSnapshotLock)", schemaLeaseClass);
    const auto readPersistenceHook = loaderText.find(
        "ISC12PrepareNativeStoreRead(");
    const auto writePersistenceHook = loaderText.find(
        "ISC12PrepareNativeStoreWrite(");
    const auto writePathCopy = loaderText.find(
        "SafeCopyReadable(", writePersistenceHook);
    const auto writeSchemaLease = loaderText.find(
        "PublishedSchemaReadLease schemaLease", writePersistenceHook);
    const auto writePhysicalSnapshot = loaderText.find(
        "SnapshotNativeObjectBuffer(", writeSchemaLease);
    const auto writeAdapter = loaderText.find(
        "AdaptNativeStoreWrite(", writePhysicalSnapshot);
    CHECK(schemaLeaseClass != std::string::npos);
    CHECK(nonBlockingSchemaLease != std::string::npos);
    CHECK(readPersistenceHook < writePersistenceHook);
    if (readPersistenceHook < writePersistenceHook) {
        const auto readHookBody = std::string_view{loaderText}.substr(
            readPersistenceHook,
            writePersistenceHook - readPersistenceHook);
        const auto readPhysicalSnapshot = readHookBody.find(
            "SnapshotNativeObjectBuffer(");
        const auto readAdapter = readHookBody.find("AdaptNativeStoreRead(");
        CHECK(readHookBody.find("PublishedSchemaReadLease schemaLease")
            == std::string_view::npos);
        CHECK(readPhysicalSnapshot < readAdapter);
    }
    CHECK(writePersistenceHook < writePathCopy);
    CHECK(writePathCopy < writeSchemaLease);
    CHECK(writeSchemaLease < writePhysicalSnapshot);
    CHECK(writePhysicalSnapshot < writeAdapter);
    CHECK(loaderText.find("standard-container read") != std::string::npos);
    CHECK(loaderText.find("standard-container write") != std::string::npos);
    CHECK(loaderText.find("NativePersistenceDisposition::ProceedNative")
        != std::string::npos);
    CHECK(loaderText.find("WriteFileAtomically(") == std::string::npos);
    CHECK(loaderText.find("BuildEnvelope(") == std::string::npos);

    const auto legacyInstall = loaderText.find(
        "auto InstallLoaderExtension(");
    const auto legacyTestingGuard = loaderText.rfind(
        "#if defined(ISC12_CODEC_PATCH_TESTING)", legacyInstall);
    const auto legacyTestingEnd = loaderText.find("#endif", legacyInstall);
    CHECK(legacyInstall != std::string::npos);
    CHECK(legacyTestingGuard != std::string::npos);
    CHECK(legacyTestingEnd != std::string::npos);
    CHECK(legacyTestingGuard < legacyInstall);
    CHECK(legacyInstall < legacyTestingEnd);

    const auto reserveBegin = loaderText.find(
        "auto ReservePublicationProcessLifetime");
    const auto readinessBegin = loaderText.find(
        "auto PublishPublicationReadiness");
    CHECK(reserveBegin != std::string::npos);
    CHECK(readinessBegin != std::string::npos);
    if (reserveBegin != std::string::npos
            && readinessBegin != std::string::npos
            && reserveBegin < readinessBegin) {
        const auto reserveBody = std::string_view{loaderText}.substr(
            reserveBegin, readinessBegin - reserveBegin);
        CHECK(reserveBody.find("AnyMutationInstalled = true;")
            != std::string_view::npos);
    }

    const auto guardBegin = loaderText.find("auto ActivateG0Guard");
    const auto guardEnd = loaderText.find("auto MarkG0CapCommitted");
    CHECK(guardBegin != std::string::npos);
    CHECK(guardEnd != std::string::npos);
    if (guardBegin != std::string::npos
            && guardEnd != std::string::npos
            && guardBegin < guardEnd) {
        const auto guardBody = std::string_view{loaderText}.substr(
            guardBegin, guardEnd - guardBegin);
        CHECK(guardBody.find(
            "InterlockedExchange(&State->capMayBeExtended, 1)")
            != std::string_view::npos);
        CHECK(guardBody.find(
            "InterlockedExchange(&State->operational, 1)")
            == std::string_view::npos);
    }

    const auto poisonBegin = loaderText.find(
        "auto MarkPublicationPoisoned");
    CHECK(poisonBegin != std::string::npos);
    if (readinessBegin != std::string::npos
            && poisonBegin != std::string::npos
            && readinessBegin < poisonBegin) {
        const auto readinessBody = std::string_view{loaderText}.substr(
            readinessBegin, poisonBegin - readinessBegin);
        const auto codecReady = readinessBody.find(
            "InterlockedExchange(&PersistenceState->codecReady, 1)");
        const auto itemTransportReady = readinessBody.find(
            "InterlockedExchange(&PersistenceState->itemTransportReady, 1)");
        const auto persistenceOperational = readinessBody.find(
            "InterlockedExchange(&PersistenceState->operational, 1)");
        const auto globalOperational = readinessBody.find(
            "InterlockedExchange(&State->operational, 1)");
        CHECK(codecReady != std::string_view::npos);
        CHECK(itemTransportReady != std::string_view::npos);
        CHECK(persistenceOperational != std::string_view::npos);
        CHECK(globalOperational != std::string_view::npos);
        CHECK(codecReady < itemTransportReady);
        CHECK(itemTransportReady < persistenceOperational);
        CHECK(persistenceOperational < globalOperational);
    }
    const auto poisonEnd = loaderText.find(
        "} // namespace", poisonBegin);
    CHECK(poisonEnd != std::string::npos);
    if (poisonBegin != std::string::npos
            && poisonEnd != std::string::npos
            && poisonBegin < poisonEnd) {
        const auto poisonBody = std::string_view{loaderText}.substr(
            poisonBegin, poisonEnd - poisonBegin);
        CHECK(poisonBody.find(
            "InterlockedExchange(&PersistenceState->itemTransportReady, 0)")
            != std::string_view::npos);
        CHECK(poisonBody.find(
            "InterlockedExchange(&PersistenceState->codecReady, 0)")
            != std::string_view::npos);
        CHECK(poisonBody.find(
            "InterlockedExchange(&PersistenceState->operational, 0)")
            != std::string_view::npos);
        CHECK(poisonBody.find(
            "InterlockedExchange(&State->operational, 0)")
            != std::string_view::npos);
        CHECK(poisonBody.find("ColdRestartRequired = true;")
            != std::string_view::npos);
        CHECK(poisonBody.find("FailClosed(")
            != std::string_view::npos);
    }
    CHECK(loaderText.find("InstallInlineHook") == std::string::npos);
    CHECK(loaderText.find("PendingSchemaCandidates.Stage(")
        != std::string::npos);
    CHECK(loaderText.find("PendingSchemaCandidates.Finalize(")
        != std::string::npos);
    CHECK(loaderText.find("CompleteSchemaSnapshotUpdate(")
        == std::string::npos);
    const auto stageSchemaBegin = loaderText.find(
        "auto StageSchemaSnapshotUpdate(");
    const auto stageSchemaEnd = loaderText.find(
        "auto InvokeNativeQsort(", stageSchemaBegin);
    CHECK(stageSchemaBegin != std::string::npos);
    CHECK(stageSchemaEnd != std::string::npos);
    if (stageSchemaBegin != std::string::npos
            && stageSchemaEnd != std::string::npos
            && stageSchemaBegin < stageSchemaEnd) {
        const auto stageSchemaBody = std::string_view{loaderText}.substr(
            stageSchemaBegin, stageSchemaEnd - stageSchemaBegin);
        CHECK(stageSchemaBody.find("SchemaReady.store(true")
            == std::string_view::npos);
    }
    CHECK(loaderText.find("GetModuleHandleW(L\"D2RCore.dll\")")
        != std::string::npos);
    CHECK(loaderText.find("GetProcAddress(core, \"LoadExcelTable\")")
        != std::string::npos);
    CHECK(loaderText.find(
            "GetProcAddress(core, \"WritePlayerSaveStatId\")")
        != std::string::npos);
    CHECK(loaderText.find(
            "GetProcAddress(core, \"WriteItemSaveStatId\")")
        != std::string::npos);
    CHECK(loaderText.find(
            "\"WritePlayerSaveWithEnvironmentCapture\"")
        != std::string::npos);
    CHECK(loaderText.find("GetProcAddress(core, \"ReadItemsByVersion\")")
        != std::string::npos);
    CHECK(loaderText.find("ProviderBytes121.size() == 0x6CU")
        != std::string::npos);
    CHECK(loaderText.find("ProviderBytes121.size() == 0x6BU")
        != std::string::npos);
    CHECK(loaderText.find("ProviderBytes12.size() == 0x6CU")
        != std::string::npos);
    CHECK(loaderText.find("ProviderBytes12.size() == 0x6BU")
        != std::string::npos);
    CHECK(loaderText.find("ProviderBytes11.size() == ProviderBytes12.size()")
        != std::string::npos);
    CHECK(loaderText.find("function.UnwindData != unwindRva")
        != std::string::npos);
    CHECK(loaderText.find("liveUnwind != (provider121")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwarder121Offset = 0x961U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwarder12Offset = 0x935U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwarder11Offset = 0x901U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderRva121 = 0x696210U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderSize121 = 0x1C04U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderUnwindRva121 = 0x56D0CCU")
        != std::string::npos);
    CHECK(loaderText.find("ProviderFuncInfoRva121 = 0x56D2B0U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwardSlotRva121 = 0x597DC0U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderRva12 = 0x634650U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderSize12 = 0x1A18U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderUnwindRva12 = 0x50EFD0U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwardSlotRva12 = 0x5372C0U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderRva11 = 0x563D80U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderSize11 = 0x1383U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderUnwindRva11 = 0x452480U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwardSlotRva11 = 0x480DE8U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderFuncInfoRva12 = 0x50F1B4U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderFuncInfoRva11 = 0x452648U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderRva121 = 0x69CE20U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderUnwindRva121 = 0x56F6C0U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderFuncInfoRva121 = 0x56F6FCU")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwardSlotRva121 = 0x597E38U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderRva12 = 0x63BE60U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderSize12 = 0x126U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderUnwindRva12 = 0x5115C4U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderFuncInfoRva12 = 0x511600U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwardSlotRva12 = 0x537338U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderRva11 = 0x56A710U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderUnwindRva11 = 0x4548C8U")
        != std::string::npos);
    CHECK(loaderText.find("ProviderFuncInfoRva11 = 0x454904U")
        != std::string::npos);
    CHECK(loaderText.find("NativeForwardSlotRva11 = 0x480E60U")
        != std::string::npos);
    CHECK(loaderText.find("liveFuncInfo != expectedFuncInfo")
        != std::string::npos);
    CHECK(loaderText.find("D2RCoreReadItemsByVersion")
        != std::string::npos);
    CHECK(loaderText.find("CalculateSha256(") != std::string::npos);
    CHECK(loaderText.find("liveProviderHash != expectedProviderHash")
        != std::string::npos);
    CHECK(loaderText.find(
            "liveCapacity == D2RCoreDynamicCapacityBytes")
        != std::string::npos);
    CHECK(loaderText.find(
            "D2RCoreWritePlayerSaveWithEnvironmentCapture")
        != std::string::npos);
    CHECK(loaderText.find("ReadUnconditionalJumpTarget(")
        != std::string::npos);
    CHECK(loaderText.find("MaximumRecordCount") != std::string::npos);
    CHECK(loaderText.find("FailClosed") != std::string::npos);
    CHECK(loaderText.find(
        "DescFunc tail publication returned an uncertain result")
        != std::string::npos);
    CHECK(loaderText.find("ShutdownRundownTimeoutMilliseconds")
        != std::string::npos);
    CHECK(loaderText.find("PAGE_EXECUTE_READ") != std::string::npos);
    CHECK(loaderText.find("PAGE_EXECUTE_READWRITE")
        != std::string::npos);
    CHECK(loaderText.find("0x31F0AB") != std::string::npos);
    CHECK(loaderText.find("0x31ED38") != std::string::npos);
    CHECK(loaderText.find(
        "PatchJmpRel32(\n                PersistenceReaderPatchRva")
        == std::string::npos);
    CHECK(loaderText.find(
        "PatchJmpRel32(\n                PersistenceWriterPatchRva")
        == std::string::npos);
    CHECK(loaderText.find("CommitPreparedCodecPatchSet(")
        == std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePlayerSaveFinalizeEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplateAuxiliaryReaderEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePlayerReaderEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePlayerPreviewEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplateCodecReturnExit")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePacket9CQueueEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePacket9DQueueEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePacket9CProducerEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePacket9DProducerEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplateItemTransportReturnExit")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePacket9CTrampoline")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplatePacket9DTrampoline")
        != std::string::npos);
    CHECK(loaderText.find(
        "ISC12PersistenceRelayTemplateItemTrampolineUnwindInfo")
        != std::string::npos);
    CHECK(loaderText.find("AuxiliaryReaderCallRva = 0x531A6D")
        != std::string::npos);
    CHECK(loaderText.find("PlayerReaderPrimaryCallRva = 0x52EC4A")
        != std::string::npos);
    CHECK(loaderText.find("PlayerReaderLegacyCallRva = 0x530A34")
        != std::string::npos);
    CHECK(loaderText.find("PlayerPreviewCallRva = 0x61CF90")
        != std::string::npos);
    CHECK(loaderText.find("PlayerSaveFinalizeCallRva = 0x5353C2")
        != std::string::npos);
    CHECK(loaderText.find("Packet9CProducerEntryRva = 0x479CD0")
        != std::string::npos);
    CHECK(loaderText.find("Packet9CProducerEpilogueEndRva = 0x479E41")
        != std::string::npos);
    CHECK(loaderText.find("Packet9DProducerEntryRva = 0x479EA0")
        != std::string::npos);
    CHECK(loaderText.find("Packet9DProducerEpilogueEndRva = 0x47A037")
        != std::string::npos);
    CHECK(loaderText.find("Packet9CQueueCallRva = 0x479E10")
        != std::string::npos);
    CHECK(loaderText.find("Packet9DQueueCallRva = 0x47A001")
        != std::string::npos);
    CHECK(loaderText.find("NativeFullItemPacketQueueRva = 0x4817F0")
        != std::string::npos);
    CHECK(loaderText.find("ReadPlayerStatsWithPreflight")
        != std::string::npos);
    CHECK(loaderText.find("CopyPlayerPreviewWithPreflight")
        != std::string::npos);
    const auto previewFunction = loaderText.find(
        "auto CopyPlayerPreviewWithPreflight(");
    const auto previewStructuralPreflight = loaderText.find(
        "PreflightPlayerPreviewContainer(", previewFunction);
    const auto previewSchemaLock = loaderText.find(
        "AcquireSRWLockShared(&SchemaSnapshotLock)", previewFunction);
    CHECK(previewFunction < previewStructuralPreflight);
    CHECK(previewStructuralPreflight < previewSchemaLock);
    CHECK(loaderText.find(
        "frontend preview accepted before schema ")
        != std::string::npos);
    CHECK(loaderText.find("publication; bytes=%u") != std::string::npos);
    CHECK(loaderText.find("version != InnerFormatVersion")
        != std::string::npos);
    CHECK(loaderText.find("AcquireSRWLockShared(&SchemaSnapshotLock)")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, auxiliaryReaderHandler) == 0x48")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, playerReaderHandler) == 0x50")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, playerPreviewHandler) == 0x58")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, itemTransportReady) == 0x60")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, packet9CProducerHandler) == 0x68")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, packet9DProducerHandler) == 0x70")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, packet9CQueueHandler) == 0x78")
        != std::string::npos);
    CHECK(loaderText.find(
        "offsetof(PersistenceRelayState, packet9DQueueHandler) == 0x80")
        != std::string::npos);
    CHECK(loaderText.find("RtlLookupFunctionEntry") != std::string::npos);
    CHECK(loaderText.find("liveEpilogueFunction = RtlLookupFunctionEntry")
        != std::string::npos);
    CHECK(loaderText.find(
        "epilogueFunction.UnwindData != function.UnwindData")
        != std::string::npos);
    CHECK(loaderText.find("function.EndAddress != expectedEpilogueEndRva")
        != std::string::npos);
    CHECK(loaderText.find("function.EndAddress > LoaderImageSize")
        != std::string::npos);
    CHECK(loaderText.find("function.UnwindData != expectedUnwindRva")
        != std::string::npos);
    CHECK(loaderText.find("header.versionAndFlags != 0x19U")
        != std::string::npos);
    CHECK(loaderText.find("NativeExceptionHandlerRva = 0x12D104CU")
        != std::string::npos);
    CHECK(loaderText.find("0x2129AFCU") != std::string::npos);
    CHECK(loaderText.find("0x2129B18U") != std::string::npos);
    CHECK(loaderText.find("matchingProviderGeneration")
        != std::string::npos);
    CHECK(loaderText.find("writerProviderRva == 0x698390U")
        != std::string::npos);
    CHECK(loaderText.find("closeProviderRva == 0x69B270U")
        != std::string::npos);
    CHECK(loaderText.find("writerProviderRva == 0x6365E0U")
        != std::string::npos);
    CHECK(loaderText.find("closeProviderRva == 0x6393B0U")
        != std::string::npos);
    CHECK(loaderText.find("writerProviderRva == 0x565640U")
        != std::string::npos);
    CHECK(loaderText.find("closeProviderRva == 0x567E00U")
        != std::string::npos);
    CHECK(loaderText.find("RtlAddFunctionTable") != std::string::npos);
    CHECK(loaderText.find("RtlDeleteFunctionTable") != std::string::npos);
    CHECK(loaderText.find("AbortFullItemPacketProducer")
        != std::string::npos);
    CHECK(loaderText.find("AbnormalTermination() != FALSE")
        != std::string::npos);
    CHECK(loaderText.find(
        "NativeFullItemPacketQueue(client, bytes, length)")
        != std::string::npos);
    CHECK(loaderText.find(
        "LoaderCodecPatchAuthority::BindPreparedRelay")
        != std::string::npos);

    std::ifstream loaderAsmFile(
        std::filesystem::path{ISC12_LOADER_ASM_PATH}, std::ios::binary);
    const std::string loaderAsmText{
        std::istreambuf_iterator<char>{loaderAsmFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!loaderAsmText.empty());
    CHECK(loaderAsmText.find("mov qword ptr [rsp+20h], rax")
        != std::string::npos);
    CHECK(loaderAsmText.find("ISC12LoaderTailMidHook PROC FRAME")
        != std::string::npos);
    CHECK(loaderAsmText.find(".allocstack 30h") != std::string::npos);
    CHECK(loaderAsmText.find(".endprolog") != std::string::npos);
    CHECK(loaderAsmText.find("mov r9, qword ptr [rsp+48h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov r15, qword ptr [rsp+0F40h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov r14, qword ptr [rsp+0F48h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov r13, qword ptr [rsp+0F50h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov rbx, qword ptr [rsp+0F78h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov rsi, qword ptr [rsp+0F80h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov rdi, qword ptr [rsp+0F88h]")
        != std::string::npos);
    CHECK(loaderAsmText.find("mov ecx, 7") != std::string::npos);
    CHECK(loaderAsmText.find("int 29h") != std::string::npos);
    CHECK(loaderAsmText.find("ud2") == std::string::npos);
    CHECK(loaderAsmText.find("ISC12LoaderRelayTemplateSuccessExit")
        != std::string::npos);
    CHECK(loaderAsmText.find("ISC12LoaderRelayTemplateVanillaExit")
        != std::string::npos);
    const auto firstDecrement = loaderAsmText.find("lock dec");
    const auto handler = loaderAsmText.find("ISC12LoaderTailMidHook PROC");
    CHECK(firstDecrement != std::string::npos);
    CHECK(handler != std::string::npos);
    CHECK(loaderAsmText.find("lock dec", handler) == std::string::npos);

    std::ifstream persistenceAsmFile(
        std::filesystem::path{ISC12_PERSISTENCE_ASM_PATH},
        std::ios::binary);
    const std::string persistenceAsmText{
        std::istreambuf_iterator<char>{persistenceAsmFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!persistenceAsmText.empty());
    CHECK(persistenceAsmText.find(
        "mov qword ptr [rsp+48h], -1") != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceReaderMidHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceWriterMidHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find(".allocstack 20h")
        != std::string::npos);
    CHECK(persistenceAsmText.find(".endprolog")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "mov r8d, dword ptr [rsp+50h]") != std::string::npos);
    CHECK(persistenceAsmText.find("mov r9d, eax") != std::string::npos);
    CHECK(persistenceAsmText.find("lea rdx, [rsp+60h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplateReaderRejectedExit")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplateWriterCommittedExit")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePlayerSaveFinalizeEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplateAuxiliaryReaderEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePlayerReaderEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePlayerPreviewEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplateCodecReturnExit")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePacket9CQueueEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePacket9DQueueEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePacket9CProducerEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePacket9DProducerEntry")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplateItemTransportReturnExit")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePacket9CTrampoline LABEL BYTE")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplatePacket9DTrampoline LABEL BYTE")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "ISC12PersistenceRelayTemplateItemTrampolineUnwindInfo LABEL BYTE")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "db 040h,053h,055h,056h,057h,0E9h")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "db 001h,005h,004h,000h,005h,070h")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "db 004h,060h,003h,050h,002h,030h")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12AuxiliaryReaderCallHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12PlayerReaderCallHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12PlayerPreviewCallHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12ItemAction9CEntryHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12ItemAction9DEntryHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12ItemAction9CQueueHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find("ISC12ItemAction9DQueueHook PROC FRAME")
        != std::string::npos);
    CHECK(persistenceAsmText.find(".allocstack 38h")
        != std::string::npos);
    CHECK(persistenceAsmText.find(".allocstack 28h")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "call ISC12ReadAuxiliaryWithPreflight")
        != std::string::npos);
    CHECK(persistenceAsmText.find("call ISC12ReadRegularWithPreflight")
        != std::string::npos);
    CHECK(persistenceAsmText.find("call ISC12CopyPreviewWithPreflight")
        != std::string::npos);
    CHECK(persistenceAsmText.find("call ISC12InvokeItemAction9CNative")
        != std::string::npos);
    CHECK(persistenceAsmText.find("call ISC12InvokeItemAction9DNative")
        != std::string::npos);
    CHECK(persistenceAsmText.find("call ISC12CaptureItemAction9CQueue")
        != std::string::npos);
    CHECK(persistenceAsmText.find("call ISC12CaptureItemAction9DQueue")
        != std::string::npos);
    CHECK(persistenceAsmText.find("jmp qword ptr [gISC12CodecReturnExit]")
        != std::string::npos);
    CHECK(persistenceAsmText.find(
        "jmp qword ptr [gISC12ItemTransportReturnExit]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("cmp dword ptr [r11+60h], 0")
        != std::string::npos);
    CHECK(persistenceAsmText.find("jmp qword ptr [r11+68h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("jmp qword ptr [r11+70h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("jmp qword ptr [r11+78h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("jmp qword ptr [r11+80h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("cmp qword ptr [rcx+18h], 0")
        != std::string::npos);
    CHECK(persistenceAsmText.find("mov rax, qword ptr [rcx+10h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("mov edx, dword ptr [rcx+20h]")
        != std::string::npos);
    CHECK(persistenceAsmText.find("PersistenceRelayFailClosed")
        != std::string::npos);
    CHECK(persistenceAsmText.find("mov ecx, 7") != std::string::npos);
    CHECK(persistenceAsmText.find("int 29h") != std::string::npos);

    std::ifstream cmakeFile(
        std::filesystem::path{ISC12_CMAKE_PATH}, std::ios::binary);
    const std::string cmakeText{
        std::istreambuf_iterator<char>{cmakeFile},
        std::istreambuf_iterator<char>{}};
    CHECK(cmakeText.find("RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE")
        != std::string::npos);
    CHECK(cmakeText.find("RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE FALSE")
        == std::string::npos);
    CHECK(cmakeText.find("tomlplusplus") == std::string::npos);
    CHECK(cmakeText.find("d2rlplugin_embed_config") == std::string::npos);
    CHECK(cmakeText.find("ruffneckk-isc12.toml") == std::string::npos);
    CHECK(cmakeText.find("isc12_persistence_relay.asm")
        != std::string::npos);

    const auto isc12SourceDirectory =
        std::filesystem::path{ISC12_SOURCE_DIRECTORY};
    std::ifstream codecPatchSourceFile(
        isc12SourceDirectory / "isc12_codec_patch.cpp", std::ios::binary);
    const std::string codecPatchSourceText{
        std::istreambuf_iterator<char>{codecPatchSourceFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!codecPatchSourceText.empty());
    CHECK(codecPatchSourceText.find(
        "\"codec.g1-bounded-writer\", 0x37F174")
        != std::string::npos);
    CHECK(codecPatchSourceText.find("BuildLiteralMutations<37>")
        != std::string::npos);
    CHECK(codecPatchSourceText.find("codec.g1-snapshot-copy-body")
        != std::string::npos);
    CHECK(codecPatchSourceText.find("codec.g1-compound-write-56")
        != std::string::npos);
    CHECK(codecPatchSourceText.find("codec.g1-writer-id")
        == std::string::npos);
    CHECK(codecPatchSourceText.find("GenericItemWriterIdMutations")
        == std::string::npos);
    CHECK(codecPatchSourceText.find("auto PreflightPreparedCodecPatchSet(")
        != std::string::npos);
    CHECK(codecPatchSourceText.find("auto CommitPreflightedCodecPatchSet(")
        != std::string::npos);
    const auto combinedCodecCommit = codecPatchSourceText.find(
        "auto CommitPreparedCodecPatchSet(");
    const auto codecTestingGuard = codecPatchSourceText.rfind(
        "#if defined(ISC12_CODEC_PATCH_TESTING)", combinedCodecCommit);
    CHECK(combinedCodecCommit != std::string::npos);
    CHECK(codecTestingGuard != std::string::npos);
    CHECK(codecTestingGuard < combinedCodecCommit);

    std::ifstream publicationAdapterSourceFile(
        isc12SourceDirectory / "isc12_publication_adapters.cpp",
        std::ios::binary);
    const std::string publicationAdapterSourceText{
        std::istreambuf_iterator<char>{publicationAdapterSourceFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!publicationAdapterSourceText.empty());
    CHECK(publicationAdapterSourceText.find(
        "G10ReaderPatchRva = 0x9FC654") != std::string::npos);
    CHECK(publicationAdapterSourceText.find(
        "G10WriterPatchRva = 0x9F95A2") != std::string::npos);
    const auto g10CommitBegin = publicationAdapterSourceText.find(
        "auto PublicationAdapterSet::CommitG10(");
    const auto codecCommitBegin = publicationAdapterSourceText.find(
        "auto PublicationAdapterSet::CommitCodec(");
    CHECK(g10CommitBegin != std::string::npos);
    CHECK(codecCommitBegin != std::string::npos);
    if (g10CommitBegin != std::string::npos
            && codecCommitBegin != std::string::npos
            && g10CommitBegin < codecCommitBegin) {
        const auto g10CommitBody =
            std::string_view{publicationAdapterSourceText}.substr(
                g10CommitBegin, codecCommitBegin - g10CommitBegin);
        const auto readerCommit = g10CommitBody.find(
            "G10ReaderPatchRva");
        const auto writerCommit = g10CommitBody.find(
            "G10WriterPatchRva");
        CHECK(readerCommit != std::string_view::npos);
        CHECK(writerCommit != std::string_view::npos);
        CHECK(readerCommit < writerCommit);
        CHECK(g10CommitBody.find("publishReadiness")
            == std::string_view::npos);
    }

    std::ifstream nativeSitesSourceFile(
        isc12SourceDirectory / "isc12_native_sites.hpp", std::ios::binary);
    const std::string nativeSitesSourceText{
        std::istreambuf_iterator<char>{nativeSitesSourceFile},
        std::istreambuf_iterator<char>{}};
    CHECK(!nativeSitesSourceText.empty());
    CHECK(nativeSitesSourceText.find(
        "0x81,0xFF,0xFF,0x01,0x00,0x00,0x73,0x0A")
        != std::string::npos);
    CHECK(nativeSitesSourceText.find(
        "0x00,0x00,0xBA,0xFF,0x0F,0x00,0x00,0x3B")
        != std::string::npos);
    CHECK(nativeSitesSourceText.find(
        "0xFA,0x0F,0x42,0xD7,0x41,0xB8,0x0C,0x00")
        != std::string::npos);
    CHECK(nativeSitesSourceText.find("GenericItemWriterIdBytes")
        == std::string::npos);

    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
