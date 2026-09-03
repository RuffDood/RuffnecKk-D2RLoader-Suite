#include "automap_sprite_package.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace RuffnecKk::MapSense {
namespace {

template <typename T>
[[nodiscard]] auto ReadUnsigned(
        std::span<const std::uint8_t> bytes,
        std::size_t offset) noexcept -> T {
    T value{};
    for (std::size_t index = 0U; index < sizeof(T); ++index) {
        value = static_cast<T>(
            value | (static_cast<T>(bytes[offset + index])
                << (index * 8U)));
    }
    return value;
}

void MixByte(std::uint64_t& digest, std::uint8_t value) noexcept {
    digest ^= value;
    digest *= UINT64_C(1099511628211);
}

template <typename T>
void MixUnsigned(std::uint64_t& digest, T value) noexcept {
    for (std::size_t index = 0U; index < sizeof(T); ++index) {
        MixByte(
            digest,
            static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

[[nodiscard]] auto Reject(
        AutomapSpritePackageParseError reason,
        AutomapSpritePackageParseError* error) noexcept -> bool {
    if (error != nullptr) *error = reason;
    return false;
}

} // namespace

auto ParseAutomapSpritePackage(
        std::span<const std::uint8_t> bytes,
        AutomapSpriteRgbaAtlas& output,
        AutomapSpritePackageParseError* error) noexcept -> bool {
    output = {};
    if (error != nullptr) *error = AutomapSpritePackageParseError::None;
    if (bytes.size() != AutomapSpritePackageBytes) {
        return Reject(
            AutomapSpritePackageParseError::InvalidLength, error);
    }
    constexpr std::array<std::uint8_t, 4U> Magic{'M', 'S', 'P', '1'};
    if (!std::equal(Magic.begin(), Magic.end(), bytes.begin())) {
        return Reject(
            AutomapSpritePackageParseError::InvalidMagic, error);
    }
    const auto version = ReadUnsigned<std::uint16_t>(bytes, 4U);
    const auto flags = ReadUnsigned<std::uint16_t>(bytes, 6U);
    if (version != AutomapSpritePackageVersion) {
        return Reject(
            AutomapSpritePackageParseError::UnsupportedVersion, error);
    }
    if (flags != AutomapSpritePaletteIndexFlag) {
        return Reject(
            AutomapSpritePackageParseError::UnsupportedFlags, error);
    }
    if (ReadUnsigned<std::uint32_t>(bytes, 8U)
            != AutomapSpriteFrameCount
        || ReadUnsigned<std::uint16_t>(bytes, 12U)
            != AutomapSpriteFrameWidth
        || ReadUnsigned<std::uint16_t>(bytes, 14U)
            != AutomapSpriteFrameHeight
        || ReadUnsigned<std::uint16_t>(bytes, 16U)
            != AutomapSpriteAtlasColumns
        || ReadUnsigned<std::uint16_t>(bytes, 18U)
            != AutomapSpriteAtlasRows
        || bytes[20U] != AutomapSpritePaletteCount
        || ReadUnsigned<std::uint32_t>(bytes, 24U)
            != AutomapSpriteIndexAtlasWidth
        || ReadUnsigned<std::uint32_t>(bytes, 28U)
            != AutomapSpriteIndexAtlasHeight
        || ReadUnsigned<std::uint32_t>(bytes, 32U)
            != AutomapSpriteIndexBytes
        || ReadUnsigned<std::uint32_t>(bytes, 36U)
            != AutomapSpritePaletteBytes) {
        return Reject(
            AutomapSpritePackageParseError::InvalidLayout, error);
    }
    if (bytes[21U] != 0U || bytes[22U] != 0U || bytes[23U] != 0U) {
        return Reject(
            AutomapSpritePackageParseError::InvalidReservedBytes, error);
    }

    const auto declaredDigest = ReadUnsigned<std::uint64_t>(bytes, 40U);
    auto digest = UINT64_C(14695981039346656037);
    MixUnsigned(digest, AutomapSpriteFrameCount);
    MixUnsigned(digest, AutomapSpriteFrameWidth);
    MixUnsigned(digest, AutomapSpriteFrameHeight);
    MixUnsigned(digest, AutomapSpriteAtlasColumns);
    MixUnsigned(digest, AutomapSpriteAtlasRows);
    MixUnsigned(digest, AutomapSpriteIndexAtlasWidth);
    MixUnsigned(digest, AutomapSpriteIndexAtlasHeight);
    for (const auto byte : bytes.subspan(
            AutomapSpritePackageHeaderBytes,
            AutomapSpritePaletteBytes + AutomapSpriteIndexBytes)) {
        MixByte(digest, byte);
    }
    if (digest != declaredDigest) {
        return Reject(
            AutomapSpritePackageParseError::DigestMismatch, error);
    }

    constexpr auto RgbaByteCount =
        static_cast<std::size_t>(AutomapSpriteIndexAtlasWidth)
        * AutomapSpriteRgbaAtlasHeight * 4U;
    try {
        output.pixels.resize(RgbaByteCount);
    } catch (...) {
        output = {};
        return Reject(
            AutomapSpritePackageParseError::AllocationFailure, error);
    }
    const auto palettes = bytes.subspan(
        AutomapSpritePackageHeaderBytes,
        AutomapSpritePaletteBytes);
    const auto indices = bytes.subspan(
        AutomapSpritePackageHeaderBytes + AutomapSpritePaletteBytes,
        AutomapSpriteIndexBytes);
    for (std::size_t act = 0U; act < AutomapSpritePaletteCount; ++act) {
        const auto palette = palettes.subspan(act * 768U, 768U);
        const auto actPixelOffset = act * AutomapSpriteIndexBytes;
        for (std::size_t index = 0U; index < indices.size(); ++index) {
            const auto paletteIndex = indices[index];
            const auto destination = (actPixelOffset + index) * 4U;
            if (paletteIndex == 0U) {
                output.pixels[destination] = 0U;
                output.pixels[destination + 1U] = 0U;
                output.pixels[destination + 2U] = 0U;
                output.pixels[destination + 3U] = 0U;
                continue;
            }
            const auto paletteOffset =
                static_cast<std::size_t>(paletteIndex) * 3U;
            output.pixels[destination] = palette[paletteOffset + 2U];
            output.pixels[destination + 1U] = palette[paletteOffset + 1U];
            output.pixels[destination + 2U] = palette[paletteOffset];
            output.pixels[destination + 3U] = 255U;
        }
    }
    output.width = AutomapSpriteIndexAtlasWidth;
    output.height = AutomapSpriteRgbaAtlasHeight;
    output.digest = digest;
    return true;
}

} // namespace RuffnecKk::MapSense
