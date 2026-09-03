#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::int64_t CurrentConfigSchemaVersion = 16;

inline constexpr float MinimumMonsterMarkerSize = 3.0F;
inline constexpr float MaximumMonsterMarkerSize = 40.0F;
inline constexpr float MinimumMonsterMarkerThickness = 1.0F;
inline constexpr float MaximumMonsterMarkerThickness = 5.0F;
inline constexpr float MinimumImmunityIndicatorSize = 8.0F;
inline constexpr float DefaultImmunityIndicatorSize = 14.0F;
inline constexpr float MaximumImmunityIndicatorSize = 32.0F;
inline constexpr float MinimumImmunityHaloThickness = 1.0F;
inline constexpr float DefaultImmunityHaloThickness = 2.0F;
inline constexpr float MaximumImmunityHaloThickness = 6.0F;
inline constexpr float MinimumAutomapLabelSize = 8.0F;
inline constexpr float DefaultAutomapLabelSize = 28.0F;
inline constexpr float MaximumAutomapLabelSize = 72.0F;
inline constexpr float MinimumAutomapObjectSize = 6.0F;
inline constexpr float DefaultAutomapObjectSize = 36.0F;
inline constexpr float MaximumAutomapObjectSize = 80.0F;
inline constexpr float MinimumMissileMarkerSize = 1.0F;
inline constexpr float DefaultMissileMarkerSize = 3.0F;
inline constexpr float MaximumMissileMarkerSize = 16.0F;
inline constexpr float MinimumNavigationLineThickness = 1.0F;
inline constexpr float DefaultNavigationLineThickness = 2.0F;
inline constexpr float MaximumNavigationLineThickness = 8.0F;
inline constexpr std::int32_t MinimumCustomLevelId = 1;
inline constexpr std::int32_t MaximumCustomLevelId = 65'535;
inline constexpr std::size_t MaximumCustomLevelTargets = 128U;
inline constexpr std::size_t MaximumCustomLevelNameLength = 128U;

struct RgbaColor {
    float red{};
    float green{};
    float blue{};
    float alpha{1.0F};
};

inline constexpr RgbaColor DefaultPhysicalImmunityColor{
    216.0F / 255.0F,
    195.0F / 255.0F,
    154.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor LegacyPhysicalImmunityColor{
    199.0F / 255.0F,
    199.0F / 255.0F,
    199.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultExitLabelColor{
    1.0F,
    211.0F / 255.0F,
    61.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor LegacyShrineLabelColor{
    1.0F,
    191.0F / 255.0F,
    31.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultShrineLabelColor = DefaultExitLabelColor;
inline constexpr RgbaColor DefaultWaypointLabelColor = DefaultExitLabelColor;
inline constexpr RgbaColor DefaultBossNameColor{
    1.0F,
    211.0F / 255.0F,
    61.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor LegacyAutomapObjectColor{
    216.0F / 255.0F,
    195.0F / 255.0F,
    154.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultAutomapObjectColor{
    184.0F / 255.0F,
    138.0F / 255.0F,
    42.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultChestOutlineColor{
    20.0F / 255.0F,
    80.0F / 255.0F,
    173.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultChestInteriorColor{
    184.0F / 255.0F,
    138.0F / 255.0F,
    42.0F / 255.0F,
    176.0F / 255.0F,
};
inline constexpr RgbaColor LegacyLockedChestColor{
    87.0F / 255.0F,
    224.0F / 255.0F,
    61.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor LegacyAmberLockedChestColor{
    216.0F / 255.0F,
    155.0F / 255.0F,
    43.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultLockedChestColor{
    0.0F,
    1.0F,
    1.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultTrappedChestColor{
    1.0F,
    59.0F / 255.0F,
    48.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultSuperChestColor{
    1.0F,
    211.0F / 255.0F,
    61.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultSuperChestStarColor =
    DefaultExitLabelColor;
inline constexpr RgbaColor DefaultArmorRackColor{
    61.0F / 255.0F,
    139.0F / 255.0F,
    1.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultWeaponRackColor{
    1.0F,
    138.0F / 255.0F,
    36.0F / 255.0F,
    1.0F,
};
inline constexpr RgbaColor DefaultFireMissileColor{
    1.0F, 0.0F, 0.0F, 127.0F / 255.0F};
inline constexpr RgbaColor DefaultColdMissileColor{
    0.0F, 208.0F / 255.0F, 1.0F, 127.0F / 255.0F};
inline constexpr RgbaColor DefaultLightningMissileColor{
    1.0F, 1.0F, 0.0F, 70.0F / 255.0F};
inline constexpr RgbaColor DefaultPoisonMissileColor{
    50.0F / 255.0F, 205.0F / 255.0F, 50.0F / 255.0F,
    127.0F / 255.0F};
inline constexpr RgbaColor DefaultPhysicalMissileColor{
    205.0F / 255.0F, 133.0F / 255.0F, 63.0F / 255.0F,
    127.0F / 255.0F};
inline constexpr RgbaColor DefaultMagicMissileColor{
    1.0F, 136.0F / 255.0F, 0.0F, 127.0F / 255.0F};

struct OverlayOptions {
    bool diagnosticPreview{};
    bool followNativeAutomap{true};
    bool startMenuOpen{};
    float opacity{1.0F};
    float scale{1.0F};
    std::int32_t frameRate{60};
};

enum class MonsterMarkerShape : std::uint8_t {
    X,
    PlayerCross,
    Dot,
};

struct MonsterMarkerStyle {
    RgbaColor color{};
    float size{18.0F};
    MonsterMarkerShape shape{MonsterMarkerShape::PlayerCross};
    float thickness{2.0F};
    bool showNames{};
    RgbaColor nameColor{1.0F, 1.0F, 1.0F, 1.0F};
    float nameSize{DefaultAutomapLabelSize};
};

struct MonsterOptions {
    bool enabled{true};
    MonsterMarkerStyle normal{{1.0F, 1.0F, 1.0F, 1.0F}, 18.0F};
    MonsterMarkerStyle minion{{1.0F, 0.831F, 0.231F, 1.0F}, 18.0F};
    MonsterMarkerStyle champion{{0.239F, 0.545F, 1.0F, 1.0F}, 20.0F};
    MonsterMarkerStyle unique{{1.0F, 0.541F, 0.141F, 1.0F}, 22.0F};
    MonsterMarkerStyle superUniqueBoss{
        {1.0F, 0.231F, 0.188F, 1.0F},
        24.0F,
        MonsterMarkerShape::PlayerCross,
        2.0F,
        true,
        DefaultBossNameColor,
        DefaultAutomapLabelSize,
    };
};

enum class ImmunityDisplayStyle : std::uint8_t {
    ColoredI,
    SplitHalo,
};

struct ImmunityOptions {
    bool enabled{true};
    ImmunityDisplayStyle style{ImmunityDisplayStyle::ColoredI};
    float indicatorSize{DefaultImmunityIndicatorSize};
    float haloThickness{DefaultImmunityHaloThickness};
    RgbaColor physical{DefaultPhysicalImmunityColor};
    RgbaColor fire{0.95F, 0.24F, 0.12F, 1.0F};
    RgbaColor cold{0.20F, 0.65F, 1.0F, 1.0F};
    RgbaColor lightning{1.0F, 0.88F, 0.18F, 1.0F};
    RgbaColor poison{0.34F, 0.88F, 0.24F, 1.0F};
    RgbaColor magic{0.78F, 0.36F, 1.0F, 1.0F};
};

struct MissileMarkerStyle {
    RgbaColor color{};
    float size{DefaultMissileMarkerSize};
};

struct MissileOptions {
    bool enabled{true};
    MissileMarkerStyle fire{DefaultFireMissileColor};
    MissileMarkerStyle cold{DefaultColdMissileColor};
    MissileMarkerStyle lightning{DefaultLightningMissileColor};
    MissileMarkerStyle poison{DefaultPoisonMissileColor};
    MissileMarkerStyle physical{DefaultPhysicalMissileColor};
    MissileMarkerStyle magic{DefaultMagicMissileColor};
};

struct AutomapLabelOptions {
    bool enabled{true};
    RgbaColor color{1.0F, 1.0F, 1.0F, 1.0F};
    float size{DefaultAutomapLabelSize};
};

struct AutomapObjectOptions {
    bool enabled{true};
    RgbaColor color{DefaultAutomapObjectColor};
    float size{DefaultAutomapObjectSize};
};

struct ChestOptions {
    bool enabled{true};
    RgbaColor outlineColor{DefaultChestOutlineColor};
    RgbaColor interiorColor{DefaultChestInteriorColor};
    RgbaColor lockedAccentColor{DefaultLockedChestColor};
    RgbaColor trappedAccentColor{DefaultTrappedChestColor};
    float size{DefaultAutomapObjectSize};
};

struct SuperChestOptions {
    bool enabled{true};
    bool starsEnabled{true};
    RgbaColor starsColor{DefaultSuperChestStarColor};
    float starsSize{DefaultAutomapLabelSize};
};

struct ObjectsOptions {
    bool enabled{true};
    AutomapLabelOptions exitLabels{
        true,
        DefaultExitLabelColor,
        DefaultAutomapLabelSize,
    };
    AutomapLabelOptions waypointLabels{
        true,
        DefaultWaypointLabelColor,
        DefaultAutomapLabelSize,
    };
    AutomapLabelOptions shrineLabels{
        true,
        DefaultShrineLabelColor,
        DefaultAutomapLabelSize,
    };
    ChestOptions chests{};
    SuperChestOptions superChests{};
    AutomapObjectOptions armorRacks{
        true,
        DefaultArmorRackColor,
        DefaultAutomapObjectSize,
    };
    AutomapObjectOptions weaponRacks{
        true,
        DefaultWeaponRackColor,
        DefaultAutomapObjectSize,
    };
};

using CustomLevelTarget = std::variant<std::int32_t, std::string>;

struct NavigationLineOptions {
    bool enabled{true};
    RgbaColor color{};
};

struct CustomLevelLineOptions {
    bool enabled{};
    RgbaColor color{0.78F, 0.36F, 1.0F, 1.0F};
    std::vector<CustomLevelTarget> targets{};
};

struct NavigationOptions {
    float lineThickness{DefaultNavigationLineThickness};
    NavigationLineOptions waypoint{
        true,
        {0.239F, 0.545F, 1.0F, 1.0F},
    };
    NavigationLineOptions progression{
        true,
        {0.34F, 0.88F, 0.24F, 1.0F},
    };
    NavigationLineOptions quests{
        true,
        {1.0F, 0.231F, 0.188F, 1.0F},
    };
    CustomLevelLineOptions customLevels{};
};

struct HudOptions {
    bool mercenaryHealth{};
    bool sessionTimer{};
    bool experienceTracker{};
    bool showWithAutomapOnly{};
};

enum class MenuTheme : std::uint8_t {
    SanctuaryGold,
    Hellfire,
    HoradricSand,
    ArcaneSanctuary,
    TristramMoon,
    KurastJade,
    NecromancerBone,
    HarrogathFrost,
    BloodMoor,
    HighContrast,
};

inline constexpr std::array<MenuTheme, 10> MenuThemes{
    MenuTheme::SanctuaryGold,
    MenuTheme::Hellfire,
    MenuTheme::HoradricSand,
    MenuTheme::ArcaneSanctuary,
    MenuTheme::TristramMoon,
    MenuTheme::KurastJade,
    MenuTheme::NecromancerBone,
    MenuTheme::HarrogathFrost,
    MenuTheme::BloodMoor,
    MenuTheme::HighContrast,
};

inline auto MenuThemeToString(MenuTheme theme) noexcept -> std::string_view {
    switch (theme) {
        case MenuTheme::SanctuaryGold: return "sanctuary_gold";
        case MenuTheme::Hellfire: return "hellfire";
        case MenuTheme::HoradricSand: return "horadric_sand";
        case MenuTheme::ArcaneSanctuary: return "arcane_sanctuary";
        case MenuTheme::TristramMoon: return "tristram_moon";
        case MenuTheme::KurastJade: return "kurast_jade";
        case MenuTheme::NecromancerBone: return "necromancer_bone";
        case MenuTheme::HarrogathFrost: return "harrogath_frost";
        case MenuTheme::BloodMoor: return "blood_moor";
        case MenuTheme::HighContrast: return "high_contrast";
    }
    return "sanctuary_gold";
}

inline auto ParseMenuTheme(std::string_view text) -> MenuTheme {
    for (const auto theme : MenuThemes) {
        if (text == MenuThemeToString(theme)) return theme;
    }
    throw std::runtime_error("Unsupported MapSense menu theme");
}

struct MenuOptions {
    MenuTheme theme{MenuTheme::SanctuaryGold};
    bool showLauncher{true};
    bool startExpanded{};
    bool rememberPosition{true};
    float positionX{0.86F};
    float positionY{0.04F};
};

struct Config {
    bool enabled{true};
    bool diagnostics{};
    OverlayOptions overlay{};
    MonsterOptions monsters{};
    ImmunityOptions immunities{};
    MissileOptions missiles{};
    ObjectsOptions objects{};
    NavigationOptions navigation{};
    HudOptions hud{};
    MenuOptions menu{};
};

inline auto IsAllowedKey(
        std::string_view key,
        std::initializer_list<std::string_view> allowed) noexcept -> bool {
    for (const auto candidate : allowed) {
        if (key == candidate) return true;
    }
    return false;
}

inline void RejectUnknownKeys(
        const toml::table& table,
        std::initializer_list<std::string_view> allowed,
        std::string_view section) {
    for (const auto& [key, unused] : table) {
        (void)unused;
        if (!IsAllowedKey(key.str(), allowed)) {
            throw std::runtime_error(
                "Unknown MapSense key in " + std::string(section)
                + ": " + std::string(key.str()));
        }
    }
}

template <class T>
inline auto ReadOptional(
        const toml::table& table,
        std::string_view key,
        T fallback) -> T {
    const auto* node = table.get(key);
    if (node == nullptr) return fallback;
    if constexpr (std::is_same_v<T, bool>) {
        if (!node->is_boolean()) {
            throw std::runtime_error(
                "MapSense key has the wrong type: " + std::string(key));
        }
    } else if constexpr (std::is_integral_v<T>) {
        if (!node->is_integer()) {
            throw std::runtime_error(
                "MapSense key has the wrong type: " + std::string(key));
        }
    }
    const auto value = node->value<T>();
    if (!value) {
        throw std::runtime_error(
            "MapSense key has the wrong type: " + std::string(key));
    }
    return *value;
}

inline auto ReadOptionalFloat(
        const toml::table& table,
        std::string_view key,
        float fallback,
        float minimum,
        float maximum) -> float {
    const auto* node = table.get(key);
    if (node == nullptr) return fallback;
    double value{};
    if (const auto floating = node->value<double>()) {
        value = *floating;
    } else if (const auto integer = node->value<std::int64_t>()) {
        value = static_cast<double>(*integer);
    } else {
        throw std::runtime_error(
            "MapSense key has the wrong type: " + std::string(key));
    }
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::runtime_error(
            "MapSense key is outside its supported range: "
            + std::string(key));
    }
    return static_cast<float>(value);
}

inline auto ReadOptionalTable(
        const toml::table& root,
        std::string_view key) -> const toml::table* {
    const auto* node = root.get(key);
    if (node == nullptr) return nullptr;
    const auto* table = node->as_table();
    if (table == nullptr) {
        throw std::runtime_error(
            "MapSense section must be a table: " + std::string(key));
    }
    return table;
}

inline auto ReadOptionalMenuTheme(
        const toml::table& table,
        std::string_view key,
        MenuTheme fallback) -> MenuTheme {
    const auto* node = table.get(key);
    if (node == nullptr) return fallback;
    const auto value = node->value<std::string>();
    if (!value) {
        throw std::runtime_error(
            "MapSense menu theme must be a string: " + std::string(key));
    }
    return ParseMenuTheme(*value);
}

inline auto HexNibble(char character) -> std::uint8_t {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
        return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    throw std::runtime_error("MapSense color contains a non-hex character");
}

inline auto ParseColor(std::string_view text) -> RgbaColor {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') {
        throw std::runtime_error(
            "MapSense colors must use #RRGGBB or #RRGGBBAA");
    }
    const auto component = [text](std::size_t offset) {
        return static_cast<std::uint8_t>(
            (HexNibble(text[offset]) << 4U) | HexNibble(text[offset + 1]));
    };
    constexpr float denominator = 255.0F;
    return {
        static_cast<float>(component(1)) / denominator,
        static_cast<float>(component(3)) / denominator,
        static_cast<float>(component(5)) / denominator,
        text.size() == 9
            ? static_cast<float>(component(7)) / denominator
            : 1.0F,
    };
}

inline auto ReadOptionalColor(
        const toml::table& table,
        std::string_view key,
        RgbaColor fallback) -> RgbaColor {
    const auto* node = table.get(key);
    if (node == nullptr) return fallback;
    const auto value = node->value<std::string>();
    if (!value) {
        throw std::runtime_error(
            "MapSense color must be a string: " + std::string(key));
    }
    return ParseColor(*value);
}

inline auto MonsterMarkerShapeToString(
        MonsterMarkerShape shape) noexcept -> std::string_view {
    switch (shape) {
        case MonsterMarkerShape::X:
            return "x";
        case MonsterMarkerShape::PlayerCross:
            return "player_cross";
        case MonsterMarkerShape::Dot:
            return "dot";
    }
    return "player_cross";
}

inline auto ParseMonsterMarkerShape(
        std::string_view text) -> MonsterMarkerShape {
    if (text == "x") return MonsterMarkerShape::X;
    if (text == "player_cross") return MonsterMarkerShape::PlayerCross;
    if (text == "dot") return MonsterMarkerShape::Dot;
    throw std::runtime_error(
        "MapSense marker shapes must use x, player_cross, or dot");
}

inline auto ReadOptionalMonsterMarkerShape(
        const toml::table& table,
        std::string_view key,
        MonsterMarkerShape fallback) -> MonsterMarkerShape {
    const auto* node = table.get(key);
    if (node == nullptr) return fallback;
    const auto value = node->value<std::string>();
    if (!value) {
        throw std::runtime_error(
            "MapSense marker shape must be a string: " + std::string(key));
    }
    return ParseMonsterMarkerShape(*value);
}

inline auto ImmunityDisplayStyleToString(
        ImmunityDisplayStyle style) noexcept -> std::string_view {
    switch (style) {
        case ImmunityDisplayStyle::ColoredI:
            return "colored_i";
        case ImmunityDisplayStyle::SplitHalo:
            return "split_halo";
    }
    return "colored_i";
}

inline auto ParseImmunityDisplayStyle(
        std::string_view text) -> ImmunityDisplayStyle {
    if (text == "colored_i") return ImmunityDisplayStyle::ColoredI;
    if (text == "split_halo") return ImmunityDisplayStyle::SplitHalo;
    throw std::runtime_error(
        "MapSense immunity styles must use colored_i or split_halo");
}

inline auto ReadOptionalImmunityDisplayStyle(
        const toml::table& table,
        std::string_view key,
        ImmunityDisplayStyle fallback) -> ImmunityDisplayStyle {
    const auto* node = table.get(key);
    if (node == nullptr) return fallback;
    const auto value = node->value<std::string>();
    if (!value) {
        throw std::runtime_error(
            "MapSense immunity style must be a string: "
            + std::string(key));
    }
    return ParseImmunityDisplayStyle(*value);
}

[[nodiscard]] constexpr auto IsSameRgbaColor(
        const RgbaColor& left,
        const RgbaColor& right) noexcept -> bool {
    return left.red == right.red
        && left.green == right.green
        && left.blue == right.blue
        && left.alpha == right.alpha;
}

inline auto ReadMonsterMarkerStyle(
        const toml::table& monsters,
        std::string_view key,
        MonsterMarkerStyle fallback,
        bool readShape,
        bool readThickness,
        bool readNames = false) -> MonsterMarkerStyle {
    const auto* style = ReadOptionalTable(monsters, key);
    if (style == nullptr) return fallback;
    if (readShape) {
        fallback.shape = ReadOptionalMonsterMarkerShape(
            *style,
            "shape",
            fallback.shape);
    }
    if (readNames && readThickness
            && fallback.shape != MonsterMarkerShape::Dot) {
        RejectUnknownKeys(
            *style,
            {"shape", "color", "size", "thickness", "show_names", "name_color", "name_size"},
            key);
        fallback.thickness = ReadOptionalFloat(
            *style,
            "thickness",
            fallback.thickness,
            MinimumMonsterMarkerThickness,
            MaximumMonsterMarkerThickness);
    } else if (readNames && readShape) {
        RejectUnknownKeys(
            *style,
            {"shape", "color", "size", "show_names", "name_color", "name_size"},
            key);
    } else if (readNames) {
        RejectUnknownKeys(
            *style,
            {"color", "size", "show_names", "name_color", "name_size"},
            key);
    } else if (readThickness
            && fallback.shape != MonsterMarkerShape::Dot) {
        RejectUnknownKeys(
            *style,
            {"shape", "color", "size", "thickness"},
            key);
        fallback.thickness = ReadOptionalFloat(
            *style,
            "thickness",
            fallback.thickness,
            MinimumMonsterMarkerThickness,
            MaximumMonsterMarkerThickness);
    } else if (readShape) {
        RejectUnknownKeys(*style, {"shape", "color", "size"}, key);
    } else {
        RejectUnknownKeys(*style, {"color", "size"}, key);
    }
    fallback.color = ReadOptionalColor(*style, "color", fallback.color);
    fallback.size = ReadOptionalFloat(
        *style,
        "size",
        fallback.size,
        MinimumMonsterMarkerSize,
        MaximumMonsterMarkerSize);
    if (readNames) {
        fallback.showNames = ReadOptional(
            *style,
            "show_names",
            fallback.showNames);
        fallback.nameColor = ReadOptionalColor(
            *style,
            "name_color",
            fallback.nameColor);
        fallback.nameSize = ReadOptionalFloat(
            *style,
            "name_size",
            fallback.nameSize,
            MinimumAutomapLabelSize,
            MaximumAutomapLabelSize);
    }
    return fallback;
}

inline auto ReadAutomapLabelOptions(
        const toml::table& objects,
        std::string_view key,
        AutomapLabelOptions fallback) -> AutomapLabelOptions {
    const auto* labels = ReadOptionalTable(objects, key);
    if (labels == nullptr) return fallback;
    RejectUnknownKeys(*labels, {"enabled", "color", "size"}, key);
    fallback.enabled = ReadOptional(
        *labels,
        "enabled",
        fallback.enabled);
    fallback.color = ReadOptionalColor(
        *labels,
        "color",
        fallback.color);
    fallback.size = ReadOptionalFloat(
        *labels,
        "size",
        fallback.size,
        MinimumAutomapLabelSize,
        MaximumAutomapLabelSize);
    return fallback;
}

inline auto ReadMissileMarkerStyle(
        const toml::table& missiles,
        std::string_view key,
        MissileMarkerStyle fallback) -> MissileMarkerStyle {
    const auto* style = ReadOptionalTable(missiles, key);
    if (style == nullptr) return fallback;
    RejectUnknownKeys(*style, {"color", "size"}, key);
    fallback.color = ReadOptionalColor(*style, "color", fallback.color);
    fallback.size = ReadOptionalFloat(
        *style,
        "size",
        fallback.size,
        MinimumMissileMarkerSize,
        MaximumMissileMarkerSize);
    return fallback;
}

inline auto ReadMissileOptions(
        const toml::table& root,
        MissileOptions fallback) -> MissileOptions {
    const auto* missiles = ReadOptionalTable(root, "missiles");
    if (missiles == nullptr) return fallback;
    RejectUnknownKeys(
        *missiles,
        {"enabled", "fire", "cold", "lightning", "poison", "physical", "magic"},
        "missiles");
    fallback.enabled = ReadOptional(
        *missiles,
        "enabled",
        fallback.enabled);
    fallback.fire = ReadMissileMarkerStyle(
        *missiles, "fire", fallback.fire);
    fallback.cold = ReadMissileMarkerStyle(
        *missiles, "cold", fallback.cold);
    fallback.lightning = ReadMissileMarkerStyle(
        *missiles, "lightning", fallback.lightning);
    fallback.poison = ReadMissileMarkerStyle(
        *missiles, "poison", fallback.poison);
    fallback.physical = ReadMissileMarkerStyle(
        *missiles, "physical", fallback.physical);
    fallback.magic = ReadMissileMarkerStyle(
        *missiles, "magic", fallback.magic);
    return fallback;
}

inline auto ReadAutomapObjectOptions(
        const toml::table& objects,
        std::string_view key,
        AutomapObjectOptions fallback) -> AutomapObjectOptions {
    const auto* marker = ReadOptionalTable(objects, key);
    if (marker == nullptr) return fallback;
    RejectUnknownKeys(*marker, {"enabled", "color", "size"}, key);
    fallback.enabled = ReadOptional(
        *marker,
        "enabled",
        fallback.enabled);
    fallback.color = ReadOptionalColor(
        *marker,
        "color",
        fallback.color);
    fallback.size = ReadOptionalFloat(
        *marker,
        "size",
        fallback.size,
        MinimumAutomapObjectSize,
        MaximumAutomapObjectSize);
    return fallback;
}

inline auto ReadChestOptions(
        const toml::table& objects,
        ChestOptions fallback,
        std::int64_t schemaVersion) -> ChestOptions {
    const auto* chests = ReadOptionalTable(objects, "chests");
    if (chests == nullptr) return fallback;
    if (schemaVersion >= 12) {
        RejectUnknownKeys(
            *chests,
            {"enabled", "outline_color", "interior_color", "locked_accent_color", "trapped_accent_color", "size"},
            "objects.chests");
    } else {
        RejectUnknownKeys(
            *chests,
            {"enabled", "color", "locked_color", "trapped_color", "size"},
            "objects.chests");
    }
    fallback.enabled = ReadOptional(
        *chests,
        "enabled",
        fallback.enabled);
    if (schemaVersion >= 12) {
        fallback.outlineColor = ReadOptionalColor(
            *chests,
            "outline_color",
            fallback.outlineColor);
        fallback.interiorColor = ReadOptionalColor(
            *chests,
            "interior_color",
            fallback.interiorColor);
        fallback.lockedAccentColor = ReadOptionalColor(
            *chests,
            "locked_accent_color",
            fallback.lockedAccentColor);
        fallback.trappedAccentColor = ReadOptionalColor(
            *chests,
            "trapped_accent_color",
            fallback.trappedAccentColor);
    } else {
        fallback.interiorColor = ReadOptionalColor(
            *chests,
            "color",
            fallback.interiorColor);
        fallback.lockedAccentColor = ReadOptionalColor(
            *chests,
            "locked_color",
            fallback.lockedAccentColor);
        fallback.trappedAccentColor = ReadOptionalColor(
            *chests,
            "trapped_color",
            fallback.trappedAccentColor);
    }
    fallback.size = ReadOptionalFloat(
        *chests,
        "size",
        fallback.size,
        MinimumAutomapObjectSize,
        MaximumAutomapObjectSize);
    return fallback;
}

inline auto ReadSuperChestOptions(
        const toml::table& objects,
        SuperChestOptions fallback,
        std::int64_t schemaVersion) -> SuperChestOptions {
    const auto* chests = ReadOptionalTable(objects, "super_chests");
    if (chests == nullptr) return fallback;
    if (schemaVersion >= 12) {
        RejectUnknownKeys(
            *chests,
            {"enabled", "stars_enabled", "stars_color", "stars_size"},
            "objects.super_chests");
    } else {
        RejectUnknownKeys(
            *chests,
            {"enabled", "color", "size", "stars_enabled", "stars_color", "stars_size"},
            "objects.super_chests");
    }
    fallback.enabled = ReadOptional(
        *chests,
        "enabled",
        fallback.enabled);
    if (schemaVersion < 12) {
        // Schema 12 deliberately gives ordinary and special chests one shared
        // geometry, size and palette. Read the retired values for strict type
        // and range validation, then discard them during migration.
        (void)ReadOptionalColor(
            *chests,
            "color",
            DefaultSuperChestColor);
        (void)ReadOptionalFloat(
            *chests,
            "size",
            DefaultAutomapObjectSize,
            MinimumAutomapObjectSize,
            MaximumAutomapObjectSize);
    }
    fallback.starsEnabled = ReadOptional(
        *chests,
        "stars_enabled",
        fallback.starsEnabled);
    fallback.starsColor = ReadOptionalColor(
        *chests,
        "stars_color",
        fallback.starsColor);
    fallback.starsSize = ReadOptionalFloat(
        *chests,
        "stars_size",
        fallback.starsSize,
        MinimumAutomapLabelSize,
        MaximumAutomapLabelSize);
    return fallback;
}

inline auto ReadObjectsOptions(
        const toml::table& root,
        ObjectsOptions fallback,
        std::int64_t schemaVersion) -> ObjectsOptions {
    const auto* objects = ReadOptionalTable(root, "objects");
    if (objects == nullptr) return fallback;
    if (schemaVersion >= 13) {
        RejectUnknownKeys(
            *objects,
            {"enabled", "exit_labels", "waypoint_labels", "shrine_labels", "chests", "super_chests", "armor_racks", "weapon_racks"},
            "objects");
    } else {
        RejectUnknownKeys(
            *objects,
            {"enabled", "exit_labels", "shrine_labels", "chests", "super_chests", "armor_racks", "weapon_racks"},
            "objects");
    }
    fallback.enabled = ReadOptional(
        *objects,
        "enabled",
        fallback.enabled);
    fallback.exitLabels = ReadAutomapLabelOptions(
        *objects,
        "exit_labels",
        fallback.exitLabels);
    if (schemaVersion >= 13) {
        fallback.waypointLabels = ReadAutomapLabelOptions(
            *objects,
            "waypoint_labels",
            fallback.waypointLabels);
    }
    fallback.shrineLabels = ReadAutomapLabelOptions(
        *objects,
        "shrine_labels",
        fallback.shrineLabels);
    fallback.chests = ReadChestOptions(
        *objects,
        fallback.chests,
        schemaVersion);
    fallback.superChests = ReadSuperChestOptions(
        *objects,
        fallback.superChests,
        schemaVersion);
    fallback.armorRacks = ReadAutomapObjectOptions(
        *objects,
        "armor_racks",
        fallback.armorRacks);
    fallback.weaponRacks = ReadAutomapObjectOptions(
        *objects,
        "weapon_racks",
        fallback.weaponRacks);
    return fallback;
}

inline auto ReadNavigationLineOptions(
        const toml::table& navigation,
        std::string_view key,
        NavigationLineOptions fallback) -> NavigationLineOptions {
    const auto* line = ReadOptionalTable(navigation, key);
    if (line == nullptr) return fallback;
    RejectUnknownKeys(*line, {"enabled", "color"}, key);
    fallback.enabled = ReadOptional(*line, "enabled", fallback.enabled);
    fallback.color = ReadOptionalColor(*line, "color", fallback.color);
    return fallback;
}

inline void ValidateCustomLevelName(std::string_view name) {
    if (name.empty() || name.size() > MaximumCustomLevelNameLength) {
        throw std::runtime_error(
            "MapSense custom level names must contain 1 to 128 characters");
    }
    const auto isWhitespace = [](char character) noexcept {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    };
    if (isWhitespace(name.front()) || isWhitespace(name.back())) {
        throw std::runtime_error(
            "MapSense custom level names cannot start or end with whitespace");
    }
    for (const auto character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU) {
            throw std::runtime_error(
                "MapSense custom level names cannot contain control characters");
        }
    }
}

inline auto ReadCustomLevelTargets(
        const toml::table& customLevels) -> std::vector<CustomLevelTarget> {
    const auto* targetsNode = customLevels.get("targets");
    if (targetsNode == nullptr) return {};
    const auto* targetsArray = targetsNode->as_array();
    if (targetsArray == nullptr) {
        throw std::runtime_error(
            "MapSense navigation.custom_levels.targets must be an array");
    }
    if (targetsArray->size() > MaximumCustomLevelTargets) {
        throw std::runtime_error(
            "MapSense supports at most 128 custom level targets");
    }

    std::vector<CustomLevelTarget> targets;
    targets.reserve(targetsArray->size());
    for (const auto& targetNode : *targetsArray) {
        const auto* target = targetNode.as_table();
        if (target == nullptr) {
            throw std::runtime_error(
                "Each MapSense custom level target must be a table");
        }
        RejectUnknownKeys(*target, {"level_id", "level_name"},
            "navigation.custom_levels.targets");
        const auto hasLevelId = target->contains("level_id");
        const auto hasLevelName = target->contains("level_name");
        if (hasLevelId == hasLevelName) {
            throw std::runtime_error(
                "Each MapSense custom level target must contain exactly one "
                "of level_id or level_name");
        }

        CustomLevelTarget parsedTarget;
        if (hasLevelId) {
            const auto* levelIdNode = target->get("level_id");
            if (levelIdNode == nullptr || !levelIdNode->is_integer()) {
                throw std::runtime_error(
                    "MapSense custom level_id must be an integer");
            }
            const auto levelId = levelIdNode->value<std::int32_t>();
            if (!levelId || *levelId < MinimumCustomLevelId
                    || *levelId > MaximumCustomLevelId) {
                throw std::runtime_error(
                    "MapSense custom level_id is outside its supported range");
            }
            parsedTarget = *levelId;
        } else {
            const auto* levelNameNode = target->get("level_name");
            const auto levelName = levelNameNode != nullptr
                ? levelNameNode->value<std::string>()
                : std::optional<std::string>{};
            if (!levelName) {
                throw std::runtime_error(
                    "MapSense custom level_name must be a string");
            }
            ValidateCustomLevelName(*levelName);
            parsedTarget = *levelName;
        }

        if (std::find(targets.begin(), targets.end(), parsedTarget)
                != targets.end()) {
            throw std::runtime_error(
                "MapSense custom level targets cannot contain duplicates");
        }
        targets.push_back(std::move(parsedTarget));
    }
    return targets;
}

inline auto ReadCustomLevelLineOptions(
        const toml::table& navigation,
        CustomLevelLineOptions fallback) -> CustomLevelLineOptions {
    const auto* customLevels = ReadOptionalTable(
        navigation,
        "custom_levels");
    if (customLevels == nullptr) return fallback;
    RejectUnknownKeys(
        *customLevels,
        {"enabled", "color", "targets"},
        "navigation.custom_levels");
    fallback.enabled = ReadOptional(
        *customLevels,
        "enabled",
        fallback.enabled);
    fallback.color = ReadOptionalColor(
        *customLevels,
        "color",
        fallback.color);
    fallback.targets = ReadCustomLevelTargets(*customLevels);
    return fallback;
}

inline auto ParseConfig(const toml::table& root) -> Config {
    RejectUnknownKeys(
        root,
        {"schema_version", "general", "overlay", "monsters", "immunities", "missiles", "objects", "navigation", "hud", "menu", "diagnostics"},
        "root");

    const auto* schemaNode = root.get("schema_version");
    const auto schemaVersion = schemaNode != nullptr && schemaNode->is_integer()
        ? schemaNode->value<std::int64_t>()
        : std::optional<std::int64_t>{};
    if (!schemaVersion) {
        throw std::runtime_error(
            "MapSense schema_version is required and must be an integer");
    }
    if (*schemaVersion < 1
            || *schemaVersion > CurrentConfigSchemaVersion) {
        throw std::runtime_error("Unsupported MapSense schema_version");
    }
    if (*schemaVersion < 6 && root.contains("navigation")) {
        throw std::runtime_error(
            "MapSense navigation settings require schema_version 6");
    }
    if (*schemaVersion < 10 && root.contains("objects")) {
        throw std::runtime_error(
            "MapSense object settings require schema_version 10");
    }
    if (*schemaVersion < 15 && root.contains("missiles")) {
        throw std::runtime_error(
            "MapSense missile settings require schema_version 15");
    }

    Config config{};
    auto legacyFeaturesEnabled = true;
    auto legacyOverlayEnabled = true;
    if (*schemaVersion < 4) {
        // Legacy schemas used the hollow X exclusively. Preserve that
        // appearance while migrating into schema 4.
        config.monsters.normal.shape = MonsterMarkerShape::X;
        config.monsters.minion.shape = MonsterMarkerShape::X;
        config.monsters.champion.shape = MonsterMarkerShape::X;
        config.monsters.unique.shape = MonsterMarkerShape::X;
        config.monsters.superUniqueBoss.shape = MonsterMarkerShape::X;
    }
    if (const auto* general = ReadOptionalTable(root, "general")) {
        if (*schemaVersion >= 16) {
            RejectUnknownKeys(*general, {"enabled"}, "general");
        } else if (*schemaVersion >= 12) {
            RejectUnknownKeys(
                *general,
                {"enabled", "features_enabled"},
                "general");
        } else {
            RejectUnknownKeys(*general, {"enabled"}, "general");
        }
        config.enabled = ReadOptional(*general, "enabled", config.enabled);
        if (*schemaVersion >= 12 && *schemaVersion < 16) {
            legacyFeaturesEnabled = ReadOptional(
                *general,
                "features_enabled",
                legacyFeaturesEnabled);
        }
    }
    if (*schemaVersion < 16) {
        config.enabled = config.enabled && legacyFeaturesEnabled;
    }
    if (const auto* overlay = ReadOptionalTable(root, "overlay")) {
        if (*schemaVersion >= 16) {
            RejectUnknownKeys(
                *overlay,
                {"diagnostic_preview", "follow_native_automap", "start_menu_open", "opacity", "scale", "frame_rate"},
                "overlay");
        } else {
            RejectUnknownKeys(
                *overlay,
                {"enabled", "diagnostic_preview", "follow_native_automap", "start_menu_open", "opacity", "scale", "frame_rate"},
                "overlay");
            legacyOverlayEnabled = ReadOptional(
                *overlay, "enabled", legacyOverlayEnabled);
        }
        config.overlay.diagnosticPreview = ReadOptional(
            *overlay, "diagnostic_preview", config.overlay.diagnosticPreview);
        config.overlay.followNativeAutomap = ReadOptional(
            *overlay, "follow_native_automap", config.overlay.followNativeAutomap);
        config.overlay.startMenuOpen = ReadOptional(
            *overlay, "start_menu_open", config.overlay.startMenuOpen);
        config.overlay.opacity = ReadOptionalFloat(
            *overlay, "opacity", config.overlay.opacity, 0.10F, 1.0F);
        config.overlay.scale = ReadOptionalFloat(
            *overlay, "scale", config.overlay.scale, 0.50F, 2.0F);
        config.overlay.frameRate = ReadOptional<std::int32_t>(
            *overlay, "frame_rate", config.overlay.frameRate);
        if (config.overlay.frameRate < 15 || config.overlay.frameRate > 240) {
            throw std::runtime_error(
                "MapSense key is outside its supported range: frame_rate");
        }
    }
    if (const auto* monsters = ReadOptionalTable(root, "monsters")) {
        if (*schemaVersion == 1 || *schemaVersion == 2) {
            RejectUnknownKeys(
                *monsters,
                {"detection_radius", "normal", "minion", "champion", "unique", "super_unique", "marker_size"},
                "monsters");

            // Schema 1/2 visibility switches are intentionally ignored during
            // migration: schema 4 always displays every monster category.
            (void)ReadOptional(*monsters, "normal", true);
            (void)ReadOptional(*monsters, "minion", true);
            (void)ReadOptional(*monsters, "champion", true);
            (void)ReadOptional(*monsters, "unique", true);
            (void)ReadOptional(*monsters, "super_unique", true);
            const auto legacySize = ReadOptionalFloat(
                *monsters,
                "marker_size",
                config.monsters.normal.size,
                MinimumMonsterMarkerSize,
                MaximumMonsterMarkerSize);
            if (monsters->contains("marker_size")) {
                config.monsters.normal.size = legacySize;
                config.monsters.minion.size = legacySize;
                config.monsters.champion.size = legacySize;
                config.monsters.unique.size = legacySize;
                config.monsters.superUniqueBoss.size = legacySize;
            }
        } else {
            if (*schemaVersion >= 16) {
                RejectUnknownKeys(
                    *monsters,
                    {"enabled", "detection_radius", "normal", "minion", "champion", "unique", "super_unique_boss"},
                    "monsters");
                config.monsters.enabled = ReadOptional(
                    *monsters,
                    "enabled",
                    config.monsters.enabled);
            } else {
                RejectUnknownKeys(
                    *monsters,
                    {"detection_radius", "normal", "minion", "champion", "unique", "super_unique_boss"},
                    "monsters");
            }
            // detection_radius is a retired compatibility key. Keep accepting
            // it in every supported schema, but never read, validate, migrate,
            // or persist its value.
            config.monsters.normal = ReadMonsterMarkerStyle(
                *monsters,
                "normal",
                config.monsters.normal,
                *schemaVersion >= 4,
                *schemaVersion >= 9);
            config.monsters.minion = ReadMonsterMarkerStyle(
                *monsters,
                "minion",
                config.monsters.minion,
                *schemaVersion >= 4,
                *schemaVersion >= 9);
            config.monsters.champion = ReadMonsterMarkerStyle(
                *monsters,
                "champion",
                config.monsters.champion,
                *schemaVersion >= 4,
                *schemaVersion >= 9);
            config.monsters.unique = ReadMonsterMarkerStyle(
                *monsters,
                "unique",
                config.monsters.unique,
                *schemaVersion >= 4,
                *schemaVersion >= 9);
            config.monsters.superUniqueBoss = ReadMonsterMarkerStyle(
                *monsters,
                "super_unique_boss",
                config.monsters.superUniqueBoss,
                *schemaVersion >= 4,
                *schemaVersion >= 9,
                *schemaVersion >= 10);
        }
    }
    if (const auto* immunities = ReadOptionalTable(root, "immunities")) {
        if (*schemaVersion >= 5) {
            RejectUnknownKeys(
                *immunities,
                {"enabled", "style", "indicator_size", "halo_thickness", "physical", "fire", "cold", "lightning", "poison", "magic"},
                "immunities");
        } else {
            RejectUnknownKeys(
                *immunities,
                {"enabled", "physical", "fire", "cold", "lightning", "poison", "magic"},
                "immunities");
        }
        config.immunities.enabled = ReadOptional(
            *immunities, "enabled", config.immunities.enabled);
        if (*schemaVersion >= 5) {
            config.immunities.style = ReadOptionalImmunityDisplayStyle(
                *immunities,
                "style",
                config.immunities.style);
            config.immunities.indicatorSize = ReadOptionalFloat(
                *immunities,
                "indicator_size",
                config.immunities.indicatorSize,
                MinimumImmunityIndicatorSize,
                MaximumImmunityIndicatorSize);
            config.immunities.haloThickness = ReadOptionalFloat(
                *immunities,
                "halo_thickness",
                config.immunities.haloThickness,
                MinimumImmunityHaloThickness,
                MaximumImmunityHaloThickness);
        }
        config.immunities.physical = ReadOptionalColor(
            *immunities, "physical", config.immunities.physical);
        config.immunities.fire = ReadOptionalColor(
            *immunities, "fire", config.immunities.fire);
        config.immunities.cold = ReadOptionalColor(
            *immunities, "cold", config.immunities.cold);
        config.immunities.lightning = ReadOptionalColor(
            *immunities, "lightning", config.immunities.lightning);
        config.immunities.poison = ReadOptionalColor(
            *immunities, "poison", config.immunities.poison);
        config.immunities.magic = ReadOptionalColor(
            *immunities, "magic", config.immunities.magic);

        // Schema 1-4 shipped Physical as neutral grey. Upgrade only that exact
        // legacy default; any user-selected color remains untouched.
        if (*schemaVersion < 5
                && IsSameRgbaColor(
                    config.immunities.physical,
                    LegacyPhysicalImmunityColor)) {
            config.immunities.physical = DefaultPhysicalImmunityColor;
        }
    }
    if (*schemaVersion >= 10) {
        config.objects = ReadObjectsOptions(
            root,
            std::move(config.objects),
            *schemaVersion);
    }
    if (*schemaVersion < 11) {
        // Schema 11 unifies the two player-facing label yellows and replaces
        // the misleading green locked-chest signal with aged amber. Upgrade
        // only exact shipped defaults so every customized color survives.
        if (IsSameRgbaColor(
                config.objects.shrineLabels.color,
                LegacyShrineLabelColor)) {
            config.objects.shrineLabels.color = DefaultShrineLabelColor;
        }
        if (IsSameRgbaColor(
                config.objects.chests.interiorColor,
                LegacyAutomapObjectColor)) {
            config.objects.chests.interiorColor = DefaultChestInteriorColor;
        }
        if (IsSameRgbaColor(
                config.objects.chests.lockedAccentColor,
                LegacyLockedChestColor)) {
            config.objects.chests.lockedAccentColor =
                LegacyAmberLockedChestColor;
        }
    }
    if (*schemaVersion < 12) {
        // Schema 12 separates structural outline from the translucent fill and
        // aligns special-chest stars with the native D2 gold. Upgrade only the
        // exact schema-11 shipped defaults; customized legacy values survive.
        if (IsSameRgbaColor(
                config.objects.chests.interiorColor,
                DefaultAutomapObjectColor)) {
            config.objects.chests.interiorColor = DefaultChestInteriorColor;
        }
        if (IsSameRgbaColor(
                config.objects.superChests.starsColor,
                RgbaColor{1.0F, 1.0F, 1.0F, 1.0F})) {
            config.objects.superChests.starsColor =
                DefaultSuperChestStarColor;
        }
    }
    if (*schemaVersion < 14
        && IsSameRgbaColor(
            config.objects.chests.lockedAccentColor,
            LegacyAmberLockedChestColor)) {
        // Schema 14 preserves PrimeMH's exact chest pixels and reserves aqua
        // exclusively for the separate locked-state padlock. Upgrade only the
        // shipped amber value so customized legacy lock colors survive.
        config.objects.chests.lockedAccentColor = DefaultLockedChestColor;
    }
    if (*schemaVersion >= 15) {
        config.missiles = ReadMissileOptions(
            root,
            std::move(config.missiles));
    }
    if (const auto* navigation = ReadOptionalTable(root, "navigation")) {
        RejectUnknownKeys(
            *navigation,
            {"line_thickness", "waypoint", "progression", "quests", "custom_levels"},
            "navigation");
        config.navigation.lineThickness = ReadOptionalFloat(
            *navigation,
            "line_thickness",
            config.navigation.lineThickness,
            MinimumNavigationLineThickness,
            MaximumNavigationLineThickness);
        config.navigation.waypoint = ReadNavigationLineOptions(
            *navigation,
            "waypoint",
            config.navigation.waypoint);
        config.navigation.progression = ReadNavigationLineOptions(
            *navigation,
            "progression",
            config.navigation.progression);
        config.navigation.quests = ReadNavigationLineOptions(
            *navigation,
            "quests",
            config.navigation.quests);
        config.navigation.customLevels = ReadCustomLevelLineOptions(
            *navigation,
            std::move(config.navigation.customLevels));
    }
    if (*schemaVersion < 8) {
        // Quest lines were reserved and hidden through schema 7. Enable the
        // first real quest adapter once during migration; schema 8 keeps any
        // explicit user choice made in the menu.
        config.navigation.quests.enabled = true;
    }
    if (const auto* hud = ReadOptionalTable(root, "hud")) {
        RejectUnknownKeys(
            *hud,
            {"mercenary_health", "session_timer", "experience_tracker", "show_with_automap_only"},
            "hud");
        config.hud.mercenaryHealth = ReadOptional(
            *hud, "mercenary_health", config.hud.mercenaryHealth);
        config.hud.sessionTimer = ReadOptional(
            *hud, "session_timer", config.hud.sessionTimer);
        config.hud.experienceTracker = ReadOptional(
            *hud, "experience_tracker", config.hud.experienceTracker);
        config.hud.showWithAutomapOnly = ReadOptional(
            *hud, "show_with_automap_only", config.hud.showWithAutomapOnly);
    }
    if (const auto* menu = ReadOptionalTable(root, "menu")) {
        if (*schemaVersion >= 16) {
            RejectUnknownKeys(
                *menu,
                {"theme", "show_launcher", "start_expanded", "remember_position", "position_x", "position_y"},
                "menu");
            config.menu.theme = ReadOptionalMenuTheme(
                *menu,
                "theme",
                config.menu.theme);
        } else {
            RejectUnknownKeys(
                *menu,
                {"show_launcher", "start_expanded", "remember_position", "position_x", "position_y"},
                "menu");
        }
        config.menu.showLauncher = ReadOptional(
            *menu, "show_launcher", config.menu.showLauncher);
        config.menu.startExpanded = ReadOptional(
            *menu, "start_expanded", config.menu.startExpanded);
        config.menu.rememberPosition = ReadOptional(
            *menu, "remember_position", config.menu.rememberPosition);
        config.menu.positionX = ReadOptionalFloat(
            *menu, "position_x", config.menu.positionX, 0.0F, 1.0F);
        config.menu.positionY = ReadOptionalFloat(
            *menu, "position_y", config.menu.positionY, 0.0F, 1.0F);
    }
    if (const auto* diagnostics = ReadOptionalTable(root, "diagnostics")) {
        RejectUnknownKeys(*diagnostics, {"enabled"}, "diagnostics");
        config.diagnostics = ReadOptional(
            *diagnostics, "enabled", config.diagnostics);
    }
    if (*schemaVersion < 16 && !legacyOverlayEnabled) {
        // The removed overlay master used to suppress every MapSense-owned
        // automap primitive. Preserve that effective legacy state by turning
        // off the individual feature families during the one-way migration.
        config.overlay.diagnosticPreview = false;
        config.monsters.enabled = false;
        config.immunities.enabled = false;
        config.missiles.enabled = false;
        config.objects.enabled = false;
        config.navigation.waypoint.enabled = false;
        config.navigation.progression.enabled = false;
        config.navigation.quests.enabled = false;
        config.navigation.customLevels.enabled = false;
    }
    return config;
}

inline auto ParseConfig(std::string_view text) -> Config {
    return ParseConfig(toml::parse(text));
}

inline auto ColorToHex(RgbaColor color) -> std::string {
    const auto channel = [](float value) {
        return static_cast<unsigned int>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    std::ostringstream output;
    output << '#' << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << channel(color.red)
           << std::setw(2) << channel(color.green)
           << std::setw(2) << channel(color.blue)
           << std::setw(2) << channel(color.alpha);
    return output.str();
}

inline auto EscapeTomlBasicString(std::string_view text) -> std::string {
    std::string escaped;
    escaped.reserve(text.size());
    for (const auto character : text) {
        switch (character) {
            case '\b':
                escaped += "\\b";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            default:
                escaped += character;
                break;
        }
    }
    return escaped;
}

inline void SerializeCustomLevelTargets(
        std::ostringstream& output,
        const std::vector<CustomLevelTarget>& targets) {
    output << "targets = [\n";
    for (const auto& target : targets) {
        output << "  { ";
        if (const auto* levelId = std::get_if<std::int32_t>(&target)) {
            output << "level_id = " << *levelId;
        } else {
            const auto& levelName = std::get<std::string>(target);
            output << "level_name = \""
                   << EscapeTomlBasicString(levelName)
                   << "\"";
        }
        output << " },\n";
    }
    output << "]\n";
}

inline auto SerializeConfig(const Config& config) -> std::string {
    std::ostringstream output;
    output << std::boolalpha << std::fixed << std::setprecision(2);
    output
        << "# RuffnecKk MapSense configuration.\n"
        << "# Changes made in the MapSense menu are saved here.\n\n"
        << "schema_version = " << CurrentConfigSchemaVersion << "\n\n"
        << "[general]\n"
        << "enabled = " << config.enabled << "\n\n"
        << "[overlay]\n"
        << "diagnostic_preview = " << config.overlay.diagnosticPreview << "\n"
        << "follow_native_automap = " << config.overlay.followNativeAutomap << "\n"
        << "opacity = " << config.overlay.opacity << "\n"
        << "scale = " << config.overlay.scale << "\n"
        << "frame_rate = " << config.overlay.frameRate << "\n\n"
        << "[monsters]\n"
        << "enabled = " << config.monsters.enabled << "\n\n"
        << "[monsters.normal]\n"
        << "shape = \"" << MonsterMarkerShapeToString(config.monsters.normal.shape)
        << "\"\n"
        << "color = \"" << ColorToHex(config.monsters.normal.color) << "\"\n"
        << "size = " << config.monsters.normal.size << "\n";
    if (config.monsters.normal.shape != MonsterMarkerShape::Dot) {
        output << "thickness = " << config.monsters.normal.thickness << "\n";
    }
    output
        << "\n"
        << "[monsters.minion]\n"
        << "shape = \"" << MonsterMarkerShapeToString(config.monsters.minion.shape)
        << "\"\n"
        << "color = \"" << ColorToHex(config.monsters.minion.color) << "\"\n"
        << "size = " << config.monsters.minion.size << "\n";
    if (config.monsters.minion.shape != MonsterMarkerShape::Dot) {
        output << "thickness = " << config.monsters.minion.thickness << "\n";
    }
    output
        << "\n"
        << "[monsters.champion]\n"
        << "shape = \"" << MonsterMarkerShapeToString(config.monsters.champion.shape)
        << "\"\n"
        << "color = \"" << ColorToHex(config.monsters.champion.color) << "\"\n"
        << "size = " << config.monsters.champion.size << "\n";
    if (config.monsters.champion.shape != MonsterMarkerShape::Dot) {
        output << "thickness = " << config.monsters.champion.thickness << "\n";
    }
    output
        << "\n"
        << "[monsters.unique]\n"
        << "shape = \"" << MonsterMarkerShapeToString(config.monsters.unique.shape)
        << "\"\n"
        << "color = \"" << ColorToHex(config.monsters.unique.color) << "\"\n"
        << "size = " << config.monsters.unique.size << "\n";
    if (config.monsters.unique.shape != MonsterMarkerShape::Dot) {
        output << "thickness = " << config.monsters.unique.thickness << "\n";
    }
    output
        << "\n"
        << "[monsters.super_unique_boss]\n"
        << "shape = \""
        << MonsterMarkerShapeToString(config.monsters.superUniqueBoss.shape)
        << "\"\n"
        << "color = \""
        << ColorToHex(config.monsters.superUniqueBoss.color) << "\"\n"
        << "size = " << config.monsters.superUniqueBoss.size << "\n";
    if (config.monsters.superUniqueBoss.shape != MonsterMarkerShape::Dot) {
        output << "thickness = "
               << config.monsters.superUniqueBoss.thickness << "\n";
    }
    output
        << "show_names = "
        << config.monsters.superUniqueBoss.showNames << "\n"
        << "name_color = \""
        << ColorToHex(config.monsters.superUniqueBoss.nameColor) << "\"\n"
        << "name_size = "
        << config.monsters.superUniqueBoss.nameSize << "\n"
        << "\n"
        << "[immunities]\n"
        << "enabled = " << config.immunities.enabled << "\n"
        << "style = \""
        << ImmunityDisplayStyleToString(config.immunities.style)
        << "\"\n"
        << "indicator_size = " << config.immunities.indicatorSize << "\n"
        << "halo_thickness = " << config.immunities.haloThickness << "\n"
        << "physical = \"" << ColorToHex(config.immunities.physical) << "\"\n"
        << "fire = \"" << ColorToHex(config.immunities.fire) << "\"\n"
        << "cold = \"" << ColorToHex(config.immunities.cold) << "\"\n"
        << "lightning = \"" << ColorToHex(config.immunities.lightning) << "\"\n"
        << "poison = \"" << ColorToHex(config.immunities.poison) << "\"\n"
        << "magic = \"" << ColorToHex(config.immunities.magic) << "\"\n\n"
        << "[missiles]\n"
        << "enabled = " << config.missiles.enabled << "\n\n"
        << "[missiles.fire]\n"
        << "color = \"" << ColorToHex(config.missiles.fire.color) << "\"\n"
        << "size = " << config.missiles.fire.size << "\n\n"
        << "[missiles.cold]\n"
        << "color = \"" << ColorToHex(config.missiles.cold.color) << "\"\n"
        << "size = " << config.missiles.cold.size << "\n\n"
        << "[missiles.lightning]\n"
        << "color = \"" << ColorToHex(config.missiles.lightning.color)
        << "\"\n"
        << "size = " << config.missiles.lightning.size << "\n\n"
        << "[missiles.poison]\n"
        << "color = \"" << ColorToHex(config.missiles.poison.color) << "\"\n"
        << "size = " << config.missiles.poison.size << "\n\n"
        << "[missiles.physical]\n"
        << "color = \"" << ColorToHex(config.missiles.physical.color)
        << "\"\n"
        << "size = " << config.missiles.physical.size << "\n\n"
        << "[missiles.magic]\n"
        << "color = \"" << ColorToHex(config.missiles.magic.color) << "\"\n"
        << "size = " << config.missiles.magic.size << "\n\n"
        << "[objects]\n"
        << "enabled = " << config.objects.enabled << "\n\n"
        << "[objects.exit_labels]\n"
        << "enabled = " << config.objects.exitLabels.enabled << "\n"
        << "color = \"" << ColorToHex(config.objects.exitLabels.color)
        << "\"\n"
        << "size = " << config.objects.exitLabels.size << "\n\n"
        << "[objects.waypoint_labels]\n"
        << "enabled = " << config.objects.waypointLabels.enabled << "\n"
        << "color = \"" << ColorToHex(config.objects.waypointLabels.color)
        << "\"\n"
        << "size = " << config.objects.waypointLabels.size << "\n\n"
        << "[objects.shrine_labels]\n"
        << "enabled = " << config.objects.shrineLabels.enabled << "\n"
        << "color = \"" << ColorToHex(config.objects.shrineLabels.color)
        << "\"\n"
        << "size = " << config.objects.shrineLabels.size << "\n\n"
        << "[objects.chests]\n"
        << "enabled = " << config.objects.chests.enabled << "\n"
        << "outline_color = \""
        << ColorToHex(config.objects.chests.outlineColor)
        << "\"\n"
        << "interior_color = \""
        << ColorToHex(config.objects.chests.interiorColor) << "\"\n"
        << "locked_accent_color = \""
        << ColorToHex(config.objects.chests.lockedAccentColor) << "\"\n"
        << "trapped_accent_color = \""
        << ColorToHex(config.objects.chests.trappedAccentColor) << "\"\n"
        << "size = " << config.objects.chests.size << "\n\n"
        << "[objects.super_chests]\n"
        << "enabled = " << config.objects.superChests.enabled << "\n"
        << "stars_enabled = "
        << config.objects.superChests.starsEnabled << "\n"
        << "stars_color = \""
        << ColorToHex(config.objects.superChests.starsColor) << "\"\n"
        << "stars_size = "
        << config.objects.superChests.starsSize << "\n\n"
        << "[objects.armor_racks]\n"
        << "enabled = " << config.objects.armorRacks.enabled << "\n"
        << "color = \"" << ColorToHex(config.objects.armorRacks.color)
        << "\"\n"
        << "size = " << config.objects.armorRacks.size << "\n\n"
        << "[objects.weapon_racks]\n"
        << "enabled = " << config.objects.weaponRacks.enabled << "\n"
        << "color = \"" << ColorToHex(config.objects.weaponRacks.color)
        << "\"\n"
        << "size = " << config.objects.weaponRacks.size << "\n\n"
        << "[navigation]\n"
        << "line_thickness = " << config.navigation.lineThickness << "\n\n"
        << "[navigation.waypoint]\n"
        << "enabled = " << config.navigation.waypoint.enabled << "\n"
        << "color = \"" << ColorToHex(config.navigation.waypoint.color)
        << "\"\n\n"
        << "[navigation.progression]\n"
        << "enabled = " << config.navigation.progression.enabled << "\n"
        << "color = \"" << ColorToHex(config.navigation.progression.color)
        << "\"\n\n"
        << "[navigation.quests]\n"
        << "enabled = " << config.navigation.quests.enabled << "\n"
        << "color = \"" << ColorToHex(config.navigation.quests.color)
        << "\"\n\n"
        << "[navigation.custom_levels]\n"
        << "enabled = " << config.navigation.customLevels.enabled << "\n"
        << "color = \"" << ColorToHex(config.navigation.customLevels.color)
        << "\"\n"
        << "# Edit only this destination list manually. Each entry must use\n"
        << "# exactly one level_id or level_name.\n";
    SerializeCustomLevelTargets(
        output,
        config.navigation.customLevels.targets);
    output
        << "\n"
        << "[menu]\n"
        << "theme = \"" << MenuThemeToString(config.menu.theme) << "\"\n"
        << "show_launcher = " << config.menu.showLauncher << "\n"
        << "start_expanded = " << config.menu.startExpanded << "\n"
        << "remember_position = " << config.menu.rememberPosition << "\n"
        << "position_x = " << config.menu.positionX << "\n"
        << "position_y = " << config.menu.positionY << "\n\n"
        << "[diagnostics]\n"
        << "enabled = " << config.diagnostics << "\n";
    return output.str();
}

} // namespace RuffnecKk::MapSense
