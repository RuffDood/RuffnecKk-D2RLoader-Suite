#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ruffneckk::isc12 {

enum class FullItemTransportProvider : std::uint8_t {
    Invalid,
    Unresolved,
    NativeG9,
    ExtendedItemStatsV1,
};

struct FullItemTransportHookObservation {
    bool querySucceeded{};
    bool trackedInlineHook{};
    std::uint32_t ownerCount{};
    std::string_view ownerPluginId{};
};

inline constexpr std::string_view ExtendedItemStatsProviderId =
    "extended-item-stats";
inline constexpr std::string_view ExtendedItemStatsProviderVersion =
    "0.3.14";
inline constexpr std::size_t FullItemTransportProviderSurfaceCount = 6;

// Accepts either the complete native transport or one exact, versioned
// external provider. A partial/mixed hook set is never treated as native.
[[nodiscard]] auto ClassifyFullItemTransportProvider(
    bool allNativeSurfacesMatch,
    std::span<const FullItemTransportHookObservation> observations,
    std::string_view pluginId,
    std::string_view pluginVersion) noexcept -> FullItemTransportProvider;

enum class FullItemPacketKind : std::uint8_t {
    ItemAction9C,
    ItemAction9D,
};

enum class FullItemPayloadDisposition : std::uint8_t {
    NativePacket,
    InvalidEncoding,
    ExceedsNativePacketCapacity,
};

struct FullItemPacketBudget {
    std::size_t headerBytes{};
    std::size_t serializerCapacityBytes{};
    std::size_t packetLimitBytes{};
    std::size_t payloadCapacityBytes{};
};

struct FullItemPayloadEstimate {
    bool valid{};
    std::size_t totalBits{};
    std::size_t totalBytes{};
    std::size_t statIdTokens{};
};

// One entry describes the bits written for one item node. The 0x9C/0x9D
// producers disable serializer recursion; the root is one packet and every
// socketed descendant is emitted later as its own 0x9D packet.
struct FullItemNodePayload {
    std::size_t directNonStatIdBits{};
    // One token per emitted record ID plus one 0xFFF sentinel per emitted list.
    std::size_t statIdTokens{};
};

struct LegacyFullItemNodePayload {
    std::size_t directLegacyBits{};
    std::size_t statIdTokens{};
};

// Snapshot-only description of one node in a packed child-index adjacency
// list. The record and terminator counts must describe the exact emitted
// stat-ID tokens; the future native adapter remains responsible for deriving
// this immutable snapshot from the live item and its stat lists.
struct FullItemPacketTreeNode {
    FullItemNodePayload payload{};
    std::size_t emittedStatRecordCount{};
    std::size_t emittedStatListTerminatorCount{};
    std::size_t firstChildListOffset{};
    std::size_t listedChildCount{};
    // The adapter's independently observed occupied-child cardinality.
    std::size_t declaredChildCount{};
    // Total available sockets; it may exceed the occupied-child count.
    std::size_t socketCapacity{};
};

struct FullItemPacketTreeView {
    std::span<const FullItemPacketTreeNode> nodes{};
    std::span<const std::size_t> childNodeIndices{};
    std::size_t rootNodeIndex{};
    std::size_t declaredNodeCount{};
};

// Caller-owned storage keeps the planner allocation-free and lets a future
// adapter impose only a native tree bound that has actually been proved.
struct FullItemPacketTreeScratch {
    std::span<std::uint8_t> nodeMarks{};
    std::span<std::size_t> traversalOrder{};
    std::span<std::size_t> nodeStack{};
};

enum class FullItemPacketTreeError : std::uint8_t {
    None,
    InvalidArgument,
    InsufficientScratch,
    CountMismatch,
    SocketCapacityExceeded,
    ChildListMismatch,
    NodeIndexOutOfRange,
    DuplicateOrCycle,
    UnreachableNode,
    InvalidEncoding,
    ExceedsNativePacketCapacity,
};

// Production G9-A uses the one real native serialization and stages the
// resulting 0x9C/0x9D packets. These are explicit ISC12 safety bounds, not
// claims about a native global socket-tree limit.
inline constexpr std::size_t MaximumStagedItemPacketBytes = 0xFC;
inline constexpr std::size_t MaximumStagedItemImmediateChildren = 7;
inline constexpr std::size_t MaximumStagedItemPacketCount = 64;
inline constexpr std::size_t MaximumStagedItemTreeDepth = 16;
inline constexpr std::size_t MaximumStagedItemTransactionBytes = 0x4000;
// The native socket walker copies the active producer's temporary flags,
// explicitly ORs 0x08, then passes that value to each child 0x9D producer.
inline constexpr std::uint32_t NestedFullItemTemporaryFlagsMask = 0x08;

enum class FullItemPacketStagingState : std::uint8_t {
    Idle,
    Collecting,
    Rejected,
    Ready,
    Flushing,
    Fatal,
};

enum class FullItemPacketStagingError : std::uint8_t {
    None,
    InvalidArgument,
    InvalidState,
    GenerationExhausted,
    UnexpectedNested9C,
    UnexpectedNested9D,
    ClientMismatch,
    ParentMismatch,
    NestedActionMismatch,
    NestedFlagsMismatch,
    NestedGambleMismatch,
    ParentPacketMissing,
    NodeLimitExceeded,
    DepthLimitExceeded,
    ImmediateChildLimitExceeded,
    DuplicateOrCycle,
    RelayWithoutTransaction,
    RelayKindMismatch,
    DuplicatePacket,
    InvalidPacketPointer,
    InvalidPacketLength,
    InvalidPacketHeader,
    PayloadOutsideBudget,
    ByteLimitExceeded,
    CaptureOrderMismatch,
    MissingPacket,
    NativeProducerException,
    UnbalancedProducerExit,
    StaleToken,
    FinalBatchInvalid,
    ReenteredDuringFlush,
};

enum class FullItemProducerDisposition : std::uint8_t {
    SkipOriginal,
    InvokeOriginal,
};

enum class FullItemProducerCompletion : std::uint8_t {
    Skipped,
    NestedComplete,
    RootReady,
    RootRejected,
    Fatal,
};

struct FullItemProducerDescriptor {
    FullItemPacketKind kind{};
    void* client{};
    void* parentItem{};
    void* item{};
    std::uint8_t action{};
    std::uint32_t temporaryFlags{};
    std::uint32_t gamble{};
};

struct FullItemProducerToken {
    std::uint64_t generation{};
    std::size_t nodeIndex{};
    std::size_t depth{};
    bool ownsRoot{};
    bool admitted{};
};

struct FullItemProducerAdmission {
    FullItemProducerToken token{};
    FullItemProducerDisposition disposition{
        FullItemProducerDisposition::SkipOriginal};
    FullItemPacketStagingError error{FullItemPacketStagingError::None};
};

struct FullItemStagedNode {
    FullItemPacketKind kind{};
    void* parentItem{};
    void* item{};
    std::uint8_t action{};
    std::uint8_t directChildCount{};
    std::uint32_t temporaryFlags{};
    std::uint32_t gamble{};
    std::size_t depth{};
    bool packetCaptured{};
};

struct FullItemStagedPacket {
    FullItemPacketKind kind{};
    std::size_t nodeIndex{};
    std::size_t byteOffset{};
    std::size_t byteLength{};
};

// This context is intentionally a plain, fixed-size value. The native loader
// adapter owns one thread_local instance and performs no allocation or locking
// while item producers are active.
struct FullItemPacketStagingContext {
    FullItemPacketStagingState state{FullItemPacketStagingState::Idle};
    FullItemPacketStagingError error{FullItemPacketStagingError::None};
    std::uint64_t generation{};
    FullItemPacketKind rootKind{};
    void* rootClient{};
    std::size_t nodeCount{};
    std::size_t packetCount{};
    std::size_t activeDepth{};
    std::size_t totalBytes{};
    std::uint32_t rejectedProducerTemporaryFlags{};
    std::uint32_t rejectedParentTemporaryFlags{};
    std::array<std::size_t, MaximumStagedItemTreeDepth>
        activeNodeIndices{};
    std::array<FullItemStagedNode, MaximumStagedItemPacketCount> nodes{};
    std::array<FullItemStagedPacket, MaximumStagedItemPacketCount> packets{};
    std::array<std::uint8_t, MaximumStagedItemTransactionBytes> bytes{};
};

struct FullItemPacketFlushResult {
    FullItemPacketStagingError error{FullItemPacketStagingError::None};
    std::size_t queuedPacketCount{};
    bool completed{};
};

using FullItemPacketQueueCallback = void (*)(
    void* context,
    void* client,
    const std::uint8_t* bytes,
    std::size_t length) noexcept;

using FullItemPacketTreeVisitor = void (*)(
    void* context,
    std::size_t nodeIndex,
    FullItemPacketKind packet) noexcept;

inline constexpr std::size_t NativeItemSerializerCapacityBytes = 0xF4;
inline constexpr std::size_t NativeItemPacketLimitBytes = 0xFC;
inline constexpr std::size_t Packet9CHeaderBytes = 8;
inline constexpr std::size_t Packet9DHeaderBytes = 13;

// Packet 0xAC serializes one bounded item-stat list into a 0xF4-byte native
// bit buffer before the 13-byte packet header is accounted for. The current
// native producer admits at most 16 copied ItemStatCost records. Widening each
// stat-ID token from 9 to 12 bits therefore remains below both native guards,
// even when every optional field uses its largest governed width.
inline constexpr std::size_t PacketACNativeCapacityBytes = 0xF4;
inline constexpr std::size_t PacketACHeaderBytes = 13;
inline constexpr std::size_t PacketACMaximumAnimationBits = 4;
inline constexpr std::size_t PacketACMaximumComponentBits = 1 + (16 * 8);
inline constexpr std::size_t PacketACMaximumUniqueMercenaryBits =
    1 + 5 + 16 + (9 * 8) + 8 + 16 + 1 + 32;
inline constexpr std::size_t PacketACMaximumOwnerBits = 1 + 31;
inline constexpr std::size_t PacketACMaximumStatBits =
    1 + (16 * (12 + 16 + 32)) + 12;
inline constexpr std::size_t PacketACMaximumBitstreamBits =
    PacketACMaximumAnimationBits
    + PacketACMaximumComponentBits
    + PacketACMaximumUniqueMercenaryBits
    + PacketACMaximumOwnerBits
    + PacketACMaximumStatBits;
inline constexpr std::size_t PacketACMaximumBitstreamBytes =
    (PacketACMaximumBitstreamBits + 7) / 8;
inline constexpr std::size_t PacketACMaximumPacketBytes =
    PacketACHeaderBytes + PacketACMaximumBitstreamBytes;
inline constexpr std::size_t PacketACPacketHeadroomBytes =
    PacketACNativeCapacityBytes - PacketACMaximumPacketBytes;

[[nodiscard]] constexpr auto FullItemPacketBudgetFor(
        FullItemPacketKind kind) noexcept -> FullItemPacketBudget {
    std::size_t header{};
    if (kind == FullItemPacketKind::ItemAction9C) {
        header = Packet9CHeaderBytes;
    } else if (kind == FullItemPacketKind::ItemAction9D) {
        header = Packet9DHeaderBytes;
    } else {
        return {};
    }
    const auto totalPayload = NativeItemPacketLimitBytes - header;
    return {
        .headerBytes = header,
        .serializerCapacityBytes = NativeItemSerializerCapacityBytes,
        .packetLimitBytes = NativeItemPacketLimitBytes,
        .payloadCapacityBytes = totalPayload < NativeItemSerializerCapacityBytes
            ? totalPayload
            : NativeItemSerializerCapacityBytes,
    };
}

// Computes the payload of exactly one 0x9C/0x9D item packet.
[[nodiscard]] auto EstimateItemPacketPayload(
    const FullItemNodePayload& node) noexcept
    -> FullItemPayloadEstimate;

// Computes one packet's exact 9->12-bit expansion from its legacy node-local
// bit count and stat-ID token count.
[[nodiscard]] auto ExpandLegacyItemPacketPayload(
    const LegacyFullItemNodePayload& node) noexcept
    -> FullItemPayloadEstimate;

[[nodiscard]] auto ClassifyFullItemPayload(
    std::size_t encodedBytes,
    FullItemPacketKind packet) noexcept -> FullItemPayloadDisposition;

// Classifies the complete send sequence. nodes[0] uses rootPacket; every later
// node is a separately serialized 0x9D descendant. This remains a pure static
// accounting model; no production whole-tree preflight calls it yet.
[[nodiscard]] auto ClassifyFullItemPacketSequence(
    std::span<const FullItemNodePayload> nodes,
    FullItemPacketKind rootPacket) noexcept -> FullItemPayloadDisposition;

// First validates the complete immutable snapshot without invoking visitor,
// then visits the accepted plan in the native depth-first preorder. A rejected
// preflight therefore invokes visitor zero times. This pure planner neither
// installs a native adapter/hook nor promises rollback after visitor begins.
[[nodiscard]] auto PreflightAndVisitFullItemPacketTree(
    const FullItemPacketTreeView& tree,
    FullItemPacketKind rootPacket,
    FullItemPacketTreeScratch scratch,
    FullItemPacketTreeVisitor visitor,
    void* context) noexcept -> FullItemPacketTreeError;

[[nodiscard]] auto BeginFullItemPacketProducer(
    FullItemPacketStagingContext& transaction,
    const FullItemProducerDescriptor& producer) noexcept
    -> FullItemProducerAdmission;

// A queue-call relay always suppresses the native call. It either captures the
// copied packet for the current producer or poisons the transaction.
[[nodiscard]] auto CaptureFullItemPacketQueueCall(
    FullItemPacketStagingContext& transaction,
    FullItemPacketKind relayKind,
    void* client,
    const std::uint8_t* bytes,
    std::size_t length) noexcept -> FullItemPacketStagingError;

[[nodiscard]] auto EndFullItemPacketProducer(
    FullItemPacketStagingContext& transaction,
    const FullItemProducerToken& token) noexcept
    -> FullItemProducerCompletion;

// SEH cleanup uses this path before unwinding out of a producer wrapper. It
// poisons the complete batch before balancing the producer token, so a packet
// captured before a later native exception can never become flushable.
[[nodiscard]] auto AbortFullItemPacketProducer(
    FullItemPacketStagingContext& transaction,
    const FullItemProducerToken& token) noexcept
    -> FullItemProducerCompletion;

[[nodiscard]] auto ValidateCapturedFullItemPacketTransaction(
    const FullItemPacketStagingContext& transaction) noexcept
    -> FullItemPacketStagingError;

// Rejected transactions invoke callback zero times and reset to Idle. A valid
// transaction flushes only after its final byte-level scan. A producer that
// reenters during callback execution is a non-rollbackable Fatal condition;
// remaining callbacks are suppressed and the context stays Fatal.
[[nodiscard]] auto FlushOrDiscardFullItemPacketTransaction(
    FullItemPacketStagingContext& transaction,
    FullItemPacketQueueCallback callback,
    void* callbackContext) noexcept -> FullItemPacketFlushResult;

static_assert(
    FullItemPacketBudgetFor(FullItemPacketKind::ItemAction9C)
        .payloadCapacityBytes == 244);
static_assert(
    FullItemPacketBudgetFor(FullItemPacketKind::ItemAction9D)
        .payloadCapacityBytes == 239);
static_assert(PacketACMaximumBitstreamBits == 1289);
static_assert(PacketACMaximumBitstreamBytes == 162);
static_assert(PacketACMaximumPacketBytes == 175);
static_assert(PacketACPacketHeadroomBytes == 69);
static_assert(PacketACMaximumPacketBytes < PacketACNativeCapacityBytes);
static_assert(
    MaximumStagedItemPacketCount * MaximumStagedItemPacketBytes
        <= MaximumStagedItemTransactionBytes);
static_assert(MaximumStagedItemTransactionBytes
    <= (std::numeric_limits<std::uint16_t>::max)());

} // namespace ruffneckk::isc12
