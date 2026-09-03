#pragma once

#include "atlas_projection.hpp"

#include <cstdint>
#include <limits>

namespace RuffnecKk::MapSense {

struct AutomapSpriteProjectedQuad final {
    AtlasProjectedPoint topLeft{};
    AtlasProjectedPoint topRight{};
    AtlasProjectedPoint bottomRight{};
    AtlasProjectedPoint bottomLeft{};
};

struct AutomapSpriteProjectionTransform final {
    double originX{};
    double originY{};
    double clientXToScreenX{};
    double clientXToScreenY{};
    double clientYToScreenX{};
    double clientYToScreenY{};
    double spriteRightX{};
    double spriteRightY{};
    double spriteDownX{};
    double spriteDownY{};
    bool valid{};
};

[[nodiscard]] inline auto BuildAutomapSpriteProjectionTransform(
        const AtlasProjectionWitness& witness,
        AutomapSpriteProjectionTransform& output) noexcept -> bool {
    output = {};
    if (!witness.valid || witness.basisClientUnits <= 0) return false;
    const auto divisor = static_cast<double>(witness.basisClientUnits);
    const auto clientXToScreenX =
        (static_cast<double>(witness.clientXBasisNative.x)
            - witness.anchorNative.x) / divisor;
    const auto clientXToScreenY =
        (static_cast<double>(witness.clientXBasisNative.y)
            - witness.anchorNative.y) / divisor;
    const auto clientYToScreenX =
        (static_cast<double>(witness.clientYBasisNative.x)
            - witness.anchorNative.x) / divisor;
    const auto clientYToScreenY =
        (static_cast<double>(witness.clientYBasisNative.y)
            - witness.anchorNative.y) / divisor;
    output = {
        .originX = static_cast<double>(witness.anchorNative.x)
            - static_cast<double>(witness.anchorClientX)
                * clientXToScreenX
            - static_cast<double>(witness.anchorClientY)
                * clientYToScreenX,
        .originY = static_cast<double>(witness.anchorNative.y)
            - static_cast<double>(witness.anchorClientX)
                * clientXToScreenY
            - static_cast<double>(witness.anchorClientY)
                * clientYToScreenY,
        .clientXToScreenX = clientXToScreenX,
        .clientXToScreenY = clientXToScreenY,
        .clientYToScreenX = clientYToScreenX,
        .clientYToScreenY = clientYToScreenY,
        .spriteRightX = clientXToScreenX * 160.0,
        .spriteRightY = clientXToScreenY * 160.0,
        .spriteDownX = clientYToScreenX * 320.0,
        .spriteDownY = clientYToScreenY * 320.0,
        .valid = true,
    };
    return true;
}

// MaxiMap's 16x32 frames are authored in the classic automap pixel space:
// pixelX=8*(tileX-tileY), pixelY=4*(tileX+tileY). D2 client coordinates are
// exactly ten times that space. Projecting all four corners through the native
// affine witness preserves native pan and zoom without reading map internals.
[[nodiscard]] inline auto ProjectAutomapSpriteQuad(
        const AutomapSpriteProjectionTransform& transform,
        std::int32_t tileX,
        std::int32_t tileY,
        AutomapSpriteProjectedQuad& output) noexcept -> bool {
    output = {};
    if (!transform.valid) return false;
    const auto clientX = std::int64_t{80}
        * (static_cast<std::int64_t>(tileX) - tileY);
    const auto clientY = std::int64_t{40}
        * (static_cast<std::int64_t>(tileX) + tileY);
    constexpr auto minimum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::min)());
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (clientX < minimum || clientY < minimum
        || clientX > maximum || clientY > maximum) {
        return false;
    }
    output.topLeft = {
        .x = transform.originX
            + static_cast<double>(clientX) * transform.clientXToScreenX
            + static_cast<double>(clientY) * transform.clientYToScreenX,
        .y = transform.originY
            + static_cast<double>(clientX) * transform.clientXToScreenY
            + static_cast<double>(clientY) * transform.clientYToScreenY,
    };
    output.topRight = {
        .x = output.topLeft.x + transform.spriteRightX,
        .y = output.topLeft.y + transform.spriteRightY,
    };
    output.bottomLeft = {
        .x = output.topLeft.x + transform.spriteDownX,
        .y = output.topLeft.y + transform.spriteDownY,
    };
    output.bottomRight = {
        .x = output.topRight.x + transform.spriteDownX,
        .y = output.topRight.y + transform.spriteDownY,
    };
    constexpr auto MaximumProjectedMagnitude = 10'000'000.0;
    return output.topLeft.x >= -MaximumProjectedMagnitude
        && output.topLeft.x <= MaximumProjectedMagnitude
        && output.topLeft.y >= -MaximumProjectedMagnitude
        && output.topLeft.y <= MaximumProjectedMagnitude;
}

[[nodiscard]] inline auto ProjectAutomapSpriteQuad(
        const AtlasProjectionWitness& witness,
        std::int32_t tileX,
        std::int32_t tileY,
        AutomapSpriteProjectedQuad& output) noexcept -> bool {
    AutomapSpriteProjectionTransform transform{};
    return BuildAutomapSpriteProjectionTransform(witness, transform)
        && ProjectAutomapSpriteQuad(transform, tileX, tileY, output);
}

} // namespace RuffnecKk::MapSense
