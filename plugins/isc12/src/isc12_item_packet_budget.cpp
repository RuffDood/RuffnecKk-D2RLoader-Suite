#include "isc12_item_packet_budget.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace ruffneckk::isc12 {

auto ClassifyFullItemTransportProvider(
        bool allNativeSurfacesMatch,
        std::span<const FullItemTransportHookObservation> observations,
        std::string_view pluginId,
        std::string_view pluginVersion) noexcept
        -> FullItemTransportProvider {
    if (allNativeSurfacesMatch) {
        return FullItemTransportProvider::NativeG9;
    }
    if (pluginId != ExtendedItemStatsProviderId
            || pluginVersion != ExtendedItemStatsProviderVersion
            || observations.size()
                != FullItemTransportProviderSurfaceCount) {
        return FullItemTransportProvider::Invalid;
    }
    for (const auto& observation : observations) {
        if (!observation.querySucceeded
                || !observation.trackedInlineHook
                || observation.ownerCount != 1
                || observation.ownerPluginId
                    != ExtendedItemStatsProviderId) {
            return FullItemTransportProvider::Invalid;
        }
    }
    return FullItemTransportProvider::ExtendedItemStatsV1;
}

namespace {

constexpr auto BitsToBytes(
        std::size_t bits,
        std::size_t& bytes) noexcept -> bool {
    constexpr auto Maximum = (std::numeric_limits<std::size_t>::max)();
    if (bits > Maximum - 7U) return false;
    bytes = (bits + 7U) / 8U;
    return true;
}

constexpr auto CheckedAdd(
        std::size_t left,
        std::size_t right,
        std::size_t& result) noexcept -> bool {
    constexpr auto Maximum = (std::numeric_limits<std::size_t>::max)();
    if (right > Maximum - left) return false;
    result = left + right;
    return true;
}

constexpr auto CheckedTokenBits(
        std::size_t tokens,
        std::size_t bitsPerToken,
        std::size_t& result) noexcept -> bool {
    constexpr auto Maximum = (std::numeric_limits<std::size_t>::max)();
    if (tokens > Maximum / bitsPerToken) return false;
    result = tokens * bitsPerToken;
    return true;
}

auto BuildEstimate(
        std::size_t directBits,
        std::size_t tokens,
        std::size_t addedBitsPerToken) noexcept -> FullItemPayloadEstimate {
    std::size_t tokenBits{};
    std::size_t usedBits{};
    if (!CheckedTokenBits(tokens, addedBitsPerToken, tokenBits)
            || !CheckedAdd(directBits, tokenBits, usedBits)) {
        return {.statIdTokens = tokens};
    }

    std::size_t totalBytes{};
    if (!BitsToBytes(usedBits, totalBytes) || totalBytes == 0) {
        return {.statIdTokens = tokens};
    }

    return {
        .valid = true,
        .totalBits = usedBits,
        .totalBytes = totalBytes,
        .statIdTokens = tokens,
    };
}

constexpr auto IsKnownPacketKind(FullItemPacketKind kind) noexcept -> bool {
    return kind == FullItemPacketKind::ItemAction9C
        || kind == FullItemPacketKind::ItemAction9D;
}

constexpr auto PacketOpcode(FullItemPacketKind kind) noexcept
        -> std::uint8_t {
    if (kind == FullItemPacketKind::ItemAction9C) return 0x9C;
    if (kind == FullItemPacketKind::ItemAction9D) return 0x9D;
    return 0;
}

auto RejectTransaction(
        FullItemPacketStagingContext& transaction,
        FullItemPacketStagingError error) noexcept -> void {
    if (transaction.error == FullItemPacketStagingError::None) {
        transaction.error = error;
    }
    if (transaction.state != FullItemPacketStagingState::Fatal) {
        transaction.state = FullItemPacketStagingState::Rejected;
    }
}

auto FailTransactionFatally(
        FullItemPacketStagingContext& transaction,
        FullItemPacketStagingError error) noexcept -> void {
    if (transaction.error == FullItemPacketStagingError::None) {
        transaction.error = error;
    }
    transaction.state = FullItemPacketStagingState::Fatal;
}

auto ResetTransactionToIdle(
        FullItemPacketStagingContext& transaction) noexcept -> void {
    transaction.state = FullItemPacketStagingState::Idle;
    transaction.error = FullItemPacketStagingError::None;
    transaction.rootKind = FullItemPacketKind::ItemAction9C;
    transaction.rootClient = nullptr;
    transaction.nodeCount = 0;
    transaction.packetCount = 0;
    transaction.activeDepth = 0;
    transaction.totalBytes = 0;
    transaction.rejectedProducerTemporaryFlags = 0;
    transaction.rejectedParentTemporaryFlags = 0;
}

auto ValidateCopiedPacket(
        FullItemPacketKind kind,
        std::uint8_t action,
        std::span<const std::uint8_t> packet) noexcept
        -> FullItemPacketStagingError {
    if (!IsKnownPacketKind(kind)) {
        return FullItemPacketStagingError::RelayKindMismatch;
    }
    const auto budget = FullItemPacketBudgetFor(kind);
    if (packet.size() <= budget.headerBytes
            || packet.size() > MaximumStagedItemPacketBytes) {
        return FullItemPacketStagingError::InvalidPacketLength;
    }
    const auto payloadBytes = packet.size() - budget.headerBytes;
    if (payloadBytes == 0 || payloadBytes > budget.payloadCapacityBytes) {
        return FullItemPacketStagingError::PayloadOutsideBudget;
    }
    if (packet[0] != PacketOpcode(kind)
            || packet[1] != action
            || packet[2] != static_cast<std::uint8_t>(packet.size())) {
        return FullItemPacketStagingError::InvalidPacketHeader;
    }
    return FullItemPacketStagingError::None;
}

auto StartTransaction(
        FullItemPacketStagingContext& transaction,
        const FullItemProducerDescriptor& producer) noexcept -> bool {
    if (transaction.generation
            == (std::numeric_limits<std::uint64_t>::max)()) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::GenerationExhausted);
        return false;
    }
    ++transaction.generation;
    transaction.state = FullItemPacketStagingState::Collecting;
    transaction.error = FullItemPacketStagingError::None;
    transaction.rootKind = producer.kind;
    transaction.rootClient = producer.client;
    transaction.nodeCount = 0;
    transaction.packetCount = 0;
    transaction.activeDepth = 0;
    transaction.totalBytes = 0;
    transaction.rejectedProducerTemporaryFlags = 0;
    transaction.rejectedParentTemporaryFlags = 0;
    return true;
}

auto AdmitNode(
        FullItemPacketStagingContext& transaction,
        const FullItemProducerDescriptor& producer,
        bool ownsRoot) noexcept -> FullItemProducerAdmission {
    const auto nodeIndex = transaction.nodeCount;
    const auto depth = transaction.activeDepth + 1U;
    transaction.nodes[nodeIndex] = {
        .kind = producer.kind,
        .parentItem = producer.parentItem,
        .item = producer.item,
        .action = producer.action,
        .directChildCount = 0,
        .temporaryFlags = producer.temporaryFlags,
        .gamble = producer.gamble,
        .depth = depth,
        .packetCaptured = false,
    };
    transaction.activeNodeIndices[transaction.activeDepth] = nodeIndex;
    ++transaction.nodeCount;
    ++transaction.activeDepth;
    return {
        .token = {
            .generation = transaction.generation,
            .nodeIndex = nodeIndex,
            .depth = depth,
            .ownsRoot = ownsRoot,
            .admitted = true,
        },
        .disposition = FullItemProducerDisposition::InvokeOriginal,
        .error = FullItemPacketStagingError::None,
    };
}

} // namespace

auto EstimateItemPacketPayload(
        const FullItemNodePayload& node) noexcept
        -> FullItemPayloadEstimate {
    return BuildEstimate(
        node.directNonStatIdBits,
        node.statIdTokens,
        12U);
}

auto ExpandLegacyItemPacketPayload(
        const LegacyFullItemNodePayload& node) noexcept
        -> FullItemPayloadEstimate {
    return BuildEstimate(
        node.directLegacyBits,
        node.statIdTokens,
        3U);
}

auto ClassifyFullItemPayload(
        std::size_t encodedBytes,
        FullItemPacketKind packet) noexcept -> FullItemPayloadDisposition {
    if (encodedBytes == 0
            || (packet != FullItemPacketKind::ItemAction9C
                && packet != FullItemPacketKind::ItemAction9D)) {
        return FullItemPayloadDisposition::InvalidEncoding;
    }

    if (encodedBytes <=
            FullItemPacketBudgetFor(packet).payloadCapacityBytes) {
        return FullItemPayloadDisposition::NativePacket;
    }
    return FullItemPayloadDisposition::ExceedsNativePacketCapacity;
}

auto ClassifyFullItemPacketSequence(
        std::span<const FullItemNodePayload> nodes,
        FullItemPacketKind rootPacket) noexcept -> FullItemPayloadDisposition {
    if (nodes.empty()) return FullItemPayloadDisposition::InvalidEncoding;

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto estimate = EstimateItemPacketPayload(nodes[index]);
        if (!estimate.valid) {
            return FullItemPayloadDisposition::InvalidEncoding;
        }
        const auto packet = index == 0
            ? rootPacket
            : FullItemPacketKind::ItemAction9D;
        const auto disposition =
            ClassifyFullItemPayload(estimate.totalBytes, packet);
        if (disposition != FullItemPayloadDisposition::NativePacket) {
            return disposition;
        }
    }
    return FullItemPayloadDisposition::NativePacket;
}

auto PreflightAndVisitFullItemPacketTree(
        const FullItemPacketTreeView& tree,
        FullItemPacketKind rootPacket,
        FullItemPacketTreeScratch scratch,
        FullItemPacketTreeVisitor visitor,
        void* context) noexcept -> FullItemPacketTreeError {
    if (visitor == nullptr || tree.nodes.empty()
            || (rootPacket != FullItemPacketKind::ItemAction9C
                && rootPacket != FullItemPacketKind::ItemAction9D)) {
        return FullItemPacketTreeError::InvalidArgument;
    }
    if (tree.declaredNodeCount != tree.nodes.size()) {
        return FullItemPacketTreeError::CountMismatch;
    }
    if (tree.rootNodeIndex >= tree.nodes.size()) {
        return FullItemPacketTreeError::NodeIndexOutOfRange;
    }
    if (scratch.nodeMarks.size() < tree.nodes.size()
            || scratch.traversalOrder.size() < tree.nodes.size()
            || scratch.nodeStack.size() < tree.nodes.size()) {
        return FullItemPacketTreeError::InsufficientScratch;
    }

    std::size_t nextChildListOffset{};
    for (std::size_t nodeIndex{};
            nodeIndex < tree.nodes.size(); ++nodeIndex) {
        const auto& node = tree.nodes[nodeIndex];
        if (node.firstChildListOffset != nextChildListOffset) {
            return FullItemPacketTreeError::ChildListMismatch;
        }
        if (node.listedChildCount != node.declaredChildCount) {
            return FullItemPacketTreeError::CountMismatch;
        }
        if (node.listedChildCount > node.socketCapacity) {
            return FullItemPacketTreeError::SocketCapacityExceeded;
        }

        std::size_t expectedStatIdTokens{};
        if (!CheckedAdd(
                node.emittedStatRecordCount,
                node.emittedStatListTerminatorCount,
                expectedStatIdTokens)
                || expectedStatIdTokens != node.payload.statIdTokens) {
            return FullItemPacketTreeError::CountMismatch;
        }

        std::size_t childListEnd{};
        if (!CheckedAdd(
                nextChildListOffset,
                node.listedChildCount,
                childListEnd)
                || childListEnd > tree.childNodeIndices.size()) {
            return FullItemPacketTreeError::ChildListMismatch;
        }
        nextChildListOffset = childListEnd;

        const auto estimate = EstimateItemPacketPayload(node.payload);
        if (!estimate.valid) {
            return FullItemPacketTreeError::InvalidEncoding;
        }
        const auto packet = nodeIndex == tree.rootNodeIndex
            ? rootPacket
            : FullItemPacketKind::ItemAction9D;
        const auto disposition =
            ClassifyFullItemPayload(estimate.totalBytes, packet);
        if (disposition
                == FullItemPayloadDisposition::ExceedsNativePacketCapacity) {
            return FullItemPacketTreeError::ExceedsNativePacketCapacity;
        }
        if (disposition != FullItemPayloadDisposition::NativePacket) {
            return FullItemPacketTreeError::InvalidEncoding;
        }
    }
    if (nextChildListOffset != tree.childNodeIndices.size()) {
        return FullItemPacketTreeError::ChildListMismatch;
    }

    auto marks = scratch.nodeMarks.first(tree.nodes.size());
    std::fill(marks.begin(), marks.end(), std::uint8_t{});
    for (const auto childNodeIndex : tree.childNodeIndices) {
        if (childNodeIndex >= tree.nodes.size()) {
            return FullItemPacketTreeError::NodeIndexOutOfRange;
        }
        if (childNodeIndex == tree.rootNodeIndex
                || marks[childNodeIndex] != 0) {
            return FullItemPacketTreeError::DuplicateOrCycle;
        }
        marks[childNodeIndex] = 1;
    }

    std::fill(marks.begin(), marks.end(), std::uint8_t{});
    auto order = scratch.traversalOrder.first(tree.nodes.size());
    auto stack = scratch.nodeStack.first(tree.nodes.size());
    std::size_t plannedNodeCount{};
    std::size_t stackSize{1};
    stack[0] = tree.rootNodeIndex;
    marks[tree.rootNodeIndex] = 1;
    while (stackSize != 0) {
        const auto nodeIndex = stack[stackSize - 1U];
        --stackSize;
        if (plannedNodeCount >= order.size()) {
            return FullItemPacketTreeError::DuplicateOrCycle;
        }
        order[plannedNodeCount] = nodeIndex;
        ++plannedNodeCount;

        const auto& node = tree.nodes[nodeIndex];
        // Reverse-push preserves the inventory walk's first-to-last order
        // while producing the native recursive depth-first preorder.
        for (std::size_t remaining = node.listedChildCount;
                remaining != 0; --remaining) {
            const auto childNodeIndex = tree.childNodeIndices[
                node.firstChildListOffset + remaining - 1U];
            if (marks[childNodeIndex] != 0
                    || stackSize >= stack.size()) {
                return FullItemPacketTreeError::DuplicateOrCycle;
            }
            marks[childNodeIndex] = 1;
            stack[stackSize] = childNodeIndex;
            ++stackSize;
        }
    }
    if (plannedNodeCount != tree.nodes.size()) {
        return FullItemPacketTreeError::UnreachableNode;
    }

    for (std::size_t position{}; position < plannedNodeCount; ++position) {
        visitor(
            context,
            order[position],
            position == 0
                ? rootPacket
                : FullItemPacketKind::ItemAction9D);
    }
    return FullItemPacketTreeError::None;
}

auto BeginFullItemPacketProducer(
        FullItemPacketStagingContext& transaction,
        const FullItemProducerDescriptor& producer) noexcept
        -> FullItemProducerAdmission {
    if (transaction.state == FullItemPacketStagingState::Fatal) {
        return {.error = transaction.error};
    }
    if (transaction.state == FullItemPacketStagingState::Flushing) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::ReenteredDuringFlush);
        return {.error = transaction.error};
    }
    if (transaction.state == FullItemPacketStagingState::Ready) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::InvalidState);
        return {.error = transaction.error};
    }
    if (transaction.state == FullItemPacketStagingState::Rejected) {
        return {
            .token = {.generation = transaction.generation},
            .error = transaction.error,
        };
    }

    if (transaction.state == FullItemPacketStagingState::Idle) {
        if (!StartTransaction(transaction, producer)) {
            return {.error = transaction.error};
        }
        FullItemProducerToken rejectedRoot{
            .generation = transaction.generation,
            .ownsRoot = true,
        };
        if (!IsKnownPacketKind(producer.kind)
                || producer.client == nullptr
                || producer.item == nullptr
                || (producer.kind == FullItemPacketKind::ItemAction9D
                    && producer.parentItem == nullptr)) {
            RejectTransaction(
                transaction,
                FullItemPacketStagingError::InvalidArgument);
            return {
                .token = rejectedRoot,
                .error = transaction.error,
            };
        }
        return AdmitNode(transaction, producer, true);
    }

    if (transaction.state != FullItemPacketStagingState::Collecting
            || transaction.activeDepth == 0
            || transaction.activeDepth > transaction.activeNodeIndices.size()) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::InvalidState);
        return {.error = transaction.error};
    }

    const auto rejectNested = [&](FullItemPacketStagingError error) {
        RejectTransaction(transaction, error);
        return FullItemProducerAdmission{
            .token = {.generation = transaction.generation},
            .error = transaction.error,
        };
    };
    if (producer.kind == FullItemPacketKind::ItemAction9C) {
        return rejectNested(
            FullItemPacketStagingError::UnexpectedNested9C);
    }
    if (producer.kind != FullItemPacketKind::ItemAction9D) {
        return rejectNested(
            FullItemPacketStagingError::UnexpectedNested9D);
    }
    if (producer.client == nullptr
            || producer.parentItem == nullptr
            || producer.item == nullptr) {
        return rejectNested(FullItemPacketStagingError::InvalidArgument);
    }

    const auto parentNodeIndex =
        transaction.activeNodeIndices[transaction.activeDepth - 1U];
    if (parentNodeIndex >= transaction.nodeCount) {
        return rejectNested(FullItemPacketStagingError::InvalidState);
    }
    auto& parentNode = transaction.nodes[parentNodeIndex];
    if (producer.client != transaction.rootClient) {
        return rejectNested(FullItemPacketStagingError::ClientMismatch);
    }
    if (producer.parentItem != parentNode.item) {
        return rejectNested(FullItemPacketStagingError::ParentMismatch);
    }
    if (producer.action != 0x12) {
        return rejectNested(
            FullItemPacketStagingError::NestedActionMismatch);
    }
    const auto expectedNestedTemporaryFlags =
        parentNode.temporaryFlags | NestedFullItemTemporaryFlagsMask;
    if (producer.temporaryFlags != expectedNestedTemporaryFlags) {
        transaction.rejectedProducerTemporaryFlags =
            producer.temporaryFlags;
        transaction.rejectedParentTemporaryFlags =
            parentNode.temporaryFlags;
        return rejectNested(
            FullItemPacketStagingError::NestedFlagsMismatch);
    }
    if (producer.gamble != 0) {
        return rejectNested(
            FullItemPacketStagingError::NestedGambleMismatch);
    }
    if (!parentNode.packetCaptured) {
        return rejectNested(
            FullItemPacketStagingError::ParentPacketMissing);
    }
    if (transaction.nodeCount >= MaximumStagedItemPacketCount) {
        return rejectNested(
            FullItemPacketStagingError::NodeLimitExceeded);
    }
    if (transaction.activeDepth >= MaximumStagedItemTreeDepth) {
        return rejectNested(
            FullItemPacketStagingError::DepthLimitExceeded);
    }
    if (parentNode.directChildCount
            >= MaximumStagedItemImmediateChildren) {
        return rejectNested(
            FullItemPacketStagingError::ImmediateChildLimitExceeded);
    }
    for (std::size_t index{}; index < transaction.nodeCount; ++index) {
        if (transaction.nodes[index].item == producer.item) {
            return rejectNested(
                FullItemPacketStagingError::DuplicateOrCycle);
        }
    }

    ++parentNode.directChildCount;
    return AdmitNode(transaction, producer, false);
}

auto CaptureFullItemPacketQueueCall(
        FullItemPacketStagingContext& transaction,
        FullItemPacketKind relayKind,
        void* client,
        const std::uint8_t* bytes,
        std::size_t length) noexcept -> FullItemPacketStagingError {
    if (transaction.state == FullItemPacketStagingState::Fatal) {
        return transaction.error;
    }
    if (transaction.state == FullItemPacketStagingState::Flushing) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::ReenteredDuringFlush);
        return transaction.error;
    }
    if (transaction.state == FullItemPacketStagingState::Idle
            || transaction.state == FullItemPacketStagingState::Ready) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::RelayWithoutTransaction);
        return transaction.error;
    }
    if (transaction.state == FullItemPacketStagingState::Rejected) {
        return transaction.error;
    }
    if (transaction.state != FullItemPacketStagingState::Collecting
            || transaction.activeDepth == 0
            || transaction.activeDepth > transaction.activeNodeIndices.size()) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::InvalidState);
        return transaction.error;
    }

    const auto reject = [&](FullItemPacketStagingError error) {
        RejectTransaction(transaction, error);
        return transaction.error;
    };
    if (!IsKnownPacketKind(relayKind)) {
        return reject(FullItemPacketStagingError::RelayKindMismatch);
    }
    if (client != transaction.rootClient) {
        return reject(FullItemPacketStagingError::ClientMismatch);
    }
    const auto nodeIndex =
        transaction.activeNodeIndices[transaction.activeDepth - 1U];
    if (nodeIndex >= transaction.nodeCount) {
        return reject(FullItemPacketStagingError::InvalidState);
    }
    auto& node = transaction.nodes[nodeIndex];
    if (node.kind != relayKind) {
        return reject(FullItemPacketStagingError::RelayKindMismatch);
    }
    if (node.packetCaptured) {
        return reject(FullItemPacketStagingError::DuplicatePacket);
    }
    if (transaction.packetCount != nodeIndex) {
        return reject(FullItemPacketStagingError::CaptureOrderMismatch);
    }
    if (bytes == nullptr) {
        return reject(FullItemPacketStagingError::InvalidPacketPointer);
    }
    const auto budget = FullItemPacketBudgetFor(relayKind);
    if (length <= budget.headerBytes
            || length > MaximumStagedItemPacketBytes) {
        return reject(FullItemPacketStagingError::InvalidPacketLength);
    }
    const auto packetAddress = reinterpret_cast<std::uintptr_t>(bytes);
    if (packetAddress > (std::numeric_limits<std::uintptr_t>::max)()
            - (length - 1U)) {
        return reject(FullItemPacketStagingError::InvalidPacketPointer);
    }
    const auto payloadBytes = length - budget.headerBytes;
    if (payloadBytes == 0 || payloadBytes > budget.payloadCapacityBytes) {
        return reject(FullItemPacketStagingError::PayloadOutsideBudget);
    }
    if (transaction.totalBytes > MaximumStagedItemTransactionBytes
            || length > MaximumStagedItemTransactionBytes
                - transaction.totalBytes) {
        return reject(FullItemPacketStagingError::ByteLimitExceeded);
    }

    const auto offset = transaction.totalBytes;
    std::memcpy(transaction.bytes.data() + offset, bytes, length);
    const auto packet = std::span<const std::uint8_t>{
        transaction.bytes.data() + offset,
        length,
    };
    const auto validation =
        ValidateCopiedPacket(relayKind, node.action, packet);
    if (validation != FullItemPacketStagingError::None) {
        return reject(validation);
    }

    transaction.packets[nodeIndex] = {
        .kind = relayKind,
        .nodeIndex = nodeIndex,
        .byteOffset = offset,
        .byteLength = length,
    };
    transaction.totalBytes += length;
    ++transaction.packetCount;
    node.packetCaptured = true;
    return FullItemPacketStagingError::None;
}

auto EndFullItemPacketProducer(
        FullItemPacketStagingContext& transaction,
        const FullItemProducerToken& token) noexcept
        -> FullItemProducerCompletion {
    if (transaction.state == FullItemPacketStagingState::Fatal) {
        return FullItemProducerCompletion::Fatal;
    }
    if (token.generation != transaction.generation) {
        if (transaction.state == FullItemPacketStagingState::Collecting
                || transaction.state == FullItemPacketStagingState::Rejected) {
            RejectTransaction(
                transaction,
                FullItemPacketStagingError::StaleToken);
        } else {
            FailTransactionFatally(
                transaction,
                FullItemPacketStagingError::StaleToken);
            return FullItemProducerCompletion::Fatal;
        }
        return token.ownsRoot
            ? FullItemProducerCompletion::RootRejected
            : FullItemProducerCompletion::Skipped;
    }
    if (!token.admitted) {
        return token.ownsRoot
            ? FullItemProducerCompletion::RootRejected
            : FullItemProducerCompletion::Skipped;
    }
    if (transaction.state != FullItemPacketStagingState::Collecting
            && transaction.state != FullItemPacketStagingState::Rejected) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::InvalidState);
        return FullItemProducerCompletion::Fatal;
    }

    if (transaction.activeDepth == 0
            || transaction.activeDepth > transaction.activeNodeIndices.size()
            || transaction.activeNodeIndices[transaction.activeDepth - 1U]
                != token.nodeIndex
            || token.nodeIndex >= transaction.nodeCount
            || token.depth != transaction.activeDepth
            || transaction.nodes[token.nodeIndex].depth != token.depth) {
        RejectTransaction(
            transaction,
            FullItemPacketStagingError::UnbalancedProducerExit);
    } else {
        if (!transaction.nodes[token.nodeIndex].packetCaptured) {
            RejectTransaction(
                transaction,
                FullItemPacketStagingError::MissingPacket);
        }
        --transaction.activeDepth;
    }

    if (!token.ownsRoot) {
        return transaction.state == FullItemPacketStagingState::Fatal
            ? FullItemProducerCompletion::Fatal
            : FullItemProducerCompletion::NestedComplete;
    }
    if (transaction.activeDepth != 0) {
        RejectTransaction(
            transaction,
            FullItemPacketStagingError::UnbalancedProducerExit);
    }
    if (transaction.state == FullItemPacketStagingState::Collecting) {
        transaction.state = FullItemPacketStagingState::Ready;
        return FullItemProducerCompletion::RootReady;
    }
    return FullItemProducerCompletion::RootRejected;
}

auto AbortFullItemPacketProducer(
        FullItemPacketStagingContext& transaction,
        const FullItemProducerToken& token) noexcept
        -> FullItemProducerCompletion {
    if (transaction.state == FullItemPacketStagingState::Fatal) {
        return FullItemProducerCompletion::Fatal;
    }
    if (transaction.state != FullItemPacketStagingState::Collecting
            && transaction.state != FullItemPacketStagingState::Rejected) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::InvalidState);
        return FullItemProducerCompletion::Fatal;
    }
    RejectTransaction(
        transaction,
        FullItemPacketStagingError::NativeProducerException);
    return EndFullItemPacketProducer(transaction, token);
}

auto ValidateCapturedFullItemPacketTransaction(
        const FullItemPacketStagingContext& transaction) noexcept
        -> FullItemPacketStagingError {
    if (transaction.state != FullItemPacketStagingState::Ready
            || transaction.error != FullItemPacketStagingError::None
            || !IsKnownPacketKind(transaction.rootKind)
            || transaction.rootClient == nullptr
            || transaction.nodeCount == 0
            || transaction.nodeCount > MaximumStagedItemPacketCount
            || transaction.packetCount != transaction.nodeCount
            || transaction.activeDepth != 0
            || transaction.totalBytes == 0
            || transaction.totalBytes > MaximumStagedItemTransactionBytes) {
        return FullItemPacketStagingError::FinalBatchInvalid;
    }

    std::array<std::size_t, MaximumStagedItemTreeDepth> path{};
    std::array<std::uint8_t, MaximumStagedItemPacketCount>
        observedChildCounts{};
    std::size_t expectedOffset{};
    std::size_t previousDepth{};
    for (std::size_t index{}; index < transaction.nodeCount; ++index) {
        const auto& node = transaction.nodes[index];
        const auto& packet = transaction.packets[index];
        if (!node.packetCaptured
                || node.item == nullptr
                || node.depth == 0
                || node.depth > MaximumStagedItemTreeDepth
                || packet.nodeIndex != index
                || packet.kind != node.kind
                || packet.byteOffset != expectedOffset
                || packet.byteLength == 0
                || packet.byteOffset > transaction.totalBytes
                || packet.byteLength
                    > transaction.totalBytes - packet.byteOffset) {
            return FullItemPacketStagingError::FinalBatchInvalid;
        }
        if (index == 0) {
            if (node.depth != 1
                    || node.kind != transaction.rootKind) {
                return FullItemPacketStagingError::FinalBatchInvalid;
            }
        } else {
            if (node.kind != FullItemPacketKind::ItemAction9D
                    || node.action != 0x12
                    || node.gamble != 0
                    || node.depth < 2
                    || node.depth > previousDepth + 1U) {
                return FullItemPacketStagingError::FinalBatchInvalid;
            }
            const auto parentIndex = path[node.depth - 2U];
            if (parentIndex >= index
                    || node.parentItem != transaction.nodes[parentIndex].item
                    || node.temporaryFlags
                        != (transaction.nodes[parentIndex].temporaryFlags
                            | NestedFullItemTemporaryFlagsMask)
                    || observedChildCounts[parentIndex]
                        >= MaximumStagedItemImmediateChildren) {
                return FullItemPacketStagingError::FinalBatchInvalid;
            }
            ++observedChildCounts[parentIndex];
        }
        for (std::size_t prior{}; prior < index; ++prior) {
            if (transaction.nodes[prior].item == node.item) {
                return FullItemPacketStagingError::DuplicateOrCycle;
            }
        }
        path[node.depth - 1U] = index;
        previousDepth = node.depth;

        const auto bytes = std::span<const std::uint8_t>{
            transaction.bytes.data() + packet.byteOffset,
            packet.byteLength,
        };
        const auto packetValidation =
            ValidateCopiedPacket(node.kind, node.action, bytes);
        if (packetValidation != FullItemPacketStagingError::None) {
            return packetValidation;
        }
        if (!CheckedAdd(
                expectedOffset,
                packet.byteLength,
                expectedOffset)) {
            return FullItemPacketStagingError::FinalBatchInvalid;
        }
    }
    if (expectedOffset != transaction.totalBytes) {
        return FullItemPacketStagingError::FinalBatchInvalid;
    }
    for (std::size_t index{}; index < transaction.nodeCount; ++index) {
        if (transaction.nodes[index].directChildCount
                != observedChildCounts[index]) {
            return FullItemPacketStagingError::FinalBatchInvalid;
        }
    }
    return FullItemPacketStagingError::None;
}

auto FlushOrDiscardFullItemPacketTransaction(
        FullItemPacketStagingContext& transaction,
        FullItemPacketQueueCallback callback,
        void* callbackContext) noexcept -> FullItemPacketFlushResult {
    if (transaction.state == FullItemPacketStagingState::Fatal) {
        return {.error = transaction.error};
    }
    if (transaction.state == FullItemPacketStagingState::Rejected) {
        const auto error = transaction.error;
        ResetTransactionToIdle(transaction);
        return {.error = error};
    }
    if (transaction.state != FullItemPacketStagingState::Ready) {
        FailTransactionFatally(
            transaction,
            FullItemPacketStagingError::InvalidState);
        return {.error = transaction.error};
    }
    if (callback == nullptr) {
        RejectTransaction(
            transaction,
            FullItemPacketStagingError::InvalidArgument);
        const auto error = transaction.error;
        ResetTransactionToIdle(transaction);
        return {.error = error};
    }

    const auto validation =
        ValidateCapturedFullItemPacketTransaction(transaction);
    if (validation != FullItemPacketStagingError::None) {
        RejectTransaction(transaction, validation);
        const auto error = transaction.error;
        ResetTransactionToIdle(transaction);
        return {.error = error};
    }

    transaction.state = FullItemPacketStagingState::Flushing;
    std::size_t queuedPacketCount{};
    for (std::size_t index{}; index < transaction.packetCount; ++index) {
        const auto& packet = transaction.packets[index];
        callback(
            callbackContext,
            transaction.rootClient,
            transaction.bytes.data() + packet.byteOffset,
            packet.byteLength);
        ++queuedPacketCount;
        if (transaction.state == FullItemPacketStagingState::Fatal) {
            return {
                .error = transaction.error,
                .queuedPacketCount = queuedPacketCount,
            };
        }
        if (transaction.state != FullItemPacketStagingState::Flushing) {
            FailTransactionFatally(
                transaction,
                FullItemPacketStagingError::InvalidState);
            return {
                .error = transaction.error,
                .queuedPacketCount = queuedPacketCount,
            };
        }
    }

    ResetTransactionToIdle(transaction);
    return {
        .queuedPacketCount = queuedPacketCount,
        .completed = true,
    };
}

} // namespace ruffneckk::isc12
