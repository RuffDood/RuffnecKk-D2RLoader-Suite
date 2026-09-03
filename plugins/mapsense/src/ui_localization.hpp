#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

// D2R ships separate European and Latin-American Spanish locales. MapSense's
// neutral Spanish wording is intentionally shared by both, so the two
// identical D2R defense fingerprints do not need an unsafe heuristic.
enum class UiLanguage : std::uint8_t {
    English,
    TraditionalChinese,
    German,
    Spanish,
    French,
    Italian,
    Korean,
    Polish,
    Japanese,
    BrazilianPortuguese,
    Russian,
    SimplifiedChinese,
    Count,
};

enum class UiTextId : std::uint16_t {
    Open,
    OpenOff,
    Collapse,
    EnableMapSense,
    Appearance,
    MenuTheme,
    MapAndReveal,
    RevealMap,
    AdditionsOpacity,
    Monsters,
    ShowMonsters,
    Normal,
    Minion,
    Champion,
    Unique,
    SuperUniqueBoss,
    Immunities,
    ShowImmunities,
    IndicatorSize,
    HaloThickness,
    Colors,
    Missiles,
    ShowMissiles,
    Fire,
    Cold,
    ColdIce,
    Lightning,
    Poison,
    Physical,
    Magic,
    Objects,
    ShowAutomapObjects,
    ExitLabels,
    ShowExitLabels,
    WaypointLabels,
    ShowWaypointLabels,
    ShrineLabels,
    ShowShrineLabels,
    Chests,
    ShowChests,
    MarkerSize,
    LockedLockColor,
    TrappedLockColor,
    SpecialChests,
    ShowSpecialChests,
    ArmorRacks,
    ShowArmorRacks,
    WeaponRacks,
    ShowWeaponRacks,
    Navigation,
    LineThickness,
    Waypoint,
    WaypointLine,
    MainProgression,
    MainProgressionLine,
    QuestTargets,
    QuestTargetLine,
    CustomLevels,
    CustomLevelLines,
    CustomTomlHint,
    LineColor,
    Shape,
    PlayerCross,
    Dot,
    Style,
    ColoredI,
    SplitHalo,
    Color,
    Size,
    Thickness,
    Names,
    ShowNames,
    NameSize,
    NameColor,
    TextSize,
    TextColor,
    MarkerColor,
    ThemeSanctuaryGold,
    ThemeHellfire,
    ThemeHoradricSand,
    ThemeArcaneSanctuary,
    ThemeTristramMoon,
    ThemeKurastJade,
    ThemeNecromancerBone,
    ThemeHarrogathFrost,
    ThemeBloodMoor,
    ThemeHighContrast,
    Count,
};

[[nodiscard]] auto DetectUiLanguageFromFingerprint(
    std::string_view defenseFormat) noexcept -> UiLanguage;

// Uses D2RLoader's existing localization service only. It adds no native hook,
// RVA, mod string-table dependency, or user-facing configuration file.
[[nodiscard]] auto RefreshUiLanguage(
    const D2RL::PluginContext* context) noexcept -> bool;
void ResetUiLanguage() noexcept;
[[nodiscard]] auto CurrentUiLanguage() noexcept -> UiLanguage;
[[nodiscard]] auto UiLanguageCode(UiLanguage language) noexcept
    -> std::string_view;
[[nodiscard]] auto UiText(
    UiTextId id,
    UiLanguage language) noexcept -> const char*;
[[nodiscard]] auto UiText(UiTextId id) noexcept -> const char*;
[[nodiscard]] auto UiLocalizationCatalogIsComplete() noexcept -> bool;

inline constexpr auto UiLanguageCount =
    static_cast<std::size_t>(UiLanguage::Count);
inline constexpr auto UiTextCount = static_cast<std::size_t>(UiTextId::Count);

} // namespace RuffnecKk::MapSense
