#include "external_atlas_geometry.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::size_t LevelRecordBytes = 12U;
constexpr std::size_t CellRecordBytes = 16U;
constexpr std::uint64_t FnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t FnvPrime = UINT64_C(1099511628211);

void SetError(
        ExternalAtlasGeometryParseError* destination,
        ExternalAtlasGeometryParseError value) noexcept {
    if (destination != nullptr) *destination = value;
}

[[nodiscard]] auto ReadU16(
        std::span<const std::uint8_t> bytes,
        std::size_t offset) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] auto ReadU32(
        std::span<const std::uint8_t> bytes,
        std::size_t offset) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] auto ReadU64(
        std::span<const std::uint8_t> bytes,
        std::size_t offset) noexcept -> std::uint64_t {
    auto value = std::uint64_t{};
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

[[nodiscard]] auto ReadI32(
        std::span<const std::uint8_t> bytes,
        std::size_t offset) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(ReadU32(bytes, offset));
}

void MixByte(std::uint64_t& digest, std::uint8_t value) noexcept {
    digest ^= value;
    digest *= FnvPrime;
}

void MixU32(std::uint64_t& digest, std::uint32_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        MixByte(digest, static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] auto AllZero(
        std::span<const std::uint8_t> bytes,
        std::size_t offset,
        std::size_t count) noexcept -> bool {
    return std::all_of(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + count),
        [](std::uint8_t value) noexcept { return value == 0U; });
}

} // namespace

auto ParseExternalAtlasGeometry(
        std::span<const std::uint8_t> bytes,
        std::uint32_t expectedSeed,
        std::uint8_t expectedDifficulty,
        std::uint8_t expectedAct,
        ExternalAtlasGeometry& output,
        ExternalAtlasGeometryParseError* error) -> bool {
    output = {};
    SetError(error, ExternalAtlasGeometryParseError::None);
    if (expectedSeed == 0U || expectedDifficulty > 2U || expectedAct >= 5U) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidRequest);
        return false;
    }
    if (bytes.size() < ExternalAtlasGeometryHeaderBytes
        || bytes.size() > ExternalAtlasGeometryMaximumBytes) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidLength);
        return false;
    }
    constexpr std::array<std::uint8_t, 4U> magic{'M', 'S', 'A', '1'};
    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidMagic);
        return false;
    }
    if (ReadU16(bytes, 4U) != ExternalAtlasGeometryProtocolVersion) {
        SetError(error, ExternalAtlasGeometryParseError::UnsupportedVersion);
        return false;
    }
    const auto flags = ReadU16(bytes, 6U);
    if (flags != ExternalAtlasStandardCampaignFlag) {
        SetError(error, ExternalAtlasGeometryParseError::UnsupportedFlags);
        return false;
    }
    const auto seed = ReadU32(bytes, 8U);
    const auto difficulty = bytes[12U];
    const auto act = bytes[13U];
    if (seed != expectedSeed || difficulty != expectedDifficulty
        || act != expectedAct) {
        SetError(error, ExternalAtlasGeometryParseError::IdentityMismatch);
        return false;
    }
    if (!AllZero(bytes, 14U, 2U)) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidReservedBytes);
        return false;
    }
    const auto levelCount = ReadU32(bytes, 16U);
    const auto cellCount = ReadU32(bytes, 20U);
    const auto expectedDigest = ReadU64(bytes, 24U);
    if (levelCount == 0U
        || levelCount > ExternalAtlasGeometryMaximumLevels
        || cellCount == 0U
        || cellCount > ExternalAtlasGeometryMaximumCells) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidCount);
        return false;
    }
    constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (levelCount > (maximum - ExternalAtlasGeometryHeaderBytes)
            / LevelRecordBytes
        || cellCount > (maximum - ExternalAtlasGeometryHeaderBytes
                - static_cast<std::size_t>(levelCount) * LevelRecordBytes)
            / CellRecordBytes) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidLength);
        return false;
    }
    const auto expectedLength = ExternalAtlasGeometryHeaderBytes
        + static_cast<std::size_t>(levelCount) * LevelRecordBytes
        + static_cast<std::size_t>(cellCount) * CellRecordBytes;
    if (bytes.size() != expectedLength) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidLength);
        return false;
    }

    ExternalAtlasGeometry candidate{
        .seed = seed,
        .difficulty = difficulty,
        .act = act,
        .flags = flags,
        .digest = expectedDigest,
    };
    try {
        candidate.levels.reserve(levelCount);
        candidate.cells.reserve(cellCount);
    } catch (...) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidCount);
        return false;
    }

    auto digest = FnvOffset;
    auto offset = ExternalAtlasGeometryHeaderBytes;
    std::uint64_t parsedCellCount{};
    for (std::uint32_t levelIndex = 0U;
            levelIndex < levelCount;
            ++levelIndex) {
        const auto levelId = ReadI32(bytes, offset);
        const auto layer = bytes[offset + 4U];
        const auto cellsInLevel = ReadU32(bytes, offset + 8U);
        if (levelId <= 0 || layer > 3U || cellsInLevel == 0U
            || !AllZero(bytes, offset + 5U, 3U)
            || std::find_if(
                candidate.levels.begin(),
                candidate.levels.end(),
                [levelId](const auto& level) noexcept {
                    return level.levelId == levelId;
                }) != candidate.levels.end()) {
            SetError(error, ExternalAtlasGeometryParseError::InvalidLevel);
            return false;
        }
        parsedCellCount += cellsInLevel;
        if (parsedCellCount > cellCount) {
            SetError(error, ExternalAtlasGeometryParseError::InvalidCount);
            return false;
        }
        candidate.levels.push_back({
            .levelId = levelId,
            .layer = layer,
            .firstCell = static_cast<std::uint32_t>(candidate.cells.size()),
            .cellCount = cellsInLevel,
        });
        MixU32(digest, static_cast<std::uint32_t>(levelId));
        MixByte(digest, layer);
        offset += LevelRecordBytes;
        for (std::uint32_t cellIndex = 0U;
                cellIndex < cellsInLevel;
                ++cellIndex) {
            const auto frame = ReadI32(bytes, offset);
            const auto tileX = ReadI32(bytes, offset + 4U);
            const auto tileY = ReadI32(bytes, offset + 8U);
            const auto wallTree = bytes[offset + 12U];
            const auto raised = bytes[offset + 13U];
            if (frame < 0 || tileX < 0 || tileY < 0
                || wallTree > 1U || raised > 1U
                || !AllZero(bytes, offset + 14U, 2U)) {
                SetError(error, ExternalAtlasGeometryParseError::InvalidCell);
                return false;
            }
            candidate.cells.push_back({
                .frame = frame,
                .tileX = tileX,
                .tileY = tileY,
                .wallTree = wallTree != 0U,
                .raised = raised != 0U,
            });
            MixU32(digest, static_cast<std::uint32_t>(frame));
            MixU32(digest, static_cast<std::uint32_t>(tileX));
            MixU32(digest, static_cast<std::uint32_t>(tileY));
            MixByte(digest, wallTree);
            MixByte(digest, raised);
            offset += CellRecordBytes;
        }
    }
    if (parsedCellCount != cellCount || offset != bytes.size()) {
        SetError(error, ExternalAtlasGeometryParseError::InvalidCount);
        return false;
    }
    if (digest != expectedDigest) {
        SetError(error, ExternalAtlasGeometryParseError::DigestMismatch);
        return false;
    }
    output = std::move(candidate);
    return true;
}

} // namespace RuffnecKk::MapSense
