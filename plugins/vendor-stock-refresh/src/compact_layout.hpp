#pragma once

#include "policy.hpp"

namespace RuffnecKk::VendorStockRefresh {

struct WidgetGeometry {
    WidgetRect rect{};
    float scale{1.0F};
};

constexpr bool SameGeometry(const WidgetGeometry& a, const WidgetGeometry& b) noexcept {
    return a.rect.x == b.rect.x && a.rect.y == b.rect.y
        && a.rect.width == b.rect.width && a.rect.height == b.rect.height
        && a.scale == b.scale;
}

constexpr bool UsableScale(float scale) noexcept {
    // Both comparisons also reject NaN and infinity before any integer conversion.
    return scale > 0.0F && scale <= 16.0F;
}

struct CompactPlacement {
    bool valid{};
    WidgetGeometry geometry{};
};

constexpr CompactPlacement CenterInPanelSlot(
    const WidgetRect& slot, const WidgetGeometry& original
) noexcept {
    if (!HasUsableSize(slot) || !HasUsableSize(original.rect)
        || !UsableScale(original.scale)) return {};
    const double width = static_cast<double>(original.rect.width) * original.scale;
    const double height = static_cast<double>(original.rect.height) * original.scale;
    if (width < 8 || height < 8 || width > slot.width || height > slot.height) return {};
    const auto x = static_cast<std::int64_t>(slot.x)
        + (static_cast<std::int64_t>(slot.width) - static_cast<std::int64_t>(width)) / 2;
    const auto y = static_cast<std::int64_t>(slot.y)
        + (static_cast<std::int64_t>(slot.height) - static_cast<std::int64_t>(height)) / 2;
    constexpr auto high = (std::numeric_limits<std::int32_t>::max)();
    if (x + width > high || y + height > high) return {};
    auto geometry = original;
    geometry.rect.x = static_cast<std::int32_t>(x);
    geometry.rect.y = static_cast<std::int32_t>(y);
    return {true, geometry};
}

constexpr CompactPlacement TranslateGoldWidget(
    const WidgetGeometry& original, const WidgetRect& oldAnchor, const WidgetRect& newAnchor
) noexcept {
    if (!HasUsableSize(original.rect) || !HasUsableSize(oldAnchor)
        || !HasUsableSize(newAnchor) || !UsableScale(original.scale)) return {};
    const auto x = static_cast<std::int64_t>(original.rect.x) + newAnchor.x - oldAnchor.x;
    const auto y = static_cast<std::int64_t>(original.rect.y) + newAnchor.y - oldAnchor.y;
    constexpr auto low = (std::numeric_limits<std::int32_t>::min)();
    constexpr auto high = (std::numeric_limits<std::int32_t>::max)();
    if (x < low || y < low || x + original.rect.width > high
        || y + original.rect.height > high) return {};
    auto geometry = original;
    geometry.rect.x = static_cast<std::int32_t>(x);
    geometry.rect.y = static_cast<std::int32_t>(y);
    return {true, geometry};
}

constexpr CompactPlacement CompactBelow(
    const WidgetRect& anchor,
    const WidgetGeometry& original
) noexcept {
    if (!HasUsableSize(anchor) || !HasUsableSize(original.rect)
        || !UsableScale(original.scale)) return {};

    const auto scale = original.scale * 0.5F;
    const double width = static_cast<double>(original.rect.width) * scale;
    const double height = static_cast<double>(original.rect.height) * scale;
    // Refuse invisible controls and implausibly large geometry before narrowing.
    if (width < 8.0 || height < 8.0 || width > 32768.0 || height > 32768.0
        || width > anchor.width) return {};

    // Use the visual footprint, not the unscaled sprite rectangle, for centering.
    const auto x = static_cast<std::int64_t>(anchor.x)
        + (static_cast<std::int64_t>(anchor.width) - static_cast<std::int64_t>(width)) / 2;
    // Gold's focus rect is inside its frame. Half a compact button's height clears
    // that frame and places the native artwork in the strip above the lower trim.
    const auto y = static_cast<std::int64_t>(anchor.y) + anchor.height
        + static_cast<std::int64_t>(height / 2.0 + 0.5);
    constexpr auto low = (std::numeric_limits<std::int32_t>::min)();
    constexpr auto high = (std::numeric_limits<std::int32_t>::max)();
    if (x < low || y < low || x + width > high || y + height > high) return {};

    auto placed = original;
    placed.rect.x = static_cast<std::int32_t>(x);
    placed.rect.y = static_cast<std::int32_t>(y);
    placed.scale = scale;
    return {true, placed};
}

// Artwork-independent placement: retain the requested native visual size unless
// the measured free rectangle requires a smaller scale. Never enlarge past it.
constexpr CompactPlacement FitButtonInArea(const WidgetRect& area, const WidgetGeometry& desired) noexcept {
    if (!HasUsableSize(area) || !HasUsableSize(desired.rect) || !UsableScale(desired.scale)) return {};
    double scale = desired.scale;
    const double sx = static_cast<double>(area.width) / desired.rect.width;
    const double sy = static_cast<double>(area.height) / desired.rect.height;
    if (scale > sx) scale = sx;
    if (scale > sy) scale = sy;
    auto geometry = desired;
    geometry.scale = static_cast<float>(scale);
    // Round down to avoid crossing a measured edge after float conversion.
    if (static_cast<double>(geometry.scale) > scale) geometry.scale *= 0.999999F;
    const double width = desired.rect.width * static_cast<double>(geometry.scale);
    const double height = desired.rect.height * static_cast<double>(geometry.scale);
    if (width < 8 || height < 8 || width > area.width || height > area.height) return {};
    const auto x = static_cast<std::int64_t>(area.x) + static_cast<std::int64_t>((area.width - width) / 2);
    constexpr auto high = (std::numeric_limits<std::int32_t>::max)();
    if (x + width > high || static_cast<double>(area.y) + height > high) return {};
    geometry.rect.x = static_cast<std::int32_t>(x); geometry.rect.y = area.y;
    return {true, geometry};
}

// The original layout remains the source of every placement. Repeated panel
// configuration must not halve the scale repeatedly or capture our own changes.
struct RefreshLayoutState {
    WidgetGeometry original{};
    WidgetGeometry applied{};
    bool hasOriginal{};
    bool hasApplied{};

    constexpr void Observe(const WidgetGeometry& current) noexcept {
        if (!hasOriginal || !SameGeometry(current, hasApplied ? applied : original)) {
            original = current;
            hasOriginal = true;
            hasApplied = false;
        }
    }

    constexpr void Applied(const WidgetGeometry& geometry) noexcept {
        applied = geometry;
        hasApplied = true;
    }

    constexpr void Restored() noexcept { hasApplied = false; }
};

} // namespace RuffnecKk::VendorStockRefresh
