#pragma once

#include "external_atlas_geometry.hpp"

#include <cstdint>
#include <filesystem>
#include <span>

namespace RuffnecKk::MapSense {

inline constexpr std::uint32_t ExternalAtlasCacheRevision = 1U;
// Revision 4 invalidates geometry produced before outdoor rooms honored their
// exact DT1 mask, waypoint/shrine/terrain LvlSub passes, and seam ownership.
// Keep the parent revision stable so seed-scoped Reveal Map intent survives
// while only the obsolete geometry receives a cache miss.
inline constexpr std::uint32_t ExternalAtlasGeometryCacheRevision = 4U;

struct ExternalAtlasCacheKey final {
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::uint8_t act{};
    // Zero is the embedded vanilla source set. Active mod data gets its own
    // namespace so geometry from one table/asset set is never reused by
    // another.
    std::uint64_t dataFingerprint{};
};

enum class ExternalAtlasCacheResult : std::uint8_t {
    Hit,
    Miss,
    Invalid,
    IoFailure,
};

[[nodiscard]] auto ResolveExternalAtlasCacheRoot(
    std::filesystem::path& output) noexcept -> bool;

[[nodiscard]] auto BuildExternalAtlasCachePath(
    const std::filesystem::path& root,
    ExternalAtlasCacheKey key,
    std::filesystem::path& output) noexcept -> bool;

[[nodiscard]] auto LoadExternalAtlasGeometryCache(
    const std::filesystem::path& root,
    ExternalAtlasCacheKey key,
    ExternalAtlasGeometry& output,
    ExternalAtlasGeometryParseError* parseError = nullptr) noexcept
    -> ExternalAtlasCacheResult;

// Validates the complete MSA1 artifact before atomically replacing its cache
// entry. A failed validation or write leaves any prior valid entry untouched.
[[nodiscard]] auto StoreExternalAtlasGeometryCache(
    const std::filesystem::path& root,
    ExternalAtlasCacheKey key,
    std::span<const std::uint8_t> bytes) noexcept -> bool;

// Reveal Map intent is seed- and difficulty-scoped. It survives a cold start
// for the same offline map seed, but never carries itself onto a rerolled map.
[[nodiscard]] auto HasExternalAtlasRevealMapIntent(
    const std::filesystem::path& root,
    std::uint32_t seed,
    std::uint8_t difficulty) noexcept -> bool;
[[nodiscard]] auto StoreExternalAtlasRevealMapIntent(
    const std::filesystem::path& root,
    std::uint32_t seed,
    std::uint8_t difficulty,
    bool enabled) noexcept -> bool;

} // namespace RuffnecKk::MapSense
