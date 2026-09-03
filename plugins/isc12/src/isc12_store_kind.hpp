#pragma once

#include <array>
#include <string_view>

namespace ruffneckk::isc12 {

enum class StoreKind : unsigned char {
    Other,
    D2S,
    D2I,
};

inline constexpr std::array CanonicalD2IStoreNames{
    std::string_view{"SharedStashSoftCoreV2.d2i"},
    std::string_view{"SharedStashHardCoreV2.d2i"},
    std::string_view{"ModernSharedStashSoftCoreV2.d2i"},
    std::string_view{"ModernSharedStashHardCoreV2.d2i"},
};

inline auto IsAsciiEqualInsensitive(
        std::string_view left,
        std::string_view right) noexcept -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index{}; index < left.size(); ++index) {
        auto a = static_cast<unsigned char>(left[index]);
        auto b = static_cast<unsigned char>(right[index]);
        if (a >= 'a' && a <= 'z') a = static_cast<unsigned char>(a - 0x20U);
        if (b >= 'a' && b <= 'z') b = static_cast<unsigned char>(b - 0x20U);
        if (a != b) return false;
    }
    return true;
}

inline auto IsReservedWin32DeviceName(std::string_view name) noexcept -> bool {
    const auto stem = name.substr(0, name.find('.'));
    for (const auto reserved : std::array{
            std::string_view{"CON"},
            std::string_view{"PRN"},
            std::string_view{"AUX"},
            std::string_view{"NUL"}}) {
        if (IsAsciiEqualInsensitive(stem, reserved)) return true;
    }
    if (stem.size() == 4
            && (IsAsciiEqualInsensitive(stem.substr(0, 3), "COM")
                || IsAsciiEqualInsensitive(stem.substr(0, 3), "LPT"))
            && stem[3] >= '1' && stem[3] <= '9') {
        return true;
    }
    return false;
}

inline auto ClassifyStoreName(std::string_view name) noexcept -> StoreKind {
    if (name.empty() || name.ends_with('.') || name.ends_with(' ')) {
        return StoreKind::Other;
    }
    for (const auto character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U
                || std::string_view{"<>:\"/\\|?*"}.find(character)
                    != std::string_view::npos) {
            return StoreKind::Other;
        }
    }
    if (IsReservedWin32DeviceName(name)) {
        return StoreKind::Other;
    }
    for (const auto candidate : CanonicalD2IStoreNames) {
        if (name == candidate) return StoreKind::D2I;
    }
    constexpr std::string_view D2SExtension{".d2s"};
    if (name.size() > D2SExtension.size()
            && name.ends_with(D2SExtension)) {
        return StoreKind::D2S;
    }
    return StoreKind::Other;
}

} // namespace ruffneckk::isc12
