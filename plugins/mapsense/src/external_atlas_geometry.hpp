#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::uint16_t ExternalAtlasGeometryProtocolVersion = 2U;
inline constexpr std::uint16_t ExternalAtlasStandardCampaignFlag = 1U;
inline constexpr std::size_t ExternalAtlasGeometryHeaderBytes = 32U;
inline constexpr std::size_t ExternalAtlasGeometryMaximumBytes =
    16U * 1'024U * 1'024U;
inline constexpr std::size_t ExternalAtlasGeometryMaximumLevels = 512U;
inline constexpr std::size_t ExternalAtlasGeometryMaximumCells = 1'000'000U;

// MSA1's standard-campaign flag deliberately excludes the four portal-gated
// Pandemonium levels (133-136). The label protocol may still describe those
// disconnected destinations, so coverage validation must require only the
// same campaign ranges that the geometry producer emits.
[[nodiscard]] constexpr auto IsExternalAtlasStandardCampaignLevel(
        std::uint8_t act,
        std::int32_t levelId) noexcept -> bool {
    switch (act) {
    case 0U: return levelId >= 1 && levelId <= 39;
    case 1U: return levelId >= 40 && levelId <= 74;
    case 2U: return levelId >= 75 && levelId <= 102;
    case 3U: return levelId >= 103 && levelId <= 108;
    case 4U: return levelId >= 109 && levelId <= 132;
    default: return false;
    }
}

struct ExternalAtlasGeometryCell final {
    std::int32_t frame{};
    std::int32_t tileX{};
    std::int32_t tileY{};
    bool wallTree{};
    bool raised{};
};

struct ExternalAtlasGeometryLevel final {
    std::int32_t levelId{};
    std::uint8_t layer{};
    std::uint32_t firstCell{};
    std::uint32_t cellCount{};
};

struct ExternalAtlasGeometry final {
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::uint8_t act{};
    std::uint16_t flags{};
    std::uint64_t digest{};
    std::vector<ExternalAtlasGeometryLevel> levels;
    std::vector<ExternalAtlasGeometryCell> cells;
};

enum class ExternalAtlasGeometryParseError : std::uint8_t {
    None,
    InvalidRequest,
    InvalidLength,
    InvalidMagic,
    UnsupportedVersion,
    UnsupportedFlags,
    IdentityMismatch,
    InvalidReservedBytes,
    InvalidCount,
    InvalidLevel,
    InvalidCell,
    DigestMismatch,
};

// Parses one bounded MSA1 artifact and validates its complete identity and
// semantic digest before publishing any geometry. Output remains empty on
// failure, so a corrupt or stale cache entry can never partially reach the
// renderer.
[[nodiscard]] auto ParseExternalAtlasGeometry(
    std::span<const std::uint8_t> bytes,
    std::uint32_t expectedSeed,
    std::uint8_t expectedDifficulty,
    std::uint8_t expectedAct,
    ExternalAtlasGeometry& output,
    ExternalAtlasGeometryParseError* error = nullptr) -> bool;

} // namespace RuffnecKk::MapSense
