#pragma once

#include "ui_localization.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

struct ImFont;
struct ImFontAtlas;

namespace RuffnecKk::MapSense {

// Separate MapSense font sets. The host's shared default remains unchanged.
enum class LocalizedFontProfile : std::uint8_t {
    Western, Japanese, Korean, SimplifiedChinese, TraditionalChinese, Count,
};
inline constexpr auto LocalizedFontProfileCount =
    static_cast<std::size_t>(LocalizedFontProfile::Count);
using LocalizedFontSources =
    std::array<std::span<const std::uint8_t>, LocalizedFontProfileCount>;

// ImGui retains range pointers until the next atlas rebuild. Own the generated
// missing-glyph ranges alongside the renderer context, not on the stack.
struct LocalizedFontRangeStorage {
    std::vector<std::vector<std::uint16_t>> ranges;
};

[[nodiscard]] constexpr auto ResolveLocalizedFontProfile(UiLanguage language)
        noexcept -> LocalizedFontProfile {
    switch (language) {
        case UiLanguage::Japanese: return LocalizedFontProfile::Japanese;
        case UiLanguage::Korean: return LocalizedFontProfile::Korean;
        case UiLanguage::SimplifiedChinese:
            return LocalizedFontProfile::SimplifiedChinese;
        case UiLanguage::TraditionalChinese:
            return LocalizedFontProfile::TraditionalChinese;
        default: return LocalizedFontProfile::Western;
    }
}

// First source wins for a shared Unicode codepoint; subsequent fonts only
// fill real holes. Japanese keeps its kanji, including when it is primary.
[[nodiscard]] constexpr auto LocalizedCjkFontOrder(LocalizedFontProfile profile)
        noexcept -> std::array<LocalizedFontProfile, 4U> {
    using P = LocalizedFontProfile;
    switch (profile) {
        case P::TraditionalChinese:
            return {P::TraditionalChinese, P::SimplifiedChinese,
                P::Japanese, P::Korean};
        case P::SimplifiedChinese:
            return {P::SimplifiedChinese, P::TraditionalChinese,
                P::Japanese, P::Korean};
        case P::Korean:
            return {P::Korean, P::Japanese,
                P::SimplifiedChinese, P::TraditionalChinese};
        default:
            return {P::Japanese, P::Korean,
                P::SimplifiedChinese, P::TraditionalChinese};
    }
}

// Uses caller-owned immutable bytes, retained through atlas destruction.
// CPU-only and shared by the renderer and the offline font-atlas tests.
[[nodiscard]] auto AddMapSenseLocalizedFont(
    ImFontAtlas& atlas, LocalizedFontRangeStorage& rangeStorage,
    const LocalizedFontSources& sources,
    LocalizedFontProfile profile, float pixelSize, bool menuOnly) -> ImFont*;

} // namespace RuffnecKk::MapSense
