#pragma once

#include "mapsense_config.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace RuffnecKk::MapSense {

inline constexpr std::uint32_t NativeAutomapVirtualKey = 0x09U;

// Tab belongs exclusively to D2R's native automap. Even if a user assigns it
// to any MapSense action, the callback must ignore it so the game receives the
// key and no reveal or settings action runs.
[[nodiscard]] constexpr auto MayTriggerMapSenseActionForVirtualKey(
        std::uint32_t virtualKey) noexcept -> bool {
    return virtualKey != NativeAutomapVirtualKey;
}

enum class NativeSettingsTab : std::uint8_t {
    Map,
    Monsters,
    Navigation,
    Projectiles,
    System,
};

struct NativeSettingsTabDescriptor {
    NativeSettingsTab tab;
    std::string_view label;
    bool hasPersistentSettings;
};

inline constexpr std::array<NativeSettingsTabDescriptor, 5> NativeSettingsTabs{{
    {NativeSettingsTab::Map, "Map", true},
    {NativeSettingsTab::Monsters, "Monsters", true},
    {NativeSettingsTab::Navigation, "Navigation", false},
    {NativeSettingsTab::Projectiles, "Projectiles", false},
    {NativeSettingsTab::System, "System", true},
}};

enum class ToggleKey : std::uint8_t {
    MapSenseEnabled,
    MonstersEnabled,
    ImmunitiesEnabled,
    DiagnosticPreview,
    DiagnosticsEnabled,
};

inline constexpr std::array<ToggleKey, 5> NativeSettingsToggleKeys{{
    ToggleKey::MapSenseEnabled,
    ToggleKey::MonstersEnabled,
    ToggleKey::ImmunitiesEnabled,
    ToggleKey::DiagnosticPreview,
    ToggleKey::DiagnosticsEnabled,
}};

[[nodiscard]] constexpr auto TabForToggle(ToggleKey key) noexcept
        -> NativeSettingsTab {
    switch (key) {
        case ToggleKey::MapSenseEnabled:
            return NativeSettingsTab::Map;
        case ToggleKey::MonstersEnabled:
        case ToggleKey::ImmunitiesEnabled:
            return NativeSettingsTab::Monsters;
        case ToggleKey::DiagnosticPreview:
        case ToggleKey::DiagnosticsEnabled:
            return NativeSettingsTab::System;
    }
    return NativeSettingsTab::System;
}

[[nodiscard]] constexpr auto ToggleLabel(ToggleKey key) noexcept
        -> std::string_view {
    switch (key) {
        case ToggleKey::MapSenseEnabled:
            return "Enable MapSense";
        case ToggleKey::MonstersEnabled:
            return "Show monsters";
        case ToggleKey::ImmunitiesEnabled:
            return "Show immunities";
        case ToggleKey::DiagnosticPreview:
            return "Show diagnostic preview";
        case ToggleKey::DiagnosticsEnabled:
            return "Enable diagnostics";
    }
    return {};
}

[[nodiscard]] inline auto ReadToggle(
        const Config& config,
        ToggleKey key) noexcept -> bool {
    switch (key) {
        case ToggleKey::MapSenseEnabled:
            return config.enabled;
        case ToggleKey::MonstersEnabled:
            return config.monsters.enabled;
        case ToggleKey::ImmunitiesEnabled:
            return config.immunities.enabled;
        case ToggleKey::DiagnosticPreview:
            return config.overlay.diagnosticPreview;
        case ToggleKey::DiagnosticsEnabled:
            return config.diagnostics;
    }
    return false;
}

inline void WriteToggle(Config& config, ToggleKey key, bool value) noexcept {
    switch (key) {
        case ToggleKey::MapSenseEnabled:
            config.enabled = value;
            return;
        case ToggleKey::MonstersEnabled:
            config.monsters.enabled = value;
            return;
        case ToggleKey::ImmunitiesEnabled:
            config.immunities.enabled = value;
            return;
        case ToggleKey::DiagnosticPreview:
            config.overlay.diagnosticPreview = value;
            return;
        case ToggleKey::DiagnosticsEnabled:
            config.diagnostics = value;
            return;
    }
}

enum class OverlayOpacityChoice : std::uint8_t {
    Low,
    Medium,
    High,
    NearOpaque,
    Opaque,
};

inline constexpr std::array<float, 5> OverlayOpacityValues{
    0.25F,
    0.50F,
    0.75F,
    0.90F,
    1.00F,
};

[[nodiscard]] constexpr auto OverlayOpacityValue(
        OverlayOpacityChoice choice) noexcept -> float {
    const auto index = static_cast<std::size_t>(choice);
    return index < OverlayOpacityValues.size()
        ? OverlayOpacityValues[index]
        : OverlayOpacityValues[
            static_cast<std::size_t>(OverlayOpacityChoice::NearOpaque)];
}

[[nodiscard]] constexpr auto OverlayOpacityLabel(
        OverlayOpacityChoice choice) noexcept -> std::string_view {
    switch (choice) {
        case OverlayOpacityChoice::Low:
            return "25%";
        case OverlayOpacityChoice::Medium:
            return "50%";
        case OverlayOpacityChoice::High:
            return "75%";
        case OverlayOpacityChoice::NearOpaque:
            return "90%";
        case OverlayOpacityChoice::Opaque:
            return "100%";
    }
    return {};
}

[[nodiscard]] inline auto NearestOverlayOpacityChoice(float value) noexcept
        -> OverlayOpacityChoice {
    if (!std::isfinite(value)) {
        return OverlayOpacityChoice::NearOpaque;
    }
    std::size_t nearest{};
    auto smallestDistance = std::fabs(value - OverlayOpacityValues.front());
    for (std::size_t index = 1; index < OverlayOpacityValues.size(); ++index) {
        const auto distance = std::fabs(value - OverlayOpacityValues[index]);
        if (distance < smallestDistance) {
            nearest = index;
            smallestDistance = distance;
        }
    }
    return static_cast<OverlayOpacityChoice>(nearest);
}

inline void SetOverlayOpacity(
        Config& config,
        OverlayOpacityChoice choice) noexcept {
    config.overlay.opacity = OverlayOpacityValue(choice);
}

enum class ImmunityDisplayMode : std::uint8_t {
    Off,
    ColoredI,
    SplitHalo,
};

inline constexpr std::array<ImmunityDisplayMode, 3> ImmunityDisplayModes{
    ImmunityDisplayMode::Off,
    ImmunityDisplayMode::ColoredI,
    ImmunityDisplayMode::SplitHalo,
};

[[nodiscard]] constexpr auto ImmunityDisplayModeLabel(
        ImmunityDisplayMode mode) noexcept -> std::string_view {
    switch (mode) {
        case ImmunityDisplayMode::Off:
            return "Off";
        case ImmunityDisplayMode::ColoredI:
            return "Colored i";
        case ImmunityDisplayMode::SplitHalo:
            return "Split halo";
    }
    return {};
}

[[nodiscard]] inline auto ReadImmunityDisplayMode(
        const Config& config) noexcept -> ImmunityDisplayMode {
    if (!config.immunities.enabled) return ImmunityDisplayMode::Off;
    return config.immunities.style == ImmunityDisplayStyle::SplitHalo
        ? ImmunityDisplayMode::SplitHalo
        : ImmunityDisplayMode::ColoredI;
}

inline void WriteImmunityDisplayMode(
        Config& config,
        ImmunityDisplayMode mode) noexcept {
    if (mode == ImmunityDisplayMode::Off) {
        config.immunities.enabled = false;
        return;
    }
    config.immunities.enabled = true;
    config.immunities.style = mode == ImmunityDisplayMode::SplitHalo
        ? ImmunityDisplayStyle::SplitHalo
        : ImmunityDisplayStyle::ColoredI;
}

enum class ImmunityPalette : std::uint8_t {
    Classic,
    HighContrast,
    ColorBlindSafe,
};

struct ImmunityPaletteColors {
    RgbaColor physical;
    RgbaColor fire;
    RgbaColor cold;
    RgbaColor lightning;
    RgbaColor poison;
    RgbaColor magic;
};

inline constexpr std::array<ImmunityPalette, 3> ImmunityPalettes{
    ImmunityPalette::Classic,
    ImmunityPalette::HighContrast,
    ImmunityPalette::ColorBlindSafe,
};

[[nodiscard]] constexpr auto ImmunityPaletteLabel(
        ImmunityPalette palette) noexcept -> std::string_view {
    switch (palette) {
        case ImmunityPalette::Classic:
            return "Classic";
        case ImmunityPalette::HighContrast:
            return "High contrast";
        case ImmunityPalette::ColorBlindSafe:
            return "Color-blind safe";
    }
    return {};
}

[[nodiscard]] constexpr auto ColorsForImmunityPalette(
        ImmunityPalette palette) noexcept -> ImmunityPaletteColors {
    switch (palette) {
        case ImmunityPalette::Classic:
            return {
                DefaultPhysicalImmunityColor,
                {0.95F, 0.24F, 0.12F, 1.0F},
                {0.20F, 0.65F, 1.00F, 1.0F},
                {1.00F, 0.88F, 0.18F, 1.0F},
                {0.34F, 0.88F, 0.24F, 1.0F},
                {0.78F, 0.36F, 1.00F, 1.0F},
            };
        case ImmunityPalette::HighContrast:
            return {
                {1.00F, 1.00F, 1.00F, 1.0F},
                {1.00F, 0.05F, 0.02F, 1.0F},
                {0.00F, 0.72F, 1.00F, 1.0F},
                {1.00F, 0.95F, 0.00F, 1.0F},
                {0.05F, 1.00F, 0.12F, 1.0F},
                {0.95F, 0.15F, 1.00F, 1.0F},
            };
        case ImmunityPalette::ColorBlindSafe:
            return {
                {0.70F, 0.70F, 0.70F, 1.0F},
                {0.84F, 0.37F, 0.00F, 1.0F},
                {0.34F, 0.71F, 0.91F, 1.0F},
                {0.94F, 0.89F, 0.26F, 1.0F},
                {0.00F, 0.62F, 0.45F, 1.0F},
                {0.80F, 0.47F, 0.65F, 1.0F},
            };
    }
    return ColorsForImmunityPalette(ImmunityPalette::Classic);
}

[[nodiscard]] constexpr auto SameColor(
        const RgbaColor& left,
        const RgbaColor& right) noexcept -> bool {
    return left.red == right.red
        && left.green == right.green
        && left.blue == right.blue
        && left.alpha == right.alpha;
}

[[nodiscard]] constexpr auto SameMonsterMarkerStyle(
        const MonsterMarkerStyle& left,
        const MonsterMarkerStyle& right) noexcept -> bool {
    return left.shape == right.shape
        && left.size == right.size
        && (left.shape == MonsterMarkerShape::Dot
            || left.thickness == right.thickness)
        && SameColor(left.color, right.color)
        && left.showNames == right.showNames
        && SameColor(left.nameColor, right.nameColor)
        && left.nameSize == right.nameSize;
}

[[nodiscard]] constexpr auto SameAutomapLabelOptions(
        const AutomapLabelOptions& left,
        const AutomapLabelOptions& right) noexcept -> bool {
    return left.enabled == right.enabled
        && SameColor(left.color, right.color)
        && left.size == right.size;
}

[[nodiscard]] constexpr auto SameMissileMarkerStyle(
        const MissileMarkerStyle& left,
        const MissileMarkerStyle& right) noexcept -> bool {
    return SameColor(left.color, right.color)
        && left.size == right.size;
}

[[nodiscard]] constexpr auto SameMissileOptions(
        const MissileOptions& left,
        const MissileOptions& right) noexcept -> bool {
    return left.enabled == right.enabled
        && SameMissileMarkerStyle(left.fire, right.fire)
        && SameMissileMarkerStyle(left.cold, right.cold)
        && SameMissileMarkerStyle(left.lightning, right.lightning)
        && SameMissileMarkerStyle(left.poison, right.poison)
        && SameMissileMarkerStyle(left.physical, right.physical)
        && SameMissileMarkerStyle(left.magic, right.magic);
}

[[nodiscard]] constexpr auto SameAutomapObjectOptions(
        const AutomapObjectOptions& left,
        const AutomapObjectOptions& right) noexcept -> bool {
    return left.enabled == right.enabled
        && SameColor(left.color, right.color)
        && left.size == right.size;
}

[[nodiscard]] constexpr auto SameChestOptions(
        const ChestOptions& left,
        const ChestOptions& right) noexcept -> bool {
    return left.enabled == right.enabled
        && SameColor(left.outlineColor, right.outlineColor)
        && SameColor(left.interiorColor, right.interiorColor)
        && SameColor(left.lockedAccentColor, right.lockedAccentColor)
        && SameColor(left.trappedAccentColor, right.trappedAccentColor)
        && left.size == right.size;
}

[[nodiscard]] constexpr auto SameSuperChestOptions(
        const SuperChestOptions& left,
        const SuperChestOptions& right) noexcept -> bool {
    return left.enabled == right.enabled
        && left.starsEnabled == right.starsEnabled
        && SameColor(left.starsColor, right.starsColor)
        && left.starsSize == right.starsSize;
}

[[nodiscard]] constexpr auto SameObjectsOptions(
        const ObjectsOptions& left,
        const ObjectsOptions& right) noexcept -> bool {
    return left.enabled == right.enabled
        && SameAutomapLabelOptions(left.exitLabels, right.exitLabels)
        && SameAutomapLabelOptions(
            left.waypointLabels,
            right.waypointLabels)
        && SameAutomapLabelOptions(left.shrineLabels, right.shrineLabels)
        && SameChestOptions(left.chests, right.chests)
        && SameSuperChestOptions(left.superChests, right.superChests)
        && SameAutomapObjectOptions(left.armorRacks, right.armorRacks)
        && SameAutomapObjectOptions(left.weaponRacks, right.weaponRacks);
}

[[nodiscard]] constexpr auto ApproximatelySameComponent(
        float left,
        float right) noexcept -> bool {
    constexpr float HexRoundTripTolerance = 0.0021F;
    const auto difference = left > right ? left - right : right - left;
    return difference <= HexRoundTripTolerance;
}

[[nodiscard]] constexpr auto ApproximatelySameColor(
        const RgbaColor& left,
        const RgbaColor& right) noexcept -> bool {
    return ApproximatelySameComponent(left.red, right.red)
        && ApproximatelySameComponent(left.green, right.green)
        && ApproximatelySameComponent(left.blue, right.blue)
        && ApproximatelySameComponent(left.alpha, right.alpha);
}

inline void ApplyImmunityPalette(
        Config& config,
        ImmunityPalette palette) noexcept {
    const auto colors = ColorsForImmunityPalette(palette);
    config.immunities.physical = colors.physical;
    config.immunities.fire = colors.fire;
    config.immunities.cold = colors.cold;
    config.immunities.lightning = colors.lightning;
    config.immunities.poison = colors.poison;
    config.immunities.magic = colors.magic;
}

[[nodiscard]] inline auto DetectImmunityPalette(
        const Config& config) noexcept -> std::optional<ImmunityPalette> {
    for (const auto palette : ImmunityPalettes) {
        const auto colors = ColorsForImmunityPalette(palette);
        if (ApproximatelySameColor(config.immunities.physical, colors.physical)
                && ApproximatelySameColor(config.immunities.fire, colors.fire)
                && ApproximatelySameColor(config.immunities.cold, colors.cold)
                && ApproximatelySameColor(
                    config.immunities.lightning,
                    colors.lightning)
                && ApproximatelySameColor(config.immunities.poison, colors.poison)
                && ApproximatelySameColor(config.immunities.magic, colors.magic)) {
            return palette;
        }
    }
    return std::nullopt;
}

inline void CopyNativeSettings(
        const Config& source,
        Config& destination) noexcept {
    destination.enabled = source.enabled;
    destination.diagnostics = source.diagnostics;
    destination.overlay.diagnosticPreview = source.overlay.diagnosticPreview;
    destination.overlay.opacity = source.overlay.opacity;
    destination.overlay.scale = source.overlay.scale;
    destination.monsters = source.monsters;
    destination.immunities = source.immunities;
    destination.missiles = source.missiles;
    destination.objects = source.objects;
}

[[nodiscard]] inline auto NativeSettingsEquivalent(
        const Config& left,
        const Config& right) noexcept -> bool {
    return left.enabled == right.enabled
        && left.diagnostics == right.diagnostics
        && left.overlay.diagnosticPreview == right.overlay.diagnosticPreview
        && left.overlay.opacity == right.overlay.opacity
        && left.overlay.scale == right.overlay.scale
        && left.monsters.enabled == right.monsters.enabled
        && SameMonsterMarkerStyle(
            left.monsters.normal,
            right.monsters.normal)
        && SameMonsterMarkerStyle(
            left.monsters.minion,
            right.monsters.minion)
        && SameMonsterMarkerStyle(
            left.monsters.champion,
            right.monsters.champion)
        && SameMonsterMarkerStyle(
            left.monsters.unique,
            right.monsters.unique)
        && SameMonsterMarkerStyle(
            left.monsters.superUniqueBoss,
            right.monsters.superUniqueBoss)
        && left.immunities.enabled == right.immunities.enabled
        && left.immunities.style == right.immunities.style
        && left.immunities.indicatorSize == right.immunities.indicatorSize
        && left.immunities.haloThickness == right.immunities.haloThickness
        && SameColor(left.immunities.physical, right.immunities.physical)
        && SameColor(left.immunities.fire, right.immunities.fire)
        && SameColor(left.immunities.cold, right.immunities.cold)
        && SameColor(left.immunities.lightning, right.immunities.lightning)
        && SameColor(left.immunities.poison, right.immunities.poison)
        && SameColor(left.immunities.magic, right.immunities.magic)
        && SameMissileOptions(left.missiles, right.missiles)
        && SameObjectsOptions(left.objects, right.objects);
}

class NativeSettingsDraft {
public:
    explicit NativeSettingsDraft(
            const Config& applied,
            const Config& defaults = {})
        : applied_(applied), draft_(applied), defaults_(defaults) {}

    [[nodiscard]] auto Applied() const noexcept -> const Config& {
        return applied_;
    }

    [[nodiscard]] auto Draft() noexcept -> Config& {
        return draft_;
    }

    [[nodiscard]] auto Draft() const noexcept -> const Config& {
        return draft_;
    }

    [[nodiscard]] auto Dirty() const noexcept -> bool {
        return !NativeSettingsEquivalent(draft_, applied_);
    }

    [[nodiscard]] auto Apply() noexcept -> const Config& {
        CopyNativeSettings(draft_, applied_);
        return applied_;
    }

    void ResetToDefaults() noexcept {
        CopyNativeSettings(defaults_, draft_);
    }

    void Discard() noexcept {
        CopyNativeSettings(applied_, draft_);
    }

private:
    Config applied_;
    Config draft_;
    Config defaults_;
};

} // namespace RuffnecKk::MapSense
