#include <D2RLPlugin/api.h>

#include "isc12_contract.hpp"
#include "isc12_codec_patch.hpp"
#include "isc12_item_packet_budget.hpp"
#include "isc12_loader.hpp"
#include "isc12_native_persistence_adapter.hpp"
#include "isc12_native_schema_adapter.hpp"
#include "isc12_player_stat_preflight.hpp"
#include "isc12_publication_adapters.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

extern "C" {

void* gISC12LoaderSuccessExit{};
void* gISC12LoaderVanillaExit{};
void* gISC12PersistenceReaderContinueExit{};
void* gISC12PersistenceReaderRejectedExit{};
void* gISC12PersistenceWriterVanillaExit{};
void* gISC12PersistenceWriterCommittedExit{};
void* gISC12PersistenceWriterRejectedExit{};
void* gISC12CodecReturnExit{};
void* gISC12ItemTransportReturnExit{};

void ISC12LoaderTailMidHook() noexcept;
extern std::uint8_t ISC12LoaderRelayTemplateBegin;
extern std::uint8_t ISC12LoaderRelayTemplateVanillaExit;
extern std::uint8_t ISC12LoaderRelayTemplateSuccessExit;
extern std::uint8_t ISC12LoaderRelayTemplateStatePointer;
extern std::uint8_t ISC12LoaderRelayTemplateEnd;

void ISC12PersistenceReaderMidHook() noexcept;
void ISC12PersistenceWriterMidHook() noexcept;
void ISC12AuxiliaryReaderCallHook() noexcept;
void ISC12PlayerReaderCallHook() noexcept;
void ISC12PlayerPreviewCallHook() noexcept;
void ISC12ItemAction9CEntryHook() noexcept;
void ISC12ItemAction9DEntryHook() noexcept;
void ISC12ItemAction9CQueueHook() noexcept;
void ISC12ItemAction9DQueueHook() noexcept;
extern std::uint8_t ISC12PersistenceRelayTemplateBegin;
extern std::uint8_t ISC12PersistenceRelayTemplateWriterEntry;
extern std::uint8_t ISC12PersistenceRelayTemplateReaderContinueExit;
extern std::uint8_t ISC12PersistenceRelayTemplateReaderRejectedExit;
extern std::uint8_t ISC12PersistenceRelayTemplateWriterVanillaExit;
extern std::uint8_t ISC12PersistenceRelayTemplateWriterCommittedExit;
extern std::uint8_t ISC12PersistenceRelayTemplateWriterRejectedExit;
extern std::uint8_t ISC12PersistenceRelayTemplatePlayerSaveFinalizeEntry;
extern std::uint8_t ISC12PersistenceRelayTemplateAuxiliaryReaderEntry;
extern std::uint8_t ISC12PersistenceRelayTemplatePlayerReaderEntry;
extern std::uint8_t ISC12PersistenceRelayTemplatePlayerPreviewEntry;
extern std::uint8_t ISC12PersistenceRelayTemplateCodecReturnExit;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9CQueueEntry;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9DQueueEntry;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9CProducerEntry;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9DProducerEntry;
extern std::uint8_t ISC12PersistenceRelayTemplateItemTransportReturnExit;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9CTrampoline;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9CTrampolineRel32;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9CTrampolineEnd;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9DTrampoline;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9DTrampolineRel32;
extern std::uint8_t ISC12PersistenceRelayTemplatePacket9DTrampolineEnd;
extern std::uint8_t ISC12PersistenceRelayTemplateItemTrampolineUnwindInfo;
extern std::uint8_t ISC12PersistenceRelayTemplateStatePointer;
extern std::uint8_t ISC12PersistenceRelayTemplateEnd;

}

namespace ruffneckk::isc12 {

class LoaderCodecPatchAuthority {
public:
    [[nodiscard]] static constexpr auto BindPreparedRelay(
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
};

namespace {

constexpr std::uintptr_t TailPatchRva = 0x31F0AB;
constexpr std::uintptr_t VanillaContinuationRva = 0x31F0B3;
constexpr std::uintptr_t EpilogueRva = 0x31F1B3;
constexpr std::uintptr_t CountImmediateRva = 0x31ED38;
constexpr std::uintptr_t NativeQsortRva = 0x12E6F60;
constexpr std::uintptr_t NativeComparatorRva = 0x320880;
constexpr std::uintptr_t NativeVectorResizeRva = 0x323810;
constexpr std::uintptr_t PersistenceReaderPatchRva = 0x9FC654;
constexpr std::uintptr_t PersistenceReaderContinueRva = 0x9FC659;
constexpr std::uintptr_t PersistenceReaderRejectedRva = 0x9FC66F;
constexpr std::uintptr_t PersistenceWriterPatchRva = 0x9F95A2;
constexpr std::uintptr_t PersistenceWriterVanillaRva = 0x9F95A7;
constexpr std::uintptr_t PersistenceWriterCommittedRva = 0x9F95E9;
constexpr std::uintptr_t PersistenceWriterRejectedRva = 0x9F9627;
constexpr std::uintptr_t AuxiliaryReaderCallRva = 0x531A6D;
constexpr std::uintptr_t PlayerReaderPrimaryCallRva = 0x52EC4A;
constexpr std::uintptr_t PlayerReaderLegacyCallRva = 0x530A34;
constexpr std::uintptr_t PlayerPreviewCallRva = 0x61CF90;
constexpr std::uintptr_t PlayerSaveFinalizeCallRva = 0x5353C2;
constexpr std::uintptr_t Packet9CProducerEntryRva = 0x479CD0;
constexpr std::uintptr_t Packet9CProducerContinuationRva = 0x479CD5;
constexpr std::uintptr_t Packet9CProducerEpilogueEndRva = 0x479E41;
constexpr std::uintptr_t Packet9DProducerEntryRva = 0x479EA0;
constexpr std::uintptr_t Packet9DProducerContinuationRva = 0x479EA5;
constexpr std::uintptr_t Packet9DProducerEpilogueEndRva = 0x47A037;
constexpr std::uintptr_t Packet9CQueueCallRva = 0x479E10;
constexpr std::uintptr_t Packet9DQueueCallRva = 0x47A001;
constexpr std::uintptr_t NativeFullItemPacketQueueRva = 0x4817F0;
static_assert(
    NativeFullItemPacketQueueRva == FullItemTransportQueueEntryRva);
constexpr std::uintptr_t NativeAuxiliaryReaderRva = 0x530A00;
constexpr std::uintptr_t NativePlayerReaderRva = 0x533760;
constexpr std::uintptr_t NativePlayerPreviewCopyRva = 0xA1E110;
constexpr std::uintptr_t NativeByteBufferResizeRva = 0xA1E1F0;
constexpr std::uintptr_t NativeSetObjectStateRva = 0xA1E200;
constexpr std::uintptr_t NativeSetObjectAuxRva = 0xA1E210;
constexpr std::uintptr_t NativeIoSuccessCodeRva = 0x1E9C0E8;

constexpr std::size_t RecordsPointerOffset = 0x1258;
constexpr std::size_t RecordCountOffset = 0x1260;
constexpr std::size_t DescriptionVectorOffset = 0x1278;
constexpr std::size_t RecordStride = CompiledItemStatRecordStride;
constexpr std::size_t DescriptionPriorityOffset = 0x30;
constexpr std::size_t DescriptionFunctionOffset = 0x32;
constexpr std::size_t SaveObjectBufferPointerOffset = 0x08;
constexpr std::size_t SaveObjectBufferSizeOffset = 0x10;
constexpr std::size_t SaveObjectNamePointerOffset = 0x20;
constexpr std::size_t MaximumNativeStoreNameLength = 255;
constexpr std::int32_t NativeMalformedPlayerStatStatus = 0x12;

constexpr auto TailExpected = std::to_array<std::uint8_t>({
    0x4C, 0x8B, 0xBC, 0x24, 0x40, 0x0F, 0x00, 0x00,
});
constexpr auto CountImmediateExpected = std::to_array<std::uint8_t>({
    0xFF, 0x01, 0x00, 0x00,
});

constexpr std::size_t RelayStateHandlerOffset = 0x10;
constexpr std::size_t RelayStateVanillaOffset = 0x18;
constexpr std::size_t RelayStateEpilogueOffset = 0x20;
constexpr std::size_t MaximumRelayTemplateSize = 512;
constexpr std::size_t MaximumPersistenceRelayTemplateSize = 1024;
constexpr ULONGLONG ShutdownRundownTimeoutMilliseconds = 5000;
constexpr std::uint64_t NativeVectorCapacityMask =
    UINT64_C(0x7FFFFFFFFFFFFFFF);

struct RelayState {
    volatile LONG activeCallbacks{};
    volatile LONG operational{};
    volatile LONG capMayBeExtended{};
    LONG reserved{};
    void* handler{};
    void* vanillaContinuation{};
    void* epilogue{};
};

struct PersistenceRelayState {
    volatile LONG activeCallbacks{};
    volatile LONG operational{};
    volatile LONG codecReady{};
    LONG reserved{};
    void* readerHandler{};
    void* writerHandler{};
    void* readerContinue{};
    void* readerRejected{};
    void* writerVanilla{};
    void* writerCommitted{};
    void* writerRejected{};
    void* auxiliaryReaderHandler{};
    void* playerReaderHandler{};
    void* playerPreviewHandler{};
    volatile LONG itemTransportReady{};
    LONG itemTransportReserved{};
    void* packet9CProducerHandler{};
    void* packet9DProducerHandler{};
    void* packet9CQueueHandler{};
    void* packet9DQueueHandler{};
    void* packet9CTrampoline{};
    void* packet9DTrampoline{};
    void* nativeFullItemPacketQueue{};
    RUNTIME_FUNCTION itemTrampolineRuntimeFunctions[2]{};
    volatile LONG itemTrampolineFunctionTableRegistered{};
};

struct NativeVectorU16 {
    std::uint16_t* begin{};
    std::uint64_t size{};
    std::uint64_t capacityAndFlags{};
};

using NativeComparatorFn = int(__cdecl*)(const void*, const void*);
using NativeQsortFn = void(__cdecl*)(
    void*, std::size_t, std::size_t, NativeComparatorFn);
using NativeVectorResizeFn = void(__fastcall*)(void*, std::size_t);
using NativeGetLinkNameCountFn = std::uint64_t(__fastcall*)(const void*);
using NativeGetLinkNameFn = const char*(__fastcall*)(
    const void*, std::int32_t, std::int32_t);
using NativeByteBufferResizeFn = void(__fastcall*)(void*, std::size_t);
using NativeSetObjectStateFn = void(__fastcall*)(void*, std::uint32_t);
using NativeSetObjectAuxFn = void(__fastcall*)(void*, std::uint64_t);
using NativePlayerStatReaderFn = std::int32_t(__fastcall*)(
    void*,
    void*,
    std::uint8_t**,
    std::uint8_t*,
    std::uint32_t,
    std::int32_t);
using NativePlayerPreviewCopyFn = std::uint32_t(__fastcall*)(
    void*, void*, std::uint32_t, std::uint64_t);
using NativeItemAction9CFn = void(__fastcall*)(
    void*, void*, std::uint8_t, std::uint32_t, std::uint32_t);
using NativeItemAction9DFn = void(__fastcall*)(
    void*, void*, void*, std::uint8_t, std::uint32_t, std::uint32_t);
using NativeFullItemPacketQueueFn = void(__fastcall*)(
    void*, const std::uint8_t*, std::size_t);

static_assert(sizeof(DescriptionEntry) == 4);
static_assert(offsetof(DescriptionEntry, statId) == 0);
static_assert(offsetof(DescriptionEntry, priority) == 2);
static_assert(offsetof(RelayState, capMayBeExtended) == 8);
static_assert(offsetof(RelayState, handler) == RelayStateHandlerOffset);
static_assert(
    offsetof(RelayState, vanillaContinuation) == RelayStateVanillaOffset);
static_assert(offsetof(RelayState, epilogue) == RelayStateEpilogueOffset);
static_assert(offsetof(PersistenceRelayState, codecReady) == 8);
static_assert(offsetof(PersistenceRelayState, readerHandler) == 0x10);
static_assert(offsetof(PersistenceRelayState, writerHandler) == 0x18);
static_assert(offsetof(PersistenceRelayState, readerContinue) == 0x20);
static_assert(offsetof(PersistenceRelayState, readerRejected) == 0x28);
static_assert(offsetof(PersistenceRelayState, writerVanilla) == 0x30);
static_assert(offsetof(PersistenceRelayState, writerCommitted) == 0x38);
static_assert(offsetof(PersistenceRelayState, writerRejected) == 0x40);
static_assert(
    offsetof(PersistenceRelayState, auxiliaryReaderHandler) == 0x48);
static_assert(offsetof(PersistenceRelayState, playerReaderHandler) == 0x50);
static_assert(offsetof(PersistenceRelayState, playerPreviewHandler) == 0x58);
static_assert(offsetof(PersistenceRelayState, itemTransportReady) == 0x60);
static_assert(offsetof(PersistenceRelayState, packet9CProducerHandler) == 0x68);
static_assert(offsetof(PersistenceRelayState, packet9DProducerHandler) == 0x70);
static_assert(offsetof(PersistenceRelayState, packet9CQueueHandler) == 0x78);
static_assert(offsetof(PersistenceRelayState, packet9DQueueHandler) == 0x80);
static_assert(offsetof(PersistenceRelayState, packet9CTrampoline) == 0x88);
static_assert(offsetof(PersistenceRelayState, packet9DTrampoline) == 0x90);
static_assert(
    offsetof(PersistenceRelayState, nativeFullItemPacketQueue) == 0x98);
static_assert(offsetof(
    PersistenceRelayState, itemTrampolineRuntimeFunctions) == 0xA0);
static_assert(offsetof(
    PersistenceRelayState, itemTrampolineFunctionTableRegistered) == 0xB8);
static_assert(sizeof(NativeVectorU16) == 24);
static_assert(offsetof(NativeVectorU16, begin) == 0);
static_assert(offsetof(NativeVectorU16, size) == 8);
static_assert(offsetof(NativeVectorU16, capacityAndFlags) == 16);
static_assert(RecordStride == 0x144);
static_assert(std::is_nothrow_move_assignable_v<
    NativeItemStatCostSchemaSnapshot>);

const D2RL::PluginContext* LoaderContext{};
std::uint8_t* LoaderBase{};
std::size_t LoaderImageSize{};
void* RelayPage{};
std::size_t RelayPageSize{};
RelayState* State{};
std::size_t StatePageSize{};
void* PersistenceRelayPage{};
std::size_t PersistenceRelayPageSize{};
PersistenceRelayState* PersistenceState{};
std::size_t PersistenceStatePageSize{};
void* PersistenceReaderRelayEntry{};
void* PersistenceWriterRelayEntry{};
void* PersistencePlayerSaveFinalizeRelayEntry{};
void* PersistenceAuxiliaryReaderRelayEntry{};
void* PersistencePlayerReaderRelayEntry{};
void* PersistencePlayerPreviewRelayEntry{};
void* PersistencePacket9CQueueRelayEntry{};
void* PersistencePacket9DQueueRelayEntry{};
void* PersistencePacket9CProducerRelayEntry{};
void* PersistencePacket9DProducerRelayEntry{};
NativeItemAction9CFn PersistencePacket9CTrampoline{};
NativeItemAction9DFn PersistencePacket9DTrampoline{};
CodecPatchActivationTargets PreparedCodecActivationTargets{};
NativeQsortFn NativeQsort{};
NativeComparatorFn NativeComparator{};
NativeVectorResizeFn NativeVectorResize{};
NativeGetLinkNameCountFn NativeGetLinkNameCount{};
NativeGetLinkNameFn NativeGetLinkName{};
NativeByteBufferResizeFn NativeByteBufferResize{};
NativeSetObjectStateFn NativeSetObjectState{};
NativeSetObjectAuxFn NativeSetObjectAux{};
NativePlayerStatReaderFn NativeAuxiliaryReader{};
NativePlayerStatReaderFn NativePlayerReader{};
NativePlayerPreviewCopyFn NativePlayerPreviewCopy{};
NativeFullItemPacketQueueFn NativeFullItemPacketQueue{};
FullItemTransportProvider PreparedFullItemTransportProvider{
    FullItemTransportProvider::Invalid};
InspectFullItemTransportProviderFn InspectFullItemTransportProvider{};
std::atomic<FullItemTransportProvider> ActiveFullItemTransportProvider{
    FullItemTransportProvider::Invalid};
bool Prepared{};
bool PersistencePrepared{};
bool AnyMutationInstalled{};
bool TailPatchInstalled{};
bool CapPatchInstalled{};
bool PersistenceReaderPatchInstalled{};
bool PersistenceWriterPatchInstalled{};
bool ColdRestartRequired{};
bool DiagnosticsEnabled{};
PublicationAdapterSet PreparedPublicationAdapters{};
std::atomic_uint64_t BuildCalls{};
std::atomic_uint64_t LastRowCount{};
std::atomic_uint64_t LastDescriptionCount{};
std::atomic_uint64_t FullItemRoot9C{};
std::atomic_uint64_t FullItemRoot9D{};
std::atomic_uint64_t FullItemTransactionsAccepted{};
std::atomic_uint64_t FullItemTransactionsRejected{};
std::atomic_uint64_t FullItemPacketsCaptured9C{};
std::atomic_uint64_t FullItemPacketsCaptured9D{};
std::atomic_uint64_t FullItemPacketsQueued{};
std::atomic_uint64_t PersistenceReadsAccepted{};
std::atomic_uint64_t PersistenceReadsRejected{};
std::atomic_uint64_t PlayerPreviewsAcceptedBeforeSchema{};
std::atomic_uint64_t PersistenceWritesDelegated{};
std::atomic_uint64_t PersistenceWritesRejected{};
std::atomic_uint64_t FullItemDiagnosticLines{};
std::atomic_bool SchemaReady{};
SRWLOCK SchemaSnapshotLock = SRWLOCK_INIT;
NativeItemStatCostSchemaSnapshot PublishedSchemaSnapshot;
NativeSchemaCandidateSet PendingSchemaCandidates;
bool HasPublishedSchemaSnapshot{};
bool SchemaUpdateInProgress{};

// Persistence hooks may run while DataTablesLoaded is attempting to replace
// the authoritative schema. They already execute under native SaveObject
// synchronization, so waiting for our exclusive reload lock could create a
// lock-order cycle. Acquire shared ownership only when immediately available,
// then retain it through the complete buffer replacement or atomic commit.
class PublishedSchemaReadLease final {
public:
    PublishedSchemaReadLease() noexcept = default;
    PublishedSchemaReadLease(const PublishedSchemaReadLease&) = delete;
    PublishedSchemaReadLease(PublishedSchemaReadLease&&) = delete;
    auto operator=(const PublishedSchemaReadLease&)
        -> PublishedSchemaReadLease& = delete;
    auto operator=(PublishedSchemaReadLease&&)
        -> PublishedSchemaReadLease& = delete;

    ~PublishedSchemaReadLease() noexcept {
        Release();
    }

    [[nodiscard]] auto TryAcquire() noexcept -> bool {
        if (held_ || !TryAcquireSRWLockShared(&SchemaSnapshotLock)) {
            return false;
        }
        held_ = true;
        if (!SchemaReady.load(std::memory_order_acquire)
                || !HasPublishedSchemaSnapshot
                || SchemaUpdateInProgress) {
            Release();
            return false;
        }
        return true;
    }

private:
    auto Release() noexcept -> void {
        if (!held_) return;
        held_ = false;
        ReleaseSRWLockShared(&SchemaSnapshotLock);
    }

    bool held_{};
};

static_assert(!std::is_copy_constructible_v<PublishedSchemaReadLease>);
static_assert(!std::is_copy_assignable_v<PublishedSchemaReadLease>);
static_assert(!std::is_move_constructible_v<PublishedSchemaReadLease>);
static_assert(!std::is_move_assignable_v<PublishedSchemaReadLease>);

thread_local bool NativeVectorMutationStarted{};
thread_local bool CurrentRowCountKnown{};
thread_local std::uint64_t CurrentRowCount{};
thread_local bool NativeSchemaCallbackFailed{};
thread_local std::array<char, MaximumNativeStatNameLength + 1>
    NativeSchemaNameBuffer{};
thread_local std::array<std::uint8_t, MaximumPlayerStatSectionBytes>
    PlayerStatPreflightBuffer{};
thread_local std::array<std::uint8_t, PlayerPreviewBufferCapacity>
    PlayerPreviewPreflightBuffer{};
thread_local FullItemPacketStagingContext FullItemPacketTransaction{};

auto SetError(std::string& error, std::string_view message) noexcept -> bool {
    try {
        error.assign(message.data(), message.size());
    } catch (...) {
        // Preserve the loader ABI if even diagnostic allocation fails.
    }
    return false;
}

auto IsAccessibleRange(
        const void* address,
        std::size_t size,
        bool requireWrite,
        bool requireExecute = false) noexcept -> bool {
    if (!address || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    const auto end = start + size;
    auto cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(cursor),
                &memory,
                sizeof(memory)) != sizeof(memory)
                || memory.State != MEM_COMMIT
                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const auto protection = memory.Protect & 0xFFU;
        const bool readable = protection == PAGE_READONLY
            || protection == PAGE_READWRITE
            || protection == PAGE_WRITECOPY
            || protection == PAGE_EXECUTE_READ
            || protection == PAGE_EXECUTE_READWRITE
            || protection == PAGE_EXECUTE_WRITECOPY;
        const bool writable = protection == PAGE_READWRITE
            || protection == PAGE_WRITECOPY
            || protection == PAGE_EXECUTE_READWRITE
            || protection == PAGE_EXECUTE_WRITECOPY;
        const bool executable = protection == PAGE_EXECUTE
            || protection == PAGE_EXECUTE_READ
            || protection == PAGE_EXECUTE_READWRITE
            || protection == PAGE_EXECUTE_WRITECOPY;
        if (!readable || (requireWrite && !writable)
                || (requireExecute && !executable)) {
            return false;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(
            memory.BaseAddress);
        if (regionStart > std::numeric_limits<std::uintptr_t>::max()
                - memory.RegionSize) {
            return false;
        }
        const auto regionEnd = regionStart + memory.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = std::min(regionEnd, end);
    }
    return true;
}

auto NativeExceptionFilter(DWORD code) noexcept -> int {
    return code == EXCEPTION_ACCESS_VIOLATION
            || code == EXCEPTION_IN_PAGE_ERROR
            || code == EXCEPTION_GUARD_PAGE
        ? EXCEPTION_EXECUTE_HANDLER
        : EXCEPTION_CONTINUE_SEARCH;
}

auto GuardedCodecExceptionFilter(DWORD) noexcept -> int {
    // No exception may cross the DLL FRAME wrapper back into a native caller.
    // The guarded helper releases any held schema lock and fast-fails instead.
    return EXCEPTION_EXECUTE_HANDLER;
}

template <typename Value>
auto SafeRead(const void* source, Value& destination) noexcept -> bool {
    static_assert(std::is_trivially_copyable_v<Value>);
    if (!IsAccessibleRange(source, sizeof(Value), false)) return false;
    __try {
        std::memcpy(&destination, source, sizeof(Value));
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto SafeCopyReadable(
        const void* source,
        void* destination,
        std::size_t size) noexcept -> bool {
    if (size == 0) return true;
    if (!destination || !IsAccessibleRange(source, size, false)) return false;
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto ApplySignedDisplacement(
        std::uintptr_t next,
        std::int32_t displacement,
        std::uintptr_t& target) noexcept -> bool {
    const auto signedDisplacement = static_cast<std::int64_t>(displacement);
    if (signedDisplacement >= 0) {
        const auto distance = static_cast<std::uintptr_t>(
            signedDisplacement);
        if (next > (std::numeric_limits<std::uintptr_t>::max)() - distance) {
            return false;
        }
        target = next + distance;
        return true;
    }
    const auto distance = static_cast<std::uintptr_t>(
        -signedDisplacement);
    if (next < distance) return false;
    target = next - distance;
    return true;
}

auto ReadUnconditionalJumpTarget(
        std::uintptr_t instruction,
        std::uintptr_t& target) noexcept -> bool {
    std::array<std::uint8_t, 2> opcode{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(instruction),
            opcode.data(),
            opcode.size())) {
        return false;
    }

    std::int32_t displacement{};
    std::uintptr_t next{};
    if (opcode[0] == 0xE9U) {
        if (instruction
                > (std::numeric_limits<std::uintptr_t>::max)() - 5U
                || !SafeRead(
                    reinterpret_cast<const void*>(instruction + 1U),
                    displacement)) {
            return false;
        }
        next = instruction + 5U;
        if (!ApplySignedDisplacement(next, displacement, target)) {
            return false;
        }
    } else if (opcode[0] == 0xFFU && opcode[1] == 0x25U) {
        if (instruction
                > (std::numeric_limits<std::uintptr_t>::max)() - 6U
                || !SafeRead(
                    reinterpret_cast<const void*>(instruction + 2U),
                    displacement)) {
            return false;
        }
        next = instruction + 6U;
        std::uintptr_t slot{};
        if (!ApplySignedDisplacement(next, displacement, slot)
                || !SafeRead(
                    reinterpret_cast<const void*>(slot), target)) {
            return false;
        }
    } else {
        return false;
    }
    return target != 0U
        && IsAccessibleRange(
            reinterpret_cast<const void*>(target), 1U, false, true);
}

template <std::size_t Size>
auto ValidateD2RCoreNativeCompilerForwarder(
        const std::uint8_t* d2rBase,
        std::uintptr_t implementation,
        std::uintptr_t forwarderOffset,
        const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    if (!d2rBase || Size < 7U
            || implementation
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - forwarderOffset) {
        return false;
    }
    const auto forwarder = implementation + forwarderOffset;
    std::array<std::uint8_t, Size> live{};
    if (!IsAccessibleRange(
            reinterpret_cast<const void*>(forwarder),
            live.size(),
            false,
            true)
            || !SafeCopyReadable(
                reinterpret_cast<const void*>(forwarder),
                live.data(),
                live.size())
            || std::memcmp(live.data(), expected.data(), 3U) != 0
            || std::memcmp(
                live.data() + 7U,
                expected.data() + 7U,
                Size - 7U) != 0) {
        return false;
    }

    std::int32_t slotDisplacement{};
    std::memcpy(
        &slotDisplacement, live.data() + 3U, sizeof(slotDisplacement));
    std::uintptr_t originalSlot{};
    if (!ApplySignedDisplacement(
            forwarder + 7U, slotDisplacement, originalSlot)) {
        return false;
    }
    std::uintptr_t originalCompiler{};
    return SafeRead(
            reinterpret_cast<const void*>(originalSlot), originalCompiler)
        && originalCompiler
            == reinterpret_cast<std::uintptr_t>(d2rBase)
                + NativeGenericCompileRva;
}

auto ValidateD2RCoreLoadExcelProviderAbi(
        const std::uint8_t* d2rBase,
        HMODULE core,
        std::uintptr_t loadExcelExport) noexcept -> bool {
    constexpr auto ExportBytes = std::to_array<std::uint8_t>({
        0x48,0x83,0xEC,0x48,0x0F,0x28,0x44,0x24,0x70,0x48,
        0x8B,0x84,0x24,0x80,0x00,0x00,0x00,0x48,0x89,0x44,
        0x24,0x30,0x0F,0x11,0x44,0x24,0x20,0xC6,0x44,0x24,
        0x38,0x00,0xE8,0x00,0x00,0x00,0x00,0x90,0x48,0x83,
        0xC4,0x48,0xC3,
    });
    constexpr auto NativeForwarder12 = std::to_array<std::uint8_t>({
        0x48,0x8B,0x05,0x00,0x00,0x00,0x00,0x48,0x8B,0x8D,
        0x70,0x13,0x00,0x00,0x48,0x89,0x4C,0x24,0x30,0x48,
        0x8B,0x8D,0x68,0x13,0x00,0x00,0x48,0x89,0x4C,0x24,
        0x28,0x48,0x8B,0x8D,0x60,0x13,0x00,0x00,0x48,0x89,
        0x4C,0x24,0x20,0x0F,0xB6,0x8D,0xC7,0x12,0x00,0x00,
        0x48,0x8B,0x95,0xB0,0x12,0x00,0x00,0x4C,0x8B,0x85,
        0xB8,0x12,0x00,0x00,0x4C,0x8B,0x8D,0xA8,0x12,0x00,
        0x00,0xFF,0xD0,
    });
    constexpr std::uintptr_t NativeForwarder12Offset = 0x935U;
    constexpr auto NativeForwarder11 = std::to_array<std::uint8_t>({
        0x48,0x8B,0x05,0x00,0x00,0x00,0x00,0x48,0x8B,0x8D,
        0xC0,0x12,0x00,0x00,0x48,0x89,0x4C,0x24,0x30,0x48,
        0x8B,0x8D,0xB8,0x12,0x00,0x00,0x48,0x89,0x4C,0x24,
        0x28,0x48,0x8B,0x8D,0xB0,0x12,0x00,0x00,0x48,0x89,
        0x4C,0x24,0x20,0x0F,0xB6,0x8D,0x17,0x12,0x00,0x00,
        0x48,0x8B,0x95,0x00,0x12,0x00,0x00,0x4C,0x8B,0x85,
        0x08,0x12,0x00,0x00,0x4C,0x8B,0x8D,0xF8,0x11,0x00,
        0x00,0xFF,0xD0,
    });
    constexpr std::uintptr_t NativeForwarder11Offset = 0x901U;
    constexpr std::size_t CallOpcodeOffset = 32U;
    static_assert(ExportBytes[CallOpcodeOffset] == 0xE8U);
    if (!core || loadExcelExport == 0U
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(loadExcelExport),
                ExportBytes.size(),
                false,
                true)) {
        return false;
    }
    std::array<std::uint8_t, ExportBytes.size()> live{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(loadExcelExport),
            live.data(),
            live.size())
            || std::memcmp(
                live.data(), ExportBytes.data(), CallOpcodeOffset + 1U) != 0
            || std::memcmp(
                live.data() + CallOpcodeOffset + 5U,
                ExportBytes.data() + CallOpcodeOffset + 5U,
                ExportBytes.size() - CallOpcodeOffset - 5U) != 0) {
        return false;
    }

    std::int32_t displacement{};
    std::memcpy(
        &displacement,
        live.data() + CallOpcodeOffset + 1U,
        sizeof(displacement));
    std::uintptr_t implementation{};
    if (!ApplySignedDisplacement(
            loadExcelExport + CallOpcodeOffset + 5U,
            displacement,
            implementation)
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(implementation),
                1U,
                false,
                true)) {
        return false;
    }

    DWORD64 functionImageBase{};
    const auto* liveFunction = RtlLookupFunctionEntry(
        static_cast<DWORD64>(implementation), &functionImageBase, nullptr);
    RUNTIME_FUNCTION function{};
    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (!liveFunction
            || functionImageBase != static_cast<DWORD64>(coreBase)
            || implementation < coreBase
            || implementation - coreBase
                > (std::numeric_limits<DWORD>::max)()
            || !SafeRead(liveFunction, function)
            || function.BeginAddress != implementation - coreBase
            || function.EndAddress <= implementation - coreBase) {
        return false;
    }
    const auto functionSize = static_cast<std::uintptr_t>(
        function.EndAddress - function.BeginAddress);
    const auto validates = [&]<std::size_t Size>(
            std::uintptr_t offset,
            const std::array<std::uint8_t, Size>& expected) noexcept {
        return offset <= functionSize
            && Size <= functionSize - offset
            && ValidateD2RCoreNativeCompilerForwarder(
                d2rBase, implementation, offset, expected);
    };
    return validates(NativeForwarder12Offset, NativeForwarder12)
        || validates(NativeForwarder11Offset, NativeForwarder11);
}

template <std::size_t ProviderSize>
auto ValidateD2RCoreSaveStatWriterAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t writerExport,
        const std::array<std::uint8_t, ProviderSize>& providerBytes12,
        const std::array<std::uint8_t, ProviderSize>& providerBytes11,
        std::uintptr_t providerRva12,
        std::uintptr_t providerRva11,
        DWORD unwindRva12,
        DWORD unwindRva11,
        std::size_t forwardCallOffset) noexcept -> bool {
    constexpr auto UnwindBytes12 = std::to_array<std::uint8_t>({
        0x19,0x0A,0x03,0x35,0x0A,0x03,0x05,0x52,
        0x01,0x50,0x00,0x00,0xB0,0x43,0x3C,0x00,
    });
    constexpr auto UnwindBytes11 = std::to_array<std::uint8_t>({
        0x19,0x0A,0x03,0x35,0x0A,0x03,0x05,0x52,
        0x01,0x50,0x00,0x00,0x80,0x97,0x32,0x00,
    });
    constexpr auto NativeWriterEntryBytes = std::to_array<std::uint8_t>({
        0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,
        0x4C,0x8B,0x49,0x18,0x48,0x8B,0xD9,0x48,0x8B,0x41,
        0x10,0x44,0x8B,0xD2,0x48,0x8B,0x51,0x08,0x49,0x8D,
        0x0C,0xC1,
    });
    if (!d2rBase || !core || writerExport == 0U
            || forwardCallOffset > ProviderSize
            || ProviderSize - forwardCallOffset < 6U
            || providerBytes12[forwardCallOffset] != 0xFFU
            || providerBytes12[forwardCallOffset + 1U] != 0x15U
            || providerBytes11[forwardCallOffset] != 0xFFU
            || providerBytes11[forwardCallOffset + 1U] != 0x15U
            || NativeBitWriterRva > d2rImageSize
            || NativeWriterEntryBytes.size()
                > d2rImageSize - NativeBitWriterRva
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(writerExport),
                ProviderSize,
                false,
                true)) {
        return false;
    }

    std::array<std::uint8_t, ProviderSize> liveProvider{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(writerExport),
            liveProvider.data(),
            liveProvider.size())) {
        return false;
    }
    const bool provider12 = liveProvider == providerBytes12;
    const bool provider11 = liveProvider == providerBytes11;
    if (!provider12 && !provider11) return false;

    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    const auto providerRva = provider12 ? providerRva12 : providerRva11;
    const auto unwindRva = provider12 ? unwindRva12 : unwindRva11;
    DWORD64 functionImageBase{};
    const auto* liveFunction = RtlLookupFunctionEntry(
        static_cast<DWORD64>(writerExport), &functionImageBase, nullptr);
    RUNTIME_FUNCTION function{};
    if (!liveFunction
            || functionImageBase != static_cast<DWORD64>(coreBase)
            || writerExport < coreBase
            || writerExport - coreBase
                > (std::numeric_limits<DWORD>::max)()
            || !SafeRead(liveFunction, function)
            || writerExport - coreBase != providerRva
            || function.BeginAddress != providerRva
            || function.EndAddress - function.BeginAddress
                != ProviderSize
            || function.UnwindData != unwindRva) {
        return false;
    }
    if (coreBase > (std::numeric_limits<std::uintptr_t>::max)()
            - function.UnwindData) {
        return false;
    }
    std::array<std::uint8_t, UnwindBytes12.size()> liveUnwind{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(coreBase + function.UnwindData),
            liveUnwind.data(),
            liveUnwind.size())
            || liveUnwind != (provider12 ? UnwindBytes12 : UnwindBytes11)) {
        return false;
    }

    std::int32_t slotDisplacement{};
    std::memcpy(
        &slotDisplacement,
        liveProvider.data() + forwardCallOffset + 2U,
        sizeof(slotDisplacement));
    std::uintptr_t nativeWriterSlot{};
    if (!ApplySignedDisplacement(
            writerExport + forwardCallOffset + 6U,
            slotDisplacement,
            nativeWriterSlot)) {
        return false;
    }
    std::uintptr_t nativeWriter{};
    const auto expectedNativeWriter =
        reinterpret_cast<std::uintptr_t>(d2rBase) + NativeBitWriterRva;
    if (!SafeRead(
            reinterpret_cast<const void*>(nativeWriterSlot), nativeWriter)
            || nativeWriter != expectedNativeWriter
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(nativeWriter),
                NativeWriterEntryBytes.size(),
                false,
                true)) {
        return false;
    }
    std::array<std::uint8_t, NativeWriterEntryBytes.size()> liveWriter{};
    return SafeCopyReadable(
            reinterpret_cast<const void*>(nativeWriter),
            liveWriter.data(),
            liveWriter.size())
        && liveWriter == NativeWriterEntryBytes;
}

auto ValidateD2RCorePlayerSaveStatWriterAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t writerExport) noexcept -> bool {
    constexpr auto ProviderBytes12 = std::to_array<std::uint8_t>({
        0x55,0x48,0x83,0xEC,0x30,0x48,0x8D,0x6C,0x24,0x30,
        0x48,0xC7,0x45,0xF8,0xFE,0xFF,0xFF,0xFF,0x8B,0x05,
        0xF8,0x24,0xF1,0xFF,0x65,0x4C,0x8B,0x0C,0x25,0x58,
        0x00,0x00,0x00,0x49,0x8B,0x04,0xC1,0x48,0x8B,0x80,
        0x68,0x04,0x00,0x00,0x48,0x85,0xC0,0x41,0x0F,0x94,
        0xC1,0x81,0xFA,0x00,0x02,0x00,0x00,0x41,0x0F,0x93,
        0xC2,0x45,0x08,0xCA,0x75,0x1D,0x41,0x89,0xD2,0x41,
        0xBB,0x01,0x00,0x00,0x00,0x49,0x89,0xC9,0x89,0xD1,
        0x49,0xD3,0xE3,0x4C,0x89,0xC9,0x41,0xC1,0xEA,0x06,
        0x4E,0x09,0x5C,0xD0,0x40,0xFF,0x15,0x23,0x0D,0xF0,
        0xFF,0x90,0x48,0x83,0xC4,0x30,0x5D,0xC3,
    });
    constexpr auto ProviderBytes11 = std::to_array<std::uint8_t>({
        0x55,0x48,0x83,0xEC,0x30,0x48,0x8D,0x6C,0x24,0x30,
        0x48,0xC7,0x45,0xF8,0xFE,0xFF,0xFF,0xFF,0x8B,0x05,
        0x38,0xE8,0xF1,0xFF,0x65,0x4C,0x8B,0x0C,0x25,0x58,
        0x00,0x00,0x00,0x49,0x8B,0x04,0xC1,0x48,0x8B,0x80,
        0x18,0x04,0x00,0x00,0x48,0x85,0xC0,0x41,0x0F,0x94,
        0xC1,0x81,0xFA,0x00,0x02,0x00,0x00,0x41,0x0F,0x93,
        0xC2,0x45,0x08,0xCA,0x75,0x1D,0x41,0x89,0xD2,0x41,
        0xBB,0x01,0x00,0x00,0x00,0x49,0x89,0xC9,0x89,0xD1,
        0x49,0xD3,0xE3,0x4C,0x89,0xC9,0x41,0xC1,0xEA,0x06,
        0x4E,0x09,0x5C,0xD0,0x40,0xFF,0x15,0xEB,0xB7,0xF1,
        0xFF,0x90,0x48,0x83,0xC4,0x30,0x5D,0xC3,
    });
    constexpr std::size_t ForwardCallOffset = 0x5FU;
    static_assert(ProviderBytes12.size() == 0x6CU);
    static_assert(ProviderBytes11.size() == ProviderBytes12.size());
    return ValidateD2RCoreSaveStatWriterAbi(
        d2rBase,
        d2rImageSize,
        core,
        writerExport,
        ProviderBytes12,
        ProviderBytes11,
        0x636550U,
        0x5655B0U,
        0x50F470U,
        0x4528BCU,
        ForwardCallOffset);
}

auto ValidateD2RCoreItemSaveStatWriterAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t writerExport) noexcept -> bool {
    constexpr auto ProviderBytes12 = std::to_array<std::uint8_t>({
        0x55,0x48,0x83,0xEC,0x30,0x48,0x8D,0x6C,0x24,0x30,
        0x48,0xC7,0x45,0xF8,0xFE,0xFF,0xFF,0xFF,0x8B,0x05,
        0x88,0x25,0xF1,0xFF,0x65,0x4C,0x8B,0x0C,0x25,0x58,
        0x00,0x00,0x00,0x49,0x8B,0x04,0xC1,0x48,0x8B,0x80,
        0x68,0x04,0x00,0x00,0x48,0x85,0xC0,0x41,0x0F,0x94,
        0xC1,0x81,0xFA,0x00,0x02,0x00,0x00,0x41,0x0F,0x93,
        0xC2,0x45,0x08,0xCA,0x75,0x1C,0x41,0x89,0xD2,0x41,
        0xBB,0x01,0x00,0x00,0x00,0x49,0x89,0xC9,0x89,0xD1,
        0x49,0xD3,0xE3,0x4C,0x89,0xC9,0x41,0xC1,0xEA,0x06,
        0x4E,0x09,0x1C,0xD0,0xFF,0x15,0xB4,0x0D,0xF0,0xFF,
        0x90,0x48,0x83,0xC4,0x30,0x5D,0xC3,
    });
    constexpr auto ProviderBytes11 = std::to_array<std::uint8_t>({
        0x55,0x48,0x83,0xEC,0x30,0x48,0x8D,0x6C,0x24,0x30,
        0x48,0xC7,0x45,0xF8,0xFE,0xFF,0xFF,0xFF,0x8B,0x05,
        0xC8,0xE8,0xF1,0xFF,0x65,0x4C,0x8B,0x0C,0x25,0x58,
        0x00,0x00,0x00,0x49,0x8B,0x04,0xC1,0x48,0x8B,0x80,
        0x18,0x04,0x00,0x00,0x48,0x85,0xC0,0x41,0x0F,0x94,
        0xC1,0x81,0xFA,0x00,0x02,0x00,0x00,0x41,0x0F,0x93,
        0xC2,0x45,0x08,0xCA,0x75,0x1C,0x41,0x89,0xD2,0x41,
        0xBB,0x01,0x00,0x00,0x00,0x49,0x89,0xC9,0x89,0xD1,
        0x49,0xD3,0xE3,0x4C,0x89,0xC9,0x41,0xC1,0xEA,0x06,
        0x4E,0x09,0x1C,0xD0,0xFF,0x15,0x7C,0xB8,0xF1,0xFF,
        0x90,0x48,0x83,0xC4,0x30,0x5D,0xC3,
    });
    constexpr std::size_t ForwardCallOffset = 0x5EU;
    static_assert(ProviderBytes12.size() == 0x6BU);
    static_assert(ProviderBytes11.size() == ProviderBytes12.size());
    return ValidateD2RCoreSaveStatWriterAbi(
        d2rBase,
        d2rImageSize,
        core,
        writerExport,
        ProviderBytes12,
        ProviderBytes11,
        0x6364C0U,
        0x565520U,
        0x50F40CU,
        0x452858U,
        ForwardCallOffset);
}

auto ValidateD2RCorePlayerSaveProviderAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t providerExport) noexcept -> bool {
    constexpr std::uintptr_t ProviderRva12 = 0x634650U;
    constexpr std::size_t ProviderSize12 = 0x1A18U;
    constexpr DWORD ProviderUnwindRva12 = 0x50EFD0U;
    constexpr std::uintptr_t ProviderFuncInfoRva12 = 0x50F1B4U;
    constexpr std::uintptr_t NativeForwardSlotRva12 = 0x5372C0U;
    constexpr Sha256Digest ProviderHash12{
        0xA4,0xA0,0xE2,0xA5,0xE7,0x0A,0xEF,0xB6,
        0x13,0x01,0x67,0x39,0xE9,0x14,0x22,0x5C,
        0xEE,0x2A,0x20,0xBB,0x06,0xF1,0x31,0x97,
        0xCE,0xAC,0x18,0x2E,0x11,0x64,0x86,0x67,
    };
    constexpr auto UnwindBytes12 = std::to_array<std::uint8_t>({
        0x19,0x22,0x0D,0x85,0x22,0x68,0xB0,0x00,
        0x1B,0x03,0x13,0x01,0x63,0x01,0x0C,0x30,
        0x0B,0x70,0x0A,0x60,0x09,0xC0,0x07,0xD0,
        0x05,0xE0,0x03,0xF0,0x01,0x50,0x00,0x00,
        0xB0,0x43,0x3C,0x00,
        0xB4,0xF1,0x50,0x00,
    });
    constexpr auto FuncInfoBytes12 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x12,0x00,0x00,0x00,
        0xDC,0xF1,0x50,0x00,0x03,0x00,0x00,0x00,
        0x6C,0xF2,0x50,0x00,0x25,0x00,0x00,0x00,
        0xE4,0xF2,0x50,0x00,0xF8,0x0A,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });
    constexpr std::uintptr_t ProviderRva11 = 0x563D80U;
    constexpr std::size_t ProviderSize11 = 0x1383U;
    constexpr DWORD ProviderUnwindRva11 = 0x452480U;
    constexpr std::uintptr_t ProviderFuncInfoRva11 = 0x452648U;
    constexpr std::uintptr_t NativeForwardSlotRva11 = 0x480DE8U;
    constexpr Sha256Digest ProviderHash11{
        0x66,0xC6,0x1B,0xC1,0x67,0x83,0x75,0xC9,
        0xE3,0x73,0xFD,0x21,0x41,0xF4,0x09,0x24,
        0x4B,0x45,0x0D,0x14,0x11,0x90,0x6B,0x7F,
        0xF8,0xC2,0x7C,0x12,0x41,0xE6,0x9F,0x6A,
    };
    constexpr auto UnwindBytes11 = std::to_array<std::uint8_t>({
        0x19,0x22,0x0D,0x85,0x22,0x68,0x84,0x00,
        0x1B,0x03,0x13,0x01,0x0B,0x01,0x0C,0x30,
        0x0B,0x70,0x0A,0x60,0x09,0xC0,0x07,0xD0,
        0x05,0xE0,0x03,0xF0,0x01,0x50,0x00,0x00,
        0x80,0x97,0x32,0x00,
        0x48,0x26,0x45,0x00,
    });
    constexpr auto FuncInfoBytes11 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x11,0x00,0x00,0x00,
        0x70,0x26,0x45,0x00,0x03,0x00,0x00,0x00,
        0xF8,0x26,0x45,0x00,0x1D,0x00,0x00,0x00,
        0x70,0x27,0x45,0x00,0x38,0x08,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });
    constexpr auto NativePlayerSaveEntryBytes =
            std::to_array<std::uint8_t>({
        0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,
        0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xC8,
        0x7E,0xFF,0xFF,0xB8,0x38,0x82,0x00,0x00,
    });
    constexpr std::size_t MaximumProviderSize = ProviderSize12;
    static_assert(ProviderSize11 <= MaximumProviderSize);
    static_assert(UnwindBytes11.size() == UnwindBytes12.size());
    static_assert(UnwindBytes12.size() == 40U);
    static_assert(FuncInfoBytes11.size() == FuncInfoBytes12.size());

    if (!d2rBase || !core || providerExport == 0U
            || NativePlayerSaveRva > d2rImageSize
            || NativePlayerSaveEntryBytes.size()
                > d2rImageSize - NativePlayerSaveRva) {
        return false;
    }

    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (providerExport < coreBase) return false;
    const auto liveProviderRva = providerExport - coreBase;
    const bool provider12 = liveProviderRva == ProviderRva12;
    const bool provider11 = liveProviderRva == ProviderRva11;
    if (!provider12 && !provider11) return false;

    const auto providerSize = provider12
        ? ProviderSize12 : ProviderSize11;
    const auto providerUnwindRva = provider12
        ? ProviderUnwindRva12 : ProviderUnwindRva11;
    const auto providerFuncInfoRva = provider12
        ? ProviderFuncInfoRva12 : ProviderFuncInfoRva11;
    const auto nativeForwardSlotRva = provider12
        ? NativeForwardSlotRva12 : NativeForwardSlotRva11;
    const auto& expectedProviderHash = provider12
        ? ProviderHash12 : ProviderHash11;
    const auto& expectedUnwind = provider12
        ? UnwindBytes12 : UnwindBytes11;
    const auto& expectedFuncInfo = provider12
        ? FuncInfoBytes12 : FuncInfoBytes11;
    if (!IsAccessibleRange(
            reinterpret_cast<const void*>(providerExport),
            providerSize,
            false,
            true)) {
        return false;
    }
    std::array<std::uint8_t, MaximumProviderSize> liveProvider{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(providerExport),
            liveProvider.data(),
            providerSize)) {
        return false;
    }
    Sha256Digest liveProviderHash{};
    if (!CalculateSha256(
            std::span<const std::uint8_t>{
                liveProvider.data(), providerSize},
            liveProviderHash)
            || liveProviderHash != expectedProviderHash) {
        return false;
    }

    DWORD64 functionImageBase{};
    const auto* liveFunction = RtlLookupFunctionEntry(
        static_cast<DWORD64>(providerExport), &functionImageBase, nullptr);
    RUNTIME_FUNCTION function{};
    if (!liveFunction
            || functionImageBase != static_cast<DWORD64>(coreBase)
            || !SafeRead(liveFunction, function)
            || function.BeginAddress != liveProviderRva
            || function.EndAddress - function.BeginAddress != providerSize
            || function.UnwindData != providerUnwindRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - providerUnwindRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - providerFuncInfoRva) {
        return false;
    }
    std::array<std::uint8_t, UnwindBytes12.size()> liveUnwind{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(coreBase + providerUnwindRva),
            liveUnwind.data(),
            liveUnwind.size())
            || liveUnwind != expectedUnwind) {
        return false;
    }
    std::array<std::uint8_t, FuncInfoBytes12.size()> liveFuncInfo{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(coreBase + providerFuncInfoRva),
            liveFuncInfo.data(),
            liveFuncInfo.size())
            || liveFuncInfo != expectedFuncInfo) {
        return false;
    }

    if (coreBase > (std::numeric_limits<std::uintptr_t>::max)()
            - nativeForwardSlotRva) {
        return false;
    }
    std::uintptr_t nativeForward{};
    const auto expectedNativeForward =
        reinterpret_cast<std::uintptr_t>(d2rBase) + NativePlayerSaveRva;
    if (!SafeRead(
            reinterpret_cast<const void*>(
                coreBase + nativeForwardSlotRva),
            nativeForward)
            || nativeForward != expectedNativeForward
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(nativeForward),
                NativePlayerSaveEntryBytes.size(),
                false,
                true)) {
        return false;
    }
    std::array<std::uint8_t, NativePlayerSaveEntryBytes.size()> liveNative{};
    return SafeCopyReadable(
            reinterpret_cast<const void*>(nativeForward),
            liveNative.data(),
            liveNative.size())
        && liveNative == NativePlayerSaveEntryBytes;
}

auto ValidateD2RCoreReadItemsByVersionAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t providerExport) noexcept -> bool {
    constexpr std::uintptr_t ProviderRva12 = 0x63BE60U;
    constexpr std::size_t ProviderSize12 = 0x126U;
    constexpr DWORD ProviderUnwindRva12 = 0x5115C4U;
    constexpr std::uintptr_t ProviderFuncInfoRva12 = 0x511600U;
    constexpr std::uintptr_t NativeForwardSlotRva12 = 0x537338U;
    constexpr Sha256Digest ProviderHash12{
        0xD2,0x19,0xFE,0xC9,0xF7,0x3D,0x25,0x45,
        0x7A,0xAB,0xF6,0xFF,0x6A,0xD8,0x68,0xF0,
        0xB5,0x80,0x58,0xC9,0x4A,0xD7,0x15,0xD8,
        0xF9,0x62,0x4E,0x05,0x2F,0x85,0xA9,0x1A,
    };
    constexpr auto UnwindBytes12 = std::to_array<std::uint8_t>({
        0x19,0x1B,0x0B,0x85,0x1B,0x03,0x13,0x01,
        0x19,0x00,0x0C,0x30,0x0B,0x70,0x0A,0x60,
        0x09,0xC0,0x07,0xD0,0x05,0xE0,0x03,0xF0,
        0x01,0x50,0x00,0x00,0xB0,0x43,0x3C,0x00,
        0x00,0x16,0x51,0x00,
    });
    constexpr auto FuncInfoBytes12 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x01,0x00,0x00,0x00,
        0x28,0x16,0x51,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
        0x30,0x16,0x51,0x00,0xC0,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });
    constexpr std::uintptr_t ProviderRva11 = 0x56A710U;
    constexpr std::size_t ProviderSize11 = 0x126U;
    constexpr DWORD ProviderUnwindRva11 = 0x4548C8U;
    constexpr std::uintptr_t ProviderFuncInfoRva11 = 0x454904U;
    constexpr std::uintptr_t NativeForwardSlotRva11 = 0x480E60U;
    constexpr Sha256Digest ProviderHash11{
        0xD4,0x7D,0xD1,0xB6,0xC1,0x6D,0x3F,0xB3,
        0xE5,0xBA,0x78,0x13,0xED,0xD6,0x42,0x1B,
        0x17,0x4D,0x3D,0xDF,0xAB,0x77,0xE0,0x35,
        0x2D,0xE2,0x0F,0xCA,0xEB,0x1E,0x18,0x88,
    };
    constexpr auto UnwindBytes11 = std::to_array<std::uint8_t>({
        0x19,0x1B,0x0B,0x85,0x1B,0x03,0x13,0x01,
        0x19,0x00,0x0C,0x30,0x0B,0x70,0x0A,0x60,
        0x09,0xC0,0x07,0xD0,0x05,0xE0,0x03,0xF0,
        0x01,0x50,0x00,0x00,0x80,0x97,0x32,0x00,
        0x04,0x49,0x45,0x00,
    });
    constexpr auto FuncInfoBytes11 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x01,0x00,0x00,0x00,
        0x2C,0x49,0x45,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
        0x34,0x49,0x45,0x00,0xC0,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });
    constexpr auto NativeReaderEntryBytes =
            std::to_array<std::uint8_t>({
        0x48,0x83,0xEC,0x48,0x41,0x83,0xF9,0x47,
        0x75,0x09,0x48,0x83,0xC4,0x48,0xE9,0xED,
        0x26,0x00,0x00,0x41,0x8D,0x41,0xB8,0x83,
        0xF8,0x21,0x77,0x09,0x48,0x83,0xC4,0x48,
    });
    constexpr std::size_t MaximumProviderSize = ProviderSize12;
    static_assert(ProviderSize11 <= MaximumProviderSize);
    static_assert(UnwindBytes11.size() == UnwindBytes12.size());
    static_assert(UnwindBytes12.size() == 36U);
    static_assert(FuncInfoBytes11.size() == FuncInfoBytes12.size());

    if (!d2rBase || !core || providerExport == 0U
            || NativeReadItemsByVersionRva > d2rImageSize
            || NativeReaderEntryBytes.size()
                > d2rImageSize - NativeReadItemsByVersionRva) {
        return false;
    }

    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (providerExport < coreBase) return false;
    const auto liveProviderRva = providerExport - coreBase;
    const bool provider12 = liveProviderRva == ProviderRva12;
    const bool provider11 = liveProviderRva == ProviderRva11;
    if (!provider12 && !provider11) return false;

    const auto providerSize = provider12
        ? ProviderSize12 : ProviderSize11;
    const auto providerUnwindRva = provider12
        ? ProviderUnwindRva12 : ProviderUnwindRva11;
    const auto providerFuncInfoRva = provider12
        ? ProviderFuncInfoRva12 : ProviderFuncInfoRva11;
    const auto nativeForwardSlotRva = provider12
        ? NativeForwardSlotRva12 : NativeForwardSlotRva11;
    const auto& expectedProviderHash = provider12
        ? ProviderHash12 : ProviderHash11;
    const auto& expectedUnwind = provider12
        ? UnwindBytes12 : UnwindBytes11;
    const auto& expectedFuncInfo = provider12
        ? FuncInfoBytes12 : FuncInfoBytes11;
    if (!IsAccessibleRange(
            reinterpret_cast<const void*>(providerExport),
            providerSize,
            false,
            true)) {
        return false;
    }
    std::array<std::uint8_t, MaximumProviderSize> liveProvider{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(providerExport),
            liveProvider.data(),
            providerSize)) {
        return false;
    }
    Sha256Digest liveProviderHash{};
    if (!CalculateSha256(
            std::span<const std::uint8_t>{
                liveProvider.data(), providerSize},
            liveProviderHash)
            || liveProviderHash != expectedProviderHash) {
        return false;
    }

    DWORD64 functionImageBase{};
    const auto* liveFunction = RtlLookupFunctionEntry(
        static_cast<DWORD64>(providerExport), &functionImageBase, nullptr);
    RUNTIME_FUNCTION function{};
    if (!liveFunction
            || functionImageBase != static_cast<DWORD64>(coreBase)
            || !SafeRead(liveFunction, function)
            || function.BeginAddress != liveProviderRva
            || function.EndAddress - function.BeginAddress != providerSize
            || function.UnwindData != providerUnwindRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - providerUnwindRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - providerFuncInfoRva) {
        return false;
    }
    std::array<std::uint8_t, UnwindBytes12.size()> liveUnwind{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(coreBase + providerUnwindRva),
            liveUnwind.data(),
            liveUnwind.size())
            || liveUnwind != expectedUnwind) {
        return false;
    }
    std::array<std::uint8_t, FuncInfoBytes12.size()> liveFuncInfo{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(coreBase + providerFuncInfoRva),
            liveFuncInfo.data(),
            liveFuncInfo.size())
            || liveFuncInfo != expectedFuncInfo) {
        return false;
    }

    if (coreBase > (std::numeric_limits<std::uintptr_t>::max)()
            - nativeForwardSlotRva) {
        return false;
    }
    std::uintptr_t nativeForward{};
    const auto expectedNativeForward =
        reinterpret_cast<std::uintptr_t>(d2rBase)
            + NativeReadItemsByVersionRva;
    if (!SafeRead(
            reinterpret_cast<const void*>(
                coreBase + nativeForwardSlotRva),
            nativeForward)
            || nativeForward != expectedNativeForward
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(nativeForward),
                NativeReaderEntryBytes.size(),
                false,
                true)) {
        return false;
    }
    std::array<std::uint8_t, NativeReaderEntryBytes.size()> liveNative{};
    return SafeCopyReadable(
            reinterpret_cast<const void*>(nativeForward),
            liveNative.data(),
            liveNative.size())
        && liveNative == NativeReaderEntryBytes;
}

template <
    std::size_t ProviderSize,
    std::size_t UnwindSize,
    std::size_t FuncInfoSize,
    std::size_t NativeEntrySize>
auto ValidateD2RCoreExactForwardingProvider(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t providerExport,
        std::uintptr_t providerRva,
        DWORD providerUnwindRva,
        std::uintptr_t providerFuncInfoRva,
        std::uintptr_t nativeForwardSlotRva,
        const Sha256Digest& expectedProviderHash,
        const std::array<std::uint8_t, UnwindSize>& expectedUnwind,
        const std::array<std::uint8_t, FuncInfoSize>& expectedFuncInfo,
        std::uintptr_t nativeForwardRva,
        const std::array<std::uint8_t, NativeEntrySize>&
            expectedNativeEntry) noexcept -> bool {
    if (!d2rBase || !core || providerExport == 0U
            || nativeForwardRva > d2rImageSize
            || expectedNativeEntry.size()
                > d2rImageSize - nativeForwardRva) {
        return false;
    }

    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (coreBase > (std::numeric_limits<std::uintptr_t>::max)() - providerRva
            || providerExport != coreBase + providerRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - providerUnwindRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - providerFuncInfoRva
            || coreBase
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - nativeForwardSlotRva) {
        return false;
    }
    if (!IsAccessibleRange(
            reinterpret_cast<const void*>(providerExport),
            ProviderSize,
            false,
            true)) {
        return false;
    }

    std::array<std::uint8_t, ProviderSize> liveProvider{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(providerExport),
            liveProvider.data(),
            liveProvider.size())) {
        return false;
    }
    Sha256Digest liveProviderHash{};
    if (!CalculateSha256(liveProvider, liveProviderHash)
            || liveProviderHash != expectedProviderHash) {
        return false;
    }

    DWORD64 functionImageBase{};
    const auto* liveFunction = RtlLookupFunctionEntry(
        static_cast<DWORD64>(providerExport), &functionImageBase, nullptr);
    RUNTIME_FUNCTION function{};
    if (!liveFunction
            || functionImageBase != static_cast<DWORD64>(coreBase)
            || !SafeRead(liveFunction, function)
            || function.BeginAddress != providerRva
            || function.EndAddress < function.BeginAddress
            || function.EndAddress - function.BeginAddress != ProviderSize
            || function.UnwindData != providerUnwindRva) {
        return false;
    }

    std::array<std::uint8_t, UnwindSize> liveUnwind{};
    std::array<std::uint8_t, FuncInfoSize> liveFuncInfo{};
    if (!SafeCopyReadable(
            reinterpret_cast<const void*>(coreBase + providerUnwindRva),
            liveUnwind.data(),
            liveUnwind.size())
            || liveUnwind != expectedUnwind
            || !SafeCopyReadable(
                reinterpret_cast<const void*>(
                    coreBase + providerFuncInfoRva),
                liveFuncInfo.data(),
                liveFuncInfo.size())
            || liveFuncInfo != expectedFuncInfo) {
        return false;
    }

    if (reinterpret_cast<std::uintptr_t>(d2rBase)
            > (std::numeric_limits<std::uintptr_t>::max)()
                - nativeForwardRva) {
        return false;
    }
    std::uintptr_t nativeForward{};
    const auto expectedNativeForward =
        reinterpret_cast<std::uintptr_t>(d2rBase) + nativeForwardRva;
    if (!SafeRead(
            reinterpret_cast<const void*>(
                coreBase + nativeForwardSlotRva),
            nativeForward)
            || nativeForward != expectedNativeForward
            || !IsAccessibleRange(
                reinterpret_cast<const void*>(nativeForward),
                expectedNativeEntry.size(),
                false,
                true)) {
        return false;
    }
    std::array<std::uint8_t, NativeEntrySize> liveNative{};
    return SafeCopyReadable(
            reinterpret_cast<const void*>(nativeForward),
            liveNative.data(),
            liveNative.size())
        && liveNative == expectedNativeEntry;
}

auto ValidateD2RCoreWriteD2SSaveProviderAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t providerExport) noexcept -> bool {
    constexpr auto NativeWriterEntryBytes =
            std::to_array<std::uint8_t>({
        0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,
        0x09,0x49,0x8B,0xD9,0x4C,0x8D,0x4C,0x24,
        0x40,0x48,0xC7,0x44,0x24,0x20,0x00,0x00,
        0x00,0x00,0xFF,0x15,0x60,0x46,0xA8,0x00,
        0x85,0xC0,0x75,0x0E,0x89,0x03,0xFF,0x15,
        0xCC,0x46,0xA8,0x00,0x48,0x83,0xC4,0x30,
        0x5B,0xC3,0x8B,0x44,0x24,0x40,0x89,0x03,
        0x8B,0x05,0xBA,0x00,0xC7,0x00,0x48,0x83,
        0xC4,0x30,0x5B,0xC3,
    });
    static_assert(NativeWriterEntryBytes.size() == 0x44U);
    constexpr Sha256Digest ProviderHash12{
        0xCF,0x95,0x8F,0xC2,0x5B,0x4B,0x53,0xAD,
        0x7C,0xE8,0xE5,0x4C,0x25,0x72,0x76,0x70,
        0xAC,0xC0,0xCE,0xE1,0xDB,0xBE,0x48,0xDD,
        0xC9,0xF5,0x2F,0xF6,0x92,0xFB,0x43,0x01,
    };
    constexpr auto UnwindBytes12 = std::to_array<std::uint8_t>({
        0x19,0x22,0x0D,0x85,0x22,0x68,0xD6,0x00,
        0x1B,0x03,0x13,0x01,0xAF,0x01,0x0C,0x30,
        0x0B,0x70,0x0A,0x60,0x09,0xC0,0x07,0xD0,
        0x05,0xE0,0x03,0xF0,0x01,0x50,0x00,0x00,
        0xB0,0x43,0x3C,0x00,0x50,0xF7,0x50,0x00,
    });
    constexpr auto FuncInfoBytes12 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x16,0x00,0x00,0x00,
        0x78,0xF7,0x50,0x00,0x01,0x00,0x00,0x00,
        0x28,0xF8,0x50,0x00,0x3C,0x00,0x00,0x00,
        0x50,0xF8,0x50,0x00,0x58,0x0D,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });
    constexpr Sha256Digest ProviderHash11{
        0xCE,0x50,0xCB,0x93,0xC2,0x47,0x61,0x17,
        0xB2,0xBC,0xAE,0x86,0xA0,0x46,0x0B,0xBE,
        0x97,0x80,0x4C,0xE4,0x5E,0x2D,0xE8,0xA4,
        0x8E,0x67,0xE0,0xC1,0x9F,0x2E,0x6D,0xA5,
    };
    constexpr auto UnwindBytes11 = std::to_array<std::uint8_t>({
        0x19,0x22,0x0D,0x85,0x22,0x68,0x57,0x00,
        0x1B,0x03,0x13,0x01,0xB1,0x00,0x0C,0x30,
        0x0B,0x70,0x0A,0x60,0x09,0xC0,0x07,0xD0,
        0x05,0xE0,0x03,0xF0,0x01,0x50,0x00,0x00,
        0x80,0x97,0x32,0x00,0x9C,0x2B,0x45,0x00,
    });
    constexpr auto FuncInfoBytes11 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x16,0x00,0x00,0x00,
        0xC4,0x2B,0x45,0x00,0x01,0x00,0x00,0x00,
        0x74,0x2C,0x45,0x00,0x3D,0x00,0x00,0x00,
        0x9C,0x2C,0x45,0x00,0x68,0x05,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });

    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (providerExport >= coreBase
            && providerExport - coreBase == 0x6365E0U) {
        return ValidateD2RCoreExactForwardingProvider<0x26C8U>(
            d2rBase,
            d2rImageSize,
            core,
            providerExport,
            0x6365E0U,
            0x50F4D4U,
            0x50F750U,
            0x539F78U,
            ProviderHash12,
            UnwindBytes12,
            FuncInfoBytes12,
            NativeD2SSaveWriterRva,
            NativeWriterEntryBytes);
    }
    if (providerExport >= coreBase
            && providerExport - coreBase == 0x565640U) {
        return ValidateD2RCoreExactForwardingProvider<0x20E0U>(
            d2rBase,
            d2rImageSize,
            core,
            providerExport,
            0x565640U,
            0x452920U,
            0x452B9CU,
            0x4796A8U,
            ProviderHash11,
            UnwindBytes11,
            FuncInfoBytes11,
            NativeD2SSaveWriterRva,
            NativeWriterEntryBytes);
    }
    return false;
}

auto ValidateD2RCoreCloseD2SSaveProviderAbi(
        const std::uint8_t* d2rBase,
        std::size_t d2rImageSize,
        HMODULE core,
        std::uintptr_t providerExport) noexcept -> bool {
    constexpr Sha256Digest ProviderHash12{
        0x03,0x70,0xAA,0x11,0xA9,0xCB,0x4A,0x0C,
        0x13,0x72,0xF7,0x0A,0x08,0xA5,0x0F,0xF0,
        0x29,0x6F,0x4F,0x37,0x37,0x29,0x37,0x39,
        0x7E,0x07,0x51,0xE9,0x20,0x34,0xD4,0xFE,
    };
    constexpr auto UnwindBytes12 = std::to_array<std::uint8_t>({
        0x19,0x20,0x0C,0x85,0x20,0x68,0x42,0x00,
        0x19,0x03,0x11,0x01,0x86,0x00,0x0A,0x30,
        0x09,0x70,0x08,0x60,0x07,0xC0,0x05,0xE0,
        0x03,0xF0,0x01,0x50,0xB0,0x43,0x3C,0x00,
        0xEC,0xFA,0x50,0x00,
    });
    constexpr auto FuncInfoBytes12 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x07,0x00,0x00,0x00,
        0x14,0xFB,0x50,0x00,0x01,0x00,0x00,0x00,
        0x4C,0xFB,0x50,0x00,0x09,0x00,0x00,0x00,
        0x74,0xFB,0x50,0x00,0x18,0x04,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });
    constexpr Sha256Digest ProviderHash11{
        0x17,0x3E,0x11,0xDE,0x2C,0xCF,0x71,0x50,
        0x9C,0x34,0xA7,0x86,0x9C,0x24,0x55,0x4D,
        0x6E,0x1F,0x37,0xD2,0x8E,0xD3,0x86,0x06,
        0xAA,0xFE,0x37,0x65,0xAC,0x91,0x3E,0xDF,
    };
    constexpr auto UnwindBytes11 = std::to_array<std::uint8_t>({
        0x19,0x19,0x0A,0x85,0x19,0x03,0x11,0x01,
        0x30,0x00,0x0A,0x30,0x09,0x70,0x08,0x60,
        0x07,0xC0,0x05,0xE0,0x03,0xF0,0x01,0x50,
        0x80,0x97,0x32,0x00,0x24,0x2F,0x45,0x00,
    });
    constexpr auto FuncInfoBytes11 = std::to_array<std::uint8_t>({
        0x22,0x05,0x93,0x19,0x07,0x00,0x00,0x00,
        0x4C,0x2F,0x45,0x00,0x01,0x00,0x00,0x00,
        0x84,0x2F,0x45,0x00,0x09,0x00,0x00,0x00,
        0xAC,0x2F,0x45,0x00,0x78,0x01,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    });

    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (providerExport >= coreBase
            && providerExport - coreBase == 0x6393B0U) {
        return ValidateD2RCoreExactForwardingProvider<0x1786U>(
            d2rBase,
            d2rImageSize,
            core,
            providerExport,
            0x6393B0U,
            0x50FA30U,
            0x50FAECU,
            0x539F90U,
            ProviderHash12,
            UnwindBytes12,
            FuncInfoBytes12,
            NativeD2SSaveCloseRva,
            NativeFileHandleCloserBytes);
    }
    if (providerExport >= coreBase
            && providerExport - coreBase == 0x567E00U) {
        return ValidateD2RCoreExactForwardingProvider<0x63AU>(
            d2rBase,
            d2rImageSize,
            core,
            providerExport,
            0x567E00U,
            0x452E84U,
            0x452F24U,
            0x4796C0U,
            ProviderHash11,
            UnwindBytes11,
            FuncInfoBytes11,
            NativeD2SSaveCloseRva,
            NativeFileHandleCloserBytes);
    }
    return false;
}

struct NativeUnwindInfoHeader {
    std::uint8_t versionAndFlags{};
    std::uint8_t prologSize{};
    std::uint8_t codeCount{};
    std::uint8_t frameRegisterAndOffset{};
};

struct NativeUnwindCode {
    std::uint8_t codeOffset{};
    std::uint8_t operationAndInfo{};
};

static_assert(sizeof(NativeUnwindInfoHeader) == 4);
static_assert(sizeof(NativeUnwindCode) == 2);

auto ValidateNativeProducerUnwind(
        std::uintptr_t entryRva,
        std::uintptr_t expectedEpilogueEndRva,
        DWORD expectedUnwindRva,
        std::uint8_t finalNonvolatileRegister,
        std::uint32_t expectedAllocation) noexcept -> bool {
    constexpr std::uintptr_t NativeExceptionHandlerRva = 0x12D104CU;
    constexpr auto NativeExceptionHandlerBytes =
            std::to_array<std::uint8_t>({
        0x48,0x83,0xEC,0x28,0x4D,0x8B,0x41,0x38,
        0x48,0x8B,0xCA,0x49,0x8B,0xD1,0xE8,0x0D,
        0x00,0x00,0x00,0xB8,0x01,0x00,0x00,0x00,
        0x48,0x83,0xC4,0x28,0xC3,
    });
    constexpr std::size_t NativeUnwindRecordSize = 28U;
    if (!LoaderBase || finalNonvolatileRegister >= 16
            || entryRva > LoaderImageSize
            || LoaderImageSize - entryRva <= 5U
            || expectedEpilogueEndRva <= entryRva + 5U
            || expectedEpilogueEndRva > LoaderImageSize
            || expectedUnwindRva > LoaderImageSize
            || LoaderImageSize - expectedUnwindRva
                < NativeUnwindRecordSize
            || NativeExceptionHandlerRva > LoaderImageSize
            || NativeExceptionHandlerBytes.size()
                > LoaderImageSize - NativeExceptionHandlerRva
            || !IsAccessibleRange(
                LoaderBase + entryRva + 5U, 1, false, true)
            || !IsAccessibleRange(
                LoaderBase + expectedEpilogueEndRva - 1U,
                1,
                false,
                true)
            || !IsAccessibleRange(
                LoaderBase + NativeExceptionHandlerRva,
                NativeExceptionHandlerBytes.size(),
                false,
                true)) {
        return false;
    }
    DWORD64 imageBase{};
    const auto instructionAddress = reinterpret_cast<DWORD64>(
        LoaderBase + entryRva + 5U);
    const auto* liveFunction = RtlLookupFunctionEntry(
        instructionAddress, &imageBase, nullptr);
    if (!liveFunction
            || imageBase != reinterpret_cast<DWORD64>(LoaderBase)) {
        return false;
    }
    RUNTIME_FUNCTION function{};
    if (!SafeRead(liveFunction, function)
            || function.BeginAddress != entryRva
            || function.EndAddress != expectedEpilogueEndRva
            || function.EndAddress > LoaderImageSize
            || function.UnwindData != expectedUnwindRva) {
        return false;
    }
    DWORD64 epilogueImageBase{};
    const auto* liveEpilogueFunction = RtlLookupFunctionEntry(
        reinterpret_cast<DWORD64>(
            LoaderBase + expectedEpilogueEndRva - 1U),
        &epilogueImageBase,
        nullptr);
    RUNTIME_FUNCTION epilogueFunction{};
    if (!liveEpilogueFunction
            || epilogueImageBase != imageBase
            || !SafeRead(liveEpilogueFunction, epilogueFunction)
            || epilogueFunction.BeginAddress != function.BeginAddress
            || epilogueFunction.EndAddress != function.EndAddress
            || epilogueFunction.UnwindData != function.UnwindData) {
        return false;
    }
    NativeUnwindInfoHeader header{};
    if (!SafeRead(LoaderBase + function.UnwindData, header)
            || header.versionAndFlags != 0x19U
            || header.prologSize != 0x20U
            || header.codeCount != 7U
            || header.frameRegisterAndOffset != 0U) {
        return false;
    }
    std::array<NativeUnwindCode, 7> codes{};
    if (!SafeCopyReadable(
            LoaderBase + function.UnwindData + sizeof(header),
            codes.data(),
            sizeof(codes))) {
        return false;
    }
    std::uint16_t padding{};
    if (!SafeRead(
            LoaderBase + function.UnwindData + sizeof(header)
                + sizeof(codes),
            padding)
            || padding != 0U
            || (finalNonvolatileRegister != 14U
                && finalNonvolatileRegister != 15U)
            || (finalNonvolatileRegister == 14U
                && expectedAllocation != 0x160U)
            || (finalNonvolatileRegister == 15U
                && expectedAllocation != 0x170U)) {
        return false;
    }
    const auto allocationSlots = static_cast<std::uint16_t>(
        expectedAllocation / 8U);
    const auto expectedCodes = std::to_array<std::uint8_t>({
        0x0E,0x01,
        static_cast<std::uint8_t>(allocationSlots & 0xFFU),
        static_cast<std::uint8_t>(allocationSlots >> 8U),
        0x07,static_cast<std::uint8_t>(finalNonvolatileRegister << 4U),
        0x05,0x70,0x04,0x60,0x03,0x50,0x02,0x30,
    });
    if (std::memcmp(
            codes.data(), expectedCodes.data(), expectedCodes.size()) != 0) {
        return false;
    }

    std::uint32_t exceptionHandlerRva{};
    std::uint32_t exceptionHandlerData{};
    const auto handlerOffset = sizeof(header) + sizeof(codes) + sizeof(padding);
    if (!SafeRead(
            LoaderBase + function.UnwindData + handlerOffset,
            exceptionHandlerRva)
            || !SafeRead(
                LoaderBase + function.UnwindData + handlerOffset
                    + sizeof(exceptionHandlerRva),
                exceptionHandlerData)
            || exceptionHandlerRva != NativeExceptionHandlerRva
            || exceptionHandlerData
                != (finalNonvolatileRegister == 14U ? 0x150U : 0x160U)) {
        return false;
    }
    std::array<std::uint8_t, NativeExceptionHandlerBytes.size()>
        liveExceptionHandler{};
    if (!SafeCopyReadable(
            LoaderBase + NativeExceptionHandlerRva,
            liveExceptionHandler.data(),
            liveExceptionHandler.size())
            || liveExceptionHandler != NativeExceptionHandlerBytes) {
        return false;
    }

    constexpr std::uint16_t BasePushMask =
        (UINT16_C(1) << 3U) | (UINT16_C(1) << 5U)
        | (UINT16_C(1) << 6U) | (UINT16_C(1) << 7U);
    const auto expectedPushMask = static_cast<std::uint16_t>(
        BasePushMask | (UINT16_C(1) << finalNonvolatileRegister));
    std::uint16_t pushMask{};
    std::uint32_t allocation{};
    bool allocationObserved{};
    for (std::size_t index{}; index < codes.size();) {
        const auto operation = codes[index].operationAndInfo & 0x0FU;
        const auto operationInfo = codes[index].operationAndInfo >> 4U;
        if (operation == 0U) { // UWOP_PUSH_NONVOL
            if (operationInfo >= 16U
                    || (pushMask & (UINT16_C(1) << operationInfo)) != 0U) {
                return false;
            }
            const auto expectedOffset = operationInfo == 3U ? 2U
                : operationInfo == 5U ? 3U
                : operationInfo == 6U ? 4U
                : operationInfo == 7U ? 5U
                : operationInfo == finalNonvolatileRegister ? 7U
                : 0U;
            if (expectedOffset == 0U
                    || codes[index].codeOffset != expectedOffset) {
                return false;
            }
            pushMask = static_cast<std::uint16_t>(
                pushMask | (UINT16_C(1) << operationInfo));
            ++index;
            continue;
        }
        if (operation != 1U || allocationObserved
                || codes[index].codeOffset != 14U) { // UWOP_ALLOC_LARGE
            return false;
        }
        if (operationInfo == 0U) {
            if (index + 1U >= codes.size()) return false;
            std::uint16_t scaled{};
            std::memcpy(&scaled, &codes[index + 1U], sizeof(scaled));
            allocation = static_cast<std::uint32_t>(scaled) * 8U;
            index += 2U;
        } else if (operationInfo == 1U) {
            if (index + 2U >= codes.size()) return false;
            std::memcpy(
                &allocation, &codes[index + 1U], sizeof(allocation));
            index += 3U;
        } else {
            return false;
        }
        allocationObserved = true;
    }
    return pushMask == expectedPushMask
        && allocationObserved
        && allocation == expectedAllocation;
}

auto InvokeNativePlayerStatReader(
        NativePlayerStatReaderFn reader,
        void* context,
        void* unit,
        std::uint8_t** cursor,
        std::uint8_t* end,
        std::uint32_t version,
        std::int32_t count,
        std::int32_t& status) noexcept -> bool {
    if (!reader) return false;
    __try {
        status = reader(context, unit, cursor, end, version, count);
        return true;
    } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativePlayerPreviewCopy(
        void* object,
        void* destination,
        std::uint32_t capacity,
        std::uint64_t offset,
        std::uint32_t& copied) noexcept -> bool {
    if (!NativePlayerPreviewCopy) return false;
    __try {
        copied = NativePlayerPreviewCopy(
            object, destination, capacity, offset);
        return true;
    } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto SafeWrite(
        void* destination,
        const void* source,
        std::size_t size) noexcept -> bool {
    if (size == 0) return true;
    if (!source || !IsAccessibleRange(destination, size, true)) return false;
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativeGetLinkNameCount(
        const void* linker,
        std::uint64_t& count) noexcept -> bool {
    if (!NativeGetLinkNameCount || !linker) return false;
    __try {
        count = NativeGetLinkNameCount(linker);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativeGetLinkName(
        const void* linker,
        std::int32_t ordinal,
        const char*& name) noexcept -> bool {
    if (!NativeGetLinkName || !linker) return false;
    __try {
        name = NativeGetLinkName(
            linker, ordinal, NativeGetLinkNameMode);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto IsReadableProtection(DWORD protection) noexcept -> bool {
    const auto baseProtection = protection & 0xFFU;
    return baseProtection == PAGE_READONLY
        || baseProtection == PAGE_READWRITE
        || baseProtection == PAGE_WRITECOPY
        || baseProtection == PAGE_EXECUTE_READ
        || baseProtection == PAGE_EXECUTE_READWRITE
        || baseProtection == PAGE_EXECUTE_WRITECOPY;
}

template <std::size_t Capacity>
auto CopyBoundedReadableCString(
        const char* name,
        std::size_t maximumLength,
        bool allowEmpty,
        std::array<char, Capacity>& output,
        std::string_view& view) noexcept -> bool {
    static_assert(Capacity > 0);
    view = {};
    if (!name || maximumLength >= Capacity) return false;
    const auto scanLimit = maximumLength + 1;
    const auto start = reinterpret_cast<std::uintptr_t>(name);
    std::size_t scanned{};
    while (scanned < scanLimit) {
        if (start > std::numeric_limits<std::uintptr_t>::max() - scanned) {
            return false;
        }
        const auto cursorAddress = start + scanned;
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(cursorAddress),
                &memory,
                sizeof(memory)) != sizeof(memory)
                || memory.State != MEM_COMMIT
                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || !IsReadableProtection(memory.Protect)) {
            return false;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(
            memory.BaseAddress);
        if (regionStart > std::numeric_limits<std::uintptr_t>::max()
                - memory.RegionSize) {
            return false;
        }
        const auto regionEnd = regionStart + memory.RegionSize;
        if (regionEnd <= cursorAddress) return false;
        const auto readable = static_cast<std::size_t>(
            regionEnd - cursorAddress);
        const auto chunk = (std::min)(readable, scanLimit - scanned);

        std::size_t localLength{};
        bool found{};
        __try {
            const auto* cursor = reinterpret_cast<const char*>(cursorAddress);
            while (localLength < chunk) {
                const auto value = cursor[localLength];
                if (value == '\0') {
                    found = true;
                    break;
                }
                const auto outputOffset = scanned + localLength;
                if (outputOffset >= maximumLength) {
                    return false;
                }
                output[outputOffset] = value;
                ++localLength;
            }
        } __except (NativeExceptionFilter(GetExceptionCode())) {
            return false;
        }
        if (found) {
            const auto length = scanned + localLength;
            if ((!allowEmpty && length == 0) || length > maximumLength) {
                return false;
            }
            output[length] = '\0';
            view = std::string_view{output.data(), length};
            return true;
        }
        scanned += chunk;
    }
    return false;
}

auto CopyBoundedReadableName(
        const char* name,
        std::string_view& view) noexcept -> bool {
    return CopyBoundedReadableCString(
        name,
        MaximumNativeStatNameLength,
        false,
        NativeSchemaNameBuffer,
        view);
}

auto GetLinkNameCountForSnapshot(const void* linker) noexcept
        -> std::uint64_t {
    std::uint64_t count{};
    if (!InvokeNativeGetLinkNameCount(linker, count)) {
        NativeSchemaCallbackFailed = true;
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    return count;
}

auto GetLinkNameForSnapshot(
        const void* linker,
        std::int32_t ordinal) noexcept -> std::string_view {
    const char* nativeName{};
    std::string_view view;
    if (!InvokeNativeGetLinkName(linker, ordinal, nativeName)
            || !CopyBoundedReadableName(nativeName, view)) {
        NativeSchemaCallbackFailed = true;
        return {};
    }
    return view;
}

auto ResetPublishedSchemaSnapshot() noexcept -> void {
    AcquireSRWLockExclusive(&SchemaSnapshotLock);
    PublishedSchemaSnapshot = {};
    PendingSchemaCandidates.Reset();
    HasPublishedSchemaSnapshot = false;
    SchemaUpdateInProgress = false;
    SchemaReady.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&SchemaSnapshotLock);
}

// The exclusive lock remains held throughout record/name capture and hashing.
// A persistence operation that already owns a shared lease may finish with the
// previous schema before this reload acquires exclusivity; once exclusivity is
// held, new persistence leases reject without waiting. This guards the
// published snapshot, not the pre-hook provenance of an already-built native
// state-1 buffer.
auto BeginSchemaSnapshotUpdate() noexcept -> void {
    AcquireSRWLockExclusive(&SchemaSnapshotLock);
    SchemaUpdateInProgress = true;
    SchemaReady.store(false, std::memory_order_release);
}

auto StageSchemaSnapshotUpdate(
        SchemaError captureResult,
        const void* sourceDataTables,
        const void* sourceRecords,
        std::size_t rowCount,
        NativeItemStatCostSchemaSnapshot&& candidate) noexcept
        -> NativeSchemaStageResult {
    auto result = NativeSchemaStageResult::InvalidArgument;
    if (captureResult == SchemaError::None) {
        result = PendingSchemaCandidates.Stage(
            sourceDataTables,
            sourceRecords,
            rowCount,
            std::move(candidate));
    }
    SchemaUpdateInProgress = false;
    // Persistence remains closed until the authoritative RotW TableView is
    // delivered by DataTablesLoaded and selects one exact staged capture.
    ReleaseSRWLockExclusive(&SchemaSnapshotLock);
    return result;
}

auto InvokeNativeQsort(
        void* entries,
        std::size_t count) -> bool {
    __try {
        NativeQsort(entries, count, sizeof(DescriptionEntry), NativeComparator);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativeResize(void* vector, std::size_t count) -> bool {
    __try {
        NativeVectorResize(vector, count);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativeByteBufferResize(
        void* object,
        std::size_t count) noexcept -> bool {
    if (!NativeByteBufferResize || !object) return false;
    __try {
        NativeByteBufferResize(object, count);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativeSetObjectState(
        void* object,
        std::uint32_t state) noexcept -> bool {
    if (!NativeSetObjectState || !object) return false;
    __try {
        NativeSetObjectState(object, state);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto InvokeNativeSetObjectAux(
        void* object,
        std::uint64_t value) noexcept -> bool {
    if (!NativeSetObjectAux || !object) return false;
    __try {
        NativeSetObjectAux(object, value);
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto ReadNativeStoreName(
        void* object,
        std::array<char, MaximumNativeStoreNameLength + 1>& storage,
        std::string_view& name) noexcept -> bool {
    name = {};
    if (!object) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    if (address > std::numeric_limits<std::uintptr_t>::max()
            - SaveObjectNamePointerOffset - sizeof(std::uint64_t)) {
        return false;
    }
    const char* pointer{};
    std::uint64_t length{};
    if (!SafeRead(
            reinterpret_cast<const void*>(
                address + SaveObjectNamePointerOffset),
            pointer)
            || !SafeRead(
                reinterpret_cast<const void*>(
                    address + SaveObjectNamePointerOffset + sizeof(void*)),
                length)
            || !pointer || length == 0
            || length > MaximumNativeStoreNameLength) {
        return false;
    }
    const auto nativeLength = static_cast<std::size_t>(length);
    if (!SafeCopyReadable(pointer, storage.data(), nativeLength)) {
        return false;
    }
    const auto pointerAddress = reinterpret_cast<std::uintptr_t>(pointer);
    if (pointerAddress > std::numeric_limits<std::uintptr_t>::max()
            - nativeLength) {
        return false;
    }
    char terminator{};
    if (!SafeRead(
            reinterpret_cast<const char*>(pointerAddress + nativeLength),
            terminator)
            || terminator != '\0') {
        return false;
    }
    storage[nativeLength] = '\0';
    name = std::string_view{storage.data(), nativeLength};
    return true;
}

auto SnapshotNativeObjectBuffer(
        void* object,
        const std::uint64_t* expectedLength,
        std::uint64_t maximumLength,
        std::vector<std::uint8_t>& bytes) noexcept -> bool {
    if (!object) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    if (address > std::numeric_limits<std::uintptr_t>::max()
            - SaveObjectBufferSizeOffset - sizeof(std::uint64_t)) {
        return false;
    }
    std::uint8_t* pointer{};
    std::uint64_t length{};
    if (!SafeRead(
            reinterpret_cast<const void*>(
                address + SaveObjectBufferPointerOffset),
            pointer)
            || !SafeRead(
                reinterpret_cast<const void*>(
                    address + SaveObjectBufferSizeOffset),
                length)
            || (expectedLength && length != *expectedLength)
            || length > maximumLength
            || length > (std::numeric_limits<std::size_t>::max)()) {
        return false;
    }
    try {
        std::vector<std::uint8_t> staged(
            static_cast<std::size_t>(length));
        if (!staged.empty()
                && (!pointer
                    || !SafeCopyReadable(
                        pointer, staged.data(), staged.size()))) {
            return false;
        }
        bytes.swap(staged);
        return true;
    } catch (...) {
        return false;
    }
}

auto ClearRejectedNativeRead(void* context) noexcept -> bool {
    return InvokeNativeByteBufferResize(context, 0)
        && InvokeNativeSetObjectAux(context, 0)
        && InvokeNativeSetObjectState(context, 0);
}

[[noreturn]] auto FailClosed(
    const char* reason,
    std::uint64_t rowCount) noexcept -> void;

[[noreturn]] auto FailClosed(
        const char* reason,
        std::uint64_t rowCount) noexcept -> void {
    if (LoaderContext) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "ISC12: fail-closed loader stop (%s; rows=%llu).",
            reason ? reason : "unspecified native failure",
            static_cast<unsigned long long>(rowCount));
        LoaderContext->LogError(message);
    }
    RaiseFailFastException(nullptr, nullptr, 0);
    TerminateProcess(GetCurrentProcess(), ERROR_INVALID_DATA);
    __assume(false);
}

auto ReadPlayerStatsWithPreflight(
        NativePlayerStatReaderFn nativeReader,
        PlayerStatStreamKind kind,
        void* context,
        void* unit,
        std::uint8_t** cursor,
        std::uint8_t* end,
        std::uint32_t version,
        std::int32_t count) noexcept -> std::int32_t {
    // ISC12 is a strict clean-sheet format. A legacy-version pass-through at
    // these retargeted exhaustive callers would reintroduce mixed-width state.
    if (version != InnerFormatVersion || !nativeReader || !cursor || !end) {
        return NativeMalformedPlayerStatStatus;
    }

    std::uint8_t* begin{};
    if (!SafeRead(cursor, begin) || !begin) {
        return NativeMalformedPlayerStatStatus;
    }
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
    if (endAddress < beginAddress) {
        return NativeMalformedPlayerStatStatus;
    }
    const auto available = static_cast<std::size_t>(
        endAddress - beginAddress);
    const auto copied = (std::min)(
        available, PlayerStatPreflightBuffer.size());
    if (copied < sizeof(PlayerStatSectionMarker)
            || !SafeCopyReadable(
                begin, PlayerStatPreflightBuffer.data(), copied)) {
        return NativeMalformedPlayerStatStatus;
    }

    AcquireSRWLockShared(&SchemaSnapshotLock);
    if (!SchemaReady.load(std::memory_order_acquire)
            || !HasPublishedSchemaSnapshot
            || SchemaUpdateInProgress) {
        ReleaseSRWLockShared(&SchemaSnapshotLock);
        return NativeMalformedPlayerStatStatus;
    }

    PlayerStatPreflightResult preflight;
    const auto preflightStatus = PreflightPlayerStatStream(
        std::span<const std::uint8_t>{
            PlayerStatPreflightBuffer.data(), copied},
        std::span<const ItemStatSemanticRow>{
            PublishedSchemaSnapshot.rows.data(),
            PublishedSchemaSnapshot.rows.size()},
        kind,
        preflight);
    if (preflightStatus != PlayerStatPreflightError::None) {
        ReleaseSRWLockShared(&SchemaSnapshotLock);
        return NativeMalformedPlayerStatStatus;
    }

    const auto expectedBytes = (preflight.consumedBits + 7U) / 8U;
    std::uint8_t* observedBegin{};
    if (expectedBytes > available
            || beginAddress > (std::numeric_limits<std::uintptr_t>::max)()
                - expectedBytes
            || !SafeRead(cursor, observedBegin)
            || observedBegin != begin) {
        ReleaseSRWLockShared(&SchemaSnapshotLock);
        return NativeMalformedPlayerStatStatus;
    }
    auto* const expectedCursor = reinterpret_cast<std::uint8_t*>(
        beginAddress + expectedBytes);
    const auto schemaRowCount = PublishedSchemaSnapshot.rows.size();

    std::int32_t nativeStatus{};
    const bool invoked = InvokeNativePlayerStatReader(
        nativeReader,
        context,
        unit,
        cursor,
        end,
        version,
        count,
        nativeStatus);
    std::uint8_t* nativeCursor{};
    const bool cursorMatches = nativeStatus != 0
        || (SafeRead(cursor, nativeCursor) && nativeCursor == expectedCursor);
    ReleaseSRWLockShared(&SchemaSnapshotLock);

    if (!invoked) {
        FailClosed("guarded player-stat reader raised a native fault", 0);
    }
    if (!cursorMatches) {
        FailClosed(
            "guarded player-stat reader diverged from its preflight cursor",
            schemaRowCount);
    }
    return nativeStatus;
}

auto CopyPlayerPreviewWithPreflight(
        void* object,
        void* destination,
        std::uint32_t capacity,
        std::uint64_t offset) noexcept -> std::uint32_t {
    // The original copy owns its lock/unlock and buffer projection contract;
    // call it exactly once, then validate only the completed local D2S bytes.
    std::uint32_t copied{};
    if (!InvokeNativePlayerPreviewCopy(
            object, destination, capacity, offset, copied)) {
        FailClosed("guarded player-preview copy raised a native fault", 0);
    }
    if (capacity != PlayerPreviewBufferCapacity
            || offset != 0
            || copied == 0
            || copied > PlayerPreviewBufferCapacity
            || !destination
            || !SafeCopyReadable(
                destination, PlayerPreviewPreflightBuffer.data(), copied)) {
        return 0;
    }

    const std::span<const std::uint8_t> previewBytes{
        PlayerPreviewPreflightBuffer.data(), copied};
    std::uint8_t dataContext{};
    if (PreflightPlayerPreviewContainer(previewBytes, dataContext)
            != PlayerPreviewPreflightError::None) {
        return 0;
    }

    AcquireSRWLockShared(&SchemaSnapshotLock);
    if (!SchemaReady.load(std::memory_order_acquire)
            && !HasPublishedSchemaSnapshot
            && !SchemaUpdateInProgress) {
        ReleaseSRWLockShared(&SchemaSnapshotLock);
        const auto accepted = PlayerPreviewsAcceptedBeforeSchema.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (DiagnosticsEnabled && LoaderContext && accepted <= 4) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "ISC12 diagnostics: frontend preview accepted before schema "
                "publication; bytes=%u; data-context=%u; ordinal=%llu.",
                copied,
                static_cast<unsigned>(dataContext),
                static_cast<unsigned long long>(accepted));
            LoaderContext->LogInfo(message);
        }
        return copied;
    }
    if (!SchemaReady.load(std::memory_order_acquire)
            || !HasPublishedSchemaSnapshot
            || SchemaUpdateInProgress) {
        ReleaseSRWLockShared(&SchemaSnapshotLock);
        return 0;
    }
    PlayerPreviewPreflightResult preflight;
    const auto status = PreflightPlayerPreviewD2S(
        previewBytes,
        std::span<const ItemStatSemanticRow>{
            PublishedSchemaSnapshot.rows.data(),
            PublishedSchemaSnapshot.rows.size()},
        preflight);
    ReleaseSRWLockShared(&SchemaSnapshotLock);
    return status == PlayerPreviewPreflightError::None ? copied : 0;
}

[[noreturn]] auto RejectSchemaSnapshotUpdate(
        SchemaError captureResult,
        const char* reason,
        std::uint64_t rowCount) noexcept -> void {
    NativeItemStatCostSchemaSnapshot rejected;
    const auto staged = StageSchemaSnapshotUpdate(
        captureResult,
        nullptr,
        nullptr,
        0,
        std::move(rejected));
    (void)staged;
    FailClosed(reason, rowCount);
}

auto PreMutationFailure(const char* reason) noexcept -> std::uint32_t {
    if (CurrentRowCountKnown
            && CurrentRowCount <= LegacySerializedSentinel) {
        return 0;
    }
    FailClosed(reason, CurrentRowCount);
}

auto ValidateImageTarget(
        std::uintptr_t rva,
        std::size_t size,
        bool executable) noexcept -> bool {
    return LoaderBase && rva <= LoaderImageSize
        && size <= LoaderImageSize - rva
        && IsAccessibleRange(LoaderBase + rva, size, false, executable);
}

auto ReleaseUnpatchedResources() noexcept -> void {
    if (AnyMutationInstalled) return;
    PersistencePrepared = false;
    if (PersistenceState
            && InterlockedCompareExchange(
                &PersistenceState->itemTrampolineFunctionTableRegistered,
                0,
                0) != 0) {
        if (!RtlDeleteFunctionTable(
                PersistenceState->itemTrampolineRuntimeFunctions)) {
            // The OS may still consult both this table and its copied xdata.
            // Treat a failed deregistration exactly like an uncertain native
            // write: retain every process-bound allocation and prohibit a
            // second preparation attempt.
            AnyMutationInstalled = true;
            ColdRestartRequired = true;
            return;
        }
        InterlockedExchange(
            &PersistenceState->itemTrampolineFunctionTableRegistered, 0);
    }
    if (PersistenceRelayPage) {
        VirtualFree(PersistenceRelayPage, 0, MEM_RELEASE);
        PersistenceRelayPage = nullptr;
        PersistenceRelayPageSize = 0;
    }
    if (PersistenceState) {
        VirtualFree(PersistenceState, 0, MEM_RELEASE);
        PersistenceState = nullptr;
        PersistenceStatePageSize = 0;
    }
    PersistenceReaderRelayEntry = nullptr;
    PersistenceWriterRelayEntry = nullptr;
    PersistencePlayerSaveFinalizeRelayEntry = nullptr;
    PersistenceAuxiliaryReaderRelayEntry = nullptr;
    PersistencePlayerReaderRelayEntry = nullptr;
    PersistencePlayerPreviewRelayEntry = nullptr;
    PersistencePacket9CQueueRelayEntry = nullptr;
    PersistencePacket9DQueueRelayEntry = nullptr;
    PersistencePacket9CProducerRelayEntry = nullptr;
    PersistencePacket9DProducerRelayEntry = nullptr;
    PersistencePacket9CTrampoline = nullptr;
    PersistencePacket9DTrampoline = nullptr;
    PreparedCodecActivationTargets = {};
    gISC12PersistenceReaderContinueExit = nullptr;
    gISC12PersistenceReaderRejectedExit = nullptr;
    gISC12PersistenceWriterVanillaExit = nullptr;
    gISC12PersistenceWriterCommittedExit = nullptr;
    gISC12PersistenceWriterRejectedExit = nullptr;
    gISC12CodecReturnExit = nullptr;
    gISC12ItemTransportReturnExit = nullptr;
    NativeAuxiliaryReader = nullptr;
    NativePlayerReader = nullptr;
    NativePlayerPreviewCopy = nullptr;
    NativeFullItemPacketQueue = nullptr;
    PreparedFullItemTransportProvider =
        FullItemTransportProvider::Invalid;
    InspectFullItemTransportProvider = nullptr;
    ActiveFullItemTransportProvider.store(
        FullItemTransportProvider::Invalid,
        std::memory_order_release);
    if (RelayPage) {
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
        RelayPageSize = 0;
    }
    if (State) {
        VirtualFree(State, 0, MEM_RELEASE);
        State = nullptr;
        StatePageSize = 0;
    }
    gISC12LoaderSuccessExit = nullptr;
    gISC12LoaderVanillaExit = nullptr;
    PersistenceReaderPatchInstalled = false;
    PersistenceWriterPatchInstalled = false;
    PreparedPublicationAdapters.Reset();
}

auto AllocatePersistenceRelayPageNear(std::uint8_t* base) noexcept -> void* {
    if (!base) return nullptr;
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto pageSize = static_cast<std::size_t>(systemInfo.dwPageSize);
    if (granularity == 0
            || pageSize < MaximumPersistenceRelayTemplateSize) {
        return nullptr;
    }
    const auto readerSite = reinterpret_cast<std::uintptr_t>(
        base + PersistenceReaderPatchRva);
    const auto writerSite = reinterpret_cast<std::uintptr_t>(
        base + PersistenceWriterPatchRva);
    const auto playerSaveFinalizeSite = reinterpret_cast<std::uintptr_t>(
        base + PlayerSaveFinalizeCallRva);
    const auto auxiliaryReaderSite = reinterpret_cast<std::uintptr_t>(
        base + AuxiliaryReaderCallRva);
    const auto playerReaderPrimarySite = reinterpret_cast<std::uintptr_t>(
        base + PlayerReaderPrimaryCallRva);
    const auto playerReaderLegacySite = reinterpret_cast<std::uintptr_t>(
        base + PlayerReaderLegacyCallRva);
    const auto playerPreviewSite = reinterpret_cast<std::uintptr_t>(
        base + PlayerPreviewCallRva);
    const auto packet9CQueueSite = reinterpret_cast<std::uintptr_t>(
        base + Packet9CQueueCallRva);
    const auto packet9DQueueSite = reinterpret_cast<std::uintptr_t>(
        base + Packet9DQueueCallRva);
    const auto packet9CProducerSite = reinterpret_cast<std::uintptr_t>(
        base + Packet9CProducerEntryRva);
    const auto packet9DProducerSite = reinterpret_cast<std::uintptr_t>(
        base + Packet9DProducerEntryRva);
    const auto aligned = readerSite & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < UINT64_C(0x70000000);
            delta += granularity) {
        if (aligned > std::numeric_limits<std::uintptr_t>::max() - delta) {
            break;
        }
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(readerSite, candidate)
                || !CanEncodeRel32(writerSite, candidate)
                || !CanEncodeRel32(playerSaveFinalizeSite, candidate)
                || !CanEncodeRel32(auxiliaryReaderSite, candidate)
                || !CanEncodeRel32(playerReaderPrimarySite, candidate)
                || !CanEncodeRel32(playerReaderLegacySite, candidate)
                || !CanEncodeRel32(playerPreviewSite, candidate)
                || !CanEncodeRel32(packet9CQueueSite, candidate)
                || !CanEncodeRel32(packet9DQueueSite, candidate)
                || !CanEncodeRel32(packet9CProducerSite, candidate)
                || !CanEncodeRel32(packet9DProducerSite, candidate)) {
            break;
        }
        if (auto* allocation = VirtualAlloc(
                reinterpret_cast<void*>(candidate),
                pageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            PersistenceRelayPageSize = pageSize;
            return allocation;
        }
    }
    return nullptr;
}

auto AllocateRelayPageNear(std::uint8_t* base) noexcept -> void* {
    if (!base) return nullptr;
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto pageSize = static_cast<std::size_t>(systemInfo.dwPageSize);
    if (granularity == 0 || pageSize < MaximumRelayTemplateSize) {
        return nullptr;
    }

    const auto patchSite = reinterpret_cast<std::uintptr_t>(
        base + TailPatchRva);
    const auto aligned = patchSite & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < UINT64_C(0x70000000);
            delta += granularity) {
        if (aligned > std::numeric_limits<std::uintptr_t>::max() - delta) {
            break;
        }
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(patchSite, candidate)) break;
        if (auto* allocation = VirtualAlloc(
                reinterpret_cast<void*>(candidate),
                pageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            RelayPageSize = pageSize;
            return allocation;
        }
    }
    return nullptr;
}

auto PrepareRelay(std::string& error) noexcept -> bool {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto pageSize = static_cast<std::size_t>(systemInfo.dwPageSize);
    if (pageSize < sizeof(RelayState)) {
        return SetError(error, "invalid Windows page size for relay state");
    }
    State = static_cast<RelayState*>(VirtualAlloc(
        nullptr,
        pageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    if (!State) return SetError(error, "relay state allocation failed");
    StatePageSize = pageSize;

    RelayPage = AllocateRelayPageNear(LoaderBase);
    if (!RelayPage) {
        ReleaseUnpatchedResources();
        return SetError(
            error,
            "no persistent relay page is available within rel32 reach");
    }

    const auto templateBegin = reinterpret_cast<std::uintptr_t>(
        &ISC12LoaderRelayTemplateBegin);
    const auto templateVanillaExit = reinterpret_cast<std::uintptr_t>(
        &ISC12LoaderRelayTemplateVanillaExit);
    const auto templateSuccessExit = reinterpret_cast<std::uintptr_t>(
        &ISC12LoaderRelayTemplateSuccessExit);
    const auto templateStatePointer = reinterpret_cast<std::uintptr_t>(
        &ISC12LoaderRelayTemplateStatePointer);
    const auto templateEnd = reinterpret_cast<std::uintptr_t>(
        &ISC12LoaderRelayTemplateEnd);
    if (templateEnd <= templateBegin) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistent relay template layout is invalid");
    }
    const auto templateSizeAddress = templateEnd - templateBegin;
    if (templateSizeAddress < sizeof(void*)
            || templateVanillaExit < templateBegin
            || templateVanillaExit >= templateEnd
            || templateSuccessExit < templateBegin
            || templateSuccessExit >= templateEnd
            || templateStatePointer < templateBegin
            || templateStatePointer > templateEnd - sizeof(void*)) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistent relay template layout is invalid");
    }
    const auto templateSize = static_cast<std::size_t>(
        templateSizeAddress);
    const auto statePointerOffset = static_cast<std::size_t>(
        templateStatePointer - templateBegin);
    const auto vanillaExitOffset = static_cast<std::size_t>(
        templateVanillaExit - templateBegin);
    const auto successExitOffset = static_cast<std::size_t>(
        templateSuccessExit - templateBegin);
    if (templateSize > MaximumRelayTemplateSize
            || templateSize > RelayPageSize) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistent relay template is too large");
    }

    std::memcpy(
        RelayPage,
        reinterpret_cast<const void*>(templateBegin),
        templateSize);
    auto* const copiedStatePointer =
        static_cast<std::uint8_t*>(RelayPage) + statePointerOffset;
    std::memcpy(copiedStatePointer, &State, sizeof(State));

    State->handler = reinterpret_cast<void*>(&ISC12LoaderTailMidHook);
    State->vanillaContinuation = LoaderBase + VanillaContinuationRva;
    State->epilogue = LoaderBase + EpilogueRva;
    gISC12LoaderVanillaExit =
        static_cast<std::uint8_t*>(RelayPage) + vanillaExitOffset;
    gISC12LoaderSuccessExit =
        static_cast<std::uint8_t*>(RelayPage) + successExitOffset;

    DWORD previousProtection{};
    if (!VirtualProtect(
            RelayPage,
            RelayPageSize,
            PAGE_EXECUTE_READ,
            &previousProtection)) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistent relay protection failed");
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), RelayPage, templateSize)) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistent relay cache flush failed");
    }
    return true;
}

auto PreparePersistenceRelay(std::string& error) noexcept -> bool {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto pageSize = static_cast<std::size_t>(systemInfo.dwPageSize);
    if (pageSize < sizeof(PersistenceRelayState)) {
        return SetError(
            error, "invalid Windows page size for persistence state");
    }
    PersistenceState = static_cast<PersistenceRelayState*>(VirtualAlloc(
        nullptr,
        pageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    if (!PersistenceState) {
        return SetError(error, "persistence relay state allocation failed");
    }
    PersistenceStatePageSize = pageSize;

    PersistenceRelayPage = AllocatePersistenceRelayPageNear(LoaderBase);
    if (!PersistenceRelayPage) {
        ReleaseUnpatchedResources();
        return SetError(
            error,
            "no persistence relay page is available within rel32 reach");
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateBegin);
    const auto writerEntry = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateWriterEntry);
    const auto readerContinue = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateReaderContinueExit);
    const auto readerRejected = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateReaderRejectedExit);
    const auto writerVanilla = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateWriterVanillaExit);
    const auto writerCommitted = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateWriterCommittedExit);
    const auto writerRejected = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateWriterRejectedExit);
    const auto playerSaveFinalize = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePlayerSaveFinalizeEntry);
    const auto auxiliaryReader = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateAuxiliaryReaderEntry);
    const auto playerReader = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePlayerReaderEntry);
    const auto playerPreview = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePlayerPreviewEntry);
    const auto codecReturn = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateCodecReturnExit);
    const auto packet9CQueue = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9CQueueEntry);
    const auto packet9DQueue = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9DQueueEntry);
    const auto packet9CProducer = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9CProducerEntry);
    const auto packet9DProducer = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9DProducerEntry);
    const auto itemTransportReturn = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateItemTransportReturnExit);
    const auto packet9CTrampoline = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9CTrampoline);
    const auto packet9CTrampolineRel32 = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9CTrampolineRel32);
    const auto packet9CTrampolineEnd = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9CTrampolineEnd);
    const auto packet9DTrampoline = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9DTrampoline);
    const auto packet9DTrampolineRel32 = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9DTrampolineRel32);
    const auto packet9DTrampolineEnd = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplatePacket9DTrampolineEnd);
    const auto itemTrampolineUnwindInfo =
        reinterpret_cast<std::uintptr_t>(
            &ISC12PersistenceRelayTemplateItemTrampolineUnwindInfo);
    const auto statePointer = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateStatePointer);
    const auto end = reinterpret_cast<std::uintptr_t>(
        &ISC12PersistenceRelayTemplateEnd);
    const auto labelInside = [begin, end](std::uintptr_t label) noexcept {
        return label >= begin && label < end;
    };
    if (end <= begin || end - begin < sizeof(void*)
            || !labelInside(writerEntry)
            || !labelInside(readerContinue)
            || !labelInside(readerRejected)
            || !labelInside(writerVanilla)
            || !labelInside(writerCommitted)
            || !labelInside(writerRejected)
            || !labelInside(playerSaveFinalize)
            || !labelInside(auxiliaryReader)
            || !labelInside(playerReader)
            || !labelInside(playerPreview)
            || !labelInside(codecReturn)
            || !labelInside(packet9CQueue)
            || !labelInside(packet9DQueue)
            || !labelInside(packet9CProducer)
            || !labelInside(packet9DProducer)
            || !labelInside(itemTransportReturn)
            || !labelInside(packet9CTrampoline)
            || !labelInside(packet9CTrampolineRel32)
            || !labelInside(packet9CTrampolineEnd)
            || !labelInside(packet9DTrampoline)
            || !labelInside(packet9DTrampolineRel32)
            || !labelInside(packet9DTrampolineEnd)
            || !labelInside(itemTrampolineUnwindInfo)
            || statePointer < begin
            || statePointer > end - sizeof(void*)
            || !(begin < readerContinue
                && readerContinue < readerRejected
                && readerRejected < writerEntry
                && writerEntry < writerVanilla
                && writerVanilla < writerCommitted
                && writerCommitted < writerRejected
                && writerRejected < playerSaveFinalize
                && playerSaveFinalize < auxiliaryReader
                && auxiliaryReader < playerReader
                && playerReader < playerPreview
                && playerPreview < codecReturn
                && codecReturn < packet9CQueue
                && packet9CQueue < packet9DQueue
                && packet9DQueue < packet9CProducer
                && packet9CProducer < packet9DProducer
                && packet9DProducer < itemTransportReturn
                && itemTransportReturn < packet9CTrampoline
                && packet9CTrampoline < packet9CTrampolineRel32
                && packet9CTrampolineRel32 < packet9CTrampolineEnd
                && packet9CTrampolineEnd <= packet9DTrampoline
                && packet9DTrampoline < packet9DTrampolineRel32
                && packet9DTrampolineRel32 < packet9DTrampolineEnd
                && packet9DTrampolineEnd <= itemTrampolineUnwindInfo
                && itemTrampolineUnwindInfo < statePointer)
            || packet9CTrampolineEnd - packet9CTrampoline != 10U
            || packet9DTrampolineEnd - packet9DTrampoline != 10U
            || packet9CTrampolineRel32 - packet9CTrampoline != 6U
            || packet9DTrampolineRel32 - packet9DTrampoline != 6U) {
        ReleaseUnpatchedResources();
        return SetError(
            error, "persistence relay template layout is invalid");
    }
    const auto templateSize = static_cast<std::size_t>(end - begin);
    if (templateSize > MaximumPersistenceRelayTemplateSize
            || templateSize > PersistenceRelayPageSize) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistence relay template is too large");
    }
    std::memcpy(
        PersistenceRelayPage,
        reinterpret_cast<const void*>(begin),
        templateSize);
    auto* const copied = static_cast<std::uint8_t*>(PersistenceRelayPage);
    std::memcpy(
        copied + static_cast<std::size_t>(statePointer - begin),
        &PersistenceState,
        sizeof(PersistenceState));

    constexpr auto ExpectedTrampolinePrefix =
        std::to_array<std::uint8_t>({0x40,0x53,0x55,0x56,0x57,0xE9});
    constexpr auto ExpectedTrampolineUnwindInfo =
        std::to_array<std::uint8_t>({
            0x01,0x05,0x04,0x00,0x05,0x70,
            0x04,0x60,0x03,0x50,0x02,0x30,
        });
    auto* const copiedPacket9CTrampoline =
        copied + static_cast<std::size_t>(packet9CTrampoline - begin);
    auto* const copiedPacket9DTrampoline =
        copied + static_cast<std::size_t>(packet9DTrampoline - begin);
    auto* const copiedItemTrampolineUnwindInfo =
        copied + static_cast<std::size_t>(
            itemTrampolineUnwindInfo - begin);
    if (std::memcmp(
            copiedPacket9CTrampoline,
            ExpectedTrampolinePrefix.data(),
            ExpectedTrampolinePrefix.size()) != 0
            || std::memcmp(
                copiedPacket9DTrampoline,
                ExpectedTrampolinePrefix.data(),
                ExpectedTrampolinePrefix.size()) != 0
            || std::memcmp(
                copiedItemTrampolineUnwindInfo,
                ExpectedTrampolineUnwindInfo.data(),
                ExpectedTrampolineUnwindInfo.size()) != 0) {
        ReleaseUnpatchedResources();
        return SetError(error, "item producer trampoline bytes are invalid");
    }
    const auto patchTrampolineRel32 = [begin, copied](
            std::uintptr_t displacementLabel,
            std::uintptr_t target) noexcept -> bool {
        auto* const displacement = copied
            + static_cast<std::size_t>(displacementLabel - begin);
        const auto next = reinterpret_cast<std::uintptr_t>(displacement)
            + sizeof(std::int32_t);
        std::int64_t distance{};
        if (target >= next) {
            const auto positive = target - next;
            if (positive > static_cast<std::uintptr_t>(
                    (std::numeric_limits<std::int32_t>::max)())) {
                return false;
            }
            distance = static_cast<std::int64_t>(positive);
        } else {
            const auto negative = next - target;
            constexpr auto MaximumNegative =
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::int32_t>::max)()) + 1ULL;
            if (negative > MaximumNegative) return false;
            distance = -static_cast<std::int64_t>(negative);
        }
        const auto encoded = static_cast<std::int32_t>(distance);
        std::memcpy(displacement, &encoded, sizeof(encoded));
        return true;
    };
    if (!patchTrampolineRel32(
            packet9CTrampolineRel32,
            reinterpret_cast<std::uintptr_t>(
                LoaderBase + Packet9CProducerContinuationRva))
            || !patchTrampolineRel32(
                packet9DTrampolineRel32,
                reinterpret_cast<std::uintptr_t>(
                    LoaderBase + Packet9DProducerContinuationRva))) {
        ReleaseUnpatchedResources();
        return SetError(
            error, "item producer trampoline lies outside rel32 reach");
    }

    PersistenceReaderRelayEntry = copied;
    PersistenceWriterRelayEntry =
        copied + static_cast<std::size_t>(writerEntry - begin);
    gISC12PersistenceReaderContinueExit =
        copied + static_cast<std::size_t>(readerContinue - begin);
    gISC12PersistenceReaderRejectedExit =
        copied + static_cast<std::size_t>(readerRejected - begin);
    gISC12PersistenceWriterVanillaExit =
        copied + static_cast<std::size_t>(writerVanilla - begin);
    gISC12PersistenceWriterCommittedExit =
        copied + static_cast<std::size_t>(writerCommitted - begin);
    gISC12PersistenceWriterRejectedExit =
        copied + static_cast<std::size_t>(writerRejected - begin);
    PersistencePlayerSaveFinalizeRelayEntry =
        copied + static_cast<std::size_t>(playerSaveFinalize - begin);
    PersistenceAuxiliaryReaderRelayEntry =
        copied + static_cast<std::size_t>(auxiliaryReader - begin);
    PersistencePlayerReaderRelayEntry =
        copied + static_cast<std::size_t>(playerReader - begin);
    PersistencePlayerPreviewRelayEntry =
        copied + static_cast<std::size_t>(playerPreview - begin);
    gISC12CodecReturnExit =
        copied + static_cast<std::size_t>(codecReturn - begin);
    PersistencePacket9CQueueRelayEntry =
        copied + static_cast<std::size_t>(packet9CQueue - begin);
    PersistencePacket9DQueueRelayEntry =
        copied + static_cast<std::size_t>(packet9DQueue - begin);
    PersistencePacket9CProducerRelayEntry =
        copied + static_cast<std::size_t>(packet9CProducer - begin);
    PersistencePacket9DProducerRelayEntry =
        copied + static_cast<std::size_t>(packet9DProducer - begin);
    gISC12ItemTransportReturnExit =
        copied + static_cast<std::size_t>(itemTransportReturn - begin);
    PersistencePacket9CTrampoline =
        reinterpret_cast<NativeItemAction9CFn>(copiedPacket9CTrampoline);
    PersistencePacket9DTrampoline =
        reinterpret_cast<NativeItemAction9DFn>(copiedPacket9DTrampoline);

    PersistenceState->readerHandler =
        reinterpret_cast<void*>(&ISC12PersistenceReaderMidHook);
    PersistenceState->writerHandler =
        reinterpret_cast<void*>(&ISC12PersistenceWriterMidHook);
    PersistenceState->readerContinue =
        LoaderBase + PersistenceReaderContinueRva;
    PersistenceState->readerRejected =
        LoaderBase + PersistenceReaderRejectedRva;
    PersistenceState->writerVanilla =
        LoaderBase + PersistenceWriterVanillaRva;
    PersistenceState->writerCommitted =
        LoaderBase + PersistenceWriterCommittedRva;
    PersistenceState->writerRejected =
        LoaderBase + PersistenceWriterRejectedRva;
    PersistenceState->auxiliaryReaderHandler =
        reinterpret_cast<void*>(&ISC12AuxiliaryReaderCallHook);
    PersistenceState->playerReaderHandler =
        reinterpret_cast<void*>(&ISC12PlayerReaderCallHook);
    PersistenceState->playerPreviewHandler =
        reinterpret_cast<void*>(&ISC12PlayerPreviewCallHook);
    PersistenceState->packet9CProducerHandler =
        reinterpret_cast<void*>(&ISC12ItemAction9CEntryHook);
    PersistenceState->packet9DProducerHandler =
        reinterpret_cast<void*>(&ISC12ItemAction9DEntryHook);
    PersistenceState->packet9CQueueHandler =
        reinterpret_cast<void*>(&ISC12ItemAction9CQueueHook);
    PersistenceState->packet9DQueueHandler =
        reinterpret_cast<void*>(&ISC12ItemAction9DQueueHook);
    PersistenceState->packet9CTrampoline =
        reinterpret_cast<void*>(PersistencePacket9CTrampoline);
    PersistenceState->packet9DTrampoline =
        reinterpret_cast<void*>(PersistencePacket9DTrampoline);
    PersistenceState->nativeFullItemPacketQueue =
        reinterpret_cast<void*>(NativeFullItemPacketQueue);

    const auto baseAddress = reinterpret_cast<std::uintptr_t>(LoaderBase);
    const auto playerSaveFinalizeRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePlayerSaveFinalizeRelayEntry);
    const auto auxiliaryReaderRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistenceAuxiliaryReaderRelayEntry);
    const auto playerReaderRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePlayerReaderRelayEntry);
    const auto playerPreviewRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePlayerPreviewRelayEntry);
    const auto packet9CQueueRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePacket9CQueueRelayEntry);
    const auto packet9DQueueRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePacket9DQueueRelayEntry);
    const auto packet9CProducerRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePacket9CProducerRelayEntry);
    const auto packet9DProducerRelayAddress =
        reinterpret_cast<std::uintptr_t>(
            PersistencePacket9DProducerRelayEntry);
    if (playerSaveFinalizeRelayAddress < baseAddress
            || auxiliaryReaderRelayAddress < baseAddress
            || playerReaderRelayAddress < baseAddress
            || playerPreviewRelayAddress < baseAddress
            || packet9CQueueRelayAddress < baseAddress
            || packet9DQueueRelayAddress < baseAddress
            || packet9CProducerRelayAddress < baseAddress
            || packet9DProducerRelayAddress < baseAddress) {
        ReleaseUnpatchedResources();
        return SetError(
            error, "persistence codec relay precedes the D2R image");
    }
    PreparedCodecActivationTargets =
        LoaderCodecPatchAuthority::BindPreparedRelay(
            auxiliaryReaderRelayAddress - baseAddress,
            playerReaderRelayAddress - baseAddress,
            playerPreviewRelayAddress - baseAddress,
            playerSaveFinalizeRelayAddress - baseAddress,
            packet9CQueueRelayAddress - baseAddress,
            packet9DQueueRelayAddress - baseAddress,
            packet9CProducerRelayAddress - baseAddress,
            packet9DProducerRelayAddress - baseAddress);
    if (!CanEncodeRel32(
            baseAddress + PersistenceReaderPatchRva,
            reinterpret_cast<std::uintptr_t>(
                PersistenceReaderRelayEntry))
            || !CanEncodeRel32(
                baseAddress + PersistenceWriterPatchRva,
                reinterpret_cast<std::uintptr_t>(
                    PersistenceWriterRelayEntry))
            || !CanEncodeRel32(
                baseAddress + PlayerSaveFinalizeCallRva,
                reinterpret_cast<std::uintptr_t>(
                    PersistencePlayerSaveFinalizeRelayEntry))
            || !CanEncodeRel32(
                baseAddress + AuxiliaryReaderCallRva,
                auxiliaryReaderRelayAddress)
            || !CanEncodeRel32(
                baseAddress + PlayerReaderPrimaryCallRva,
                playerReaderRelayAddress)
            || !CanEncodeRel32(
                baseAddress + PlayerReaderLegacyCallRva,
                playerReaderRelayAddress)
            || !CanEncodeRel32(
                baseAddress + PlayerPreviewCallRva,
                playerPreviewRelayAddress)
            || !CanEncodeRel32(
                baseAddress + Packet9CQueueCallRva,
                packet9CQueueRelayAddress)
            || !CanEncodeRel32(
                baseAddress + Packet9DQueueCallRva,
                packet9DQueueRelayAddress)
            || !CanEncodeRel32(
                baseAddress + Packet9CProducerEntryRva,
                packet9CProducerRelayAddress)
            || !CanEncodeRel32(
                baseAddress + Packet9DProducerEntryRva,
                packet9DProducerRelayAddress)) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistence relay lies outside rel32 reach");
    }
    if (!ValidateNativeProducerUnwind(
            Packet9CProducerEntryRva,
            Packet9CProducerEpilogueEndRva,
            0x2129AFCU,
            14U,
            0x160U)
            || !ValidateNativeProducerUnwind(
                Packet9DProducerEntryRva,
                Packet9DProducerEpilogueEndRva,
                0x2129B18U,
                15U,
                0x170U)) {
        ReleaseUnpatchedResources();
        return SetError(
            error,
            "live item producer unwind metadata is incompatible");
    }

    DWORD previousProtection{};
    if (!VirtualProtect(
            PersistenceRelayPage,
            PersistenceRelayPageSize,
            PAGE_EXECUTE_READ,
            &previousProtection)) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistence relay protection failed");
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(),
            PersistenceRelayPage,
            templateSize)) {
        ReleaseUnpatchedResources();
        return SetError(error, "persistence relay cache flush failed");
    }
    const auto relayBaseAddress = reinterpret_cast<std::uintptr_t>(
        PersistenceRelayPage);
    const auto asRuntimeOffset = [relayBaseAddress](
            const void* address,
            DWORD& output) noexcept -> bool {
        const auto absolute = reinterpret_cast<std::uintptr_t>(address);
        if (absolute < relayBaseAddress
                || absolute - relayBaseAddress
                    > (std::numeric_limits<DWORD>::max)()) {
            return false;
        }
        output = static_cast<DWORD>(absolute - relayBaseAddress);
        return true;
    };
    auto& packet9CFunction =
        PersistenceState->itemTrampolineRuntimeFunctions[0];
    auto& packet9DFunction =
        PersistenceState->itemTrampolineRuntimeFunctions[1];
    if (!asRuntimeOffset(
            copiedPacket9CTrampoline, packet9CFunction.BeginAddress)
            || !asRuntimeOffset(
                copied + static_cast<std::size_t>(
                    packet9CTrampolineEnd - begin),
                packet9CFunction.EndAddress)
            || !asRuntimeOffset(
                copiedItemTrampolineUnwindInfo,
                packet9CFunction.UnwindData)
            || !asRuntimeOffset(
                copiedPacket9DTrampoline, packet9DFunction.BeginAddress)
            || !asRuntimeOffset(
                copied + static_cast<std::size_t>(
                    packet9DTrampolineEnd - begin),
                packet9DFunction.EndAddress)
            || !asRuntimeOffset(
                copiedItemTrampolineUnwindInfo,
                packet9DFunction.UnwindData)
            || packet9CFunction.BeginAddress >= packet9CFunction.EndAddress
            || packet9DFunction.BeginAddress >= packet9DFunction.EndAddress
            || packet9CFunction.EndAddress > packet9DFunction.BeginAddress
            || !RtlAddFunctionTable(
                PersistenceState->itemTrampolineRuntimeFunctions,
                2,
                reinterpret_cast<DWORD64>(PersistenceRelayPage))) {
        ReleaseUnpatchedResources();
        return SetError(
            error, "item producer trampoline unwind registration failed");
    }
    InterlockedExchange(
        &PersistenceState->itemTrampolineFunctionTableRegistered, 1);
    PersistencePrepared = true;
    return true;
}

auto BuildDescriptionIndexNative(void* dataTables) -> std::uint32_t {
    ++BuildCalls;
    NativeVectorMutationStarted = false;
    CurrentRowCountKnown = false;
    CurrentRowCount = 0;
    LastDescriptionCount.store(0, std::memory_order_release);

    if (!dataTables) return PreMutationFailure("null DataTables pointer");
    const auto dataTablesAddress = reinterpret_cast<std::uintptr_t>(dataTables);
    if (dataTablesAddress
            > std::numeric_limits<std::uintptr_t>::max()
                - DescriptionVectorOffset - sizeof(NativeVectorU16)) {
        return PreMutationFailure("DataTables address overflow");
    }

    std::uint64_t rowCount{};
    if (!SafeRead(
            reinterpret_cast<const void*>(
                dataTablesAddress + RecordCountOffset),
            rowCount)) {
        return PreMutationFailure("ItemStatCost count is unreadable");
    }
    CurrentRowCount = rowCount;
    CurrentRowCountKnown = true;
    LastRowCount.store(rowCount, std::memory_order_release);
    if (rowCount > MaximumRecordCount) {
        FailClosed("ItemStatCost count exceeds 4095", rowCount);
    }
    if (rowCount == 0) {
        FailClosed("ItemStatCost schema cannot be empty", rowCount);
    }

    BeginSchemaSnapshotUpdate();

    std::uint8_t* records{};
    if (!SafeRead(
            reinterpret_cast<const void*>(
                dataTablesAddress + RecordsPointerOffset),
            records)) {
        RejectSchemaSnapshotUpdate(
            SchemaError::InvalidArgument,
            "ItemStatCost record pointer is unreadable",
            rowCount);
    }
    const auto recordBytes = static_cast<std::size_t>(rowCount) * RecordStride;
    std::vector<std::uint8_t> ownedRecords;
    try {
        ownedRecords.resize(recordBytes);
    } catch (...) {
        RejectSchemaSnapshotUpdate(
            SchemaError::Allocation,
            "ItemStatCost record snapshot allocation failed",
            rowCount);
    }
    if (!records
            || !SafeCopyReadable(
                records, ownedRecords.data(), ownedRecords.size())) {
        RejectSchemaSnapshotUpdate(
            SchemaError::InvalidArgument,
            "ItemStatCost record snapshot failed",
            rowCount);
    }

    void* linker{};
    if (!SafeRead(
            reinterpret_cast<const void*>(
                dataTablesAddress + NativeItemStatCostLinkerOffset),
            linker)) {
        RejectSchemaSnapshotUpdate(
            SchemaError::InvalidArgument,
            "ItemStatCost linker pointer is unreadable",
            rowCount);
    }
    NativeSchemaCallbackFailed = false;
    NativeItemStatCostSchemaSnapshot schemaCandidate;
    const auto schemaResult = BuildNativeItemStatCostSchemaSnapshot(
        linker,
        ownedRecords,
        static_cast<std::size_t>(rowCount),
        NativeItemStatCostLinkerCallbacks{
            &GetLinkNameCountForSnapshot,
            &GetLinkNameForSnapshot,
        },
        schemaCandidate);
    const auto schemaStage = StageSchemaSnapshotUpdate(
        schemaResult,
        dataTables,
        records,
        static_cast<std::size_t>(rowCount),
        std::move(schemaCandidate));
    if (schemaStage != NativeSchemaStageResult::Staged) {
        const auto* reason = schemaResult != SchemaError::None
            ? NativeSchemaCallbackFailed
                ? "ItemStatCost schema linker access failed"
                : "ItemStatCost schema snapshot is invalid"
            : schemaStage == NativeSchemaStageResult::CapacityExceeded
                ? "ItemStatCost schema candidate capacity was exceeded"
                : "ItemStatCost schema candidate staging failed";
        FailClosed(reason, rowCount);
    }

    std::array<DescriptionEntry, MaximumRecordCount> staged;
    std::size_t descriptionCount{};
    for (std::size_t rowIndex{};
            rowIndex < static_cast<std::size_t>(rowCount);
            ++rowIndex) {
        const auto* const record =
            ownedRecords.data() + rowIndex * RecordStride;
        std::uint8_t function{};
        if (!SafeRead(record + DescriptionFunctionOffset, function)) {
            return PreMutationFailure("DescFunc is unreadable");
        }
        if (function == 0) continue;
        std::int16_t priority{};
        if (!SafeRead(record + DescriptionPriorityOffset, priority)) {
            return PreMutationFailure("DescPriority is unreadable");
        }
        staged[descriptionCount++] = {
            static_cast<std::uint16_t>(rowIndex),
            priority,
        };
    }
    LastDescriptionCount.store(descriptionCount, std::memory_order_release);

    if (descriptionCount > 1
            && !InvokeNativeQsort(staged.data(), descriptionCount)) {
        return PreMutationFailure("native DescPriority sort failed");
    }

    auto* const vectorAddress = reinterpret_cast<void*>(
        dataTablesAddress + DescriptionVectorOffset);
    NativeVectorU16 vectorBefore{};
    if (!SafeRead(vectorAddress, vectorBefore)
            || !IsAccessibleRange(
                vectorAddress, sizeof(NativeVectorU16), true)) {
        return PreMutationFailure("description vector layout is inaccessible");
    }
    const auto capacityBefore =
        vectorBefore.capacityAndFlags & NativeVectorCapacityMask;
    if (vectorBefore.size > capacityBefore
            || (capacityBefore != 0 && !vectorBefore.begin)) {
        return PreMutationFailure("description vector layout is invalid");
    }

    NativeVectorMutationStarted = true;
    if (!InvokeNativeResize(vectorAddress, descriptionCount)) {
        FailClosed("native description vector resize failed", rowCount);
    }

    NativeVectorU16 vectorAfter{};
    if (!SafeRead(vectorAddress, vectorAfter)) {
        FailClosed("resized description vector is unreadable", rowCount);
    }
    const auto capacityAfter =
        vectorAfter.capacityAndFlags & NativeVectorCapacityMask;
    const auto outputBytes = descriptionCount * sizeof(std::uint16_t);
    if (vectorAfter.size != descriptionCount
            || capacityAfter < vectorAfter.size
            || (descriptionCount != 0 && !vectorAfter.begin)) {
        FailClosed("resized description vector has an invalid layout", rowCount);
    }
    if (descriptionCount != 0) {
        std::array<std::uint16_t, MaximumRecordCount> ids;
        for (std::size_t index{}; index < descriptionCount; ++index) {
            ids[index] = staged[index].statId;
        }
        if (!SafeWrite(vectorAfter.begin, ids.data(), outputBytes)) {
            FailClosed("resized description vector is unwritable", rowCount);
        }
    }

    NativeVectorMutationStarted = false;
    return 1;
}

auto ResolveFullItemTransportProvider() noexcept
        -> FullItemTransportProvider {
    auto provider = ActiveFullItemTransportProvider.load(
        std::memory_order_acquire);
    if (provider == FullItemTransportProvider::Unresolved) {
        if (FullItemPacketTransaction.state
                != FullItemPacketStagingState::Idle
                || !InspectFullItemTransportProvider) {
            FailClosed(
                "full-item transport cannot be resolved during an active transaction",
                0);
        }
        const auto inspected = InspectFullItemTransportProvider();
        if (inspected == FullItemTransportProvider::Invalid
                || inspected == FullItemTransportProvider::Unresolved) {
            FailClosed(
                "full-item transport changed to an unattested provider",
                0);
        }
        auto expected = FullItemTransportProvider::Unresolved;
        if (ActiveFullItemTransportProvider.compare_exchange_strong(
                expected,
                inspected,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            provider = inspected;
            if (DiagnosticsEnabled && LoaderContext) {
                LoaderContext->LogInfo(
                    provider
                            == FullItemTransportProvider::ExtendedItemStatsV1
                        ? "ISC12 diagnostics: G9 transport sealed to the attested ExtendedItemStats 0.3.14 provider."
                        : "ISC12 diagnostics: G9 transport sealed to the native atomic packet path.");
            }
        } else {
            provider = expected;
            if (provider != inspected) {
                FailClosed(
                    "concurrent full-item transport resolution diverged",
                    0);
            }
        }
    }
    if (provider != FullItemTransportProvider::NativeG9
            && provider
                != FullItemTransportProvider::ExtendedItemStatsV1) {
        FailClosed("full-item transport is unavailable", 0);
    }
    return provider;
}

auto QueueFullItemPacket(
        void*,
        void* client,
        const std::uint8_t* bytes,
        std::size_t length) noexcept -> void {
    if (!NativeFullItemPacketQueue) {
        FailClosed("native full-item queue is unavailable", 0);
    }
    __try {
        NativeFullItemPacketQueue(client, bytes, length);
    } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
        FailClosed("native full-item queue raised an exception", 0);
    }
}

struct FullItemRuntimeSelfProbe {
    FullItemPacketStagingContext* transaction{};
    std::size_t callbackCount{};
    bool reenterOnFirstCallback{};
    FullItemProducerDisposition reentryDisposition{
        FullItemProducerDisposition::InvokeOriginal};
};

auto ObserveFullItemRuntimeSelfProbe(
        void* context,
        void*,
        const std::uint8_t*,
        std::size_t) noexcept -> void {
    auto& probe = *static_cast<FullItemRuntimeSelfProbe*>(context);
    ++probe.callbackCount;
    if (!probe.reenterOnFirstCallback || probe.callbackCount != 1U
            || !probe.transaction) {
        return;
    }
    const auto reentry = BeginFullItemPacketProducer(
        *probe.transaction,
        FullItemProducerDescriptor{
            .kind = FullItemPacketKind::ItemAction9C,
            .client = &probe,
            .item = &probe,
        });
    probe.reentryDisposition = reentry.disposition;
}

auto CaptureRuntimeSelfProbePacket(
        FullItemPacketStagingContext& transaction,
        FullItemPacketKind kind,
        void* client,
        std::uint8_t action,
        std::uint8_t fill) noexcept -> bool {
    std::array<std::uint8_t, 32> packet{};
    packet.fill(fill);
    packet[0] = kind == FullItemPacketKind::ItemAction9C ? 0x9C : 0x9D;
    packet[1] = action;
    packet[2] = static_cast<std::uint8_t>(packet.size());
    return CaptureFullItemPacketQueueCall(
        transaction,
        kind,
        client,
        packet.data(),
        packet.size()) == FullItemPacketStagingError::None;
}

auto RunFullItemPacketStagingRuntimeSelfTest(std::string& error) -> bool {
    error.clear();
    int client{};
    std::array<int, 5> items{};
    const auto root = FullItemProducerDescriptor{
        .kind = FullItemPacketKind::ItemAction9D,
        .client = &client,
        .parentItem = &items[4],
        .item = &items[0],
        .action = 0x22,
        .temporaryFlags = 0,
        .gamble = 0,
    };
    const auto child = [&](std::size_t itemIndex) {
        return FullItemProducerDescriptor{
            .kind = FullItemPacketKind::ItemAction9D,
            .client = &client,
            .parentItem = &items[0],
            .item = &items[itemIndex],
            .action = 0x12,
            .temporaryFlags = NestedFullItemTemporaryFlagsMask,
            .gamble = 0,
        };
    };

    FullItemPacketStagingContext acceptedTransaction{};
    const auto acceptedRoot = BeginFullItemPacketProducer(
        acceptedTransaction, root);
    if (acceptedRoot.disposition
            != FullItemProducerDisposition::InvokeOriginal
            || !CaptureRuntimeSelfProbePacket(
                acceptedTransaction,
                root.kind,
                &client,
                root.action,
                0x31)) {
        error = "runtime G9 self-test could not stage its accepted root";
        return false;
    }
    for (std::size_t itemIndex = 1; itemIndex <= 2; ++itemIndex) {
        const auto admitted = BeginFullItemPacketProducer(
            acceptedTransaction, child(itemIndex));
        if (admitted.disposition
                != FullItemProducerDisposition::InvokeOriginal
                || !CaptureRuntimeSelfProbePacket(
                    acceptedTransaction,
                    FullItemPacketKind::ItemAction9D,
                    &client,
                    0x12,
                    static_cast<std::uint8_t>(0x31 + itemIndex))
                || EndFullItemPacketProducer(
                    acceptedTransaction, admitted.token)
                    != FullItemProducerCompletion::NestedComplete) {
            error = "runtime G9 self-test could not stage a descendant";
            return false;
        }
    }
    if (EndFullItemPacketProducer(
            acceptedTransaction, acceptedRoot.token)
            != FullItemProducerCompletion::RootReady) {
        error = "runtime G9 self-test did not make the accepted tree ready";
        return false;
    }
    FullItemRuntimeSelfProbe acceptedProbe{
        .transaction = &acceptedTransaction,
    };
    const auto acceptedFlush = FlushOrDiscardFullItemPacketTransaction(
        acceptedTransaction,
        &ObserveFullItemRuntimeSelfProbe,
        &acceptedProbe);
    if (!acceptedFlush.completed
            || acceptedFlush.error != FullItemPacketStagingError::None
            || acceptedFlush.queuedPacketCount != 3
            || acceptedProbe.callbackCount != 3) {
        error = "runtime G9 self-test did not flush the accepted tree exactly";
        return false;
    }

    FullItemPacketStagingContext overflowTransaction{};
    const auto overflowRoot = BeginFullItemPacketProducer(
        overflowTransaction, root);
    if (!CaptureRuntimeSelfProbePacket(
            overflowTransaction,
            root.kind,
            &client,
            root.action,
            0x41)) {
        error = "runtime G9 self-test could not stage its overflow root";
        return false;
    }
    overflowTransaction.nodeCount = MaximumStagedItemPacketCount;
    const auto overflowAdmission = BeginFullItemPacketProducer(
        overflowTransaction, child(1));
    if (overflowAdmission.error
            != FullItemPacketStagingError::NodeLimitExceeded
            || EndFullItemPacketProducer(
                overflowTransaction, overflowRoot.token)
                != FullItemProducerCompletion::RootRejected) {
        error = "runtime G9 self-test did not reject the node overflow";
        return false;
    }
    FullItemRuntimeSelfProbe overflowProbe{
        .transaction = &overflowTransaction,
    };
    const auto overflowFlush = FlushOrDiscardFullItemPacketTransaction(
        overflowTransaction,
        &ObserveFullItemRuntimeSelfProbe,
        &overflowProbe);
    if (overflowFlush.error
            != FullItemPacketStagingError::NodeLimitExceeded
            || overflowFlush.queuedPacketCount != 0
            || overflowProbe.callbackCount != 0) {
        error = "runtime G9 self-test exposed a rejected overflow packet";
        return false;
    }

    FullItemPacketStagingContext reentryTransaction{};
    const auto reentryRoot = BeginFullItemPacketProducer(
        reentryTransaction, root);
    if (!CaptureRuntimeSelfProbePacket(
            reentryTransaction,
            root.kind,
            &client,
            root.action,
            0x51)) {
        error = "runtime G9 self-test could not stage its reentry root";
        return false;
    }
    const auto reentryChild = BeginFullItemPacketProducer(
        reentryTransaction, child(1));
    if (!CaptureRuntimeSelfProbePacket(
            reentryTransaction,
            FullItemPacketKind::ItemAction9D,
            &client,
            0x12,
            0x52)
            || EndFullItemPacketProducer(
                reentryTransaction, reentryChild.token)
                != FullItemProducerCompletion::NestedComplete
            || EndFullItemPacketProducer(
                reentryTransaction, reentryRoot.token)
                != FullItemProducerCompletion::RootReady) {
        error = "runtime G9 self-test could not prepare the reentry batch";
        return false;
    }
    FullItemRuntimeSelfProbe reentryProbe{
        .transaction = &reentryTransaction,
        .reenterOnFirstCallback = true,
    };
    const auto reentryFlush = FlushOrDiscardFullItemPacketTransaction(
        reentryTransaction,
        &ObserveFullItemRuntimeSelfProbe,
        &reentryProbe);
    if (reentryFlush.error
            != FullItemPacketStagingError::ReenteredDuringFlush
            || reentryFlush.queuedPacketCount != 1
            || reentryProbe.callbackCount != 1
            || reentryProbe.reentryDisposition
                != FullItemProducerDisposition::SkipOriginal
            || reentryTransaction.state
                != FullItemPacketStagingState::Fatal) {
        error = "runtime G9 self-test did not contain flush reentry";
        return false;
    }

    if (LoaderContext) {
        LoaderContext->LogInfo(
            "ISC12 diagnostics: G9 runtime self-test passed; "
            "accepted-tree=3/3; overflow=zero-callback; "
            "flush-reentry=one-callback-then-fatal.");
    }
    return true;
}

auto CompleteFullItemProducer(
        const FullItemProducerToken& token,
        bool aborted) noexcept -> void {
    const auto completion = aborted
        ? AbortFullItemPacketProducer(FullItemPacketTransaction, token)
        : EndFullItemPacketProducer(FullItemPacketTransaction, token);
    if (completion == FullItemProducerCompletion::Fatal
            || FullItemPacketTransaction.state
                == FullItemPacketStagingState::Fatal) {
        FailClosed("full-item staging transaction became fatal", 0);
    }
    if (completion != FullItemProducerCompletion::RootReady
            && completion != FullItemProducerCompletion::RootRejected) {
        return;
    }
    const auto rootKind = FullItemPacketTransaction.rootKind;
    const auto nodeCount = FullItemPacketTransaction.nodeCount;
    const auto packetCount = FullItemPacketTransaction.packetCount;
    const auto totalBytes = FullItemPacketTransaction.totalBytes;
    const auto stagingError = FullItemPacketTransaction.error;
    const auto rejectedProducerTemporaryFlags =
        FullItemPacketTransaction.rejectedProducerTemporaryFlags;
    const auto rejectedParentTemporaryFlags =
        FullItemPacketTransaction.rejectedParentTemporaryFlags;
    auto& rootCounter = rootKind == FullItemPacketKind::ItemAction9C
        ? FullItemRoot9C : FullItemRoot9D;
    const auto rootOrdinal = rootCounter.fetch_add(
        1, std::memory_order_relaxed) + 1U;
    const auto flush = FlushOrDiscardFullItemPacketTransaction(
        FullItemPacketTransaction,
        &QueueFullItemPacket,
        nullptr);
    if (FullItemPacketTransaction.state
            == FullItemPacketStagingState::Fatal
            || (!flush.completed && flush.queuedPacketCount != 0)) {
        FailClosed(
            "full-item queue reentered after staged publication began", 0);
    }
    const bool accepted = completion == FullItemProducerCompletion::RootReady
        && flush.completed
        && flush.error == FullItemPacketStagingError::None;
    if (accepted) {
        FullItemTransactionsAccepted.fetch_add(1, std::memory_order_relaxed);
        FullItemPacketsQueued.fetch_add(
            flush.queuedPacketCount, std::memory_order_relaxed);
    } else {
        FullItemTransactionsRejected.fetch_add(1, std::memory_order_relaxed);
    }
    const auto diagnosticOrdinal = FullItemDiagnosticLines.fetch_add(
        1, std::memory_order_relaxed);
    if (DiagnosticsEnabled && LoaderContext
            && (!accepted || diagnosticOrdinal < 32U)) {
        char message[384]{};
        if (stagingError
                == FullItemPacketStagingError::NestedFlagsMismatch) {
            std::snprintf(
                message,
                sizeof(message),
                "ISC12 diagnostics: G9 transaction rejected; root=0x%02X; "
                "root-ordinal=%llu; nodes=%zu; captured=%zu; bytes=%zu; "
                "queued=%zu; staging-error=%u; flush-error=%u; "
                "nested-flags=0x%08X; parent-flags=0x%08X.",
                rootKind == FullItemPacketKind::ItemAction9C ? 0x9C : 0x9D,
                static_cast<unsigned long long>(rootOrdinal),
                nodeCount,
                packetCount,
                totalBytes,
                flush.queuedPacketCount,
                static_cast<unsigned>(stagingError),
                static_cast<unsigned>(flush.error),
                rejectedProducerTemporaryFlags,
                rejectedParentTemporaryFlags);
        } else {
            std::snprintf(
                message,
                sizeof(message),
                "ISC12 diagnostics: G9 transaction %s; root=0x%02X; "
                "root-ordinal=%llu; nodes=%zu; captured=%zu; bytes=%zu; "
                "queued=%zu; staging-error=%u; flush-error=%u.",
                accepted ? "accepted" : "rejected",
                rootKind == FullItemPacketKind::ItemAction9C ? 0x9C : 0x9D,
                static_cast<unsigned long long>(rootOrdinal),
                nodeCount,
                packetCount,
                totalBytes,
                flush.queuedPacketCount,
                static_cast<unsigned>(stagingError),
                static_cast<unsigned>(flush.error));
        }
        if (accepted) LoaderContext->LogInfo(message);
        else LoaderContext->LogWarn(message);
    }
}

auto VerifyPublicationPattern(
        void*,
        const NativePattern& pattern) noexcept -> bool {
    if (!LoaderBase || pattern.rva == 0 || pattern.bytes.empty()
            || pattern.bytes.size() != pattern.mask.size()
            || !ValidateImageTarget(
                pattern.rva, pattern.bytes.size(), true)) {
        return false;
    }
    __try {
        for (std::size_t index{}; index < pattern.bytes.size(); ++index) {
            if (pattern.mask[index] != 0
                    && LoaderBase[pattern.rva + index]
                        != pattern.bytes[index]) {
                if (pattern.rva == LoaderCompileCallRva) {
                    return InspectLoaderCompileProviderContract(
                            LoaderBase, LoaderImageSize)
                        == LoaderCompileProviderKind::D2RCoreLoadExcelTable;
                }
                if (pattern.rva == PlayerSaveStatWriterCallRva) {
                    return InspectPlayerSaveStatWriterProviderContract(
                            LoaderBase, LoaderImageSize)
                        == PlayerSaveStatWriterProviderKind::
                            D2RCoreWritePlayerSaveStatId;
                }
                if (pattern.rva == PlayerSaveDynamicCapacityRva
                        || pattern.rva == PlayerSaveDynamicCallRva) {
                    return InspectPlayerSaveProviderContract(
                            LoaderBase, LoaderImageSize)
                        == PlayerSaveProviderKind::
                            D2RCoreWritePlayerSaveWithEnvironmentCapture;
                }
                if (pattern.rva == D2SContainerVersionForwardRva) {
                    return InspectD2SItemReadProviderContract(
                            LoaderBase, LoaderImageSize)
                        == D2SItemReadProviderKind::
                            D2RCoreReadItemsByVersion;
                }
                if (pattern.rva == D2SSaveWriterProviderCallRva
                        || pattern.rva == D2SSaveCloseProviderCallRva) {
                    return InspectD2SSaveIoProviderContract(
                            LoaderBase, LoaderImageSize)
                        == D2SSaveIoProviderKind::
                            D2RCoreWriteAndCloseWithEnvironment;
                }
                if (pattern.rva == FullItemTransportQueueEntryRva) {
                    return PreparedFullItemTransportProvider
                        == FullItemTransportProvider::
                            ExtendedItemStatsV1;
                }
                return pattern.rva == ItemSaveStatWriterCallRva
                    && InspectItemSaveStatWriterProviderContract(
                        LoaderBase, LoaderImageSize)
                        == ItemSaveStatWriterProviderKind::
                            D2RCoreWriteItemSaveStatId;
            }
        }
        return true;
    } __except (NativeExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

auto PatchPublicationRel32(
        void*,
        std::uintptr_t rva,
        std::span<const std::uint8_t> expected,
        std::uintptr_t targetRva,
        std::size_t overwriteSize) noexcept -> bool {
    if (!LoaderContext || expected.empty()
            || expected.size()
                > (std::numeric_limits<std::uint32_t>::max)()
            || overwriteSize
                > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    return LoaderContext->PatchJmpRel32(
        rva,
        expected.data(),
        static_cast<std::uint32_t>(expected.size()),
        targetRva,
        static_cast<std::uint32_t>(overwriteSize));
}

auto PatchPublicationU32(
        void*,
        std::uintptr_t rva,
        std::span<const std::uint8_t> expected,
        std::uint32_t replacement) noexcept -> bool {
    if (!LoaderContext || expected.empty()
            || expected.size()
                > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    return LoaderContext->PatchWriteU32(
        rva,
        expected.data(),
        static_cast<std::uint32_t>(expected.size()),
        replacement);
}

auto PatchPublicationCodecByte(
        void*,
        std::uintptr_t rva,
        std::uint8_t expected,
        std::uint8_t replacement) noexcept -> bool {
    return LoaderContext
        && LoaderContext->PatchWriteU8(rva, &expected, 1U, replacement);
}

auto FlushPublicationInstructionCache(
        void*,
        std::uintptr_t firstRva,
        std::size_t size) noexcept -> bool {
    return LoaderBase && ValidateImageTarget(firstRva, size, true)
        && FlushInstructionCache(
            GetCurrentProcess(), LoaderBase + firstRva, size) != 0;
}

auto MarkG0TailCommitted(void*) noexcept -> void {
    TailPatchInstalled = true;
}

auto ActivateG0Guard(void*) noexcept -> void {
    if (!State) FailClosed("G0 guard state is unavailable", 0);
    InterlockedExchange(&State->capMayBeExtended, 1);
}

auto MarkG0CapCommitted(void*) noexcept -> void {
    CapPatchInstalled = true;
}

auto MarkG10ReaderCommitted(void*) noexcept -> void {
    PersistenceReaderPatchInstalled = true;
}

auto MarkG10WriterCommitted(void*) noexcept -> void {
    PersistenceWriterPatchInstalled = true;
}

auto ReservePublicationProcessLifetime(void*) noexcept -> void {
    AnyMutationInstalled = true;
}

auto PublishPublicationReadiness(void*) noexcept -> void {
    if (!State || !PersistenceState) {
        FailClosed("publication readiness state is unavailable", 0);
    }
    InterlockedExchange(&PersistenceState->codecReady, 1);
    InterlockedExchange(&PersistenceState->itemTransportReady, 1);
    InterlockedExchange(&PersistenceState->operational, 1);
    // G0/global operational is deliberately the final visible readiness bit.
    InterlockedExchange(&State->operational, 1);
}

auto MarkPublicationPoisoned(void*) noexcept -> void {
    if (PersistenceState) {
        InterlockedExchange(&PersistenceState->itemTransportReady, 0);
        InterlockedExchange(&PersistenceState->codecReady, 0);
        InterlockedExchange(&PersistenceState->operational, 0);
    }
    if (State) InterlockedExchange(&State->operational, 0);
    ColdRestartRequired = true;
    FailClosed(
        "canonical publication was poisoned after a native mutation attempt",
        LastRowCount.load(std::memory_order_acquire));
}

} // namespace

auto InspectLoaderCompileProviderContract(
        const std::uint8_t* base,
        std::size_t imageSize) noexcept -> LoaderCompileProviderKind {
    constexpr auto PrefixSize = LoaderCompileCallInstructionOffset + 1U;
    static_assert(PrefixSize <= LoaderCompileCallBytes.size());
    if (!base
            || LoaderCompileCallRva > imageSize
            || LoaderCompileCallBytes.size()
                > imageSize - LoaderCompileCallRva
            || !IsAccessibleRange(
                base + LoaderCompileCallRva,
                LoaderCompileCallBytes.size(),
                false,
                true)) {
        return LoaderCompileProviderKind::Invalid;
    }

    std::array<std::uint8_t, LoaderCompileCallBytes.size()> live{};
    if (!SafeCopyReadable(
            base + LoaderCompileCallRva,
            live.data(),
            live.size())
            || std::memcmp(
                live.data(), LoaderCompileCallBytes.data(), PrefixSize) != 0) {
        return LoaderCompileProviderKind::Invalid;
    }

    std::int32_t displacement{};
    std::memcpy(
        &displacement,
        live.data() + LoaderCompileCallInstructionOffset + 1U,
        sizeof(displacement));
    const auto instruction = reinterpret_cast<std::uintptr_t>(base)
        + LoaderCompileCallRva + LoaderCompileCallInstructionOffset;
    if (instruction
            > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
        return LoaderCompileProviderKind::Invalid;
    }
    std::uintptr_t callTarget{};
    if (!ApplySignedDisplacement(
            instruction + 5U, displacement, callTarget)) {
        return LoaderCompileProviderKind::Invalid;
    }
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    if (NativeGenericCompileRva <= imageSize
            && callTarget == baseAddress + NativeGenericCompileRva) {
        return LoaderCompileProviderKind::NativeGenericCompiler;
    }

    const auto core = GetModuleHandleW(L"D2RCore.dll");
    const auto loadExcelExport = core
        ? GetProcAddress(core, "LoadExcelTable") : nullptr;
    if (!loadExcelExport) {
        return LoaderCompileProviderKind::Invalid;
    }
    const auto loadExcelExportAddress = reinterpret_cast<std::uintptr_t>(
        loadExcelExport);
    if (!ValidateD2RCoreLoadExcelProviderAbi(
            base, core, loadExcelExportAddress)) {
        return LoaderCompileProviderKind::Invalid;
    }

    std::array<std::uintptr_t, 5> visited{};
    auto current = callTarget;
    for (std::size_t depth{}; depth < visited.size(); ++depth) {
        if (current == loadExcelExportAddress) {
            return LoaderCompileProviderKind::D2RCoreLoadExcelTable;
        }
        for (std::size_t index{}; index < depth; ++index) {
            if (visited[index] == current) {
                return LoaderCompileProviderKind::Invalid;
            }
        }
        visited[depth] = current;
        std::uintptr_t next{};
        if (!ReadUnconditionalJumpTarget(current, next)) {
            return LoaderCompileProviderKind::Invalid;
        }
        current = next;
    }
    return LoaderCompileProviderKind::Invalid;
}

auto InspectPlayerSaveStatWriterProviderContract(
        const std::uint8_t* base,
        std::size_t imageSize) noexcept -> PlayerSaveStatWriterProviderKind {
    constexpr auto PrefixSize =
        PlayerSaveStatWriterCallInstructionOffset + 1U;
    static_assert(PrefixSize <= PlayerWriterIdBytes.size());
    if (!base
            || PlayerSaveStatWriterCallRva > imageSize
            || PlayerWriterIdBytes.size()
                > imageSize - PlayerSaveStatWriterCallRva
            || !IsAccessibleRange(
                base + PlayerSaveStatWriterCallRva,
                PlayerWriterIdBytes.size(),
                false,
                true)) {
        return PlayerSaveStatWriterProviderKind::Invalid;
    }

    std::array<std::uint8_t, PlayerWriterIdBytes.size()> live{};
    if (!SafeCopyReadable(
            base + PlayerSaveStatWriterCallRva,
            live.data(),
            live.size())
            || std::memcmp(
                live.data(), PlayerWriterIdBytes.data(), PrefixSize) != 0) {
        return PlayerSaveStatWriterProviderKind::Invalid;
    }

    std::int32_t displacement{};
    std::memcpy(
        &displacement,
        live.data() + PlayerSaveStatWriterCallInstructionOffset + 1U,
        sizeof(displacement));
    const auto instruction = reinterpret_cast<std::uintptr_t>(base)
        + PlayerSaveStatWriterCallRva
        + PlayerSaveStatWriterCallInstructionOffset;
    if (instruction
            > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
        return PlayerSaveStatWriterProviderKind::Invalid;
    }
    std::uintptr_t callTarget{};
    if (!ApplySignedDisplacement(
            instruction + 5U, displacement, callTarget)) {
        return PlayerSaveStatWriterProviderKind::Invalid;
    }
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    if (NativeBitWriterRva <= imageSize
            && callTarget == baseAddress + NativeBitWriterRva) {
        return PlayerSaveStatWriterProviderKind::NativeBitWriter;
    }

    const auto core = GetModuleHandleW(L"D2RCore.dll");
    const auto writerExport = core
        ? GetProcAddress(core, "WritePlayerSaveStatId") : nullptr;
    if (!writerExport) {
        return PlayerSaveStatWriterProviderKind::Invalid;
    }
    const auto writerExportAddress = reinterpret_cast<std::uintptr_t>(
        writerExport);
    if (!ValidateD2RCorePlayerSaveStatWriterAbi(
            base, imageSize, core, writerExportAddress)) {
        return PlayerSaveStatWriterProviderKind::Invalid;
    }

    std::array<std::uintptr_t, 5> visited{};
    auto current = callTarget;
    for (std::size_t depth{}; depth < visited.size(); ++depth) {
        if (current == writerExportAddress) {
            return PlayerSaveStatWriterProviderKind::
                D2RCoreWritePlayerSaveStatId;
        }
        for (std::size_t index{}; index < depth; ++index) {
            if (visited[index] == current) {
                return PlayerSaveStatWriterProviderKind::Invalid;
            }
        }
        visited[depth] = current;
        std::uintptr_t next{};
        if (!ReadUnconditionalJumpTarget(current, next)) {
            return PlayerSaveStatWriterProviderKind::Invalid;
        }
        current = next;
    }
    return PlayerSaveStatWriterProviderKind::Invalid;
}

auto InspectItemSaveStatWriterProviderContract(
        const std::uint8_t* base,
        std::size_t imageSize) noexcept -> ItemSaveStatWriterProviderKind {
    constexpr auto PrefixSize =
        ItemSaveStatWriterCallInstructionOffset + 1U;
    static_assert(PrefixSize <= GenericItemBoundedWriterBytes.size());
    if (!base
            || ItemSaveStatWriterCallRva > imageSize
            || GenericItemBoundedWriterBytes.size()
                > imageSize - ItemSaveStatWriterCallRva
            || !IsAccessibleRange(
                base + ItemSaveStatWriterCallRva,
                GenericItemBoundedWriterBytes.size(),
                false,
                true)) {
        return ItemSaveStatWriterProviderKind::Invalid;
    }

    std::array<std::uint8_t, GenericItemBoundedWriterBytes.size()> live{};
    if (!SafeCopyReadable(
            base + ItemSaveStatWriterCallRva,
            live.data(),
            live.size())
            || std::memcmp(
                live.data(),
                GenericItemBoundedWriterBytes.data(),
                PrefixSize) != 0) {
        return ItemSaveStatWriterProviderKind::Invalid;
    }

    std::int32_t displacement{};
    std::memcpy(
        &displacement,
        live.data() + ItemSaveStatWriterCallInstructionOffset + 1U,
        sizeof(displacement));
    const auto instruction = reinterpret_cast<std::uintptr_t>(base)
        + ItemSaveStatWriterCallRva + ItemSaveStatWriterCallInstructionOffset;
    if (instruction
            > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
        return ItemSaveStatWriterProviderKind::Invalid;
    }
    std::uintptr_t callTarget{};
    if (!ApplySignedDisplacement(
            instruction + 5U, displacement, callTarget)) {
        return ItemSaveStatWriterProviderKind::Invalid;
    }
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    if (NativeBitWriterRva <= imageSize
            && callTarget == baseAddress + NativeBitWriterRva) {
        return ItemSaveStatWriterProviderKind::NativeBitWriter;
    }

    const auto core = GetModuleHandleW(L"D2RCore.dll");
    const auto writerExport = core
        ? GetProcAddress(core, "WriteItemSaveStatId") : nullptr;
    if (!writerExport) {
        return ItemSaveStatWriterProviderKind::Invalid;
    }
    const auto writerExportAddress = reinterpret_cast<std::uintptr_t>(
        writerExport);
    if (!ValidateD2RCoreItemSaveStatWriterAbi(
            base, imageSize, core, writerExportAddress)) {
        return ItemSaveStatWriterProviderKind::Invalid;
    }

    std::array<std::uintptr_t, 5> visited{};
    auto current = callTarget;
    for (std::size_t depth{}; depth < visited.size(); ++depth) {
        if (current == writerExportAddress) {
            return ItemSaveStatWriterProviderKind::D2RCoreWriteItemSaveStatId;
        }
        for (std::size_t index{}; index < depth; ++index) {
            if (visited[index] == current) {
                return ItemSaveStatWriterProviderKind::Invalid;
            }
        }
        visited[depth] = current;
        std::uintptr_t next{};
        if (!ReadUnconditionalJumpTarget(current, next)) {
            return ItemSaveStatWriterProviderKind::Invalid;
        }
        current = next;
    }
    return ItemSaveStatWriterProviderKind::Invalid;
}

auto InspectPlayerSaveProviderContract(
        const std::uint8_t* base,
        std::size_t imageSize) noexcept -> PlayerSaveProviderKind {
    constexpr auto D2RCoreDynamicCapacityBytes =
            std::to_array<std::uint8_t>({
        0x41,0xBD,0xFF,0xFF,0x00,0x00,0x66,0x90,0x33,
        0xD2,0x49,0x8B,0xCE,0xE8,0x46,0x6B,0x06,0x00,
    });
    constexpr auto CallPrefixSize =
        PlayerSaveDynamicCallInstructionOffset + 1U;
    static_assert(
        D2RCoreDynamicCapacityBytes.size()
            == PlayerSaveDynamicCapacityBytes.size());
    static_assert(CallPrefixSize <= PlayerSaveDynamicCallBytes.size());
    if (!base
            || PlayerSaveDynamicCapacityRva > imageSize
            || PlayerSaveDynamicCapacityBytes.size()
                > imageSize - PlayerSaveDynamicCapacityRva
            || PlayerSaveDynamicCallRva > imageSize
            || PlayerSaveDynamicCallBytes.size()
                > imageSize - PlayerSaveDynamicCallRva
            || !IsAccessibleRange(
                base + PlayerSaveDynamicCapacityRva,
                PlayerSaveDynamicCapacityBytes.size(),
                false,
                true)
            || !IsAccessibleRange(
                base + PlayerSaveDynamicCallRva,
                PlayerSaveDynamicCallBytes.size(),
                false,
                true)) {
        return PlayerSaveProviderKind::Invalid;
    }

    std::array<std::uint8_t, PlayerSaveDynamicCapacityBytes.size()>
        liveCapacity{};
    std::array<std::uint8_t, PlayerSaveDynamicCallBytes.size()> liveCall{};
    if (!SafeCopyReadable(
            base + PlayerSaveDynamicCapacityRva,
            liveCapacity.data(),
            liveCapacity.size())
            || !SafeCopyReadable(
                base + PlayerSaveDynamicCallRva,
                liveCall.data(),
                liveCall.size())
            || std::memcmp(
                liveCall.data(),
                PlayerSaveDynamicCallBytes.data(),
                CallPrefixSize) != 0) {
        return PlayerSaveProviderKind::Invalid;
    }

    const bool nativeCapacity =
        liveCapacity == PlayerSaveDynamicCapacityBytes;
    const bool d2rCoreCapacity =
        liveCapacity == D2RCoreDynamicCapacityBytes;
    if (!nativeCapacity && !d2rCoreCapacity) {
        return PlayerSaveProviderKind::Invalid;
    }

    std::int32_t displacement{};
    std::memcpy(
        &displacement,
        liveCall.data() + PlayerSaveDynamicCallInstructionOffset + 1U,
        sizeof(displacement));
    const auto instruction = reinterpret_cast<std::uintptr_t>(base)
        + PlayerSaveDynamicCallRva
        + PlayerSaveDynamicCallInstructionOffset;
    if (instruction
            > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
        return PlayerSaveProviderKind::Invalid;
    }
    std::uintptr_t callTarget{};
    if (!ApplySignedDisplacement(
            instruction + 5U, displacement, callTarget)) {
        return PlayerSaveProviderKind::Invalid;
    }

    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    if (nativeCapacity
            && NativePlayerSaveRva <= imageSize
            && callTarget == baseAddress + NativePlayerSaveRva) {
        return PlayerSaveProviderKind::NativePlayerSave;
    }
    if (!d2rCoreCapacity) return PlayerSaveProviderKind::Invalid;

    const auto core = GetModuleHandleW(L"D2RCore.dll");
    const auto providerExport = core
        ? GetProcAddress(
            core, "WritePlayerSaveWithEnvironmentCapture")
        : nullptr;
    if (!providerExport) return PlayerSaveProviderKind::Invalid;
    const auto providerExportAddress = reinterpret_cast<std::uintptr_t>(
        providerExport);
    if (!ValidateD2RCorePlayerSaveProviderAbi(
            base,
            imageSize,
            core,
            providerExportAddress)) {
        return PlayerSaveProviderKind::Invalid;
    }

    std::array<std::uintptr_t, 5> visited{};
    auto current = callTarget;
    for (std::size_t depth{}; depth < visited.size(); ++depth) {
        if (current == providerExportAddress) {
            return PlayerSaveProviderKind::
                D2RCoreWritePlayerSaveWithEnvironmentCapture;
        }
        for (std::size_t index{}; index < depth; ++index) {
            if (visited[index] == current) {
                return PlayerSaveProviderKind::Invalid;
            }
        }
        visited[depth] = current;
        std::uintptr_t next{};
        if (!ReadUnconditionalJumpTarget(current, next)) {
            return PlayerSaveProviderKind::Invalid;
        }
        current = next;
    }
    return PlayerSaveProviderKind::Invalid;
}

auto InspectD2SItemReadProviderContract(
        const std::uint8_t* base,
        std::size_t imageSize) noexcept -> D2SItemReadProviderKind {
    constexpr auto CallPrefixSize =
        D2SContainerVersionForwardCallOffset + 1U;
    static_assert(
        D2SContainerVersionForwardBytes[
            D2SContainerVersionForwardCallOffset] == 0xE8U);
    static_assert(CallPrefixSize <= D2SContainerVersionForwardBytes.size());
    if (!base
            || D2SContainerVersionForwardRva > imageSize
            || D2SContainerVersionForwardBytes.size()
                > imageSize - D2SContainerVersionForwardRva
            || !IsAccessibleRange(
                base + D2SContainerVersionForwardRva,
                D2SContainerVersionForwardBytes.size(),
                false,
                true)) {
        return D2SItemReadProviderKind::Invalid;
    }

    std::array<std::uint8_t, D2SContainerVersionForwardBytes.size()> live{};
    if (!SafeCopyReadable(
            base + D2SContainerVersionForwardRva,
            live.data(),
            live.size())
            || std::memcmp(
                live.data(),
                D2SContainerVersionForwardBytes.data(),
                CallPrefixSize) != 0
            || std::memcmp(
                live.data() + D2SContainerVersionForwardCallOffset + 5U,
                D2SContainerVersionForwardBytes.data()
                    + D2SContainerVersionForwardCallOffset + 5U,
                D2SContainerVersionForwardBytes.size()
                    - D2SContainerVersionForwardCallOffset - 5U) != 0) {
        return D2SItemReadProviderKind::Invalid;
    }

    std::int32_t displacement{};
    std::memcpy(
        &displacement,
        live.data() + D2SContainerVersionForwardCallOffset + 1U,
        sizeof(displacement));
    const auto instruction = reinterpret_cast<std::uintptr_t>(base)
        + D2SContainerVersionForwardRva
        + D2SContainerVersionForwardCallOffset;
    if (instruction
            > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
        return D2SItemReadProviderKind::Invalid;
    }
    std::uintptr_t callTarget{};
    if (!ApplySignedDisplacement(
            instruction + 5U, displacement, callTarget)) {
        return D2SItemReadProviderKind::Invalid;
    }

    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    if (NativeReadItemsByVersionRva <= imageSize
            && callTarget == baseAddress + NativeReadItemsByVersionRva) {
        return D2SItemReadProviderKind::NativeReadItemsByVersion;
    }

    const auto core = GetModuleHandleW(L"D2RCore.dll");
    const auto providerExport = core
        ? GetProcAddress(core, "ReadItemsByVersion")
        : nullptr;
    if (!providerExport) return D2SItemReadProviderKind::Invalid;
    const auto providerExportAddress = reinterpret_cast<std::uintptr_t>(
        providerExport);
    if (!ValidateD2RCoreReadItemsByVersionAbi(
            base,
            imageSize,
            core,
            providerExportAddress)) {
        return D2SItemReadProviderKind::Invalid;
    }

    std::array<std::uintptr_t, 5> visited{};
    auto current = callTarget;
    for (std::size_t depth{}; depth < visited.size(); ++depth) {
        if (current == providerExportAddress) {
            return D2SItemReadProviderKind::D2RCoreReadItemsByVersion;
        }
        for (std::size_t index{}; index < depth; ++index) {
            if (visited[index] == current) {
                return D2SItemReadProviderKind::Invalid;
            }
        }
        visited[depth] = current;
        std::uintptr_t next{};
        if (!ReadUnconditionalJumpTarget(current, next)) {
            return D2SItemReadProviderKind::Invalid;
        }
        current = next;
    }
    return D2SItemReadProviderKind::Invalid;
}

auto InspectD2SSaveIoProviderContract(
        const std::uint8_t* base,
        std::size_t imageSize) noexcept -> D2SSaveIoProviderKind {
    static_assert(
        SaveObjectWriterBufferLayoutWitnessBytes[
            D2SSaveWriterProviderCallOffset] == 0xE8U);
    static_assert(
        SaveObjectWriterCommittedContinuationBytes[
            D2SSaveCloseProviderCallOffset] == 0xE8U);
    if (!base
            || D2SSaveWriterProviderCallRva > imageSize
            || SaveObjectWriterBufferLayoutWitnessBytes.size()
                > imageSize - D2SSaveWriterProviderCallRva
            || D2SSaveCloseProviderCallRva > imageSize
            || SaveObjectWriterCommittedContinuationBytes.size()
                > imageSize - D2SSaveCloseProviderCallRva
            || !IsAccessibleRange(
                base + D2SSaveWriterProviderCallRva,
                SaveObjectWriterBufferLayoutWitnessBytes.size(),
                false,
                true)
            || !IsAccessibleRange(
                base + D2SSaveCloseProviderCallRva,
                SaveObjectWriterCommittedContinuationBytes.size(),
                false,
                true)) {
        return D2SSaveIoProviderKind::Invalid;
    }

    std::array<
        std::uint8_t,
        SaveObjectWriterBufferLayoutWitnessBytes.size()> liveWriter{};
    std::array<
        std::uint8_t,
        SaveObjectWriterCommittedContinuationBytes.size()> liveClose{};
    if (!SafeCopyReadable(
            base + D2SSaveWriterProviderCallRva,
            liveWriter.data(),
            liveWriter.size())
            || !SafeCopyReadable(
                base + D2SSaveCloseProviderCallRva,
                liveClose.data(),
                liveClose.size())) {
        return D2SSaveIoProviderKind::Invalid;
    }

    const auto matchesRel32Variant = [](
            const auto& live,
            const auto& expected,
            std::size_t callOffset) noexcept -> bool {
        return callOffset <= live.size()
            && 5U <= live.size() - callOffset
            && live.size() == expected.size()
            && live[callOffset] == 0xE8U
            && expected[callOffset] == 0xE8U
            && std::memcmp(
                live.data(), expected.data(), callOffset + 1U) == 0
            && std::memcmp(
                live.data() + callOffset + 5U,
                expected.data() + callOffset + 5U,
                live.size() - callOffset - 5U) == 0;
    };
    if (!matchesRel32Variant(
            liveWriter,
            SaveObjectWriterBufferLayoutWitnessBytes,
            D2SSaveWriterProviderCallOffset)
            || !matchesRel32Variant(
                liveClose,
                SaveObjectWriterCommittedContinuationBytes,
                D2SSaveCloseProviderCallOffset)) {
        return D2SSaveIoProviderKind::Invalid;
    }

    const auto resolveCall = [base](
            const auto& live,
            std::uintptr_t siteRva,
            std::size_t callOffset,
            std::uintptr_t& target) noexcept -> bool {
        std::int32_t displacement{};
        std::memcpy(
            &displacement,
            live.data() + callOffset + 1U,
            sizeof(displacement));
        const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
        if (baseAddress
                > (std::numeric_limits<std::uintptr_t>::max)() - siteRva
                || baseAddress + siteRva
                    > (std::numeric_limits<std::uintptr_t>::max)()
                        - callOffset
                || baseAddress + siteRva + callOffset
                    > (std::numeric_limits<std::uintptr_t>::max)() - 5U) {
            return false;
        }
        return ApplySignedDisplacement(
            baseAddress + siteRva + callOffset + 5U,
            displacement,
            target);
    };
    std::uintptr_t writerTarget{};
    std::uintptr_t closeTarget{};
    if (!resolveCall(
            liveWriter,
            D2SSaveWriterProviderCallRva,
            D2SSaveWriterProviderCallOffset,
            writerTarget)
            || !resolveCall(
                liveClose,
                D2SSaveCloseProviderCallRva,
                D2SSaveCloseProviderCallOffset,
                closeTarget)) {
        return D2SSaveIoProviderKind::Invalid;
    }

    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    if (baseAddress
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - NativeD2SSaveWriterRva
            || baseAddress
                > (std::numeric_limits<std::uintptr_t>::max)()
                    - NativeD2SSaveCloseRva) {
        return D2SSaveIoProviderKind::Invalid;
    }
    if (NativeD2SSaveWriterRva <= imageSize
            && NativeD2SSaveCloseRva <= imageSize
            && writerTarget == baseAddress + NativeD2SSaveWriterRva
            && closeTarget == baseAddress + NativeD2SSaveCloseRva) {
        return D2SSaveIoProviderKind::NativeWriteAndClose;
    }

    const auto core = GetModuleHandleW(L"D2RCore.dll");
    const auto writerExport = core
        ? GetProcAddress(core, "WriteD2sFileWithEnvironment") : nullptr;
    const auto closeExport = core
        ? GetProcAddress(core, "CloseD2sFileWithEnvironment") : nullptr;
    if (!writerExport || !closeExport) {
        return D2SSaveIoProviderKind::Invalid;
    }
    const auto writerExportAddress = reinterpret_cast<std::uintptr_t>(
        writerExport);
    const auto closeExportAddress = reinterpret_cast<std::uintptr_t>(
        closeExport);
    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    if (writerExportAddress < coreBase || closeExportAddress < coreBase) {
        return D2SSaveIoProviderKind::Invalid;
    }
    const auto writerProviderRva = writerExportAddress - coreBase;
    const auto closeProviderRva = closeExportAddress - coreBase;
    const bool matchingProviderGeneration =
        (writerProviderRva == 0x6365E0U
            && closeProviderRva == 0x6393B0U)
        || (writerProviderRva == 0x565640U
            && closeProviderRva == 0x567E00U);
    if (!matchingProviderGeneration
            || !ValidateD2RCoreWriteD2SSaveProviderAbi(
            base,
            imageSize,
            core,
            writerExportAddress)
            || !ValidateD2RCoreCloseD2SSaveProviderAbi(
                base,
                imageSize,
                core,
                closeExportAddress)) {
        return D2SSaveIoProviderKind::Invalid;
    }

    const auto resolvesProvider = [](
            std::uintptr_t current,
            std::uintptr_t expected) noexcept -> bool {
        std::array<std::uintptr_t, 5> visited{};
        for (std::size_t depth{}; depth < visited.size(); ++depth) {
            if (current == expected) return true;
            for (std::size_t index{}; index < depth; ++index) {
                if (visited[index] == current) return false;
            }
            visited[depth] = current;
            std::uintptr_t next{};
            if (!ReadUnconditionalJumpTarget(current, next)) return false;
            current = next;
        }
        return false;
    };
    return resolvesProvider(writerTarget, writerExportAddress)
            && resolvesProvider(closeTarget, closeExportAddress)
        ? D2SSaveIoProviderKind::D2RCoreWriteAndCloseWithEnvironment
        : D2SSaveIoProviderKind::Invalid;
}

extern "C" auto ISC12InvokeItemAction9CNative(
        void* client,
        void* item,
        std::uint8_t action,
        std::uint32_t temporaryFlags,
        std::uint32_t gamble) noexcept -> void {
    const auto transport = ResolveFullItemTransportProvider();
    if (transport == FullItemTransportProvider::ExtendedItemStatsV1) {
        if (!PersistencePacket9CTrampoline) {
            FailClosed("full-item 0x9C trampoline is unavailable", 0);
        }
        __try {
            PersistencePacket9CTrampoline(
                client, item, action, temporaryFlags, gamble);
        } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
            FailClosed(
                "external full-item 0x9C producer raised an exception",
                0);
        }
        return;
    }
    const auto admission = BeginFullItemPacketProducer(
        FullItemPacketTransaction,
        FullItemProducerDescriptor{
            .kind = FullItemPacketKind::ItemAction9C,
            .client = client,
            .item = item,
            .action = action,
            .temporaryFlags = temporaryFlags,
            .gamble = gamble,
        });
    if (FullItemPacketTransaction.state
            == FullItemPacketStagingState::Fatal) {
        FailClosed("full-item 0x9C producer admission became fatal", 0);
    }
    __try {
        __try {
            if (admission.disposition
                    == FullItemProducerDisposition::InvokeOriginal) {
                if (!PersistencePacket9CTrampoline) {
                    FailClosed("full-item 0x9C trampoline is unavailable", 0);
                }
                PersistencePacket9CTrampoline(
                    client, item, action, temporaryFlags, gamble);
            }
        } __finally {
            CompleteFullItemProducer(
                admission.token, AbnormalTermination() != FALSE);
        }
    } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
        FailClosed("full-item 0x9C producer raised an exception", 0);
    }
}

extern "C" auto ISC12InvokeItemAction9DNative(
        void* client,
        void* parentItem,
        void* item,
        std::uint8_t action,
        std::uint32_t temporaryFlags,
        std::uint32_t gamble) noexcept -> void {
    const auto transport = ResolveFullItemTransportProvider();
    if (transport == FullItemTransportProvider::ExtendedItemStatsV1) {
        if (!PersistencePacket9DTrampoline) {
            FailClosed("full-item 0x9D trampoline is unavailable", 0);
        }
        __try {
            PersistencePacket9DTrampoline(
                client,
                parentItem,
                item,
                action,
                temporaryFlags,
                gamble);
        } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
            FailClosed(
                "external full-item 0x9D producer raised an exception",
                0);
        }
        return;
    }
    const auto admission = BeginFullItemPacketProducer(
        FullItemPacketTransaction,
        FullItemProducerDescriptor{
            .kind = FullItemPacketKind::ItemAction9D,
            .client = client,
            .parentItem = parentItem,
            .item = item,
            .action = action,
            .temporaryFlags = temporaryFlags,
            .gamble = gamble,
        });
    if (FullItemPacketTransaction.state
            == FullItemPacketStagingState::Fatal) {
        FailClosed("full-item 0x9D producer admission became fatal", 0);
    }
    __try {
        __try {
            if (admission.disposition
                    == FullItemProducerDisposition::InvokeOriginal) {
                if (!PersistencePacket9DTrampoline) {
                    FailClosed("full-item 0x9D trampoline is unavailable", 0);
                }
                PersistencePacket9DTrampoline(
                    client,
                    parentItem,
                    item,
                    action,
                    temporaryFlags,
                    gamble);
            }
        } __finally {
            CompleteFullItemProducer(
                admission.token, AbnormalTermination() != FALSE);
        }
    } __except (GuardedCodecExceptionFilter(GetExceptionCode())) {
        FailClosed("full-item 0x9D producer raised an exception", 0);
    }
}

extern "C" auto ISC12CaptureItemAction9CQueue(
        void* client,
        const std::uint8_t* bytes,
        std::size_t length) noexcept -> void {
    const auto transport = ResolveFullItemTransportProvider();
    if (transport == FullItemTransportProvider::ExtendedItemStatsV1) {
        QueueFullItemPacket(nullptr, client, bytes, length);
        return;
    }
    const auto result = CaptureFullItemPacketQueueCall(
        FullItemPacketTransaction,
        FullItemPacketKind::ItemAction9C,
        client,
        bytes,
        length);
    if (result == FullItemPacketStagingError::None) {
        FullItemPacketsCaptured9C.fetch_add(1, std::memory_order_relaxed);
    }
    if (FullItemPacketTransaction.state
            == FullItemPacketStagingState::Fatal) {
        FailClosed("full-item 0x9C queue relay was reached out of contract", 0);
    }
}

extern "C" auto ISC12CaptureItemAction9DQueue(
        void* client,
        const std::uint8_t* bytes,
        std::size_t length) noexcept -> void {
    const auto transport = ResolveFullItemTransportProvider();
    if (transport == FullItemTransportProvider::ExtendedItemStatsV1) {
        QueueFullItemPacket(nullptr, client, bytes, length);
        return;
    }
    const auto result = CaptureFullItemPacketQueueCall(
        FullItemPacketTransaction,
        FullItemPacketKind::ItemAction9D,
        client,
        bytes,
        length);
    if (result == FullItemPacketStagingError::None) {
        FullItemPacketsCaptured9D.fetch_add(1, std::memory_order_relaxed);
    }
    if (FullItemPacketTransaction.state
            == FullItemPacketStagingState::Fatal) {
        FailClosed("full-item 0x9D queue relay was reached out of contract", 0);
    }
}

extern "C" auto ISC12ReadAuxiliaryWithPreflight(
        void* context,
        void* unit,
        std::uint8_t** cursor,
        std::uint8_t* end,
        std::uint32_t version,
        std::int32_t count) noexcept -> std::int32_t {
    return ReadPlayerStatsWithPreflight(
        NativeAuxiliaryReader,
        PlayerStatStreamKind::Auxiliary,
        context,
        unit,
        cursor,
        end,
        version,
        count);
}

extern "C" auto ISC12ReadRegularWithPreflight(
        void* context,
        void* unit,
        std::uint8_t** cursor,
        std::uint8_t* end,
        std::uint32_t version,
        std::int32_t count) noexcept -> std::int32_t {
    return ReadPlayerStatsWithPreflight(
        NativePlayerReader,
        PlayerStatStreamKind::Regular,
        context,
        unit,
        cursor,
        end,
        version,
        count);
}

extern "C" auto ISC12CopyPreviewWithPreflight(
        void* object,
        void* destination,
        std::uint32_t capacity,
        std::uint64_t offset) noexcept -> std::uint32_t {
    return CopyPlayerPreviewWithPreflight(
        object, destination, capacity, offset);
}

extern "C" auto ISC12BuildDescriptionIndex(void* dataTables) noexcept
        -> std::uint32_t {
    try {
        return BuildDescriptionIndexNative(dataTables);
    } catch (...) {
        if (NativeVectorMutationStarted
                || !CurrentRowCountKnown
                || CurrentRowCount > LegacySerializedSentinel) {
            FailClosed(
                NativeVectorMutationStarted
                    ? "exception after native vector mutation"
                    : "exception on an extended ItemStatCost table",
                CurrentRowCount);
        }
        return 0;
    }
}

extern "C" auto ISC12PrepareNativeStoreRead(
        void* object,
        std::uint64_t announcedLength,
        std::uint32_t actualLength,
        std::uint32_t nativeStatus) noexcept -> std::uint32_t {
    static_assert(
        static_cast<std::uint32_t>(
            NativePersistenceDisposition::ProceedNative) == 0);
    static_assert(
        static_cast<std::uint32_t>(
            NativePersistenceDisposition::Success) == 1);
    static_assert(
        static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject) == 2);
    static_assert(
        static_cast<std::uint32_t>(
            NativePersistenceDisposition::Fatal) == 3);

    std::array<char, MaximumNativeStoreNameLength + 1> nameStorage{};
    std::string_view storeName;
    if (!ReadNativeStoreName(object, nameStorage, storeName)) {
        if (!ClearRejectedNativeRead(object)) {
            FailClosed("native reader rejection cleanup failed", 0);
        }
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject);
    }
    if (ClassifyStoreName(storeName) == StoreKind::Other) {
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::ProceedNative);
    }

    std::uint32_t nativeIoSuccessCode{};
    if (!LoaderBase
            || !SafeRead(
                LoaderBase + NativeIoSuccessCodeRva,
                nativeIoSuccessCode)) {
        FailClosed("native I/O success code became unreadable", 0);
    }

    const bool codecReady = PersistenceState
        && InterlockedCompareExchange(
            &PersistenceState->codecReady, 0, 0) != 0;
    std::vector<std::uint8_t> physicalBytes;
    if (codecReady
            && !NativeReadMaySnapshot(
                nativeStatus,
                nativeIoSuccessCode,
                announcedLength,
                actualLength)) {
        if (!ClearRejectedNativeRead(object)) {
            FailClosed("native reader length rejection cleanup failed", 0);
        }
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject);
    }
    const std::uint64_t expectedLength = actualLength;
    if (codecReady
            && !SnapshotNativeObjectBuffer(
                object,
                &expectedLength,
                MaximumNativePhysicalStoreLength,
                physicalBytes)) {
        if (!ClearRejectedNativeRead(object)) {
            FailClosed("native reader buffer rejection cleanup failed", 0);
        }
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject);
    }

    const NativeReadRequest request{
        .storeName = storeName,
        .codecReady = codecReady,
        .readStatus = nativeStatus == nativeIoSuccessCode ? 0U : 1U,
        .announcedLength = announcedLength,
        .actualLength = actualLength,
        .physicalBytes = physicalBytes,
    };
    const auto result = AdaptNativeStoreRead(
        request,
        NativeReadCallbacks{
            .context = object,
            .clearRejectedRead = &ClearRejectedNativeRead,
        });
    if (result.disposition == NativePersistenceDisposition::Fatal) {
        FailClosed("native reader adapter entered a fatal state", 0);
    }
    const bool accepted =
        result.disposition == NativePersistenceDisposition::Success;
    if (accepted) {
        PersistenceReadsAccepted.fetch_add(1, std::memory_order_relaxed);
    } else if (result.disposition == NativePersistenceDisposition::Reject) {
        PersistenceReadsRejected.fetch_add(1, std::memory_order_relaxed);
    }
    if (DiagnosticsEnabled && LoaderContext
            && result.storeKind != StoreKind::Other) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "ISC12 diagnostics: standard-container read %s; store=%.*s; "
            "physical-bytes=%u; error=%u/%u.",
            accepted ? "accepted" : "rejected",
            static_cast<int>(storeName.size()),
            storeName.data(),
            actualLength,
            static_cast<unsigned>(result.error),
            static_cast<unsigned>(result.persistenceError));
        if (accepted) LoaderContext->LogInfo(message);
        else LoaderContext->LogWarn(message);
    }
    return static_cast<std::uint32_t>(result.disposition);
}

extern "C" auto ISC12PrepareNativeStoreWrite(
        void* object,
        const char* nativePath) noexcept -> std::uint32_t {
    std::array<char, MaximumNativeStoreNameLength + 1> nameStorage{};
    std::string_view storeName;
    if (!ReadNativeStoreName(object, nameStorage, storeName)) {
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject);
    }
    if (ClassifyStoreName(storeName) == StoreKind::Other) {
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::ProceedNative);
    }

    const bool codecReady = PersistenceState
        && InterlockedCompareExchange(
            &PersistenceState->codecReady, 0, 0) != 0;
    std::array<char, NativePersistencePathCapacity> pathStorage{};
    if (!nativePath
            || !SafeCopyReadable(
                nativePath, pathStorage.data(), pathStorage.size())) {
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject);
    }

    PublishedSchemaReadLease schemaLease;
    const bool schemaReady = codecReady && schemaLease.TryAcquire();
    std::vector<std::uint8_t> physicalBytes;
    if (codecReady && schemaReady
            && !SnapshotNativeObjectBuffer(
                object,
                nullptr,
                MaximumNativeInnerStoreLength,
                physicalBytes)) {
        return static_cast<std::uint32_t>(
            NativePersistenceDisposition::Reject);
    }

    const auto result = AdaptNativeStoreWrite(
        NativeWriteRequest{
            .storeName = storeName,
            .nativePathUtf8 = pathStorage,
            .codecReady = codecReady,
            .schemaReady = schemaReady,
            .physicalBytes = physicalBytes,
        });
    if (result.disposition == NativePersistenceDisposition::Fatal) {
        FailClosed("native writer adapter entered a fatal state", 0);
    }
    const bool delegated = result.storeKind != StoreKind::Other
        && result.disposition == NativePersistenceDisposition::ProceedNative;
    if (delegated) {
        PersistenceWritesDelegated.fetch_add(1, std::memory_order_relaxed);
    } else if (result.disposition == NativePersistenceDisposition::Reject) {
        PersistenceWritesRejected.fetch_add(1, std::memory_order_relaxed);
    }
    if (DiagnosticsEnabled && LoaderContext
            && result.storeKind != StoreKind::Other) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "ISC12 diagnostics: standard-container write %s; store=%.*s; "
            "physical-bytes=%zu; error=%u/%u.",
            delegated ? "delegated" : "rejected",
            static_cast<int>(storeName.size()),
            storeName.data(),
            physicalBytes.size(),
            static_cast<unsigned>(result.error),
            static_cast<unsigned>(result.persistenceError));
        if (delegated) LoaderContext->LogInfo(message);
        else LoaderContext->LogWarn(message);
    }
    return static_cast<std::uint32_t>(result.disposition);
}

auto PrepareLoaderExtension(
        const D2RL::PluginContext* context,
        std::uint8_t* base,
        std::size_t imageSize,
        FullItemTransportProvider initialTransportProvider,
        InspectFullItemTransportProviderFn inspectTransportProvider,
        bool diagnostics,
        std::string& error) noexcept -> bool {
    error.clear();
    if (!context || !base || imageSize == 0
            || context->exeBase != reinterpret_cast<std::uintptr_t>(base)
            || (initialTransportProvider
                    != FullItemTransportProvider::NativeG9
                && initialTransportProvider
                    != FullItemTransportProvider::ExtendedItemStatsV1)
            || !inspectTransportProvider) {
        return SetError(error, "loader extension received an invalid context");
    }
    if (AnyMutationInstalled) {
        return SetError(
            error,
            "loader extension cannot be prepared again after native mutation");
    }

    ReleaseUnpatchedResources();
    if (AnyMutationInstalled) {
        return SetError(
            error,
            "loader resources remain process-bound after unwind deregistration failure");
    }
    Prepared = false;
    PersistencePrepared = false;
    TailPatchInstalled = false;
    CapPatchInstalled = false;
    PersistenceReaderPatchInstalled = false;
    PersistenceWriterPatchInstalled = false;
    ColdRestartRequired = false;
    LoaderContext = context;
    LoaderBase = base;
    LoaderImageSize = imageSize;
    PreparedFullItemTransportProvider = initialTransportProvider;
    InspectFullItemTransportProvider = inspectTransportProvider;
    ActiveFullItemTransportProvider.store(
        initialTransportProvider
                == FullItemTransportProvider::ExtendedItemStatsV1
            ? initialTransportProvider
            : FullItemTransportProvider::Unresolved,
        std::memory_order_release);
    BuildCalls.store(0, std::memory_order_release);
    LastRowCount.store(0, std::memory_order_release);
    LastDescriptionCount.store(0, std::memory_order_release);
    DiagnosticsEnabled = diagnostics;
    FullItemRoot9C.store(0, std::memory_order_release);
    FullItemRoot9D.store(0, std::memory_order_release);
    FullItemTransactionsAccepted.store(0, std::memory_order_release);
    FullItemTransactionsRejected.store(0, std::memory_order_release);
    FullItemPacketsCaptured9C.store(0, std::memory_order_release);
    FullItemPacketsCaptured9D.store(0, std::memory_order_release);
    FullItemPacketsQueued.store(0, std::memory_order_release);
    PersistenceReadsAccepted.store(0, std::memory_order_release);
    PersistenceReadsRejected.store(0, std::memory_order_release);
    PlayerPreviewsAcceptedBeforeSchema.store(0, std::memory_order_release);
    PersistenceWritesDelegated.store(0, std::memory_order_release);
    PersistenceWritesRejected.store(0, std::memory_order_release);
    FullItemDiagnosticLines.store(0, std::memory_order_release);
    ResetPublishedSchemaSnapshot();

    if (DiagnosticsEnabled
            && !RunFullItemPacketStagingRuntimeSelfTest(error)) {
        return false;
    }

    if (!ValidateImageTarget(TailPatchRva, TailExpected.size(), true)
            || !ValidateImageTarget(
                CountImmediateRva, CountImmediateExpected.size(), true)
            || !ValidateImageTarget(NativeQsortRva, 1, true)
            || !ValidateImageTarget(NativeComparatorRva, 1, true)
            || !ValidateImageTarget(NativeVectorResizeRva, 1, true)
            || !ValidateImageTarget(NativeGetLinkNameCountRva, 1, true)
            || !ValidateImageTarget(NativeGetLinkNameRva, 1, true)
            || !ValidateImageTarget(
                PersistenceReaderPatchRva, 5, true)
            || !ValidateImageTarget(
                PersistenceReaderContinueRva, 1, true)
            || !ValidateImageTarget(
                PersistenceReaderRejectedRva, 1, true)
            || !ValidateImageTarget(
                PersistenceWriterPatchRva, 5, true)
            || !ValidateImageTarget(
                PersistenceWriterVanillaRva, 1, true)
            || !ValidateImageTarget(
                PersistenceWriterCommittedRva, 1, true)
            || !ValidateImageTarget(
                PersistenceWriterRejectedRva, 1, true)
            || !ValidateImageTarget(
                AuxiliaryReaderCallRva, 5, true)
            || !ValidateImageTarget(
                PlayerReaderPrimaryCallRva, 5, true)
            || !ValidateImageTarget(
                PlayerReaderLegacyCallRva, 5, true)
            || !ValidateImageTarget(
                PlayerPreviewCallRva, 5, true)
            || !ValidateImageTarget(
                PlayerSaveFinalizeCallRva, 5, true)
            || !ValidateImageTarget(
                Packet9CProducerEntryRva,
                Packet9CProducerEntryBytes.size(),
                true)
            || !ValidateImageTarget(
                Packet9DProducerEntryRva,
                Packet9DProducerEntryBytes.size(),
                true)
            || !ValidateImageTarget(
                Packet9CQueueCallRva,
                Packet9CQueueCallBytes.size(),
                true)
            || !ValidateImageTarget(
                Packet9DQueueCallRva,
                Packet9DQueueCallBytes.size(),
                true)
            || !ValidateImageTarget(
                NativeFullItemPacketQueueRva,
                NativeQueueEntryBytes.size(),
                true)
            || !ValidateImageTarget(
                NativeAuxiliaryReaderRva, 1, true)
            || !ValidateImageTarget(
                NativePlayerReaderRva, 1, true)
            || !ValidateImageTarget(
                NativePlayerPreviewCopyRva, 1, true)
            || !ValidateImageTarget(
                NativeByteBufferResizeRva, 1, true)
            || !ValidateImageTarget(
                NativeSetObjectStateRva, 1, true)
            || !ValidateImageTarget(
                NativeSetObjectAuxRva, 1, true)
            || !ValidateImageTarget(
                NativeIoSuccessCodeRva,
                sizeof(std::uint32_t),
                false)
            || !ValidateImageTarget(VanillaContinuationRva, 1, true)
            || !ValidateImageTarget(EpilogueRva, 1, true)) {
        return SetError(error, "loader extension target lies outside executable image");
    }

    NativeQsort = reinterpret_cast<NativeQsortFn>(LoaderBase + NativeQsortRva);
    NativeComparator = reinterpret_cast<NativeComparatorFn>(
        LoaderBase + NativeComparatorRva);
    NativeVectorResize = reinterpret_cast<NativeVectorResizeFn>(
        LoaderBase + NativeVectorResizeRva);
    NativeGetLinkNameCount = reinterpret_cast<NativeGetLinkNameCountFn>(
        LoaderBase + NativeGetLinkNameCountRva);
    NativeGetLinkName = reinterpret_cast<NativeGetLinkNameFn>(
        LoaderBase + NativeGetLinkNameRva);
    NativeByteBufferResize = reinterpret_cast<NativeByteBufferResizeFn>(
        LoaderBase + NativeByteBufferResizeRva);
    NativeSetObjectState = reinterpret_cast<NativeSetObjectStateFn>(
        LoaderBase + NativeSetObjectStateRva);
    NativeSetObjectAux = reinterpret_cast<NativeSetObjectAuxFn>(
        LoaderBase + NativeSetObjectAuxRva);
    NativeAuxiliaryReader = reinterpret_cast<NativePlayerStatReaderFn>(
        LoaderBase + NativeAuxiliaryReaderRva);
    NativePlayerReader = reinterpret_cast<NativePlayerStatReaderFn>(
        LoaderBase + NativePlayerReaderRva);
    NativePlayerPreviewCopy = reinterpret_cast<NativePlayerPreviewCopyFn>(
        LoaderBase + NativePlayerPreviewCopyRva);
    NativeFullItemPacketQueue =
        reinterpret_cast<NativeFullItemPacketQueueFn>(
            LoaderBase + NativeFullItemPacketQueueRva);
    if (!PrepareRelay(error)) return false;
    if (!PreparePersistenceRelay(error)) return false;

    const auto baseAddress = reinterpret_cast<std::uintptr_t>(LoaderBase);
    const auto g0RelayAddress = reinterpret_cast<std::uintptr_t>(RelayPage);
    const auto readerRelayAddress = reinterpret_cast<std::uintptr_t>(
        PersistenceReaderRelayEntry);
    const auto writerRelayAddress = reinterpret_cast<std::uintptr_t>(
        PersistenceWriterRelayEntry);
    if (g0RelayAddress < baseAddress
            || readerRelayAddress < baseAddress
            || writerRelayAddress < baseAddress
            || !PreparedPublicationAdapters.Bind(
                PublicationAdapterTargets{
                    .g0DescriptionRelayRva = g0RelayAddress - baseAddress,
                    .g10ReaderRelayRva = readerRelayAddress - baseAddress,
                    .g10WriterRelayRva = writerRelayAddress - baseAddress,
                    .codec = PreparedCodecActivationTargets,
                },
                PublicationAdapterNativeCallbacks{
                    .context = LoaderBase,
                    .verifyPattern = &VerifyPublicationPattern,
                    .patchRel32 = &PatchPublicationRel32,
                    .patchU32 = &PatchPublicationU32,
                    .writeCodecByte = &PatchPublicationCodecByte,
                    .flushInstructionCache =
                        &FlushPublicationInstructionCache,
                    .markG0TailCommitted = &MarkG0TailCommitted,
                    .activateG0Guard = &ActivateG0Guard,
                    .markG0CapCommitted = &MarkG0CapCommitted,
                    .markG10ReaderCommitted = &MarkG10ReaderCommitted,
                    .markG10WriterCommitted = &MarkG10WriterCommitted,
                    .reserveProcessLifetime =
                        &ReservePublicationProcessLifetime,
                    .publishReadiness = &PublishPublicationReadiness,
                    .markPoisoned = &MarkPublicationPoisoned,
                })) {
        return SetError(
            error,
            "native publication adapters could not bind prepared resources");
    }

    Prepared = true;
    return true;
}

#if defined(ISC12_CODEC_PATCH_TESTING)
auto InstallLoaderExtension(
        const NativePublicationLeaseView& quiescence,
        std::string& error) noexcept
        -> LoaderInstallResult {
    error.clear();
    if (!quiescence.IsHeld()) {
        SetError(error,
            "loader-owned native publication quiescence is unavailable");
        return LoaderInstallResult::QuiescenceRequired;
    }
    if (!Prepared || !PersistencePrepared
            || !LoaderContext || !LoaderBase || !RelayPage || !State
            || !PersistenceRelayPage || !PersistenceState
            || !PersistenceReaderRelayEntry
            || !PersistenceWriterRelayEntry
            || !PersistencePlayerSaveFinalizeRelayEntry
            || !PersistenceAuxiliaryReaderRelayEntry
            || !PersistencePlayerReaderRelayEntry
            || !PersistencePlayerPreviewRelayEntry
            || !PersistencePacket9CQueueRelayEntry
            || !PersistencePacket9DQueueRelayEntry
            || !PersistencePacket9CProducerRelayEntry
            || !PersistencePacket9DProducerRelayEntry
            || !PersistencePacket9CTrampoline
            || !PersistencePacket9DTrampoline
            || !gISC12CodecReturnExit
            || !gISC12ItemTransportReturnExit
            || !NativeAuxiliaryReader
            || !NativePlayerReader
            || !NativePlayerPreviewCopy
            || !NativeFullItemPacketQueue
            || PreparedCodecActivationTargets.AuxiliaryReaderRelayRva()
                == 0
            || PreparedCodecActivationTargets.PlayerReaderRelayRva()
                == 0
            || PreparedCodecActivationTargets.PlayerPreviewRelayRva()
                == 0
            || PreparedCodecActivationTargets.PlayerSaveFinalizeRelayRva()
                == 0
            || PreparedCodecActivationTargets.Packet9CQueueRelayRva() == 0
            || PreparedCodecActivationTargets.Packet9DQueueRelayRva() == 0
            || PreparedCodecActivationTargets.Packet9CEntryRelayRva() == 0
            || PreparedCodecActivationTargets.Packet9DEntryRelayRva() == 0) {
        SetError(error, "loader extension was not prepared");
        return LoaderInstallResult::FailedBeforeMutation;
    }

    const auto baseAddress = reinterpret_cast<std::uintptr_t>(LoaderBase);
    const auto relayAddress = reinterpret_cast<std::uintptr_t>(RelayPage);
    if (relayAddress < baseAddress
            || !CanEncodeRel32(baseAddress + TailPatchRva, relayAddress)) {
        SetError(error, "persistent relay is outside rel32 reach");
        return LoaderInstallResult::FailedBeforeMutation;
    }
    const auto relayRva = relayAddress - baseAddress;
    const auto result = CommitLoaderMutation(
        quiescence,
        [&]() noexcept {
            // A false PluginSDK result does not prove that this non-aligned
            // seam remained untouched. Reserve the relay/state for the rest
            // of the process before the first native write is attempted.
            AnyMutationInstalled = true;
        },
        [&]() noexcept {
            return LoaderContext->PatchJmpRel32(
                TailPatchRva,
                TailExpected.data(),
                static_cast<std::uint32_t>(TailExpected.size()),
                relayRva,
                static_cast<std::uint32_t>(TailExpected.size()));
        },
        [&]() noexcept {
            TailPatchInstalled = true;
            // PatchWriteU32 returning false does not prove that its four-byte
            // target remained untouched. Publish the conservative guard before
            // attempting that write so an inactive relay never assumes 511.
            InterlockedExchange(&State->capMayBeExtended, 1);
            InterlockedExchange(&State->operational, 1);
        },
        [&]() noexcept {
            return LoaderContext->PatchWriteU32(
                CountImmediateRva,
                CountImmediateExpected.data(),
                static_cast<std::uint32_t>(CountImmediateExpected.size()),
                SerializedSentinel);
        },
        [&]() noexcept {
            CapPatchInstalled = true;
        });

    if (result == LoaderInstallResult::FailedBeforeMutation) {
        SetError(error, "DescFunc tail seam is already owned");
        return result;
    }
    if (result
            == LoaderInstallResult::PartialCommitColdRestartRequired) {
        ColdRestartRequired = true;
        if (!TailPatchInstalled) {
            std::array<std::uint8_t, TailExpected.size()> observedSeam{};
            const bool observed = SafeRead(
                LoaderBase + TailPatchRva, observedSeam);
            char message[384]{};
            if (observed) {
                std::snprintf(
                    message,
                    sizeof(message),
                    "DescFunc tail publication returned an uncertain result "
                    "(observed seam=%02X %02X %02X %02X %02X %02X %02X %02X); "
                    "relay/state retained and cold restart required",
                    static_cast<unsigned>(observedSeam[0]),
                    static_cast<unsigned>(observedSeam[1]),
                    static_cast<unsigned>(observedSeam[2]),
                    static_cast<unsigned>(observedSeam[3]),
                    static_cast<unsigned>(observedSeam[4]),
                    static_cast<unsigned>(observedSeam[5]),
                    static_cast<unsigned>(observedSeam[6]),
                    static_cast<unsigned>(observedSeam[7]));
            } else {
                std::snprintf(
                    message,
                    sizeof(message),
                    "DescFunc tail publication returned an uncertain result "
                    "(seam unreadable); relay/state retained and cold restart "
                    "required");
            }
            SetError(error, message);
            if (LoaderContext) LoaderContext->LogError(message);
            FailClosed(
                "DescFunc tail publication returned an uncertain result; "
                "cold restart required",
                0);
        }
        std::uint32_t observedCap{};
        const bool observed = SafeRead(
            LoaderBase + CountImmediateRva, observedCap);
        char message[256]{};
        if (observed) {
            std::snprintf(
                message,
                sizeof(message),
                "count-cap commit failed after the safe tail was installed "
                "(observed immediate=0x%08X); cold restart required",
                observedCap);
        } else {
            std::snprintf(
                message,
                sizeof(message),
                "count-cap commit failed after the safe tail was installed "
                "(immediate unreadable); cold restart required");
        }
        SetError(error, message);
        return result;
    }
    return result;
}
#endif

auto ShutdownLoaderExtension() noexcept -> void {
    if (PersistenceState) {
        InterlockedExchange(&PersistenceState->itemTransportReady, 0);
        InterlockedExchange(&PersistenceState->codecReady, 0);
        InterlockedExchange(&PersistenceState->operational, 0);
        const auto deadline = GetTickCount64()
            + ShutdownRundownTimeoutMilliseconds;
        while (InterlockedCompareExchange(
                &PersistenceState->activeCallbacks, 0, 0) != 0) {
            if (GetTickCount64() >= deadline) {
                FailClosed(
                    "persistence relay rundown timed out during unload",
                    LastRowCount.load(std::memory_order_acquire));
            }
            YieldProcessor();
        }
        PersistenceState->readerHandler = nullptr;
        PersistenceState->writerHandler = nullptr;
        PersistenceState->auxiliaryReaderHandler = nullptr;
        PersistenceState->playerReaderHandler = nullptr;
        PersistenceState->playerPreviewHandler = nullptr;
        PersistenceState->packet9CProducerHandler = nullptr;
        PersistenceState->packet9DProducerHandler = nullptr;
        PersistenceState->packet9CQueueHandler = nullptr;
        PersistenceState->packet9DQueueHandler = nullptr;
    }
    if (State) {
        InterlockedExchange(&State->operational, 0);
        const auto deadline = GetTickCount64()
            + ShutdownRundownTimeoutMilliseconds;
        while (InterlockedCompareExchange(
                &State->activeCallbacks, 0, 0) != 0) {
            if (GetTickCount64() >= deadline) {
                FailClosed(
                    "loader relay rundown timed out during unload",
                    LastRowCount.load(std::memory_order_acquire));
            }
            YieldProcessor();
        }
        State->handler = nullptr;
    }
    Prepared = false;
    PersistencePrepared = false;
    ResetPublishedSchemaSnapshot();
    if (!AnyMutationInstalled) {
        ReleaseUnpatchedResources();
        LoaderContext = nullptr;
        LoaderBase = nullptr;
        LoaderImageSize = 0;
        NativeQsort = nullptr;
        NativeComparator = nullptr;
        NativeVectorResize = nullptr;
        NativeGetLinkNameCount = nullptr;
        NativeGetLinkName = nullptr;
        NativeByteBufferResize = nullptr;
        NativeSetObjectState = nullptr;
        NativeSetObjectAux = nullptr;
        NativeAuxiliaryReader = nullptr;
        NativePlayerReader = nullptr;
        NativePlayerPreviewCopy = nullptr;
        gISC12LoaderSuccessExit = nullptr;
        gISC12LoaderVanillaExit = nullptr;
    }
    // Once any native patch call is attempted, the RX relay and RW guard state
    // remain process-lifetime. A false PluginSDK result cannot prove that the
    // target bytes stayed untouched. The inactive relay never enters the
    // unloaded DLL.
}

auto FinalizePublishedSchemaSnapshot(
        const void* activeRecords,
        std::size_t activeRowCount,
        std::size_t activeRowSize,
        std::uint64_t revision,
        std::string& error) noexcept -> NativeSchemaFinalizeResult {
    error.clear();
    AcquireSRWLockExclusive(&SchemaSnapshotLock);
    SchemaUpdateInProgress = true;
    SchemaReady.store(false, std::memory_order_release);

    auto result = NativeSchemaFinalizeResult::InvalidState;
    if (Prepared && AnyMutationInstalled && State && PersistenceState
            && InterlockedCompareExchange(&State->operational, 0, 0) != 0
            && InterlockedCompareExchange(
                &PersistenceState->operational, 0, 0) != 0
            && InterlockedCompareExchange(
                &PersistenceState->codecReady, 0, 0) != 0
            && InterlockedCompareExchange(
                &PersistenceState->itemTransportReady, 0, 0) != 0) {
        result = PendingSchemaCandidates.Finalize(
            activeRecords,
            activeRowCount,
            activeRowSize,
            revision,
            HasPublishedSchemaSnapshot,
            PublishedSchemaSnapshot.schemaHash,
            PublishedSchemaSnapshot);
    }

    if (result == NativeSchemaFinalizeResult::Published) {
        HasPublishedSchemaSnapshot = true;
    }
    const bool accepted =
        result == NativeSchemaFinalizeResult::Published
        || result == NativeSchemaFinalizeResult::AcceptedExisting;
    if (accepted) {
        LastRowCount.store(activeRowCount, std::memory_order_release);
        SchemaReady.store(true, std::memory_order_release);
    } else {
        switch (result) {
        case NativeSchemaFinalizeResult::InvalidState:
            SetError(error,
                "native publication is not active at the schema boundary");
            break;
        case NativeSchemaFinalizeResult::InvalidTableView:
            SetError(error,
                "authoritative RotW ItemStatCost view is invalid");
            break;
        case NativeSchemaFinalizeResult::InvalidAuthoritativeSnapshot:
            SetError(error,
                "authoritative RotW ItemStatCost bytes cannot produce a valid schema snapshot");
            break;
        case NativeSchemaFinalizeResult::MissingCandidate:
            SetError(error,
                "authoritative RotW ItemStatCost view has no exact staged capture");
            break;
        case NativeSchemaFinalizeResult::InvalidOrDuplicateRevision:
            SetError(error,
                "DataTablesLoaded revision is zero or duplicates the previous load");
            break;
        case NativeSchemaFinalizeResult::Diverged:
            SetError(error,
                "authoritative ItemStatCost schema diverged after publication");
            break;
        default:
            SetError(error, "ItemStatCost schema finalization failed");
            break;
        }
    }
    SchemaUpdateInProgress = false;
    ReleaseSRWLockExclusive(&SchemaSnapshotLock);
    return result;
}

[[noreturn]] auto FailClosedNativePublication(
        const char* reason) noexcept -> void {
    FailClosed(
        reason ? reason : "native publication became indeterminate",
        LastRowCount.load(std::memory_order_acquire));
}

auto TryGetPreparedPublicationCallbacks(
        PublicationCoordinatorCallbacks& output) noexcept -> bool {
    if (!Prepared || !PersistencePrepared
            || !PreparedPublicationAdapters.IsBound()
            || AnyMutationInstalled) {
        return false;
    }
    output = PreparedPublicationAdapters.CoordinatorCallbacks();
    return true;
}

auto GetLoaderRuntimeStatus() noexcept -> LoaderRuntimeStatus {
    LoaderRuntimeStatus status{
        .prepared = Prepared,
        .persistencePrepared = PersistencePrepared,
        .tailPatchInstalled = TailPatchInstalled,
        .capPatchInstalled = CapPatchInstalled,
        .persistenceReaderPatchInstalled =
            PersistenceReaderPatchInstalled,
        .persistenceWriterPatchInstalled =
            PersistenceWriterPatchInstalled,
        .publicationAdaptersBound =
            PreparedPublicationAdapters.IsBound(),
        .operational = State
            && InterlockedCompareExchange(&State->operational, 0, 0) != 0,
        .schemaReady = SchemaReady.load(std::memory_order_acquire),
        .persistenceCodecReady = PersistenceState
            && InterlockedCompareExchange(
                &PersistenceState->codecReady, 0, 0) != 0,
        .itemTransportReady = PersistenceState
            && InterlockedCompareExchange(
                &PersistenceState->itemTransportReady, 0, 0) != 0,
        .coldRestartRequired = ColdRestartRequired,
        .buildCalls = BuildCalls.load(std::memory_order_acquire),
        .lastRowCount = LastRowCount.load(std::memory_order_acquire),
        .lastDescriptionCount =
            LastDescriptionCount.load(std::memory_order_acquire),
        .fullItemRoot9C = FullItemRoot9C.load(std::memory_order_acquire),
        .fullItemRoot9D = FullItemRoot9D.load(std::memory_order_acquire),
        .fullItemTransactionsAccepted = FullItemTransactionsAccepted.load(
            std::memory_order_acquire),
        .fullItemTransactionsRejected = FullItemTransactionsRejected.load(
            std::memory_order_acquire),
        .fullItemPacketsCaptured9C = FullItemPacketsCaptured9C.load(
            std::memory_order_acquire),
        .fullItemPacketsCaptured9D = FullItemPacketsCaptured9D.load(
            std::memory_order_acquire),
        .fullItemPacketsQueued = FullItemPacketsQueued.load(
            std::memory_order_acquire),
        .persistenceReadsAccepted = PersistenceReadsAccepted.load(
            std::memory_order_acquire),
        .persistenceReadsRejected = PersistenceReadsRejected.load(
            std::memory_order_acquire),
        .persistenceWritesDelegated = PersistenceWritesDelegated.load(
            std::memory_order_acquire),
        .persistenceWritesRejected = PersistenceWritesRejected.load(
            std::memory_order_acquire),
    };
    return status;
}

} // namespace ruffneckk::isc12
