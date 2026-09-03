#include "isc12_player_stat_preflight.hpp"

#include "isc12_contract.hpp"
#include "isc12_envelope.hpp"

#include <limits>

namespace ruffneckk::isc12 {
namespace {

auto ReadBits(
        std::span<const std::uint8_t> bytes,
        std::size_t& bitPosition,
        std::size_t width,
        std::uint16_t& output) noexcept -> bool {
    if (bytes.size() > (std::numeric_limits<std::size_t>::max)() / 8U) {
        return false;
    }
    const auto totalBits = bytes.size() * 8U;
    if (bitPosition > totalBits || width > totalBits - bitPosition) {
        return false;
    }
    std::uint16_t value{};
    for (std::size_t bit{}; bit < width; ++bit) {
        const auto sourceBit = bitPosition + bit;
        if ((bytes[sourceBit / 8U]
                & static_cast<std::uint8_t>(
                    1U << (sourceBit % 8U))) != 0) {
            value |= static_cast<std::uint16_t>(1U << bit);
        }
    }
    bitPosition += width;
    output = value;
    return true;
}

auto SkipBits(
        std::span<const std::uint8_t> bytes,
        std::size_t& bitPosition,
        std::size_t width) noexcept -> bool {
    if (bytes.size() > (std::numeric_limits<std::size_t>::max)() / 8U) {
        return false;
    }
    const auto totalBits = bytes.size() * 8U;
    if (bitPosition > totalBits || width > totalBits - bitPosition) {
        return false;
    }
    bitPosition += width;
    return true;
}

} // namespace

auto PreflightPlayerStatStream(
        std::span<const std::uint8_t> bytes,
        std::span<const ItemStatSemanticRow> schema,
        PlayerStatStreamKind kind,
        PlayerStatPreflightResult& output) noexcept
        -> PlayerStatPreflightError {
    if (schema.empty() || schema.size() > MaximumRecordCount
            || bytes.size() < sizeof(PlayerStatSectionMarker)) {
        return PlayerStatPreflightError::InvalidArgument;
    }
    if (bytes[0] != static_cast<std::uint8_t>(PlayerStatSectionMarker)
            || bytes[1] != static_cast<std::uint8_t>(
                PlayerStatSectionMarker >> 8U)) {
        return PlayerStatPreflightError::InvalidMarker;
    }
    for (const auto& row : schema) {
        if (row.csvBits > MaximumSerializedCsvBits
                || row.csvParamBits > MaximumSerializedCsvParamBits) {
            return PlayerStatPreflightError::UnsafeSchema;
        }
    }

    std::size_t bitPosition = sizeof(PlayerStatSectionMarker) * 8U;
    std::size_t entryCount{};
    for (;;) {
        if (bytes.size() > (std::numeric_limits<std::size_t>::max)() / 8U) {
            return PlayerStatPreflightError::InvalidArgument;
        }
        const auto totalBits = bytes.size() * 8U;
        if (bitPosition == totalBits) {
            return PlayerStatPreflightError::MissingSentinel;
        }
        std::uint16_t statId{};
        if (!ReadBits(bytes, bitPosition, SerializedBitWidth, statId)) {
            return PlayerStatPreflightError::Truncated;
        }
        if (statId == SerializedSentinel) {
            output = {
                .consumedBits = bitPosition,
                .entryCount = entryCount,
            };
            return PlayerStatPreflightError::None;
        }
        if (!IsValidStatId(statId) || statId >= schema.size()) {
            return PlayerStatPreflightError::InvalidStatId;
        }
        if (entryCount >= MaximumPlayerStatEntries) {
            return PlayerStatPreflightError::TooManyEntries;
        }
        const auto& row = schema[statId];
        if (row.csvBits == 0) {
            return PlayerStatPreflightError::UnserializedStat;
        }
        const auto valueBits = kind == PlayerStatStreamKind::Auxiliary
            ? static_cast<std::size_t>(32)
            : static_cast<std::size_t>(row.csvBits);
        const auto payloadBits = static_cast<std::size_t>(row.csvParamBits)
            + valueBits;
        if (!SkipBits(bytes, bitPosition, payloadBits)) {
            return PlayerStatPreflightError::Truncated;
        }
        ++entryCount;
    }
}

auto PreflightPlayerPreviewContainer(
        std::span<const std::uint8_t> d2s,
        std::uint8_t& dataContext) noexcept
        -> PlayerPreviewPreflightError {
    if (d2s.size() > PlayerPreviewBufferCapacity
            || d2s.size() <= PlayerPreviewDataContextOffset
            || (d2s.size() != PlayerPreviewHeaderOnlyLength
                && d2s.size() <= PlayerPreviewRegularStatOffset)) {
        return PlayerPreviewPreflightError::InvalidArgument;
    }
    if (!ValidateInnerStore(StoreKind::D2S, d2s)) {
        return PlayerPreviewPreflightError::InvalidContainer;
    }
    const auto observedDataContext = d2s[PlayerPreviewDataContextOffset];
    if (observedDataContext >= PlayerPreviewDataContextCount) {
        return PlayerPreviewPreflightError::InvalidDataContext;
    }
    dataContext = observedDataContext;
    return PlayerPreviewPreflightError::None;
}

auto PreflightPlayerPreviewD2S(
        std::span<const std::uint8_t> d2s,
        std::span<const ItemStatSemanticRow> schema,
        PlayerPreviewPreflightResult& output) noexcept
        -> PlayerPreviewPreflightError {
    std::uint8_t dataContext{};
    const auto containerStatus = PreflightPlayerPreviewContainer(
        d2s, dataContext);
    if (containerStatus != PlayerPreviewPreflightError::None) {
        return containerStatus;
    }

    if (d2s.size() == PlayerPreviewHeaderOnlyLength) {
        output = {
            .playerStats = {},
            .dataContext = dataContext,
        };
        return PlayerPreviewPreflightError::None;
    }

    PlayerStatPreflightResult playerStats;
    if (PreflightPlayerStatStream(
            d2s.subspan(PlayerPreviewRegularStatOffset),
            schema,
            PlayerStatStreamKind::Regular,
            playerStats) != PlayerStatPreflightError::None) {
        return PlayerPreviewPreflightError::InvalidPlayerStatStream;
    }
    output = {
        .playerStats = playerStats,
        .dataContext = dataContext,
    };
    return PlayerPreviewPreflightError::None;
}

} // namespace ruffneckk::isc12
