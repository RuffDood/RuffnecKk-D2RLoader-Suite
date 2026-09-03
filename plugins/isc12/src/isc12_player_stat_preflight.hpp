#pragma once

#include "isc12_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ruffneckk::isc12 {

inline constexpr std::uint16_t PlayerStatSectionMarker = 0x6667;
inline constexpr std::size_t MaximumPlayerStatEntries = 512;
inline constexpr std::size_t MaximumPlayerStatSectionBytes =
    sizeof(PlayerStatSectionMarker)
    + ((MaximumPlayerStatEntries * (SerializedBitWidth
        + MaximumSerializedCsvParamBits + MaximumSerializedCsvBits)
        + SerializedBitWidth + 7U) / 8U);
inline constexpr std::size_t PlayerPreviewDataContextOffset = 0xF8;
// D2R 3.3 creates a valid header-only D2S before the character has entered a
// game for the first time. The regular player-stat section is appended only
// after that first game save.
inline constexpr std::size_t PlayerPreviewHeaderOnlyLength = 0x193;
inline constexpr std::size_t PlayerPreviewRegularStatOffset = 0x341;
inline constexpr std::uint8_t PlayerPreviewDataContextCount = 4;
inline constexpr std::size_t PlayerPreviewBufferCapacity = 0x4000;

enum class PlayerStatStreamKind : std::uint8_t {
    Auxiliary,
    Regular,
};

enum class PlayerStatPreflightError : std::uint8_t {
    None,
    InvalidArgument,
    InvalidMarker,
    UnsafeSchema,
    InvalidStatId,
    UnserializedStat,
    TooManyEntries,
    Truncated,
    MissingSentinel,
};

struct PlayerStatPreflightResult {
    std::size_t consumedBits{};
    std::size_t entryCount{};
};

enum class PlayerPreviewPreflightError : std::uint8_t {
    None,
    InvalidArgument,
    InvalidContainer,
    InvalidDataContext,
    InvalidPlayerStatStream,
};

struct PlayerPreviewPreflightResult {
    PlayerStatPreflightResult playerStats{};
    std::uint8_t dataContext{};
};

// Validates the schema-independent portion of a frontend D2S preview: fixed
// buffer bounds, the standard v105 container and the data-context domain. This
// is sufficient during initial character enumeration, which occurs before
// DataTablesLoaded publishes the authoritative ItemStatCost schema.
auto PreflightPlayerPreviewContainer(
    std::span<const std::uint8_t> d2s,
    std::uint8_t& dataContext) noexcept -> PlayerPreviewPreflightError;

// Validates one complete 0x6667 player-stat section without allocating or
// publishing decoded state. Bits use the native least-significant-bit-first
// order. Trailing bytes are allowed because the section is embedded in larger
// D2S/D2I payloads. On failure, output remains unchanged.
auto PreflightPlayerStatStream(
    std::span<const std::uint8_t> bytes,
    std::span<const ItemStatSemanticRow> schema,
    PlayerStatStreamKind kind,
    PlayerStatPreflightResult& output) noexcept -> PlayerStatPreflightError;

// Validates the complete current-format D2S copied into the fixed frontend
// preview buffer before its branch-B stat decoder can publish any projection.
// The exact v105 container, declared size, checksum and data-context domain are
// prerequisites for either the native 0x193-byte header-only new-character
// form or the regular 12-bit stat stream at offset 0x341. On failure, output
// remains unchanged.
auto PreflightPlayerPreviewD2S(
    std::span<const std::uint8_t> d2s,
    std::span<const ItemStatSemanticRow> schema,
    PlayerPreviewPreflightResult& output) noexcept
    -> PlayerPreviewPreflightError;

} // namespace ruffneckk::isc12
