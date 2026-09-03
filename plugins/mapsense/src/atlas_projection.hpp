#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace RuffnecKk::MapSense {

struct AtlasProjectionPoint final {
    std::int32_t x{};
    std::int32_t y{};
};

// Four samples from D2R's already-governed client-to-automap function. The
// diagonal witness proves that the current pan/zoom transform is affine before
// MapSense reuses it for seed-generated atlas geometry.
struct AtlasProjectionWitness final {
    std::int32_t anchorClientX{};
    std::int32_t anchorClientY{};
    AtlasProjectionPoint anchorNative{};
    AtlasProjectionPoint clientXBasisNative{};
    AtlasProjectionPoint clientYBasisNative{};
    std::int32_t basisClientUnits{};
    bool valid{};
};

struct AtlasProjectedPoint final {
    double x{};
    double y{};
};

[[nodiscard]] inline auto BuildAtlasProjectionWitness(
        std::int32_t anchorClientX,
        std::int32_t anchorClientY,
        AtlasProjectionPoint anchorNative,
        AtlasProjectionPoint clientXBasisNative,
        AtlasProjectionPoint clientYBasisNative,
        AtlasProjectionPoint diagonalNative,
        std::int32_t basisClientUnits,
        AtlasProjectionWitness& output) noexcept -> bool {
    output = {};
    if (basisClientUnits < 32 || basisClientUnits > 4'096) return false;
    const auto predictedDiagonalX = static_cast<std::int64_t>(
        clientXBasisNative.x) + clientYBasisNative.x - anchorNative.x;
    const auto predictedDiagonalY = static_cast<std::int64_t>(
        clientXBasisNative.y) + clientYBasisNative.y - anchorNative.y;
    const auto absolute = [](std::int64_t value) noexcept {
        return value < 0 ? static_cast<std::uint64_t>(-value)
            : static_cast<std::uint64_t>(value);
    };
    constexpr std::uint64_t MaximumAffineRoundingError = 2U;
    if (absolute(predictedDiagonalX - diagonalNative.x)
            > MaximumAffineRoundingError
        || absolute(predictedDiagonalY - diagonalNative.y)
            > MaximumAffineRoundingError) {
        return false;
    }
    const auto basisXX = static_cast<std::int64_t>(clientXBasisNative.x)
        - anchorNative.x;
    const auto basisXY = static_cast<std::int64_t>(clientXBasisNative.y)
        - anchorNative.y;
    const auto basisYX = static_cast<std::int64_t>(clientYBasisNative.x)
        - anchorNative.x;
    const auto basisYY = static_cast<std::int64_t>(clientYBasisNative.y)
        - anchorNative.y;
    const auto determinant = basisXX * basisYY - basisYX * basisXY;
    if (determinant == 0) return false;
    output = {
        .anchorClientX = anchorClientX,
        .anchorClientY = anchorClientY,
        .anchorNative = anchorNative,
        .clientXBasisNative = clientXBasisNative,
        .clientYBasisNative = clientYBasisNative,
        .basisClientUnits = basisClientUnits,
        .valid = true,
    };
    return true;
}

[[nodiscard]] inline auto ProjectAtlasClientPoint(
        const AtlasProjectionWitness& witness,
        std::int32_t clientX,
        std::int32_t clientY,
        AtlasProjectedPoint& output) noexcept -> bool {
    output = {};
    if (!witness.valid || witness.basisClientUnits <= 0) return false;
    const auto deltaX = static_cast<std::int64_t>(clientX)
        - witness.anchorClientX;
    const auto deltaY = static_cast<std::int64_t>(clientY)
        - witness.anchorClientY;
    const auto basisXX = static_cast<std::int64_t>(
        witness.clientXBasisNative.x) - witness.anchorNative.x;
    const auto basisXY = static_cast<std::int64_t>(
        witness.clientXBasisNative.y) - witness.anchorNative.y;
    const auto basisYX = static_cast<std::int64_t>(
        witness.clientYBasisNative.x) - witness.anchorNative.x;
    const auto basisYY = static_cast<std::int64_t>(
        witness.clientYBasisNative.y) - witness.anchorNative.y;
    const auto divisor = static_cast<double>(witness.basisClientUnits);
    const auto projectedX = static_cast<double>(witness.anchorNative.x)
        + (static_cast<double>(deltaX) * static_cast<double>(basisXX)
            + static_cast<double>(deltaY) * static_cast<double>(basisYX))
            / divisor;
    const auto projectedY = static_cast<double>(witness.anchorNative.y)
        + (static_cast<double>(deltaX) * static_cast<double>(basisXY)
            + static_cast<double>(deltaY) * static_cast<double>(basisYY))
            / divisor;
    constexpr auto MaximumProjectedMagnitude = 10'000'000.0;
    if (!std::isfinite(projectedX) || !std::isfinite(projectedY)
        || std::abs(projectedX) > MaximumProjectedMagnitude
        || std::abs(projectedY) > MaximumProjectedMagnitude) {
        return false;
    }
    output = {.x = projectedX, .y = projectedY};
    return true;
}

} // namespace RuffnecKk::MapSense
