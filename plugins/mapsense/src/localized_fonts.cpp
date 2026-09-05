#include "localized_fonts.hpp"

#include <imgui.h>

// Read the same cmap as the pinned ImGui rasterizer before adding a source.
// This avoids copying a whole optional CJK font for zero contributed glyphs.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <bitset>
#include <cmath>
#include <limits>

namespace RuffnecKk::MapSense {
namespace {

constexpr ImWchar WesternRanges[]{
    0x0020, 0x024F, 0x0300, 0x052F, 0x1E00, 0x1EFF,
    0x2000, 0x206F, 0x20A0, 0x20CF, 0,
};
// Disjoint Unicode blocks, NOT the overbroad 0x1100..0xD7AF interval.
// Full BMP Han coverage preserves kanji and traditional/rare mod labels.
constexpr ImWchar CjkRanges[]{
    0x1100, 0x11FF, 0x3000, 0x318F, 0x3190, 0x31BF,
    0x31F0, 0x31FF, 0x3400, 0x4DBF, 0x4E00, 0x9FFF,
    0xA960, 0xA97F, 0xAC00, 0xD7FF, 0xF900, 0xFAFF,
    0xFE30, 0xFE4F, 0xFF00, 0xFFEF, 0,
};

auto MenuRanges(LocalizedFontProfile profile) -> const ImWchar* {
    // ImGui retains ranges until Build(). Process lifetime avoids CRT teardown
    // ordering with the renderer; this small immutable set is built once.
    static auto* const ranges = [] {
        auto* result = new std::array<ImVector<ImWchar>, LocalizedFontProfileCount>{};
        for (std::size_t bucket = 0; bucket < result->size(); ++bucket) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(WesternRanges);
            for (std::size_t language = 0U; language < UiLanguageCount; ++language) {
                const auto locale = static_cast<UiLanguage>(language);
                if (static_cast<std::size_t>(ResolveLocalizedFontProfile(locale))
                        != bucket) continue;
                for (std::size_t text = 0U; text < UiTextCount; ++text)
                    builder.AddText(UiText(static_cast<UiTextId>(text), locale));
            }
            builder.BuildRanges(&(*result)[bucket]);
        }
        return result;
    }();
    return (*ranges)[static_cast<std::size_t>(profile)].Data;
}

auto MissingSourceRanges(std::span<const std::uint8_t> bytes,
        const ImWchar* requested, std::bitset<65'536U>& claimed,
        LocalizedFontRangeStorage& storage) -> const ImWchar* {
    if (bytes.size() < 12U) return nullptr;
    stbtt_fontinfo info{};
    const auto offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    if (offset < 0 || static_cast<std::size_t>(offset) >= bytes.size()
            || !stbtt_InitFont(&info, bytes.data(), offset)) return nullptr;
    ImFontGlyphRangesBuilder builder;
    auto contributed = false;
    for (const auto* pair = requested; pair[0] != 0U; pair += 2) {
        for (std::uint32_t codepoint = pair[0]; codepoint <= pair[1]; ++codepoint) {
            if (claimed[codepoint] || stbtt_FindGlyphIndex(&info,
                    static_cast<int>(codepoint)) == 0) continue;
            claimed.set(codepoint);
            builder.AddChar(static_cast<ImWchar>(codepoint));
            contributed = true;
        }
    }
    if (!contributed) return nullptr;
    ImVector<ImWchar> result;
    builder.BuildRanges(&result);
    storage.ranges.emplace_back(result.begin(), result.end());
    static_assert(sizeof(ImWchar) == sizeof(std::uint16_t));
    return storage.ranges.back().data();
}

auto AddSource(ImFontAtlas& atlas, std::span<const std::uint8_t> bytes,
        const ImWchar* ranges, float pixelSize, bool merge,
        bool menuOnly = true) -> ImFont* {
    if (bytes.empty() || bytes.size()
            > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return nullptr;
    }
    ImFontConfig config{};
    config.MergeMode = merge;
    config.FontDataOwnedByAtlas = false;
    config.PixelSnapH = false;
    // CJK labels contain tens of thousands of glyphs. RasterizerDensity already
    // supersamples them; a second 2x2 oversampling would quadruple atlas memory.
    config.OversampleH = menuOnly ? 2 : 1;
    config.OversampleV = menuOnly ? 2 : 1;
    config.RasterizerDensity = 1.25F;
    return atlas.AddFontFromMemoryTTF(const_cast<std::uint8_t*>(bytes.data()),
        static_cast<int>(bytes.size()), pixelSize, &config, ranges);
}

} // namespace

auto AddMapSenseLocalizedFont(ImFontAtlas& atlas,
        LocalizedFontRangeStorage& rangeStorage,
        const LocalizedFontSources& sources, LocalizedFontProfile profile,
        float pixelSize, bool menuOnly) -> ImFont* {
    if (!std::isfinite(pixelSize) || pixelSize < 1.0F || pixelSize > 72.0F)
        return nullptr;
    if (static_cast<std::size_t>(profile) >= LocalizedFontProfileCount)
        return nullptr;
    // D3D12 accepts non-power-of-two textures. Packing a broad CJK atlas into
    // a narrow power-of-two strip can exceed its 16384-pixel dimension limit.
    atlas.TexDesiredWidth = 8'192;
    atlas.Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
    // Western glyphs retain their original family. CJK glyphs are deliberately
    // excluded here so the active locale, not Segoe coverage, owns them.
    std::bitset<65'536U> claimed;
    const auto* western = MissingSourceRanges(sources[0], WesternRanges,
        claimed, rangeStorage);
    auto* font = western == nullptr ? nullptr
        : AddSource(atlas, sources[0], western, pixelSize, false);
    if (font == nullptr) {
        ImFontConfig fallback{};
        fallback.SizePixels = pixelSize;
        font = atlas.AddFontDefault(&fallback);
        for (unsigned int codepoint = 0x20U; codepoint <= 0xFFU; ++codepoint)
            claimed.set(codepoint);
    }
    if (font == nullptr) return nullptr;
    const auto* ranges = menuOnly ? MenuRanges(profile) : CjkRanges;
    for (const auto source : LocalizedCjkFontOrder(profile)) {
        const auto bytes = sources[static_cast<std::size_t>(source)];
        const auto* missing = MissingSourceRanges(bytes, ranges, claimed, rangeStorage);
        if (missing != nullptr)
            (void)AddSource(atlas, bytes, missing, pixelSize, true, menuOnly);
    }
    return font;
}

} // namespace RuffnecKk::MapSense
