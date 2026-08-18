#pragma once

#include "config.hpp"

#include <cstdint>
#include <limits>

namespace RuffnecKk::VendorStockRefresh {

inline constexpr std::uint8_t NormalVendorMode = 2;
inline constexpr std::uint8_t GambleVendorMode = 3;
inline constexpr std::uint32_t NormalRefreshAction = 0x56535246u; // "VSRF"
inline constexpr std::uint32_t VanillaNormalVendorAction = 1u;
inline constexpr std::uint32_t VanillaGambleRefreshAction = 2u;

struct WidgetRect {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

struct WidgetPosition {
    bool valid{};
    std::int32_t x{};
    std::int32_t y{};
};

constexpr auto HasUsableSize(const WidgetRect& rect) noexcept -> bool {
    return rect.width > 0 && rect.height > 0;
}

constexpr auto CenterBelow(
    const WidgetRect& anchor,
    const WidgetRect& widget
) noexcept -> WidgetPosition {
    if (!HasUsableSize(anchor) || !HasUsableSize(widget)) {
        return {};
    }

    const auto x = static_cast<std::int64_t>(anchor.x)
        + (static_cast<std::int64_t>(anchor.width) - widget.width) / 2;
    const auto gap = (static_cast<std::int64_t>(widget.height) + 5) / 6;
    const auto y = static_cast<std::int64_t>(anchor.y) + anchor.height + gap;
    if (x < (std::numeric_limits<std::int32_t>::min)()
        || x > (std::numeric_limits<std::int32_t>::max)()
        || y < (std::numeric_limits<std::int32_t>::min)()
        || y > (std::numeric_limits<std::int32_t>::max)()) {
        return {};
    }

    return {
        .valid = true,
        .x = static_cast<std::int32_t>(x),
        .y = static_cast<std::int32_t>(y),
    };
}

constexpr auto UnionRect(
    const WidgetRect& first,
    const WidgetRect& second
) noexcept -> WidgetRect {
    if (!HasUsableSize(first)) {
        return second;
    }
    if (!HasUsableSize(second)) {
        return first;
    }

    const auto left = first.x < second.x ? first.x : second.x;
    const auto top = first.y < second.y ? first.y : second.y;
    const auto firstRight = static_cast<std::int64_t>(first.x) + first.width;
    const auto secondRight = static_cast<std::int64_t>(second.x) + second.width;
    const auto firstBottom = static_cast<std::int64_t>(first.y) + first.height;
    const auto secondBottom = static_cast<std::int64_t>(second.y) + second.height;
    const auto right = firstRight > secondRight ? firstRight : secondRight;
    const auto bottom = firstBottom > secondBottom ? firstBottom : secondBottom;
    if (right - left > (std::numeric_limits<std::int32_t>::max)()
        || bottom - top > (std::numeric_limits<std::int32_t>::max)()) {
        return {};
    }

    return {
        .x = left,
        .y = top,
        .width = static_cast<std::int32_t>(right - left),
        .height = static_cast<std::int32_t>(bottom - top),
    };
}

constexpr auto ShouldArmNormalRefresh(
    bool hasRefreshMarker,
    std::uint8_t requestedMode,
    bool hasVendorEntry,
    bool vendorInventoryFilled
) noexcept -> bool {
    return hasRefreshMarker
        && requestedMode == NormalVendorMode
        && hasVendorEntry
        && vendorInventoryFilled;
}

constexpr auto RefreshActionForPanel(bool isGambling) noexcept -> std::uint32_t {
    return isGambling ? VanillaGambleRefreshAction : NormalRefreshAction;
}

constexpr auto ShouldShowNormalRefresh(bool isGambling) noexcept -> bool {
    return !isGambling;
}

} // namespace RuffnecKk::VendorStockRefresh
