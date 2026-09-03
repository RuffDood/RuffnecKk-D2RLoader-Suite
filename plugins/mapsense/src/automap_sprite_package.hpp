#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::uint16_t AutomapSpritePackageVersion = 1U;
inline constexpr std::uint16_t AutomapSpritePaletteIndexFlag = 1U;
inline constexpr std::uint32_t AutomapSpriteFrameCount = 1'499U;
inline constexpr std::uint16_t AutomapSpriteFrameWidth = 16U;
inline constexpr std::uint16_t AutomapSpriteFrameHeight = 32U;
inline constexpr std::uint16_t AutomapSpriteAtlasColumns = 64U;
inline constexpr std::uint16_t AutomapSpriteAtlasRows = 24U;
inline constexpr std::uint8_t AutomapSpritePaletteCount = 5U;
inline constexpr std::uint32_t AutomapSpriteIndexAtlasWidth = 1'024U;
inline constexpr std::uint32_t AutomapSpriteIndexAtlasHeight = 768U;
inline constexpr std::uint32_t AutomapSpriteRgbaAtlasHeight =
    AutomapSpriteIndexAtlasHeight * AutomapSpritePaletteCount;
inline constexpr std::size_t AutomapSpritePackageHeaderBytes = 48U;
inline constexpr std::size_t AutomapSpritePaletteBytes =
    static_cast<std::size_t>(AutomapSpritePaletteCount) * 768U;
inline constexpr std::size_t AutomapSpriteIndexBytes =
    static_cast<std::size_t>(AutomapSpriteIndexAtlasWidth)
    * AutomapSpriteIndexAtlasHeight;
inline constexpr std::size_t AutomapSpritePackageBytes =
    AutomapSpritePackageHeaderBytes + AutomapSpritePaletteBytes
    + AutomapSpriteIndexBytes;

struct AutomapSpriteRgbaAtlas final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t digest{};
    std::vector<std::uint8_t> pixels;
};

enum class AutomapSpritePackageParseError : std::uint8_t {
    None,
    InvalidLength,
    InvalidMagic,
    UnsupportedVersion,
    UnsupportedFlags,
    InvalidLayout,
    InvalidReservedBytes,
    DigestMismatch,
    AllocationFailure,
};

// Strictly validates one reproducible MSP1 package and expands the exact five
// act palettes into one vertically stacked RGBA texture. Palette index zero
// remains transparent, matching D2's DC6 semantics.
[[nodiscard]] auto ParseAutomapSpritePackage(
    std::span<const std::uint8_t> bytes,
    AutomapSpriteRgbaAtlas& output,
    AutomapSpritePackageParseError* error = nullptr) noexcept -> bool;

} // namespace RuffnecKk::MapSense
