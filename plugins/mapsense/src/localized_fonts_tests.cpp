#include "localized_fonts.hpp"

#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

using namespace RuffnecKk::MapSense;
namespace {
int Failures{};
void Check(bool value, const char* expression, int line) {
    if (!value) {
        std::cerr << "FAIL " << line << ": " << expression << '\n';
        ++Failures;
    }
}
#define CHECK(value) Check(static_cast<bool>(value), #value, __LINE__)

auto ReadFont(const std::filesystem::path& path) -> std::vector<std::uint8_t> {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto length = input.tellg();
    if (length <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return input ? bytes : std::vector<std::uint8_t>{};
}

void CheckProfiles() {
    using P = LocalizedFontProfile;
    CHECK(ResolveLocalizedFontProfile(UiLanguage::Japanese) == P::Japanese);
    CHECK(ResolveLocalizedFontProfile(UiLanguage::Korean) == P::Korean);
    CHECK(ResolveLocalizedFontProfile(UiLanguage::TraditionalChinese)
        == P::TraditionalChinese);
    CHECK(ResolveLocalizedFontProfile(UiLanguage::SimplifiedChinese)
        == P::SimplifiedChinese);
    CHECK(ResolveLocalizedFontProfile(UiLanguage::French) == P::Western);
    CHECK(ResolveLocalizedFontProfile(static_cast<UiLanguage>(255)) == P::Western);
    for (std::size_t i = 1U; i < LocalizedFontProfileCount; ++i) {
        const auto profile = static_cast<P>(i);
        const auto order = LocalizedCjkFontOrder(profile);
        CHECK(order[0] == profile);
        CHECK(std::set<P>(order.begin(), order.end()).size() == 4U);
    }
    // No Windows optional font is a startup requirement.
    LocalizedFontRangeStorage storage;
    ImFontAtlas atlas;
    auto* fallback = AddMapSenseLocalizedFont(atlas, storage, {}, P::TraditionalChinese,
        15.0F, true);
    CHECK(fallback != nullptr);
    CHECK(atlas.Build());
    CHECK(fallback->FindGlyphNoFallback('A') != nullptr);
    CHECK(AddMapSenseLocalizedFont(atlas, storage, {}, P::Western, 0.0F, false) == nullptr);
}

void CheckRealFonts(const LocalizedFontSources& sources) {
    using P = LocalizedFontProfile;
    const std::array<ImWchar, 16U> samples{
        0x4E00, 0x5B57, 0x90AA, 0x60E1, 0x6D1E, 0x7A9F,
        0x9AA8, 0x98DF, 0x9F8D, 0x958B, 0x95E8, 0x3042,
        0x30A2, 0x3105, 0xAC00, 0x1100};
    for (std::size_t index = 1U; index < LocalizedFontProfileCount; ++index) {
        if (sources[index].empty()) {
            std::cout << "SKIP optional system font profile " << index << '\n';
            continue;
        }
        const auto profile = static_cast<P>(index);
        for (const auto size : {15.0F, 20.0F, 30.0F}) {
            LocalizedFontRangeStorage storage;
            ImFontAtlas atlas;
            const auto sourcesBefore = atlas.ConfigData.Size;
            auto* labels = AddMapSenseLocalizedFont(atlas, storage, sources, profile,
                size, false);
            auto* menu = AddMapSenseLocalizedFont(atlas, storage, sources, profile,
                size, true);
            LocalizedFontSources primaryOnly{};
            primaryOnly[0] = sources[0];
            primaryOnly[index] = sources[index];
            auto* reference = AddMapSenseLocalizedFont(atlas, storage, primaryOnly,
                profile, size, false);
            CHECK(labels != nullptr && menu != nullptr && reference != nullptr);
            // This pinned ImGui copies borrowed FontData on AddFont; compare
            // bytes rather than assuming the source allocation is retained.
            const auto& primary = atlas.ConfigData[sourcesBefore + 1];
            CHECK(primary.FontDataSize == static_cast<int>(sources[index].size()));
            CHECK(std::memcmp(primary.FontData, sources[index].data(),
                sources[index].size()) == 0);
            CHECK(atlas.Build());
            CHECK(atlas.TexWidth <= 16'384 && atlas.TexHeight <= 16'384);
            for (std::size_t language = 0; language < UiLanguageCount; ++language) {
                const auto locale = static_cast<UiLanguage>(language);
                if (ResolveLocalizedFontProfile(locale) != profile) continue;
                for (std::size_t text = 0; text < UiTextCount; ++text) {
                    const auto* cursor = UiText(static_cast<UiTextId>(text), locale);
                    while (*cursor != '\0') {
                        unsigned int codepoint{};
                        const auto length = ImTextCharFromUtf8(&codepoint, cursor, nullptr);
                        CHECK(length > 0);
                        if (length <= 0) break;
                        if (codepoint >= 0x20U)
                            CHECK(menu->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr);
                        cursor += length;
                    }
                }
            }
            std::size_t compared{};
            for (const auto codepoint : samples) {
                const auto* expected = reference->FindGlyphNoFallback(codepoint);
                if (expected == nullptr) continue;
                const auto* actual = labels->FindGlyphNoFallback(codepoint);
                CHECK(actual != nullptr);
                if (actual == nullptr) continue;
                CHECK(std::abs(actual->AdvanceX - expected->AdvanceX) < 0.001F);
                CHECK(std::abs((actual->X1 - actual->X0)
                    - (expected->X1 - expected->X0)) < 0.001F);
                CHECK(std::abs((actual->Y1 - actual->Y0)
                    - (expected->Y1 - expected->Y0)) < 0.001F);
                ++compared;
            }
            CHECK(compared > 0U);
            // Japanese must retain kanji, not only hiragana/katakana.
            if (profile == P::Japanese) {
                CHECK(labels->FindGlyphNoFallback(0x5B57) != nullptr);
                CHECK(labels->FindGlyphNoFallback(0x3042) != nullptr);
                CHECK(labels->FindGlyphNoFallback(0x30A2) != nullptr);
            }
            std::cout << "PASS profile=" << index << " px=" << size
                << " primary-glyphs=" << compared << " atlas="
                << atlas.TexWidth << 'x' << atlas.TexHeight << '\n';
        }
    }

    // Simulate graphics first, language later: adding private sets does not
    // replace or mutate the shared client's font metrics/pointer.
    LocalizedFontRangeStorage lateStorage;
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    auto* shared = AddMapSenseLocalizedFont(*io.Fonts, lateStorage, sources, P::Western,
        15.0F, false);
    io.FontDefault = shared;
    CHECK(io.Fonts->Build());
    const auto sharedAdvance = shared->FindGlyph('8')->AdvanceX;
    for (const auto profile : {P::TraditionalChinese, P::SimplifiedChinese,
            P::Japanese, P::Korean}) {
        auto* labels = AddMapSenseLocalizedFont(*io.Fonts, lateStorage, sources, profile,
            15.0F, false);
        for (const auto scale : {1.25F, 1.3333334F, 1.5F, 1.75F, 2.0F}) {
            CHECK(AddMapSenseLocalizedFont(*io.Fonts, lateStorage, sources, profile,
                15.0F * scale, true) != nullptr);
        }
        CHECK(io.Fonts->Build());
        CHECK(io.FontDefault == shared && labels != shared);
        CHECK(shared->FindGlyph('8')->AdvanceX == sharedAdvance);
        CHECK(io.Fonts->TexWidth <= 16'384 && io.Fonts->TexHeight <= 16'384);
        std::cout << "PASS late locale=" << static_cast<int>(profile)
            << " shared-font-preserved atlas=" << io.Fonts->TexWidth
            << 'x' << io.Fonts->TexHeight << '\n';
    }
    ImGui::DestroyContext(context);

    // Missing primary font still uses the next installed family, not tofu.
    auto missing = sources;
    missing[static_cast<std::size_t>(P::TraditionalChinese)] = {};
    LocalizedFontRangeStorage missingStorage;
    ImFontAtlas atlas;
    auto* font = AddMapSenseLocalizedFont(atlas, missingStorage, missing, P::TraditionalChinese,
        15.0F, false);
    CHECK(atlas.Build());
    if (!missing[static_cast<std::size_t>(P::SimplifiedChinese)].empty())
        CHECK(font->FindGlyphNoFallback(0x5B57) != nullptr);
}
} // namespace

int main() {
    CheckProfiles();
    std::array<wchar_t, 32'768U> windows{};
    const auto count = GetWindowsDirectoryW(windows.data(),
        static_cast<UINT>(windows.size()));
    CHECK(count > 0U && count < windows.size());
    const auto root = std::filesystem::path(windows.data()) / L"Fonts";
    std::array<std::vector<std::uint8_t>, LocalizedFontProfileCount> bytes{
        ReadFont(root / L"segoeui.ttf"), ReadFont(root / L"msgothic.ttc"),
        ReadFont(root / L"malgun.ttf"), ReadFont(root / L"msyh.ttc"),
        ReadFont(root / L"msjh.ttc")};
    LocalizedFontSources sources;
    for (std::size_t i = 0; i < sources.size(); ++i) sources[i] = bytes[i];
    CheckRealFonts(sources);
    return Failures == 0 ? 0 : 1;
}
