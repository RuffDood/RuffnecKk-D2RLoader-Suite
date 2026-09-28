#pragma once
#include "compact_layout.hpp"
#include <array>
#include <string_view>

namespace RuffnecKk::VendorStockRefresh {
inline constexpr WidgetRect CompanionSlot{520, 1352, 116, 116};
inline constexpr WidgetRect CompanionGold{421, 1272, 313, 58};
inline constexpr std::array NativeBackgrounds{
    "PANEL/Vendors/VendorShop_BG", "PANEL/Vendors/VendorForge_BG"};
inline constexpr std::array CompanionBackgrounds{
    "D2RLoader\\ruffneckk-vendor-stock-refresh\\vendorshop_bg",
    "D2RLoader\\ruffneckk-vendor-stock-refresh\\vendorforge_bg"};
constexpr bool SameImagePath(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    const auto normalize = [](char c) {
        if (c == '\\') return '/';
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    for (std::size_t i = 0; i < a.size(); ++i)
        if (normalize(a[i]) != normalize(b[i])) return false;
    return true;
}
constexpr bool IsDesktopBackground(const WidgetGeometry& g) noexcept {
    return g.rect.x == 0 && g.rect.y == 0 && g.rect.width == 1162
        && g.rect.height == 1507 && g.scale == 1.0F;
}
}
