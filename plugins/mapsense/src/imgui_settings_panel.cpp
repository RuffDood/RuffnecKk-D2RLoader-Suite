#include "imgui_settings_panel.hpp"
#include "ui_localization.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace RuffnecKk::MapSense {
namespace {

constexpr auto PanelTitle = "MapSense###RuffnecKkMapSenseSettings";
constexpr auto ExpandedWidth = 520.0F;
constexpr auto ExpandedHeight = 620.0F;
constexpr auto LauncherWidth = 164.0F;
constexpr auto LauncherHeight = 68.0F;

struct PositionRuntimeState {
    ImGuiContext* context{};
    ImVec2 displaySize{};
    bool initialized{};
    bool previousExpanded{};
    bool positionDirty{};
};

auto GetPositionRuntimeState() noexcept -> PositionRuntimeState& {
    static PositionRuntimeState state{};
    return state;
}

struct MenuCorePalette final {
    ImVec4 background;
    ImVec4 surface;
    ImVec4 accent;
    ImVec4 text;
};

struct MenuPanelPalette final {
    ImVec4 text;
    ImVec4 windowBg;
    ImVec4 border;
    ImVec4 titleBg;
    ImVec4 titleBgActive;
    ImVec4 frameBg;
    ImVec4 frameBgHovered;
    ImVec4 frameBgActive;
    ImVec4 button;
    ImVec4 buttonHovered;
    ImVec4 buttonActive;
    ImVec4 header;
    ImVec4 headerHovered;
    ImVec4 headerActive;
    ImVec4 checkMark;
    ImVec4 sliderGrab;
    ImVec4 sliderGrabActive;
};

[[nodiscard]] auto Rgb(
        std::uint32_t value,
        float alpha = 1.0F) noexcept -> ImVec4 {
    constexpr auto Denominator = 255.0F;
    return {
        static_cast<float>((value >> 16U) & 0xFFU) / Denominator,
        static_cast<float>((value >> 8U) & 0xFFU) / Denominator,
        static_cast<float>(value & 0xFFU) / Denominator,
        alpha,
    };
}

[[nodiscard]] auto Mix(
        const ImVec4& left,
        const ImVec4& right,
        float amount) noexcept -> ImVec4 {
    const auto inverse = 1.0F - amount;
    return {
        left.x * inverse + right.x * amount,
        left.y * inverse + right.y * amount,
        left.z * inverse + right.z * amount,
        left.w * inverse + right.w * amount,
    };
}

[[nodiscard]] auto MakeDerivedPalette(
        const MenuCorePalette& core) noexcept -> MenuPanelPalette {
    auto windowBg = core.background;
    windowBg.w = 0.98F;
    return {
        core.text,
        windowBg,
        Mix(core.surface, core.accent, 0.55F),
        Mix(core.background, core.surface, 0.35F),
        Mix(core.background, core.surface, 0.78F),
        core.surface,
        Mix(core.surface, core.accent, 0.25F),
        Mix(core.surface, core.accent, 0.42F),
        Mix(core.background, core.surface, 0.72F),
        Mix(core.surface, core.accent, 0.34F),
        Mix(core.surface, core.accent, 0.54F),
        Mix(core.background, core.surface, 0.76F),
        Mix(core.surface, core.accent, 0.31F),
        Mix(core.surface, core.accent, 0.50F),
        core.accent,
        Mix(core.surface, core.accent, 0.58F),
        core.accent,
    };
}

[[nodiscard]] auto PaletteFor(MenuTheme theme) noexcept -> MenuPanelPalette {
    if (theme == MenuTheme::SanctuaryGold) {
        // Preserve the established MapSense appearance as the default theme.
        return {
            {0.86F, 0.80F, 0.67F, 1.0F},
            {0.025F, 0.018F, 0.014F, 0.98F},
            {0.62F, 0.48F, 0.22F, 1.0F},
            {0.055F, 0.030F, 0.020F, 1.0F},
            {0.12F, 0.065F, 0.030F, 1.0F},
            {0.12F, 0.065F, 0.038F, 1.0F},
            {0.22F, 0.12F, 0.055F, 1.0F},
            {0.30F, 0.17F, 0.07F, 1.0F},
            {0.18F, 0.09F, 0.045F, 1.0F},
            {0.34F, 0.20F, 0.075F, 1.0F},
            {0.48F, 0.30F, 0.095F, 1.0F},
            {0.19F, 0.10F, 0.045F, 1.0F},
            {0.31F, 0.19F, 0.07F, 1.0F},
            {0.43F, 0.28F, 0.10F, 1.0F},
            {0.86F, 0.67F, 0.24F, 1.0F},
            {0.62F, 0.48F, 0.22F, 1.0F},
            {0.86F, 0.67F, 0.24F, 1.0F},
        };
    }

    MenuCorePalette core{};
    switch (theme) {
        case MenuTheme::Hellfire:
            core = {Rgb(0x090303U), Rgb(0x260907U),
                Rgb(0xF05A28U), Rgb(0xF3D7C1U)};
            break;
        case MenuTheme::HoradricSand:
            core = {Rgb(0x0B0803U), Rgb(0x2B1B09U),
                Rgb(0xE3B85BU), Rgb(0xF0E2BDU)};
            break;
        case MenuTheme::ArcaneSanctuary:
            core = {Rgb(0x050513U), Rgb(0x17152EU),
                Rgb(0xA96DFFU), Rgb(0xECE6FFU)};
            break;
        case MenuTheme::TristramMoon:
            core = {Rgb(0x04080DU), Rgb(0x101D2AU),
                Rgb(0x61B7E8U), Rgb(0xDCEBF4U)};
            break;
        case MenuTheme::KurastJade:
            core = {Rgb(0x03100AU), Rgb(0x0D2B1DU),
                Rgb(0x49C98AU), Rgb(0xD8ECDDU)};
            break;
        case MenuTheme::NecromancerBone:
            core = {Rgb(0x070906U), Rgb(0x171C14U),
                Rgb(0x9CCB55U), Rgb(0xE1E0C9U)};
            break;
        case MenuTheme::HarrogathFrost:
            core = {Rgb(0x040A12U), Rgb(0x102536U),
                Rgb(0x72D8F0U), Rgb(0xE2F7FAU)};
            break;
        case MenuTheme::BloodMoor:
            core = {Rgb(0x0B0405U), Rgb(0x2A0E14U),
                Rgb(0xD74658U), Rgb(0xEFD7DAU)};
            break;
        case MenuTheme::HighContrast:
            core = {Rgb(0x000000U), Rgb(0x161616U),
                Rgb(0xFFD83DU), Rgb(0xFFFFFFU)};
            break;
        case MenuTheme::SanctuaryGold:
            break;
    }
    return MakeDerivedPalette(core);
}

class ScopedPanelStyle final {
public:
    explicit ScopedPanelStyle(MenuTheme theme) noexcept {
        const auto palette = PaletteFor(theme);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            palette.text);
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            palette.windowBg);
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            palette.border);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            palette.titleBg);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            palette.titleBgActive);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            palette.frameBg);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            palette.frameBgHovered);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            palette.frameBgActive);
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            palette.button);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            palette.buttonHovered);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            palette.buttonActive);
        ImGui::PushStyleColor(
            ImGuiCol_Header,
            palette.header);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            palette.headerHovered);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderActive,
            palette.headerActive);
        ImGui::PushStyleColor(
            ImGuiCol_CheckMark,
            palette.checkMark);
        ImGui::PushStyleColor(
            ImGuiCol_SliderGrab,
            palette.sliderGrab);
        ImGui::PushStyleColor(
            ImGuiCol_SliderGrabActive,
            palette.sliderGrabActive);
    }

    ~ScopedPanelStyle() noexcept {
        ImGui::PopStyleColor(17);
        ImGui::PopStyleVar(3);
    }

    ScopedPanelStyle(const ScopedPanelStyle&) = delete;
    auto operator=(const ScopedPanelStyle&) -> ScopedPanelStyle& = delete;
};

void Invoke(
        const ImGuiSettingsActionCallback callback,
        const ImGuiSettingsAction action) noexcept {
    if (callback != nullptr) {
        callback(action);
    }
}

[[nodiscard]] auto MenuThemeLabel(MenuTheme theme) noexcept -> const char* {
    switch (theme) {
        case MenuTheme::SanctuaryGold:
            return UiText(UiTextId::ThemeSanctuaryGold);
        case MenuTheme::Hellfire:
            return UiText(UiTextId::ThemeHellfire);
        case MenuTheme::HoradricSand:
            return UiText(UiTextId::ThemeHoradricSand);
        case MenuTheme::ArcaneSanctuary:
            return UiText(UiTextId::ThemeArcaneSanctuary);
        case MenuTheme::TristramMoon:
            return UiText(UiTextId::ThemeTristramMoon);
        case MenuTheme::KurastJade:
            return UiText(UiTextId::ThemeKurastJade);
        case MenuTheme::NecromancerBone:
            return UiText(UiTextId::ThemeNecromancerBone);
        case MenuTheme::HarrogathFrost:
            return UiText(UiTextId::ThemeHarrogathFrost);
        case MenuTheme::BloodMoor:
            return UiText(UiTextId::ThemeBloodMoor);
        case MenuTheme::HighContrast:
            return UiText(UiTextId::ThemeHighContrast);
    }
    return UiText(UiTextId::ThemeSanctuaryGold);
}

void UpdateRememberedPosition(
        Config& config,
        const ImVec2 position,
        const ImVec2 size,
        const ImVec2 displaySize) noexcept {
    if (!config.menu.rememberPosition) {
        return;
    }

    const auto rangeX = std::max(0.0F, displaySize.x - size.x);
    const auto rangeY = std::max(0.0F, displaySize.y - size.y);
    config.menu.positionX = rangeX > 0.0F
        ? std::clamp(position.x / rangeX, 0.0F, 1.0F)
        : 0.0F;
    config.menu.positionY = rangeY > 0.0F
        ? std::clamp(position.y / rangeY, 0.0F, 1.0F)
        : 0.0F;
}

[[nodiscard]] auto MonsterMarkerShapeLabel(
        MonsterMarkerShape shape) noexcept -> const char* {
    switch (shape) {
        case MonsterMarkerShape::X:
            return "X";
        case MonsterMarkerShape::PlayerCross:
            return UiText(UiTextId::PlayerCross);
        case MonsterMarkerShape::Dot:
            return UiText(UiTextId::Dot);
    }
    return UiText(UiTextId::PlayerCross);
}

[[nodiscard]] auto DrawMonsterMarkerShape(
        MonsterMarkerShape& shape) noexcept -> bool {
    constexpr std::array Shapes{
        MonsterMarkerShape::X,
        MonsterMarkerShape::PlayerCross,
        MonsterMarkerShape::Dot,
    };

    auto changed = false;
    if (ImGui::BeginCombo(
            UiText(UiTextId::Shape),
            MonsterMarkerShapeLabel(shape))) {
        for (const auto candidate : Shapes) {
            const auto selected = candidate == shape;
            if (ImGui::Selectable(
                    MonsterMarkerShapeLabel(candidate),
                    selected)) {
                shape = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

[[nodiscard]] auto ImmunityDisplayStyleLabel(
        ImmunityDisplayStyle style) noexcept -> const char* {
    switch (style) {
        case ImmunityDisplayStyle::ColoredI:
            return UiText(UiTextId::ColoredI);
        case ImmunityDisplayStyle::SplitHalo:
            return UiText(UiTextId::SplitHalo);
    }
    return UiText(UiTextId::ColoredI);
}

[[nodiscard]] auto DrawImmunityDisplayStyle(
        ImmunityDisplayStyle& style) noexcept -> bool {
    constexpr std::array Styles{
        ImmunityDisplayStyle::ColoredI,
        ImmunityDisplayStyle::SplitHalo,
    };

    auto changed = false;
    if (ImGui::BeginCombo(
            UiText(UiTextId::Style),
            ImmunityDisplayStyleLabel(style))) {
        for (const auto candidate : Styles) {
            const auto selected = candidate == style;
            if (ImGui::Selectable(
                    ImmunityDisplayStyleLabel(candidate),
                    selected)) {
                style = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

[[nodiscard]] auto ColorChannelByte(float value) noexcept -> int {
    return static_cast<int>(std::lround(
        std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

void DrawColorDetailsTooltip(
        const RgbaColor& color,
        float dpiScale) noexcept {
    const auto hex = ColorToHex(color);
    char bytes[64]{};
    char normalized[96]{};
    (void)std::snprintf(
        bytes,
        sizeof(bytes),
        "R: %d, G: %d, B: %d, A: %d",
        ColorChannelByte(color.red),
        ColorChannelByte(color.green),
        ColorChannelByte(color.blue),
        ColorChannelByte(color.alpha));
    (void)std::snprintf(
        normalized,
        sizeof(normalized),
        "(%.3f, %.3f, %.3f, %.3f)",
        color.red,
        color.green,
        color.blue,
        color.alpha);

    const auto& style = ImGui::GetStyle();
    const auto lineHeight = ImGui::GetTextLineHeight();
    const auto widestLine = std::max({
        ImGui::CalcTextSize(hex.c_str()).x,
        ImGui::CalcTextSize(bytes).x,
        ImGui::CalcTextSize(normalized).x,
    });
    const ImVec2 tooltipSize{
        widestLine + style.WindowPadding.x * 2.0F,
        lineHeight * 3.0F
            + style.ItemSpacing.y * 2.0F
            + style.WindowPadding.y * 2.0F,
    };

    const auto* viewport = ImGui::GetMainViewport();
    const auto workMinimum = viewport->WorkPos;
    const ImVec2 workMaximum{
        viewport->WorkPos.x + viewport->WorkSize.x,
        viewport->WorkPos.y + viewport->WorkSize.y,
    };
    const auto mouse = ImGui::GetMousePos();
    const auto gap = 10.0F * dpiScale;
    auto tooltipX = mouse.x + gap;
    if (tooltipX + tooltipSize.x > workMaximum.x) {
        tooltipX = mouse.x - tooltipSize.x - gap;
    }
    tooltipX = std::clamp(
        tooltipX,
        workMinimum.x,
        std::max(workMinimum.x, workMaximum.x - tooltipSize.x));

    auto tooltipY = mouse.y - tooltipSize.y - gap;
    if (tooltipY < workMinimum.y) tooltipY = mouse.y + gap;
    tooltipY = std::clamp(
        tooltipY,
        workMinimum.y,
        std::max(workMinimum.y, workMaximum.y - tooltipSize.y));

    ImGui::SetNextWindowPos(
        ImVec2{tooltipX, tooltipY},
        ImGuiCond_Always);
    if (ImGui::BeginTooltip()) {
        ImGui::TextUnformatted(hex.c_str());
        ImGui::TextUnformatted(bytes);
        ImGui::TextUnformatted(normalized);
        ImGui::EndTooltip();
    }
}

[[nodiscard]] auto DrawMonsterMarkerColor(
        RgbaColor& color,
        float dpiScale,
        const char* label = nullptr) noexcept -> bool {
    if (label == nullptr) label = UiText(UiTextId::Color);
    auto channels = std::array<float, 4>{
        color.red,
        color.green,
        color.blue,
        color.alpha,
    };
    const ImVec4 preview{
        channels[0],
        channels[1],
        channels[2],
        channels[3],
    };
    constexpr auto PreviewFlags = ImGuiColorEditFlags_AlphaPreviewHalf
        | ImGuiColorEditFlags_NoTooltip
        | ImGuiColorEditFlags_NoDragDrop;
    const auto previewSize = ImVec2{42.0F * dpiScale, 0.0F};
    const auto popupName = "Color Picker";

    if (ImGui::ColorButton("##Color", preview, PreviewFlags, previewSize)) {
        ImGui::OpenPopup(popupName);
    }
    const auto previewHovered = ImGui::IsItemHovered(
        ImGuiHoveredFlags_Stationary
            | ImGuiHoveredFlags_DelayShort
            | ImGuiHoveredFlags_NoSharedDelay);
    const auto previewActive = ImGui::IsItemActive();
    const auto previewMinimum = ImGui::GetItemRectMin();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    if (previewHovered
            && !previewActive
            && !ImGui::IsPopupOpen(popupName)) {
        DrawColorDetailsTooltip(color, dpiScale);
    }

    const auto parentPosition = ImGui::GetWindowPos();
    const auto parentContentMinimum = ImGui::GetWindowContentRegionMin();
    const auto parentContentMaximum = ImGui::GetWindowContentRegionMax();
    const ImVec2 contentMinimum{
        parentPosition.x + parentContentMinimum.x,
        parentPosition.y + parentContentMinimum.y,
    };
    const ImVec2 contentMaximum{
        parentPosition.x + parentContentMaximum.x,
        parentPosition.y + parentContentMaximum.y,
    };
    const auto contentWidth = std::max(
        1.0F,
        contentMaximum.x - contentMinimum.x);
    const auto contentHeight = std::max(
        1.0F,
        contentMaximum.y - contentMinimum.y);
    const ImVec2 popupSize{
        std::min(290.0F * dpiScale, contentWidth),
        std::min(330.0F * dpiScale, contentHeight),
    };
    const ImVec2 popupPosition{
        std::clamp(
            previewMinimum.x,
            contentMinimum.x,
            std::max(contentMinimum.x, contentMaximum.x - popupSize.x)),
        std::clamp(
            previewMinimum.y,
            contentMinimum.y,
            std::max(contentMinimum.y, contentMaximum.y - popupSize.y)),
    };

    ImGui::SetNextWindowPos(popupPosition, ImGuiCond_Always);
    ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);
    auto saveRequested = false;
    if (ImGui::BeginPopup(
            popupName,
            ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings)) {
        constexpr auto PickerFlags = ImGuiColorEditFlags_AlphaBar
            | ImGuiColorEditFlags_NoInputs
            | ImGuiColorEditFlags_NoOptions
            | ImGuiColorEditFlags_NoLabel
            | ImGuiColorEditFlags_NoSidePreview
            | ImGuiColorEditFlags_NoTooltip
            | ImGuiColorEditFlags_PickerHueBar
            | ImGuiColorEditFlags_InputRGB;
        ImGui::SetNextItemWidth(-1.0F);
        (void)ImGui::ColorPicker4("##Picker", channels.data(), PickerFlags);
        color = {channels[0], channels[1], channels[2], channels[3]};
        saveRequested = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndPopup();
    }
    return saveRequested;
}

struct ImmunityColorControl final {
    UiTextId label;
    RgbaColor ImmunityOptions::* color;
};

[[nodiscard]] auto DrawImmunityColors(
        ImmunityOptions& immunities,
        float dpiScale) noexcept -> bool {
    constexpr std::array Controls{
        ImmunityColorControl{UiTextId::Physical, &ImmunityOptions::physical},
        ImmunityColorControl{UiTextId::Fire, &ImmunityOptions::fire},
        ImmunityColorControl{UiTextId::Lightning, &ImmunityOptions::lightning},
        ImmunityColorControl{UiTextId::Cold, &ImmunityOptions::cold},
        ImmunityColorControl{UiTextId::Poison, &ImmunityOptions::poison},
        ImmunityColorControl{UiTextId::Magic, &ImmunityOptions::magic},
    };

    auto saveRequested = false;
    if (ImGui::BeginTable(
            "##ImmunityColors",
            2,
            ImGuiTableFlags_SizingStretchSame)) {
        for (const auto& control : Controls) {
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(control.label));
            saveRequested |= DrawMonsterMarkerColor(
                immunities.*(control.color),
                dpiScale,
                UiText(control.label));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return saveRequested;
}

[[nodiscard]] auto DrawMonsterMarkerStyle(
        const char* label,
        MonsterMarkerStyle& style,
        float dpiScale,
        bool drawNameOptions = false) noexcept -> bool {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);

    auto saveRequested = DrawMonsterMarkerShape(style.shape);

    (void)ImGui::SliderFloat(
        UiText(UiTextId::Size),
        &style.size,
        MinimumMonsterMarkerSize,
        MaximumMonsterMarkerSize,
        "%.0f px",
        ImGuiSliderFlags_AlwaysClamp);
    saveRequested |= ImGui::IsItemDeactivatedAfterEdit();

    if (style.shape != MonsterMarkerShape::Dot) {
        (void)ImGui::SliderFloat(
            UiText(UiTextId::Thickness),
            &style.thickness,
            MinimumMonsterMarkerThickness,
            MaximumMonsterMarkerThickness,
            "%.1f px",
            ImGuiSliderFlags_AlwaysClamp);
        saveRequested |= ImGui::IsItemDeactivatedAfterEdit();
    }
    saveRequested |= DrawMonsterMarkerColor(style.color, dpiScale);

    if (drawNameOptions) {
        ImGui::PushID("Names");
        ImGui::SeparatorText(UiText(UiTextId::Names));
        saveRequested |= ImGui::Checkbox(
            UiText(UiTextId::ShowNames),
            &style.showNames);
        if (style.showNames) {
            (void)ImGui::SliderFloat(
                UiText(UiTextId::NameSize),
                &style.nameSize,
                MinimumAutomapLabelSize,
                MaximumAutomapLabelSize,
                "%.0f px",
                ImGuiSliderFlags_AlwaysClamp);
            saveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            saveRequested |= DrawMonsterMarkerColor(
                style.nameColor,
                dpiScale,
                UiText(UiTextId::NameColor));
        }
        ImGui::PopID();
    }

    ImGui::PopID();
    return saveRequested;
}

[[nodiscard]] auto DrawAutomapLabelOptions(
        const char* sectionLabel,
        const char* enabledLabel,
        AutomapLabelOptions& options,
        float dpiScale) noexcept -> bool {
    ImGui::PushID(sectionLabel);
    ImGui::SeparatorText(sectionLabel);
    auto saveRequested = ImGui::Checkbox(enabledLabel, &options.enabled);
    if (options.enabled) {
        (void)ImGui::SliderFloat(
            UiText(UiTextId::TextSize),
            &options.size,
            MinimumAutomapLabelSize,
            MaximumAutomapLabelSize,
            "%.0f px",
            ImGuiSliderFlags_AlwaysClamp);
        saveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        saveRequested |= DrawMonsterMarkerColor(
            options.color,
            dpiScale,
            UiText(UiTextId::TextColor));
    }
    ImGui::PopID();
    return saveRequested;
}

[[nodiscard]] auto DrawAutomapObjectOptions(
        const char* sectionLabel,
        const char* enabledLabel,
        AutomapObjectOptions& options,
        float dpiScale) noexcept -> bool {
    ImGui::PushID(sectionLabel);
    ImGui::SeparatorText(sectionLabel);
    auto saveRequested = ImGui::Checkbox(enabledLabel, &options.enabled);
    if (options.enabled) {
        (void)ImGui::SliderFloat(
            UiText(UiTextId::MarkerSize),
            &options.size,
            MinimumAutomapObjectSize,
            MaximumAutomapObjectSize,
            "%.0f px",
            ImGuiSliderFlags_AlwaysClamp);
        saveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PushID("MarkerColor");
        saveRequested |= DrawMonsterMarkerColor(
            options.color,
            dpiScale,
            UiText(UiTextId::MarkerColor));
        ImGui::PopID();
    }
    ImGui::PopID();
    return saveRequested;
}

[[nodiscard]] auto DrawChestOptions(
        ChestOptions& options,
        SuperChestOptions& specialOptions,
        float dpiScale) noexcept -> bool {
    ImGui::PushID("Chests");
    ImGui::SeparatorText(UiText(UiTextId::Chests));
    auto saveRequested = ImGui::Checkbox(
        UiText(UiTextId::ShowChests),
        &options.enabled);
    if (options.enabled) {
        (void)ImGui::SliderFloat(
            UiText(UiTextId::MarkerSize),
            &options.size,
            MinimumAutomapObjectSize,
            MaximumAutomapObjectSize,
            "%.0f px",
            ImGuiSliderFlags_AlwaysClamp);
        saveRequested |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::PushID("LockedAccentColor");
        saveRequested |= DrawMonsterMarkerColor(
            options.lockedAccentColor,
            dpiScale,
            UiText(UiTextId::LockedLockColor));
        ImGui::PopID();
        ImGui::PushID("TrappedAccentColor");
        saveRequested |= DrawMonsterMarkerColor(
            options.trappedAccentColor,
            dpiScale,
            UiText(UiTextId::TrappedLockColor));
        ImGui::PopID();
    }

    ImGui::SeparatorText(UiText(UiTextId::SpecialChests));
    saveRequested |= ImGui::Checkbox(
        UiText(UiTextId::ShowSpecialChests),
        &specialOptions.enabled);
    ImGui::PopID();
    return saveRequested;
}

[[nodiscard]] auto DrawMissileMarkerStyle(
        const char* label,
        MissileMarkerStyle& style,
        float dpiScale) noexcept -> bool {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    (void)ImGui::SliderFloat(
        UiText(UiTextId::Size),
        &style.size,
        MinimumMissileMarkerSize,
        MaximumMissileMarkerSize,
        "%.0f px",
        ImGuiSliderFlags_AlwaysClamp);
    auto saveRequested = ImGui::IsItemDeactivatedAfterEdit();
    saveRequested |= DrawMonsterMarkerColor(style.color, dpiScale);
    ImGui::PopID();
    return saveRequested;
}

[[nodiscard]] auto DrawNavigationLineOptions(
        const char* sectionLabel,
        const char* enabledLabel,
        NavigationLineOptions& options,
        float dpiScale) noexcept -> bool {
    ImGui::PushID(sectionLabel);
    ImGui::SeparatorText(sectionLabel);
    auto saveRequested = ImGui::Checkbox(enabledLabel, &options.enabled);
    saveRequested |= DrawMonsterMarkerColor(
        options.color,
        dpiScale,
        UiText(UiTextId::LineColor));
    ImGui::PopID();
    return saveRequested;
}

[[nodiscard]] auto DrawCustomLevelLineOptions(
        CustomLevelLineOptions& options,
        float dpiScale) noexcept -> bool {
    ImGui::PushID("CustomLevelLines");
    ImGui::SeparatorText(UiText(UiTextId::CustomLevels));
    auto saveRequested = ImGui::Checkbox(
        UiText(UiTextId::CustomLevelLines),
        &options.enabled);
    saveRequested |= DrawMonsterMarkerColor(
        options.color,
        dpiScale,
        UiText(UiTextId::LineColor));
    ImGui::TextDisabled("%s", UiText(UiTextId::CustomTomlHint));
    ImGui::PopID();
    return saveRequested;
}

} // namespace

auto DrawImGuiSettingsPanel(
        Config& config,
        bool& expanded,
        bool revealMapEnabled,
        const ImGuiSettingsActionCallback actionCallback) noexcept
        -> ImGuiSettingsBounds {
    const ScopedPanelStyle style{config.menu.theme};
    const auto& io = ImGui::GetIO();
    const auto dpiScale = std::max(1.0F, io.FontGlobalScale);
    const auto frameExpanded = expanded;
    auto& positionState = GetPositionRuntimeState();
    auto* const currentContext = ImGui::GetCurrentContext();
    if (positionState.context != currentContext) {
        positionState = {};
        positionState.context = currentContext;
    }
    const auto displaySizeChanged = positionState.initialized
        && (positionState.displaySize.x != io.DisplaySize.x
            || positionState.displaySize.y != io.DisplaySize.y);
    const auto sizeModeChanged = positionState.initialized
        && positionState.previousExpanded != frameExpanded;
    const auto reanchorPosition = !positionState.initialized
        || displaySizeChanged
        || sizeModeChanged;
    const auto oldPositionX = config.menu.positionX;
    const auto oldPositionY = config.menu.positionY;
    const auto requestedSize = frameExpanded
        ? ImVec2{
            std::min(ExpandedWidth * dpiScale, io.DisplaySize.x),
            std::min(ExpandedHeight * dpiScale, io.DisplaySize.y)}
        : ImVec2{LauncherWidth * dpiScale, LauncherHeight * dpiScale};

    const auto availableX = std::max(0.0F, io.DisplaySize.x - requestedSize.x);
    const auto availableY = std::max(0.0F, io.DisplaySize.y - requestedSize.y);
    const ImVec2 anchoredPosition{
        std::clamp(config.menu.positionX, 0.0F, 1.0F) * availableX,
        std::clamp(config.menu.positionY, 0.0F, 1.0F) * availableY,
    };
    ImGui::SetNextWindowPos(
        anchoredPosition,
        reanchorPosition ? ImGuiCond_Always : ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(requestedSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.98F);

    auto windowFlags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus;
    if (!frameExpanded) {
        windowFlags |= ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse;
    }

    auto windowOpen = true;
    const auto contentVisible = ImGui::Begin(
        PanelTitle,
        frameExpanded ? &windowOpen : nullptr,
        windowFlags);

    const auto windowPosition = ImGui::GetWindowPos();
    const auto windowSize = ImGui::GetWindowSize();
    UpdateRememberedPosition(
        config,
        windowPosition,
        windowSize,
        io.DisplaySize);

    auto saveRequested = false;
    ImGuiSettingsBounds bounds{
        true,
        frameExpanded,
        false,
        windowPosition.x,
        windowPosition.y,
        windowPosition.x + windowSize.x,
        windowPosition.y + windowSize.y,
    };

    if (contentVisible) {
        if (!frameExpanded) {
            if (ImGui::Button(
                    config.enabled
                        ? UiText(UiTextId::Open)
                        : UiText(UiTextId::OpenOff),
                    ImVec2{ImGui::GetContentRegionAvail().x, 0.0F})) {
                expanded = true;
            }
        } else {
            if (ImGui::Button(UiText(UiTextId::Collapse))) {
                expanded = false;
            }

            saveRequested |= ImGui::Checkbox(
                UiText(UiTextId::EnableMapSense),
                &config.enabled);

            if (ImGui::CollapsingHeader(UiText(UiTextId::Appearance))) {
                if (ImGui::BeginCombo(
                        UiText(UiTextId::MenuTheme),
                        MenuThemeLabel(config.menu.theme))) {
                    for (const auto theme : MenuThemes) {
                        const auto selected = config.menu.theme == theme;
                        if (ImGui::Selectable(
                                MenuThemeLabel(theme),
                                selected)) {
                            config.menu.theme = theme;
                            saveRequested = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            if (ImGui::CollapsingHeader(
                    UiText(UiTextId::MapAndReveal),
                    ImGuiTreeNodeFlags_DefaultOpen)) {
                auto revealMap = revealMapEnabled;
                ImGui::BeginDisabled(!config.enabled);
                if (ImGui::Checkbox(
                        UiText(UiTextId::RevealMap),
                        &revealMap)) {
                    Invoke(
                        actionCallback,
                        ImGuiSettingsAction::ToggleRevealMap);
                }
                ImGui::EndDisabled();

                (void)ImGui::SliderFloat(
                    UiText(UiTextId::AdditionsOpacity),
                    &config.overlay.opacity,
                    0.10F,
                    1.0F,
                    "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
                saveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            }

            if (ImGui::CollapsingHeader(
                    UiText(UiTextId::Monsters),
                    ImGuiTreeNodeFlags_DefaultOpen)) {
                saveRequested |= ImGui::Checkbox(
                    UiText(UiTextId::ShowMonsters),
                    &config.monsters.enabled);
                ImGui::BeginDisabled(!config.monsters.enabled);
                saveRequested |= DrawMonsterMarkerStyle(
                    UiText(UiTextId::Normal),
                    config.monsters.normal,
                    dpiScale);
                saveRequested |= DrawMonsterMarkerStyle(
                    UiText(UiTextId::Minion),
                    config.monsters.minion,
                    dpiScale);
                saveRequested |= DrawMonsterMarkerStyle(
                    UiText(UiTextId::Champion),
                    config.monsters.champion,
                    dpiScale);
                saveRequested |= DrawMonsterMarkerStyle(
                    UiText(UiTextId::Unique),
                    config.monsters.unique,
                    dpiScale);
                saveRequested |= DrawMonsterMarkerStyle(
                    UiText(UiTextId::SuperUniqueBoss),
                    config.monsters.superUniqueBoss,
                    dpiScale,
                    true);
                ImGui::EndDisabled();
            }

            if (ImGui::CollapsingHeader(UiText(UiTextId::Immunities))) {
                saveRequested |= ImGui::Checkbox(
                    UiText(UiTextId::ShowImmunities),
                    &config.immunities.enabled);

                if (config.immunities.enabled) {
                    saveRequested |= DrawImmunityDisplayStyle(
                        config.immunities.style);

                    if (config.immunities.style
                            == ImmunityDisplayStyle::ColoredI) {
                        (void)ImGui::SliderFloat(
                            UiText(UiTextId::IndicatorSize),
                            &config.immunities.indicatorSize,
                            MinimumImmunityIndicatorSize,
                            MaximumImmunityIndicatorSize,
                            "%.0f px",
                            ImGuiSliderFlags_AlwaysClamp);
                    } else {
                        (void)ImGui::SliderFloat(
                            UiText(UiTextId::HaloThickness),
                            &config.immunities.haloThickness,
                            MinimumImmunityHaloThickness,
                            MaximumImmunityHaloThickness,
                            "%.1f px",
                            ImGuiSliderFlags_AlwaysClamp);
                    }
                    saveRequested |= ImGui::IsItemDeactivatedAfterEdit();

                    ImGui::SeparatorText(UiText(UiTextId::Colors));
                    saveRequested |= DrawImmunityColors(
                        config.immunities,
                        dpiScale);
                }
            }

            if (ImGui::CollapsingHeader(UiText(UiTextId::Missiles))) {
                saveRequested |= ImGui::Checkbox(
                    UiText(UiTextId::ShowMissiles),
                    &config.missiles.enabled);
                if (config.missiles.enabled) {
                    saveRequested |= DrawMissileMarkerStyle(
                        UiText(UiTextId::Fire),
                        config.missiles.fire,
                        dpiScale);
                    saveRequested |= DrawMissileMarkerStyle(
                        UiText(UiTextId::ColdIce),
                        config.missiles.cold,
                        dpiScale);
                    saveRequested |= DrawMissileMarkerStyle(
                        UiText(UiTextId::Lightning),
                        config.missiles.lightning,
                        dpiScale);
                    saveRequested |= DrawMissileMarkerStyle(
                        UiText(UiTextId::Poison),
                        config.missiles.poison,
                        dpiScale);
                    saveRequested |= DrawMissileMarkerStyle(
                        UiText(UiTextId::Physical),
                        config.missiles.physical,
                        dpiScale);
                    saveRequested |= DrawMissileMarkerStyle(
                        UiText(UiTextId::Magic),
                        config.missiles.magic,
                        dpiScale);
                }
            }

            if (ImGui::CollapsingHeader(UiText(UiTextId::Objects))) {
                saveRequested |= ImGui::Checkbox(
                    UiText(UiTextId::ShowAutomapObjects),
                    &config.objects.enabled);

                if (config.objects.enabled) {
                    saveRequested |= DrawAutomapLabelOptions(
                        UiText(UiTextId::ExitLabels),
                        UiText(UiTextId::ShowExitLabels),
                        config.objects.exitLabels,
                        dpiScale);
                    saveRequested |= DrawAutomapLabelOptions(
                        UiText(UiTextId::WaypointLabels),
                        UiText(UiTextId::ShowWaypointLabels),
                        config.objects.waypointLabels,
                        dpiScale);
                    saveRequested |= DrawAutomapLabelOptions(
                        UiText(UiTextId::ShrineLabels),
                        UiText(UiTextId::ShowShrineLabels),
                        config.objects.shrineLabels,
                        dpiScale);
                    saveRequested |= DrawChestOptions(
                        config.objects.chests,
                        config.objects.superChests,
                        dpiScale);
                    saveRequested |= DrawAutomapObjectOptions(
                        UiText(UiTextId::ArmorRacks),
                        UiText(UiTextId::ShowArmorRacks),
                        config.objects.armorRacks,
                        dpiScale);
                    saveRequested |= DrawAutomapObjectOptions(
                        UiText(UiTextId::WeaponRacks),
                        UiText(UiTextId::ShowWeaponRacks),
                        config.objects.weaponRacks,
                        dpiScale);
                }
            }

            if (ImGui::CollapsingHeader(UiText(UiTextId::Navigation))) {
                (void)ImGui::SliderFloat(
                    UiText(UiTextId::LineThickness),
                    &config.navigation.lineThickness,
                    MinimumNavigationLineThickness,
                    MaximumNavigationLineThickness,
                    "%.1f px",
                    ImGuiSliderFlags_AlwaysClamp);
                saveRequested |= ImGui::IsItemDeactivatedAfterEdit();

                saveRequested |= DrawNavigationLineOptions(
                    UiText(UiTextId::Waypoint),
                    UiText(UiTextId::WaypointLine),
                    config.navigation.waypoint,
                    dpiScale);
                saveRequested |= DrawNavigationLineOptions(
                    UiText(UiTextId::MainProgression),
                    UiText(UiTextId::MainProgressionLine),
                    config.navigation.progression,
                    dpiScale);
                saveRequested |= DrawNavigationLineOptions(
                    UiText(UiTextId::QuestTargets),
                    UiText(UiTextId::QuestTargetLine),
                    config.navigation.quests,
                    dpiScale);
                saveRequested |= DrawCustomLevelLineOptions(
                    config.navigation.customLevels,
                    dpiScale);
            }
        }
    }

    if (frameExpanded && !windowOpen) {
        expanded = false;
    }

    const auto positionChanged = oldPositionX != config.menu.positionX
        || oldPositionY != config.menu.positionY;
    if (positionChanged && !reanchorPosition && config.menu.rememberPosition)
        positionState.positionDirty = true;
    if (positionState.positionDirty
        && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        saveRequested = true;
        positionState.positionDirty = false;
    }
    if (!config.menu.rememberPosition)
        positionState.positionDirty = false;
    bounds.saveRequested = saveRequested;

    positionState.displaySize = io.DisplaySize;
    positionState.previousExpanded = frameExpanded;
    positionState.initialized = true;

    ImGui::End();
    return bounds;
}

} // namespace RuffnecKk::MapSense
