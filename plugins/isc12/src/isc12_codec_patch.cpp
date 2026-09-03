#include "isc12_codec_patch.hpp"

#include <array>
#include <limits>

namespace ruffneckk::isc12 {
namespace {

template<std::size_t Size>
consteval auto CountByteDifferences(
        const std::array<std::uint8_t, Size>& expected,
        const std::array<std::uint8_t, Size>& replacement) noexcept
        -> std::size_t {
    std::size_t count{};
    for (std::size_t index{}; index < Size; ++index) {
        if (expected[index] != replacement[index]) ++count;
    }
    return count;
}

template<std::size_t MutationCount, std::size_t Size>
consteval auto BuildLiteralMutations(
        const std::array<std::uint8_t, Size>& expected,
        const std::array<std::uint8_t, Size>& replacement) noexcept
        -> std::array<CodecByteMutation, MutationCount> {
    std::array<CodecByteMutation, MutationCount> mutations{};
    std::size_t mutationIndex{};
    for (std::size_t index{}; index < Size; ++index) {
        if (expected[index] == replacement[index]) continue;
        mutations[mutationIndex++] =
            CodecByteMutation{index, expected[index], replacement[index]};
    }
    return mutations;
}

inline constexpr std::array WidthAndSentinelMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{19, 0x01, 0x0F},
};
inline constexpr std::array WriterIdMutation{
    CodecByteMutation{2, 0x09, 0x0C},
};
inline constexpr std::array WriterTerminatorMutations{
    CodecByteMutation{2, 0x01, 0x0F},
    CodecByteMutation{12, 0x09, 0x0C},
};
inline constexpr std::array PreviewWidthAndSentinelMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{17, 0x01, 0x0F},
};
inline constexpr std::array GenericItemReaderFirstMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{11, 0xF6, 0xF3},
    CodecByteMutation{21, 0x01, 0x0F},
};
inline constexpr std::array GenericItemReaderNextMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{17, 0x01, 0x0F},
};
static_assert(CountByteDifferences(
    GenericItemBoundedWriterBytes,
    GenericItemBoundedWriterReplacementBytes) == 37);
inline constexpr auto GenericItemBoundedWriterMutations =
    BuildLiteralMutations<37>(
        GenericItemBoundedWriterBytes,
        GenericItemBoundedWriterReplacementBytes);
inline constexpr std::array GenericItemWriterTerminatorMutations{
    CodecByteMutation{2, 0x01, 0x0F},
    CodecByteMutation{7, 0x09, 0x0C},
};
inline constexpr std::array AuxiliaryReaderRelayCallMutations{
    CodecByteMutation{26, 0x8E, 0x00,
        CodecByteMutation::ReplacementSource::AuxiliaryReaderRelayRel32, 0},
    CodecByteMutation{27, 0xEF, 0x00,
        CodecByteMutation::ReplacementSource::AuxiliaryReaderRelayRel32, 1},
    CodecByteMutation{28, 0xFF, 0x00,
        CodecByteMutation::ReplacementSource::AuxiliaryReaderRelayRel32, 2},
    CodecByteMutation{29, 0xFF, 0x00,
        CodecByteMutation::ReplacementSource::AuxiliaryReaderRelayRel32, 3},
};
inline constexpr std::array PlayerReaderPrimaryRelayCallMutations{
    CodecByteMutation{35, 0x11, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 0},
    CodecByteMutation{36, 0x4B, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 1},
    CodecByteMutation{37, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 2},
    CodecByteMutation{38, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 3},
};
inline constexpr std::array PlayerReaderLegacyRelayCallMutations{
    CodecByteMutation{17, 0x27, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 0},
    CodecByteMutation{18, 0x2D, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 1},
    CodecByteMutation{19, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 2},
    CodecByteMutation{20, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::PlayerReaderRelayRel32, 3},
};
inline constexpr std::array PlayerPreviewRelayCallMutations{
    CodecByteMutation{16, 0x7B, 0x00,
        CodecByteMutation::ReplacementSource::PlayerPreviewRelayRel32, 0},
    CodecByteMutation{17, 0x11, 0x00,
        CodecByteMutation::ReplacementSource::PlayerPreviewRelayRel32, 1},
    CodecByteMutation{18, 0x40, 0x00,
        CodecByteMutation::ReplacementSource::PlayerPreviewRelayRel32, 2},
    CodecByteMutation{19, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::PlayerPreviewRelayRel32, 3},
};
inline constexpr std::array PlayerSaveRelayCallMutations{
    CodecByteMutation{6, 0x49, 0x00,
        CodecByteMutation::ReplacementSource::PlayerSaveFinalizeRel32, 0},
    CodecByteMutation{7, 0x62, 0x00,
        CodecByteMutation::ReplacementSource::PlayerSaveFinalizeRel32, 1},
    CodecByteMutation{8, 0x4E, 0x00,
        CodecByteMutation::ReplacementSource::PlayerSaveFinalizeRel32, 2},
    CodecByteMutation{9, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::PlayerSaveFinalizeRel32, 3},
};
inline constexpr std::array OverflowGuardStatusMutations{
    CodecByteMutation{11, 0x33, 0x8B},
    CodecByteMutation{12, 0xC0, 0xC2},
};
inline constexpr std::array Packet9CQueueRelayCallMutations{
    CodecByteMutation{1, 0xDB, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CQueueRelayRel32, 0},
    CodecByteMutation{2, 0x79, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CQueueRelayRel32, 1},
    CodecByteMutation{3, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CQueueRelayRel32, 2},
    CodecByteMutation{4, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CQueueRelayRel32, 3},
};
inline constexpr std::array Packet9DQueueRelayCallMutations{
    CodecByteMutation{1, 0xEA, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DQueueRelayRel32, 0},
    CodecByteMutation{2, 0x77, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DQueueRelayRel32, 1},
    CodecByteMutation{3, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DQueueRelayRel32, 2},
    CodecByteMutation{4, 0x00, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DQueueRelayRel32, 3},
};
inline constexpr std::array Packet9CEntryRelayJumpMutations{
    CodecByteMutation{0, 0x40, 0xE9},
    CodecByteMutation{1, 0x53, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CEntryRelayRel32, 0},
    CodecByteMutation{2, 0x55, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CEntryRelayRel32, 1},
    CodecByteMutation{3, 0x56, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CEntryRelayRel32, 2},
    CodecByteMutation{4, 0x57, 0x00,
        CodecByteMutation::ReplacementSource::Packet9CEntryRelayRel32, 3},
};
inline constexpr std::array Packet9DEntryRelayJumpMutations{
    CodecByteMutation{0, 0x40, 0xE9},
    CodecByteMutation{1, 0x53, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DEntryRelayRel32, 0},
    CodecByteMutation{2, 0x55, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DEntryRelayRel32, 1},
    CodecByteMutation{3, 0x56, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DEntryRelayRel32, 2},
    CodecByteMutation{4, 0x57, 0x00,
        CodecByteMutation::ReplacementSource::Packet9DEntryRelayRel32, 3},
};
inline constexpr std::array Packet3EProducerBoundMutations{
    CodecByteMutation{4, 0x01, 0x0F},
};
inline constexpr std::array Packet3EProducerWidthMutations{
    CodecByteMutation{2, 0x09, 0x0C},
};
inline constexpr std::array Packet3EConsumerWidthMutations{
    CodecByteMutation{1, 0x09, 0x0C},
};
inline constexpr std::array PacketA8ProducerIdMutations{
    CodecByteMutation{2, 0x09, 0x0C},
};
inline constexpr std::array PacketA8ProducerTerminatorMutations{
    CodecByteMutation{2, 0x01, 0x0F},
    CodecByteMutation{12, 0x09, 0x0C},
};
inline constexpr std::array PacketA8ConsumerFirstWidthMutations{
    CodecByteMutation{3, 0x09, 0x0C},
};
inline constexpr std::array PacketA8ConsumerFirstSentinelMutations{
    CodecByteMutation{3, 0x01, 0x0F},
};
inline constexpr std::array PacketA8ConsumerNextMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{22, 0x01, 0x0F},
};
inline constexpr std::array PacketAACountCapMutations{
    CodecByteMutation{3, 0x01, 0x0F},
};
inline constexpr std::array PacketAAEstimatorIdMutations{
    CodecByteMutation{3, 0x09, 0x0C},
};
inline constexpr std::array PacketAAEstimatorTerminatorMutations{
    CodecByteMutation{3, 0x09, 0x0C},
};
inline constexpr std::array PacketAAProducerIdMutations{
    CodecByteMutation{2, 0x09, 0x0C},
};
inline constexpr std::array PacketAAProducerTerminatorMutations{
    CodecByteMutation{2, 0x01, 0x0F},
    CodecByteMutation{7, 0x09, 0x0C},
};
inline constexpr std::array PacketAAConsumerFirstMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{22, 0x01, 0x0F},
};
inline constexpr std::array PacketAAConsumerNextMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{22, 0x01, 0x0F},
};
inline constexpr std::array PacketACProducerIdMutations{
    CodecByteMutation{2, 0x09, 0x0C},
};
inline constexpr std::array PacketACProducerTerminatorMutations{
    CodecByteMutation{2, 0x09, 0x0C},
    CodecByteMutation{17, 0x01, 0x0F},
};
inline constexpr std::array PacketACConsumerFirstMutations{
    CodecByteMutation{4, 0x09, 0x0C},
    CodecByteMutation{14, 0x01, 0x0F},
};
inline constexpr std::array PacketACConsumerNextMutations{
    CodecByteMutation{1, 0x09, 0x0C},
    CodecByteMutation{25, 0x01, 0x0F},
};

inline constexpr std::array G5Sites{
    // Consumer first, then producer width, then the producer admission bound.
    CodecPatchSite{
        NativePattern{"packet.g5-3e-consumer-width", 0x12C955,
            Packet3EConsumerWidthBytes, Packet3EConsumerWidthMask},
        Packet3EConsumerWidthMutations},
    CodecPatchSite{
        NativePattern{"packet.g5-3e-producer-width", 0x47A237,
            Packet3EProducerWidthBytes, Packet3EProducerWidthMask},
        Packet3EProducerWidthMutations},
    CodecPatchSite{
        NativePattern{"packet.g5-3e-producer-bound", 0x47A21F,
            Packet3EProducerBoundBytes, Packet3EProducerBoundMask},
        Packet3EProducerBoundMutations},
};
inline constexpr std::array G5Witnesses{
    NativePattern{"packet.g5-3e-budget", 0x47A2A9,
        Packet3EBudgetBytes, Packet3EBudgetMask},
};

inline constexpr std::array G6Sites{
    CodecPatchSite{
        NativePattern{"packet.g6-a8-consumer-first-width", 0x12E7DA,
            PacketA8ConsumerFirstWidthBytes,
            PacketA8ConsumerFirstWidthMask},
        PacketA8ConsumerFirstWidthMutations},
    CodecPatchSite{
        NativePattern{"packet.g6-a8-consumer-first-sentinel", 0x12E823,
            PacketA8ConsumerFirstSentinelBytes,
            PacketA8ConsumerFirstSentinelMask},
        PacketA8ConsumerFirstSentinelMutations},
    CodecPatchSite{
        NativePattern{"packet.g6-a8-consumer-next", 0x12E939,
            PacketA8ConsumerNextBytes, PacketA8ConsumerNextMask},
        PacketA8ConsumerNextMutations},
    CodecPatchSite{
        NativePattern{"packet.g6-a8-producer-id", 0x53A615,
            PacketA8ProducerIdBytes, PacketA8ProducerIdMask},
        PacketA8ProducerIdMutations},
    CodecPatchSite{
        NativePattern{"packet.g6-a8-producer-terminator", 0x53A68A,
            PacketA8ProducerTerminatorBytes,
            PacketA8ProducerTerminatorMask},
        PacketA8ProducerTerminatorMutations},
};
inline constexpr std::array G6Witnesses{
    NativePattern{"packet.g6-a8-copy-cap", 0x53A4CE,
        PacketA8CopyCapBytes, PacketA8CopyCapMask},
    NativePattern{"packet.g6-a8-bitstream-capacity", 0x53A4EE,
        PacketA8BitstreamCapacityBytes, PacketA8BitstreamCapacityMask},
    NativePattern{"packet.g6-a8-size-guard", 0x53A6A9,
        PacketA8PacketSizeGuardBytes, PacketA8PacketSizeGuardMask},
};

inline constexpr std::array G7Sites{
    CodecPatchSite{
        NativePattern{"packet.g7-aa-consumer-first", 0x12EAD1,
            PacketAAConsumerFirstBytes, PacketAAConsumerFirstMask},
        PacketAAConsumerFirstMutations},
    CodecPatchSite{
        NativePattern{"packet.g7-aa-consumer-next", 0x12EBF9,
            PacketAAConsumerNextBytes, PacketAAConsumerNextMask},
        PacketAAConsumerNextMutations},
    CodecPatchSite{
        NativePattern{"packet.g7-aa-estimator-id", 0x539EE7,
            PacketAAEstimatorIdBytes, PacketAAEstimatorIdMask},
        PacketAAEstimatorIdMutations},
    CodecPatchSite{
        NativePattern{"packet.g7-aa-estimator-terminator", 0x539F78,
            PacketAAEstimatorTerminatorBytes,
            PacketAAEstimatorTerminatorMask},
        PacketAAEstimatorTerminatorMutations},
    CodecPatchSite{
        NativePattern{"packet.g7-aa-producer-id", 0x53A057,
            PacketAAProducerIdBytes, PacketAAProducerIdMask},
        PacketAAProducerIdMutations},
    CodecPatchSite{
        NativePattern{"packet.g7-aa-producer-terminator", 0x53A09E,
            PacketAAProducerTerminatorBytes,
            PacketAAProducerTerminatorMask},
        PacketAAProducerTerminatorMutations},
    // The widened ItemStatCost domain becomes reachable only after every
    // estimator, consumer and producer width above is published.
    CodecPatchSite{
        NativePattern{"packet.g7-aa-count-cap", 0x53994B,
            PacketAACountCapBytes, PacketAACountCapMask},
        PacketAACountCapMutations},
};
inline constexpr std::array G7Witnesses{
    NativePattern{"packet.g7-aa-budget-guard", 0x539FE2,
        PacketAABudgetGuardBytes, PacketAABudgetGuardMask},
};

inline constexpr std::array G8Sites{
    CodecPatchSite{
        NativePattern{"packet.g8-ac-consumer-first", 0x12F4F2,
            PacketACConsumerFirstBytes, PacketACConsumerFirstMask},
        PacketACConsumerFirstMutations},
    CodecPatchSite{
        NativePattern{"packet.g8-ac-consumer-next", 0x12F66F,
            PacketACConsumerNextBytes, PacketACConsumerNextMask},
        PacketACConsumerNextMutations},
    CodecPatchSite{
        NativePattern{"packet.g8-ac-producer-id", 0x47CC1C,
            PacketACProducerIdBytes, PacketACProducerIdMask},
        PacketACProducerIdMutations},
    CodecPatchSite{
        NativePattern{"packet.g8-ac-producer-terminator", 0x47CCA1,
            PacketACProducerTerminatorBytes,
            PacketACProducerTerminatorMask},
        PacketACProducerTerminatorMutations},
};
inline constexpr std::array G8Witnesses{
    NativePattern{"packet.g8-ac-copy-cap", 0x47CB2E,
        PacketACCopyCapBytes, PacketACCopyCapMask},
    NativePattern{"packet.g8-ac-bitstream-capacity", 0x47C6BE,
        PacketACBitstreamCapacityBytes, PacketACBitstreamCapacityMask},
    NativePattern{"packet.g8-ac-size-guard", 0x47CD08,
        PacketACPacketSizeGuardBytes, PacketACPacketSizeGuardMask},
};

inline constexpr std::array G9Sites{
    // The two queue calls suppress native dispatch before either producer
    // entry can begin a staged transaction. The entry JMPs publish last.
    CodecPatchSite{
        NativePattern{"transport.g9-queue-9c-call", 0x479E10,
            Packet9CQueueCallBytes, Packet9CQueueCallMask},
        Packet9CQueueRelayCallMutations},
    CodecPatchSite{
        NativePattern{"transport.g9-queue-9d-call", 0x47A001,
            Packet9DQueueCallBytes, Packet9DQueueCallMask},
        Packet9DQueueRelayCallMutations},
    CodecPatchSite{
        NativePattern{"transport.g9-producer-9c-entry", 0x479CD0,
            Packet9CProducerEntryBytes, Packet9CProducerEntryMask},
        Packet9CEntryRelayJumpMutations},
    CodecPatchSite{
        NativePattern{"transport.g9-producer-9d-entry", 0x479EA0,
            Packet9DProducerEntryBytes, Packet9DProducerEntryMask},
        Packet9DEntryRelayJumpMutations},
};

inline constexpr std::array G9Witnesses{
    NativePattern{"transport.g9-packet-9c-before-queue", 0x479D85,
        Packet9CBeforeQueueBytes, Packet9CBeforeQueueMask},
    NativePattern{"transport.g9-packet-9c-after-queue", 0x479E15,
        Packet9CAfterQueueBytes, Packet9CAfterQueueMask},
    NativePattern{"transport.g9-packet-9d-before-queue", 0x479F76,
        Packet9DBeforeQueueBytes, Packet9DBeforeQueueMask},
    NativePattern{"transport.g9-packet-9d-after-queue", 0x47A006,
        Packet9DAfterQueueBytes, Packet9DAfterQueueMask},
    NativePattern{"transport.g9-producer-9c-epilogue", 0x479E23,
        Packet9CProducerEpilogueBytes,
        Packet9CProducerEpilogueMask},
    NativePattern{"transport.g9-producer-9d-epilogue", 0x47A019,
        Packet9DProducerEpilogueBytes,
        Packet9DProducerEpilogueMask},
    NativePattern{"transport.g9-serializer-overflow", 0x375F25,
        FullItemSerializerOverflowBytes, FullItemSerializerOverflowMask},
    NativePattern{"transport.g9-packet-9c-consumer-bound", 0x12E2F0,
        Packet9CConsumerPayloadBoundBytes,
        Packet9CConsumerPayloadBoundMask},
    NativePattern{"transport.g9-packet-9d-consumer-buffer-header", 0x12E4B0,
        Packet9DConsumerBufferHeaderBytes,
        Packet9DConsumerBufferHeaderMask},
    NativePattern{"transport.g9-socketed-item-walker", 0x481BAD,
        SocketedItemPacketWalkerBytes,
        SocketedItemPacketWalkerMask},
    NativePattern{"transport.g9-native-queue-entry", 0x4817F0,
        NativeQueueEntryBytes, NativeQueueEntryMask},
    NativePattern{"transport.g9-native-queue-span-dispatch", 0x4818B6,
        NativeQueueSpanDispatchBytes, NativeQueueSpanDispatchMask},
};

inline constexpr std::array G1Sites{
    // Publish the bounded serializer body before any other G1 width change.
    // IDs 0..510 retain the native compound suppression-table comparison;
    // IDs >=511 bypass that fixed 511-DWORD table and write a 12-bit token.
    CodecPatchSite{
        NativePattern{"codec.g1-bounded-writer", 0x37F174,
            GenericItemBoundedWriterBytes,
            GenericItemBoundedWriterMask},
        GenericItemBoundedWriterMutations},
    CodecPatchSite{
        NativePattern{"codec.g1-reader-first", 0x37AB2B,
            GenericItemReaderFirstBytes, GenericItemReaderFirstMask},
        GenericItemReaderFirstMutations},
    CodecPatchSite{
        NativePattern{"codec.g1-reader-next", 0x37B7D4,
            GenericItemReaderNextBytes, GenericItemReaderNextMask},
        GenericItemReaderNextMutations},
    CodecPatchSite{
        NativePattern{"codec.g1-writer-terminator", 0x37F983,
            GenericItemWriterTerminatorBytes,
            GenericItemWriterTerminatorMask},
        GenericItemWriterTerminatorMutations},
};

// These fourteen witnesses close every fixed-scratch assumption consumed
// by the bounded rewrite: sole owner callsite/frame, the 511-DWORD clear,
// adjacent snapshot, its bounded 8-byte copy, direct uint16 ID extraction and
// all eight low-ID compound partner writes.
inline constexpr std::array G1Witnesses{
    NativePattern{"codec.g1-serializer-owner-frame", 0x37D140,
        GenericItemSerializerOwnerFrameBytes,
        GenericItemSerializerOwnerFrameMask},
    NativePattern{"codec.g1-serializer-owner-call", 0x3800E8,
        GenericItemSerializerOwnerCallBytes,
        GenericItemSerializerOwnerCallMask},
    NativePattern{"codec.g1-suppression-table-clear", 0x37F08A,
        GenericItemSuppressionTableClearBytes,
        GenericItemSuppressionTableClearMask},
    NativePattern{"codec.g1-snapshot-layout", 0x37F09B,
        GenericItemSnapshotLayoutBytes, GenericItemSnapshotLayoutMask},
    NativePattern{"codec.g1-snapshot-direct-id", 0x37F0D0,
        GenericItemSnapshotDirectIdBytes,
        GenericItemSnapshotDirectIdMask},
    NativePattern{"codec.g1-snapshot-copy-body", 0x2F6527,
        GenericItemSnapshotCopyBodyBytes,
        GenericItemSnapshotCopyBodyMask},
    NativePattern{"codec.g1-compound-write-51", 0x37F295,
        GenericItemCompoundWrite51Bytes,
        GenericItemCompoundWrite51Mask},
    NativePattern{"codec.g1-compound-write-49", 0x37F36D,
        GenericItemCompoundWrite49Bytes,
        GenericItemCompoundWrite49Mask},
    NativePattern{"codec.g1-compound-write-18", 0x37F445,
        GenericItemCompoundWrite18Bytes,
        GenericItemCompoundWrite18Mask},
    NativePattern{"codec.g1-compound-write-53", 0x37F51D,
        GenericItemCompoundWrite53Bytes,
        GenericItemCompoundWrite53Mask},
    NativePattern{"codec.g1-compound-write-58", 0x37F685,
        GenericItemCompoundWrite58Bytes,
        GenericItemCompoundWrite58Mask},
    NativePattern{"codec.g1-compound-write-59", 0x37F754,
        GenericItemCompoundWrite59Bytes,
        GenericItemCompoundWrite59Mask},
    NativePattern{"codec.g1-compound-write-55", 0x37F832,
        GenericItemCompoundWrite55Bytes,
        GenericItemCompoundWrite55Mask},
    NativePattern{"codec.g1-compound-write-56", 0x37F901,
        GenericItemCompoundWrite56Bytes,
        GenericItemCompoundWrite56Mask},
};

inline constexpr std::array G2Sites{
    // Publish the fail-closed reader guard before changing any ID width.
    CodecPatchSite{
        NativePattern{"codec.g2-reader-call", 0x531A54,
            AuxPlayerReaderCallBytes, AuxPlayerReaderCallMask},
        AuxiliaryReaderRelayCallMutations},
    CodecPatchSite{
        NativePattern{"codec.g2-reader-first", 0x530A99,
            AuxPlayerReaderFirstBytes, AuxPlayerReaderFirstMask},
        WidthAndSentinelMutations},
    CodecPatchSite{
        NativePattern{"codec.g2-reader-next", 0x530BA3,
            AuxPlayerReaderNextBytes, AuxPlayerReaderNextMask},
        WidthAndSentinelMutations},
    CodecPatchSite{
        NativePattern{"codec.g2-writer-id", 0x5340C0,
            AuxPlayerWriterIdBytes, AuxPlayerWriterIdMask},
        WriterIdMutation},
    CodecPatchSite{
        NativePattern{"codec.g2-writer-terminator", 0x534139,
            AuxPlayerWriterTerminatorBytes, AuxPlayerWriterTerminatorMask},
        WriterTerminatorMutations},
};
inline constexpr std::array G2Witnesses{
    NativePattern{"codec.g2-reader-owner", 0x530A00,
        AuxPlayerReaderOwnerBytes, AuxPlayerReaderOwnerMask},
    NativePattern{"codec.g2-reader-malformed-return", 0x530BCF,
        AuxPlayerReaderMalformedReturnBytes,
        AuxPlayerReaderMalformedReturnMask},
    NativePattern{"codec.g2-reader-marker", 0x530A6B,
        AuxPlayerReaderMarkerBytes, AuxPlayerReaderMarkerMask},
    NativePattern{"codec.g2-reader-fields", 0x530B69,
        AuxPlayerReaderFieldsBytes, AuxPlayerReaderFieldsMask},
    NativePattern{"codec.g2-buffer-allocation", 0x534006,
        AuxPlayerBufferAllocationBytes, AuxPlayerBufferAllocationMask},
    NativePattern{"codec.g2-count-cap-source", 0x533EAD,
        AuxPlayerCountCapSourceBytes, AuxPlayerCountCapSourceMask},
    NativePattern{"codec.g2-count-cap-use", 0x53405C,
        AuxPlayerCountCapUseBytes, AuxPlayerCountCapUseMask},
    NativePattern{"codec.g2-param-guard", 0x5340D2,
        AuxPlayerParamGuardBytes, AuxPlayerParamGuardMask},
    NativePattern{"codec.g2-param-value-writes", 0x5340FD,
        AuxPlayerParamAndValueWritesBytes,
        AuxPlayerParamAndValueWritesMask},
};

inline constexpr std::array G3Sites{
    // Both exhaustive G3 callers are guarded before the modern width sites.
    CodecPatchSite{
        NativePattern{"codec.g3-reader-primary-call", 0x52EC28,
            PlayerReaderPrimaryCallBytes, PlayerReaderPrimaryCallMask},
        PlayerReaderPrimaryRelayCallMutations},
    CodecPatchSite{
        NativePattern{"codec.g3-reader-legacy-call", 0x530A24,
            PlayerReaderLegacyCallBytes, PlayerReaderLegacyCallMask},
        PlayerReaderLegacyRelayCallMutations},
    CodecPatchSite{
        NativePattern{"codec.g3-reader-first", 0x53395E,
            PlayerReaderFirstBytes, PlayerReaderFirstMask},
        WidthAndSentinelMutations},
    CodecPatchSite{
        NativePattern{"codec.g3-reader-next", 0x533A93,
            PlayerReaderNextBytes, PlayerReaderNextMask},
        WidthAndSentinelMutations},
    CodecPatchSite{
        NativePattern{"codec.g3-writer-id", 0x5352F6,
            PlayerWriterIdBytes, PlayerWriterIdMask},
        WriterIdMutation},
    CodecPatchSite{
        NativePattern{"codec.g3-writer-terminator", 0x5353A8,
            PlayerWriterTerminatorBytes, PlayerWriterTerminatorMask},
        WriterTerminatorMutations},
    CodecPatchSite{
        NativePattern{"codec.g3-writer-relay-call", 0x5353BD,
            PlayerSaveWriterRelayCallBytes,
            PlayerSaveWriterRelayCallMask},
        PlayerSaveRelayCallMutations},
    // Publish the overflow result only after the relay call has been fully
    // written and its instruction-cache range has been flushed.
    CodecPatchSite{
        NativePattern{"codec.g3-writer-status-publish", 0x5353C7,
            PlayerSaveWriterStatusPublishBytes,
            PlayerSaveWriterStatusPublishMask},
        OverflowGuardStatusMutations},
};
inline constexpr std::array G3Witnesses{
    NativePattern{"codec.g3-reader-owner", 0x533760,
        PlayerReaderOwnerBytes, PlayerReaderOwnerMask},
    NativePattern{"codec.g3-reader-marker", 0x533924,
        PlayerReaderMarkerBytes, PlayerReaderMarkerMask},
    NativePattern{"codec.g3-reader-modern-tail", 0x533910,
        PlayerReaderModernTailBytes, PlayerReaderModernTailMask},
    NativePattern{"codec.g3-reader-legacy-malformed-return", 0x5338ED,
        PlayerReaderLegacyMalformedReturnBytes,
        PlayerReaderLegacyMalformedReturnMask},
    NativePattern{"codec.g3-reader-modern-malformed-return", 0x533ABF,
        PlayerReaderModernMalformedReturnBytes,
        PlayerReaderModernMalformedReturnMask},
    NativePattern{"codec.g3-reader-param-fields", 0x533A38,
        PlayerReaderParamFieldsBytes, PlayerReaderParamFieldsMask},
    NativePattern{"codec.g3-reader-value-fields", 0x533A52,
        PlayerReaderValueFieldsBytes, PlayerReaderValueFieldsMask},
    NativePattern{"codec.g3-buffer-origin", 0x52F0C3,
        PlayerSaveBufferOriginBytes, PlayerSaveBufferOriginMask},
    NativePattern{"codec.g3-buffer-window", 0x52F0E7,
        PlayerSaveBufferWindowBytes, PlayerSaveBufferWindowMask},
    NativePattern{"codec.g3-writer-setup", 0x52F4E9,
        PlayerSaveWriterSetupBytes, PlayerSaveWriterSetupMask},
    NativePattern{"codec.g3-writer-status-check", 0x52F522,
        PlayerSaveWriterStatusCheckBytes, PlayerSaveWriterStatusCheckMask},
    NativePattern{"codec.g3-writer-initial-bound", 0x535115,
        PlayerSaveWriterInitialBoundBytes,
        PlayerSaveWriterInitialBoundMask},
    NativePattern{"codec.g3-count-cap-use", 0x535162,
        PlayerSaveCountCapUseBytes, PlayerSaveCountCapUseMask},
    NativePattern{"codec.g3-bitwriter-initialization", 0xA1B650,
        BitWriterInitializationBytes, BitWriterInitializationMask},
    NativePattern{"codec.g3-bitwriter-overrun", 0xA1B72C,
        BitWriterOverrunFlagBytes, BitWriterOverrunFlagMask},
    NativePattern{"codec.g3-bitwriter-core", 0xA1B7A0,
        BitWriterSuccessfulCoreBytes, BitWriterSuccessfulCoreMask},
    NativePattern{"codec.g3-bitwriter-used-end", 0xA1B610,
        BitWriterUsedEndBytes, BitWriterUsedEndMask},
    NativePattern{"codec.g3-csvbits-32-bypass", 0x5351DD,
        PlayerSaveCsvBits32BypassBytes,
        PlayerSaveCsvBits32BypassMask},
    NativePattern{"codec.g3-csvbits-clamp", 0x5352DB,
        PlayerSaveCsvBitsClampBytes, PlayerSaveCsvBitsClampMask},
    NativePattern{"codec.g3-csvparambits-16-guard", 0x535308,
        PlayerSaveCsvParamBits16GuardBytes,
        PlayerSaveCsvParamBits16GuardMask},
    NativePattern{"codec.g3-param-value-writes", 0x535352,
        PlayerSaveParamAndValueWritesBytes,
        PlayerSaveParamAndValueWritesMask},
    NativePattern{"codec.g3-bitreader-signed-wrapper", 0xA1B680,
        BitReaderSignedWrapperBytes, BitReaderSignedWrapperMask},
    NativePattern{"codec.g3-stack-caller-capacity", 0x41360B,
        PlayerSaveStackCallerCapacityBytes,
        PlayerSaveStackCallerCapacityMask},
    NativePattern{"codec.g3-dynamic-capacity", 0x41E138,
        PlayerSaveDynamicCapacityBytes, PlayerSaveDynamicCapacityMask},
    NativePattern{"codec.g3-dynamic-allocation", 0x41E186,
        PlayerSaveDynamicAllocationBytes,
        PlayerSaveDynamicAllocationMask},
    NativePattern{"codec.g3-dynamic-call", 0x41E1E9,
        PlayerSaveDynamicCallBytes, PlayerSaveDynamicCallMask},
};

inline constexpr std::array G4Sites{
    // The copy wrapper validates the complete v105 D2S and its branch-B stat
    // stream before either 12-bit preview reader can run.
    CodecPatchSite{
        NativePattern{"codec.g4-preview-buffer-read", 0x61CF81,
            PlayerPreviewBufferReadBytes, PlayerPreviewBufferReadMask},
        PlayerPreviewRelayCallMutations},
    CodecPatchSite{
        NativePattern{"codec.g4-preview-b-first", 0x61D647,
            PreviewReaderBFirstBytes, PreviewReaderBFirstMask},
        PreviewWidthAndSentinelMutations},
    CodecPatchSite{
        NativePattern{"codec.g4-preview-b-next", 0x61D690,
            PreviewReaderBNextBytes, PreviewReaderBNextMask},
        PreviewWidthAndSentinelMutations},
};
inline constexpr std::array G4Witnesses{
    NativePattern{"codec.g4-preview-decoder", 0x61CEF0,
        PlayerPreviewDecoderEntryBytes, PlayerPreviewDecoderEntryMask},
    NativePattern{"codec.g4-preview-dispatch", 0x61CFFF,
        PlayerPreviewVersionDispatcherBytes,
        PlayerPreviewVersionDispatcherMask},
    NativePattern{"codec.g4-preview-buffer-allocation", 0x61CF41,
        PlayerPreviewBufferAllocationBytes,
        PlayerPreviewBufferAllocationMask},
    NativePattern{"codec.g4-preview-buffer-read-owner", 0xA1E110,
        PlayerPreviewBufferReadOwnerBytes,
        PlayerPreviewBufferReadOwnerMask},
    NativePattern{"codec.g4-preview-buffer-read-layout", 0xA1E194,
        PlayerPreviewBufferReadLayoutBytes,
        PlayerPreviewBufferReadLayoutMask},
    NativePattern{"codec.g4-preview-buffer-read-return", 0xA1E1C6,
        PlayerPreviewBufferReadReturnBytes,
        PlayerPreviewBufferReadReturnMask},
    NativePattern{"codec.g4-preview-context-source", 0x61D43F,
        PlayerPreviewContextSourceBytes,
        PlayerPreviewContextSourceMask},
    NativePattern{"codec.g4-preview-branch-b-layout", 0x61D5E4,
        PlayerPreviewBranchBLayoutBytes,
        PlayerPreviewBranchBLayoutMask},
    NativePattern{"codec.g4-preview-reject-exit", 0x61D87D,
        PlayerPreviewRejectExitBytes,
        PlayerPreviewRejectExitMask},
    NativePattern{"codec.g4-data-context-owner", 0x300A90,
        DataTablesContextOwnerBytes,
        DataTablesContextOwnerMask},
    NativePattern{"codec.g4-preview-a-legacy-first", 0x61D247,
        PreviewReaderAFirstBytes, PreviewReaderAFirstMask},
    NativePattern{"codec.g4-preview-a-legacy-next", 0x61D290,
        PreviewReaderANextBytes, PreviewReaderANextMask},
    NativePattern{"codec.g4-bitreader-thunk", 0xA1B6C0,
        BitReaderThunkBytes, BitReaderThunkMask},
    NativePattern{"codec.g4-bitreader-overrun", 0xA1BA92,
        BitReaderOverrunFlagBytes, BitReaderOverrunFlagMask},
    NativePattern{"codec.g4-bitreader-core", 0xA1BAD0,
        BitReaderSuccessfulCoreBytes, BitReaderSuccessfulCoreMask},
    NativePattern{"codec.g4-inner-vanilla-magic", 0x61CF95,
        PlayerPreviewInnerVanillaMagicBytes,
        PlayerPreviewInnerVanillaMagicMask},
};

inline constexpr std::array CodecGroups{
    // G9 owns transport before any 12-bit codec can become reachable. Its
    // internal order is queue9C, queue9D, entry9C, entry9D.
    CodecPatchGroup{
        CodecPatchGroupId::FullItemTransport,
        "G9-full-item-transport",
        G9Sites,
        G9Witnesses},
    CodecPatchGroup{
        CodecPatchGroupId::Packet3E,
        "G5-packet-3e",
        G5Sites,
        G5Witnesses},
    CodecPatchGroup{
        CodecPatchGroupId::PacketA8,
        "G6-packet-a8",
        G6Sites,
        G6Witnesses},
    CodecPatchGroup{
        CodecPatchGroupId::PacketAA,
        "G7-packet-aa",
        G7Sites,
        G7Witnesses},
    CodecPatchGroup{
        CodecPatchGroupId::PacketAC,
        "G8-packet-ac",
        G8Sites,
        G8Witnesses},
    CodecPatchGroup{
        CodecPatchGroupId::AuxiliaryPlayer,
        "G2-aux-player-codec",
        G2Sites,
        G2Witnesses},
    CodecPatchGroup{
        CodecPatchGroupId::PlayerPreview,
        "G4-player-preview-codec",
        G4Sites,
        G4Witnesses},
    // The exhaustive G2/G4 guards publish before G1's width windows.
    // The live quiescence lease still spans the whole canonical transaction.
    CodecPatchGroup{
        CodecPatchGroupId::GenericItem,
        "G1-generic-item-codec",
        G1Sites,
        G1Witnesses},
    // Keep G3 last so its overflow-status publication is the final site in a
    // whole-codec transaction.
    CodecPatchGroup{
        CodecPatchGroupId::PlayerSave,
        "G3-player-save-codec",
        G3Sites,
        G3Witnesses},
};

auto AbsoluteMutationRva(
        const CodecPatchSite& site,
        const CodecByteMutation& mutation,
        std::uintptr_t& output) noexcept -> bool {
    if (mutation.patternOffset > (std::numeric_limits<std::uintptr_t>::max)()
            - site.pattern.rva) {
        return false;
    }
    output = site.pattern.rva + mutation.patternOffset;
    return true;
}

auto MutationFlushRange(
        const CodecPatchSite& site,
        std::uintptr_t& firstRva,
        std::size_t& size) noexcept -> bool {
    if (site.mutations.empty()) return false;
    auto firstOffset = (std::numeric_limits<std::size_t>::max)();
    std::size_t lastOffset{};
    for (const auto& mutation : site.mutations) {
        if (mutation.patternOffset < firstOffset) {
            firstOffset = mutation.patternOffset;
        }
        if (mutation.patternOffset > lastOffset) {
            lastOffset = mutation.patternOffset;
        }
    }
    if (firstOffset > lastOffset
            || firstOffset > (std::numeric_limits<std::uintptr_t>::max)()
                - site.pattern.rva) {
        return false;
    }
    firstRva = site.pattern.rva + firstOffset;
    size = lastOffset - firstOffset + 1U;
    return true;
}

auto IsRel32Source(CodecByteMutation::ReplacementSource source) noexcept
        -> bool {
    using Source = CodecByteMutation::ReplacementSource;
    return source == Source::AuxiliaryReaderRelayRel32
        || source == Source::PlayerReaderRelayRel32
        || source == Source::PlayerPreviewRelayRel32
        || source == Source::PlayerSaveFinalizeRel32
        || source == Source::Packet9CQueueRelayRel32
        || source == Source::Packet9DQueueRelayRel32
        || source == Source::Packet9CEntryRelayRel32
        || source == Source::Packet9DEntryRelayRel32;
}

auto IsEntryJmpSource(
        CodecByteMutation::ReplacementSource source) noexcept -> bool {
    using Source = CodecByteMutation::ReplacementSource;
    return source == Source::Packet9CEntryRelayRel32
        || source == Source::Packet9DEntryRelayRel32;
}

auto HasEntryJmpOpcodeMutation(const CodecPatchSite& site) noexcept -> bool {
    for (const auto& mutation : site.mutations) {
        if (mutation.patternOffset == 0
                && mutation.expected == 0x40
                && mutation.replacement == 0xE9
                && mutation.source
                    == CodecByteMutation::ReplacementSource::Literal
                && mutation.sourceByteIndex == 0) {
            return true;
        }
    }
    return false;
}

auto RelayTargetRva(
        CodecByteMutation::ReplacementSource source,
        const CodecPatchActivationTargets& activationTargets) noexcept
        -> std::uintptr_t {
    using Source = CodecByteMutation::ReplacementSource;
    switch (source) {
    case Source::AuxiliaryReaderRelayRel32:
        return activationTargets.AuxiliaryReaderRelayRva();
    case Source::PlayerReaderRelayRel32:
        return activationTargets.PlayerReaderRelayRva();
    case Source::PlayerPreviewRelayRel32:
        return activationTargets.PlayerPreviewRelayRva();
    case Source::PlayerSaveFinalizeRel32:
        return activationTargets.PlayerSaveFinalizeRelayRva();
    case Source::Packet9CQueueRelayRel32:
        return activationTargets.Packet9CQueueRelayRva();
    case Source::Packet9DQueueRelayRel32:
        return activationTargets.Packet9DQueueRelayRva();
    case Source::Packet9CEntryRelayRel32:
        return activationTargets.Packet9CEntryRelayRva();
    case Source::Packet9DEntryRelayRel32:
        return activationTargets.Packet9DEntryRelayRva();
    case Source::Literal:
        return 0;
    }
    return 0;
}

auto EncodeRel32(
        std::uintptr_t target,
        std::uintptr_t nextInstructionRva,
        std::array<std::uint8_t, 4>& output) noexcept -> bool {
    if (target == 0) return false;
    std::int32_t displacement{};
    if (target >= nextInstructionRva) {
        const auto distance = target - nextInstructionRva;
        if (distance > static_cast<std::uintptr_t>(
                (std::numeric_limits<std::int32_t>::max)())) {
            return false;
        }
        displacement = static_cast<std::int32_t>(distance);
    } else {
        const auto distance = nextInstructionRva - target;
        constexpr auto MaximumNegativeDistance =
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::int32_t>::max)()) + 1ULL;
        if (distance > MaximumNegativeDistance) return false;
        displacement = distance == MaximumNegativeDistance
            ? (std::numeric_limits<std::int32_t>::min)()
            : -static_cast<std::int32_t>(distance);
    }

    const auto encoded = static_cast<std::uint32_t>(displacement);
    for (std::size_t index{}; index < output.size(); ++index) {
        output[index] = static_cast<std::uint8_t>(encoded >> (index * 8U));
    }
    return true;
}

auto ResolveCodecReplacement(
        const CodecPatchSite& site,
        const CodecByteMutation& mutation,
        const CodecPatchActivationTargets& activationTargets,
        std::uint8_t& output) noexcept -> bool {
    if (mutation.source == CodecByteMutation::ReplacementSource::Literal) {
        output = mutation.replacement;
        return true;
    }
    if (!IsRel32Source(mutation.source)
            || mutation.sourceByteIndex >= 4
            || mutation.patternOffset < mutation.sourceByteIndex) {
        return false;
    }
    const auto displacementOffset =
        mutation.patternOffset - mutation.sourceByteIndex;
    const auto entryJmp = IsEntryJmpSource(mutation.source);
    const auto expectedOpcode = static_cast<std::uint8_t>(
        entryJmp ? 0x40 : 0xE8);
    if (site.pattern.bytes.size() < 4U
            || displacementOffset == 0
            || displacementOffset > site.pattern.bytes.size() - 4U
            || site.pattern.bytes[displacementOffset - 1U]
                != expectedOpcode
            || (entryJmp && !HasEntryJmpOpcodeMutation(site))
            || site.pattern.rva >
                (std::numeric_limits<std::uintptr_t>::max)()
                    - displacementOffset - 4U) {
        return false;
    }
    const auto nextInstructionRva =
        site.pattern.rva + displacementOffset + 4U;
    std::array<std::uint8_t, 4> encoded{};
    const auto target = RelayTargetRva(mutation.source, activationTargets);
    if (target == site.pattern.rva
            || target == site.pattern.rva + 5U
            || !EncodeRel32(
            target,
            nextInstructionRva,
            encoded)) {
        return false;
    }
    bool redirectsToOriginal = true;
    for (std::size_t index{}; index < encoded.size(); ++index) {
        redirectsToOriginal = redirectsToOriginal
            && encoded[index]
                == site.pattern.bytes[displacementOffset + index];
    }
    if (redirectsToOriginal) return false;
    output = encoded[mutation.sourceByteIndex];
    return true;
}

auto WitnessOverlapsSiteMutation(
        const NativePattern& witness,
        const CodecPatchSite& site) noexcept -> bool {
    for (const auto& mutation : site.mutations) {
        std::uintptr_t mutationRva{};
        if (!AbsoluteMutationRva(site, mutation, mutationRva)) return true;
        if (mutationRva >= witness.rva
                && mutationRva - witness.rva < witness.bytes.size()) {
            return true;
        }
    }
    return false;
}

auto ValidateWitnessMutationSeparation(
        std::span<const CodecPatchGroup> groups) noexcept
        -> CodecPatchPlanError {
    for (const auto& witnessGroup : groups) {
        for (const auto& witness : witnessGroup.witnesses) {
            for (const auto& mutationGroup : groups) {
                for (const auto& site : mutationGroup.sites) {
                    if (WitnessOverlapsSiteMutation(witness, site)) {
                        return CodecPatchPlanError::WitnessOverlapsMutation;
                    }
                }
            }
        }
    }
    return CodecPatchPlanError::None;
}

} // namespace

auto PreparedCodecPatchGroups() noexcept -> std::span<const CodecPatchGroup> {
    return CodecGroups;
}

auto ValidateCodecPatchGroup(const CodecPatchGroup& group) noexcept
        -> CodecPatchPlanError {
    if (!group.name || group.sites.empty()) {
        return CodecPatchPlanError::EmptyGroup;
    }

    for (const auto& site : group.sites) {
        const auto& pattern = site.pattern;
        if (!pattern.id || pattern.rva == 0 || pattern.bytes.empty()
                || pattern.bytes.size() != pattern.mask.size()
                || site.mutations.empty()) {
            return CodecPatchPlanError::InvalidPattern;
        }
        bool hasRel32{};
        auto rel32Source = CodecByteMutation::ReplacementSource::Literal;
        std::size_t rel32Offset{};
        std::array<bool, 4> rel32Bytes{};
        for (const auto& mutation : site.mutations) {
            if (mutation.patternOffset >= pattern.bytes.size()
                    || pattern.mask[mutation.patternOffset] != 0xFF
                    || pattern.bytes[mutation.patternOffset]
                        != mutation.expected) {
                return CodecPatchPlanError::InvalidMutation;
            }
            if (mutation.source
                    == CodecByteMutation::ReplacementSource::Literal) {
                if (mutation.sourceByteIndex != 0
                        || mutation.expected == mutation.replacement) {
                    return CodecPatchPlanError::InvalidMutation;
                }
            } else if (IsRel32Source(mutation.source)) {
                if (mutation.replacement != 0
                        || mutation.sourceByteIndex >= rel32Bytes.size()
                        || mutation.patternOffset
                            < mutation.sourceByteIndex) {
                    return CodecPatchPlanError::InvalidMutation;
                }
                const auto candidateOffset =
                    mutation.patternOffset - mutation.sourceByteIndex;
                if (!hasRel32) {
                    hasRel32 = true;
                    rel32Source = mutation.source;
                    rel32Offset = candidateOffset;
                } else if (mutation.source != rel32Source
                        || candidateOffset != rel32Offset) {
                    return CodecPatchPlanError::InvalidMutation;
                }
                if (rel32Bytes[mutation.sourceByteIndex]) {
                    return CodecPatchPlanError::DuplicateMutation;
                }
                rel32Bytes[mutation.sourceByteIndex] = true;
            } else {
                return CodecPatchPlanError::InvalidMutation;
            }
            std::uintptr_t mutationRva{};
            if (!AbsoluteMutationRva(site, mutation, mutationRva)) {
                return CodecPatchPlanError::InvalidMutation;
            }

            for (const auto& otherSite : group.sites) {
                for (const auto& otherMutation : otherSite.mutations) {
                    if (&site == &otherSite && &mutation == &otherMutation) {
                        continue;
                    }
                    std::uintptr_t otherRva{};
                    if (!AbsoluteMutationRva(
                            otherSite, otherMutation, otherRva)) {
                        return CodecPatchPlanError::InvalidMutation;
                    }
                    if (otherRva == mutationRva) {
                        return CodecPatchPlanError::DuplicateMutation;
                    }
                }
            }
        }
        if (hasRel32) {
            const auto entryJmp = IsEntryJmpSource(rel32Source);
            if (pattern.bytes.size() < 4U
                    || rel32Offset == 0
                    || rel32Offset > pattern.bytes.size() - 4U
                    || pattern.bytes[rel32Offset - 1U]
                        != static_cast<std::uint8_t>(
                            entryJmp ? 0x40 : 0xE8)
                    || (entryJmp && !HasEntryJmpOpcodeMutation(site))) {
                return CodecPatchPlanError::InvalidMutation;
            }
            for (const auto observed : rel32Bytes) {
                if (!observed) {
                    return CodecPatchPlanError::InvalidMutation;
                }
            }
        }
    }
    for (const auto& witness : group.witnesses) {
        if (!witness.id || witness.rva == 0 || witness.bytes.empty()
                || witness.bytes.size() != witness.mask.size()) {
            return CodecPatchPlanError::InvalidPattern;
        }
        for (const auto& site : group.sites) {
            if (WitnessOverlapsSiteMutation(witness, site)) {
                return CodecPatchPlanError::WitnessOverlapsMutation;
            }
        }
    }
    return CodecPatchPlanError::None;
}

auto PreflightPreparedCodecPatchSet(
        const NativePublicationLeaseView& quiescence,
        const CodecPatchActivationTargets& activationTargets,
        const CodecPatchPreflightCallbacks& callbacks) noexcept
        -> CodecPatchPreflightResult {
    const std::span<const CodecPatchGroup> groups{CodecGroups};
    if (groups.empty() || !callbacks.verifyPattern) {
        return {
            .status = CodecPatchPreflightStatus::InvalidPlan,
            .planError = groups.empty()
                ? CodecPatchPlanError::EmptyGroup
                : CodecPatchPlanError::None,
        };
    }
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
    }
    for (const auto& group : groups) {
        if (!quiescence.IsHeld()) {
            return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
        }
        const auto planError = ValidateCodecPatchGroup(group);
        if (!quiescence.IsHeld()) {
            return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
        }
        if (planError != CodecPatchPlanError::None) {
            return {
                .status = CodecPatchPreflightStatus::InvalidPlan,
                .planError = planError,
            };
        }
    }
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
    }
    const auto witnessSeparationError =
        ValidateWitnessMutationSeparation(groups);
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
    }
    if (witnessSeparationError != CodecPatchPlanError::None) {
        return {
            .status = CodecPatchPreflightStatus::InvalidPlan,
            .planError = witnessSeparationError,
        };
    }
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
    }
    for (std::size_t groupIndex{}; groupIndex < groups.size(); ++groupIndex) {
        const auto& group = groups[groupIndex];
        for (const auto& site : group.sites) {
            for (const auto& mutation : site.mutations) {
                if (!quiescence.IsHeld()) {
                    return {
                        .status =
                            CodecPatchPreflightStatus::QuiescenceRequired,
                    };
                }
                std::uintptr_t mutationRva{};
                if (!AbsoluteMutationRva(site, mutation, mutationRva)) {
                    return {
                        .status = CodecPatchPreflightStatus::InvalidPlan,
                        .planError = CodecPatchPlanError::InvalidMutation,
                    };
                }
                for (std::size_t otherGroupIndex = groupIndex + 1;
                        otherGroupIndex < groups.size(); ++otherGroupIndex) {
                    for (const auto& otherSite
                            : groups[otherGroupIndex].sites) {
                        for (const auto& otherMutation
                                : otherSite.mutations) {
                            std::uintptr_t otherRva{};
                            if (!AbsoluteMutationRva(
                                    otherSite, otherMutation, otherRva)) {
                                return {
                                    .status =
                                        CodecPatchPreflightStatus::InvalidPlan,
                                    .planError =
                                        CodecPatchPlanError::InvalidMutation,
                                };
                            }
                            if (otherRva == mutationRva) {
                                return {
                                    .status =
                                        CodecPatchPreflightStatus::InvalidPlan,
                                    .planError =
                                        CodecPatchPlanError::DuplicateMutation,
                                };
                            }
                        }
                    }
                }
                if (!quiescence.IsHeld()) {
                    return {
                        .status =
                            CodecPatchPreflightStatus::QuiescenceRequired,
                    };
                }
            }
        }
    }

    std::array<std::uintptr_t, PreparedCodecMutationCount> mutationRvas{};
    std::array<std::uint8_t, PreparedCodecMutationCount> expectedBytes{};
    std::array<std::uint8_t, PreparedCodecMutationCount> resolvedBytes{};
    std::array<PreparedCodecPatchPlan::FlushRange,
        PreparedCodecMutableSiteCount> flushRanges{};
    std::size_t resolvedCount{};
    std::size_t flushRangeCount{};
    for (const auto& group : groups) {
        for (const auto& site : group.sites) {
            if (!quiescence.IsHeld()) {
                return {
                    .status = CodecPatchPreflightStatus::QuiescenceRequired,
                };
            }
            if (flushRangeCount >= flushRanges.size()) {
                return {
                    .status = CodecPatchPreflightStatus::InvalidPlan,
                    .planError = CodecPatchPlanError::InvalidMutation,
                };
            }
            const auto firstMutationIndex = resolvedCount;
            for (const auto& mutation : site.mutations) {
                if (!quiescence.IsHeld()) {
                    return {
                        .status =
                            CodecPatchPreflightStatus::QuiescenceRequired,
                    };
                }
                if (resolvedCount >= resolvedBytes.size()
                        || !AbsoluteMutationRva(
                            site,
                            mutation,
                            mutationRvas[resolvedCount])
                        || !ResolveCodecReplacement(
                            site,
                            mutation,
                            activationTargets,
                            resolvedBytes[resolvedCount])) {
                    return {
                        .status = CodecPatchPreflightStatus::InvalidPlan,
                        .planError = IsRel32Source(mutation.source)
                            ? CodecPatchPlanError::InvalidActivationTarget
                            : CodecPatchPlanError::InvalidMutation,
                    };
                }
                expectedBytes[resolvedCount] = mutation.expected;
                ++resolvedCount;
                if (!quiescence.IsHeld()) {
                    return {
                        .status =
                            CodecPatchPreflightStatus::QuiescenceRequired,
                    };
                }
            }
            std::uintptr_t firstRva{};
            std::size_t flushSize{};
            if (!MutationFlushRange(site, firstRva, flushSize)) {
                return {
                    .status = CodecPatchPreflightStatus::InvalidPlan,
                    .planError = CodecPatchPlanError::InvalidMutation,
                };
            }
            if (!quiescence.IsHeld()) {
                return {
                    .status = CodecPatchPreflightStatus::QuiescenceRequired,
                };
            }
            flushRanges[flushRangeCount++] = {
                .firstMutationIndex = firstMutationIndex,
                .mutationCount = resolvedCount - firstMutationIndex,
                .firstRva = firstRva,
                .size = flushSize,
            };
        }
    }
    if (resolvedCount != resolvedBytes.size()
            || flushRangeCount != flushRanges.size()) {
        return {
            .status = CodecPatchPreflightStatus::InvalidPlan,
            .planError = CodecPatchPlanError::InvalidMutation,
        };
    }
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
    }
    for (const auto& group : groups) {
        for (const auto& site : group.sites) {
            if (!quiescence.IsHeld()) {
                return {
                    .status = CodecPatchPreflightStatus::QuiescenceRequired,
                };
            }
            const auto matches =
                callbacks.verifyPattern(callbacks.context, site.pattern);
            if (!quiescence.IsHeld()) {
                return {
                    .status = CodecPatchPreflightStatus::QuiescenceRequired,
                };
            }
            if (!matches) {
                return {.status = CodecPatchPreflightStatus::PreflightFailed};
            }
        }
        for (const auto& witness : group.witnesses) {
            if (!quiescence.IsHeld()) {
                return {
                    .status = CodecPatchPreflightStatus::QuiescenceRequired,
                };
            }
            const auto matches =
                callbacks.verifyPattern(callbacks.context, witness);
            if (!quiescence.IsHeld()) {
                return {
                    .status = CodecPatchPreflightStatus::QuiescenceRequired,
                };
            }
            if (!matches) {
                return {.status = CodecPatchPreflightStatus::PreflightFailed};
            }
        }
    }
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchPreflightStatus::QuiescenceRequired};
    }

    CodecPatchPreflightResult result{
        .status = CodecPatchPreflightStatus::Prepared,
    };
    result.plan.emplace(PreparedCodecPatchPlan{
        mutationRvas,
        expectedBytes,
        resolvedBytes,
        flushRanges,
    });
    return result;
}

auto CommitPreflightedCodecPatchSet(
        const NativePublicationLeaseView& quiescence,
        const PreparedCodecPatchPlan& plan,
        const CodecPatchCommitCallbacks& callbacks) noexcept
        -> CodecPatchCommitResult {
    if (!callbacks.writeByte || !callbacks.flushInstructionCache) {
        return {.status = CodecPatchCommitStatus::InvalidPlan};
    }
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchCommitStatus::QuiescenceRequired};
    }
    CodecPatchCommitResult result{
        .status = CodecPatchCommitStatus::Active,
    };
    for (const auto& flushRange : plan.flushRanges_) {
        const auto mutationEnd =
            flushRange.firstMutationIndex + flushRange.mutationCount;
        for (auto mutationIndex = flushRange.firstMutationIndex;
                mutationIndex < mutationEnd; ++mutationIndex) {
            if (!quiescence.IsHeld()) {
                result.status = result.mutationAttempted
                    ? CodecPatchCommitStatus::
                        PartialCommitColdRestartRequired
                    : CodecPatchCommitStatus::QuiescenceRequired;
                return result;
            }
            ++result.attemptedMutations;
            const auto replacement = plan.resolvedBytes_[mutationIndex];
            const auto expected = plan.expectedBytes_[mutationIndex];
            if (replacement == expected) {
                ++result.confirmedMutations;
                ++result.confirmedNoOpMutations;
                continue;
            }
            result.mutationAttempted = true;
            if (!callbacks.writeByte(
                    callbacks.context,
                    plan.mutationRvas_[mutationIndex],
                    expected,
                    replacement)) {
                result.status = CodecPatchCommitStatus::
                    PartialCommitColdRestartRequired;
                return result;
            }
            ++result.confirmedMutations;
            if (!quiescence.IsHeld()) {
                result.status = result.mutationAttempted
                    ? CodecPatchCommitStatus::
                        PartialCommitColdRestartRequired
                    : CodecPatchCommitStatus::QuiescenceRequired;
                return result;
            }
        }
        if (!quiescence.IsHeld()) {
            result.status = result.mutationAttempted
                ? CodecPatchCommitStatus::PartialCommitColdRestartRequired
                : CodecPatchCommitStatus::QuiescenceRequired;
            return result;
        }
        if (!callbacks.flushInstructionCache(
                callbacks.context,
                flushRange.firstRva,
                flushRange.size)) {
            result.status = CodecPatchCommitStatus::
                PartialCommitColdRestartRequired;
            return result;
        }
        ++result.confirmedFlushes;
        if (!quiescence.IsHeld()) {
            result.status = result.mutationAttempted
                ? CodecPatchCommitStatus::PartialCommitColdRestartRequired
                : CodecPatchCommitStatus::QuiescenceRequired;
            return result;
        }
    }
    return result;
}

#if defined(ISC12_CODEC_PATCH_TESTING)
auto CommitPreparedCodecPatchSet(
        const NativePublicationLeaseView& quiescence,
        const CodecPatchActivationTargets& activationTargets,
        const CodecPatchCallbacks& callbacks) noexcept
        -> CodecPatchCommitResult {
    if (!callbacks.verifyPattern || !callbacks.writeByte
            || !callbacks.flushInstructionCache
            || !callbacks.reserveMutationLifetime) {
        return {.status = CodecPatchCommitStatus::InvalidPlan};
    }

    auto preflight = PreflightPreparedCodecPatchSet(
        quiescence,
        activationTargets,
        CodecPatchPreflightCallbacks{
            .context = callbacks.context,
            .verifyPattern = callbacks.verifyPattern,
        });
    if (preflight.status != CodecPatchPreflightStatus::Prepared
            || !preflight.plan) {
        const auto status = [&preflight]() noexcept {
            switch (preflight.status) {
            case CodecPatchPreflightStatus::Prepared:
            case CodecPatchPreflightStatus::InvalidPlan:
                return CodecPatchCommitStatus::InvalidPlan;
            case CodecPatchPreflightStatus::QuiescenceRequired:
                return CodecPatchCommitStatus::QuiescenceRequired;
            case CodecPatchPreflightStatus::PreflightFailed:
                return CodecPatchCommitStatus::PreflightFailed;
            }
            return CodecPatchCommitStatus::InvalidPlan;
        }();
        return {
            .status = status,
            .planError = preflight.planError,
        };
    }

    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchCommitStatus::QuiescenceRequired};
    }
    callbacks.reserveMutationLifetime(callbacks.context);
    if (!quiescence.IsHeld()) {
        return {.status = CodecPatchCommitStatus::QuiescenceRequired};
    }
    return CommitPreflightedCodecPatchSet(
        quiescence,
        *preflight.plan,
        CodecPatchCommitCallbacks{
            .context = callbacks.context,
            .writeByte = callbacks.writeByte,
            .flushInstructionCache = callbacks.flushInstructionCache,
        });
}
#endif

static_assert(
    GenericItemReaderFirstMutations.size()
        + GenericItemReaderNextMutations.size()
        + GenericItemBoundedWriterMutations.size()
        + GenericItemWriterTerminatorMutations.size()
        + WidthAndSentinelMutations.size() * 4
        + WriterIdMutation.size() * 2
        + WriterTerminatorMutations.size() * 2
        + PreviewWidthAndSentinelMutations.size() * 2
        + AuxiliaryReaderRelayCallMutations.size()
        + PlayerReaderPrimaryRelayCallMutations.size()
        + PlayerReaderLegacyRelayCallMutations.size()
        + PlayerPreviewRelayCallMutations.size()
        + PlayerSaveRelayCallMutations.size()
        + OverflowGuardStatusMutations.size()
        + Packet9CQueueRelayCallMutations.size()
        + Packet9DQueueRelayCallMutations.size()
        + Packet9CEntryRelayJumpMutations.size()
        + Packet9DEntryRelayJumpMutations.size()
        + Packet3EConsumerWidthMutations.size()
        + Packet3EProducerWidthMutations.size()
        + Packet3EProducerBoundMutations.size()
        + PacketA8ConsumerFirstWidthMutations.size()
        + PacketA8ConsumerFirstSentinelMutations.size()
        + PacketA8ConsumerNextMutations.size()
        + PacketA8ProducerIdMutations.size()
        + PacketA8ProducerTerminatorMutations.size()
        + PacketAAConsumerFirstMutations.size()
        + PacketAAConsumerNextMutations.size()
        + PacketAAEstimatorIdMutations.size()
        + PacketAAEstimatorTerminatorMutations.size()
        + PacketAAProducerIdMutations.size()
        + PacketAAProducerTerminatorMutations.size()
        + PacketAACountCapMutations.size()
        + PacketACConsumerFirstMutations.size()
        + PacketACConsumerNextMutations.size()
        + PacketACProducerIdMutations.size()
        + PacketACProducerTerminatorMutations.size()
    == PreparedCodecMutationCount);
static_assert(
    G9Sites.size() + G1Sites.size() + G2Sites.size() + G3Sites.size()
        + G4Sites.size() + G5Sites.size() + G6Sites.size()
        + G7Sites.size() + G8Sites.size()
    == PreparedCodecMutableSiteCount);
static_assert(
    G9Witnesses.size() + G1Witnesses.size() + G2Witnesses.size()
        + G3Witnesses.size()
        + G4Witnesses.size() + G5Witnesses.size() + G6Witnesses.size()
        + G7Witnesses.size() + G8Witnesses.size()
    == PreparedCodecWitnessCount);

} // namespace ruffneckk::isc12
