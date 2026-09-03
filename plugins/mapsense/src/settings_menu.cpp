#include "settings_menu.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::MapSense {
namespace {

constexpr auto Gold = ImVec4{0.86F, 0.67F, 0.24F, 1.0F};
constexpr auto MutedGold = ImVec4{0.62F, 0.48F, 0.22F, 1.0F};
constexpr auto Parchment = ImVec4{0.84F, 0.78F, 0.65F, 1.0F};
constexpr auto MutedText = ImVec4{0.53F, 0.49F, 0.42F, 1.0F};
constexpr auto PlannedText = ImVec4{0.78F, 0.54F, 0.22F, 1.0F};
constexpr float Pi = 3.14159265358979323846F;

enum class MarkerShape : std::uint8_t {
    Circle,
    Square,
    Diamond,
    Star,
    Hexagon,
};

struct MonsterPreviewEntry {
    const char* label;
    bool enabled;
    MarkerShape shape;
    ImVec4 color;
};

void DrawSectionTitle(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(Gold, "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

void DrawPlannedNotice(const char* text) {
    ImGui::TextColored(PlannedText, "PLANNED - NOT LIVE");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
}

void DrawDisabledFeature(const char* label) {
    bool placeholder = false;
    ImGui::BeginDisabled();
    ImGui::Checkbox(label, &placeholder);
    ImGui::EndDisabled();
}

auto ToColor(const RgbaColor& color) -> ImVec4 {
    return {color.red, color.green, color.blue, color.alpha};
}

void DrawColorEditor(const char* label, RgbaColor& color) {
    auto channels = std::array<float, 4>{
        color.red,
        color.green,
        color.blue,
        color.alpha,
    };
    if (ImGui::ColorEdit4(
            label,
            channels.data(),
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
        color = {channels[0], channels[1], channels[2], channels[3]};
    }
}

void DrawPolygon(
    ImDrawList& drawList,
    const ImVec2 center,
    const float radius,
    const int pointCount,
    const float rotation,
    const ImU32 color) {
    for (int point = 0; point < pointCount; ++point) {
        const auto angle = rotation
            + (2.0F * Pi * static_cast<float>(point)
                / static_cast<float>(pointCount));
        drawList.PathLineTo({
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius,
        });
    }
    drawList.PathFillConvex(color);
}

void DrawStar(
    ImDrawList& drawList,
    const ImVec2 center,
    const float radius,
    const ImU32 color) {
    constexpr int pointCount = 10;
    for (int point = 0; point < pointCount; ++point) {
        const auto pointRadius = (point % 2 == 0) ? radius : radius * 0.45F;
        const auto angle = -Pi * 0.5F
            + (2.0F * Pi * static_cast<float>(point)
                / static_cast<float>(pointCount));
        drawList.PathLineTo({
            center.x + std::cos(angle) * pointRadius,
            center.y + std::sin(angle) * pointRadius,
        });
    }
    drawList.PathFillConvex(color);
}

void DrawMarker(
    ImDrawList& drawList,
    const MarkerShape shape,
    const ImVec2 center,
    const float radius,
    const ImU32 color) {
    switch (shape) {
        case MarkerShape::Circle:
            drawList.AddCircleFilled(center, radius, color, 24);
            break;
        case MarkerShape::Square:
            drawList.AddRectFilled(
                {center.x - radius, center.y - radius},
                {center.x + radius, center.y + radius},
                color);
            break;
        case MarkerShape::Diamond:
            DrawPolygon(drawList, center, radius, 4, Pi * 0.25F, color);
            break;
        case MarkerShape::Star:
            DrawStar(drawList, center, radius, color);
            break;
        case MarkerShape::Hexagon:
            DrawPolygon(drawList, center, radius, 6, 0.0F, color);
            break;
    }
}

void DrawMonsterPreview(const Config& draft) {
    const auto monsters = std::array<MonsterPreviewEntry, 5>{
        MonsterPreviewEntry{
            "Normal", draft.monsters.normal, MarkerShape::Circle,
            {0.72F, 0.72F, 0.68F, 1.0F}},
        MonsterPreviewEntry{
            "Minion", draft.monsters.minion, MarkerShape::Square,
            {0.45F, 0.72F, 0.92F, 1.0F}},
        MonsterPreviewEntry{
            "Champion", draft.monsters.champion, MarkerShape::Diamond,
            {0.28F, 0.48F, 1.0F, 1.0F}},
        MonsterPreviewEntry{
            "Unique", draft.monsters.unique, MarkerShape::Star,
            {0.95F, 0.75F, 0.22F, 1.0F}},
        MonsterPreviewEntry{
            "Super Unique", draft.monsters.superUnique, MarkerShape::Hexagon,
            {0.92F, 0.28F, 0.18F, 1.0F}},
    };

    auto enabledCount = std::size_t{};
    for (const auto& monster : monsters) {
        if (monster.enabled) {
            ++enabledCount;
        }
    }

    const auto availableWidth = ImGui::GetContentRegionAvail().x;
    const auto canvasSize = ImVec2{std::max(availableWidth, 320.0F), 118.0F};
    ImGui::InvisibleButton("##monster-preview", canvasSize);

    const auto canvasMin = ImGui::GetItemRectMin();
    const auto canvasMax = ImGui::GetItemRectMax();
    auto& drawList = *ImGui::GetWindowDrawList();
    drawList.AddRectFilled(
        canvasMin,
        canvasMax,
        ImGui::ColorConvertFloat4ToU32({0.035F, 0.025F, 0.018F, 0.96F}),
        2.0F);
    drawList.AddRect(
        canvasMin,
        canvasMax,
        ImGui::ColorConvertFloat4ToU32(MutedGold),
        2.0F,
        ImDrawFlags_None,
        1.0F);

    if (enabledCount == 0U) {
        const char* emptyText = "No monster marker enabled";
        const auto textSize = ImGui::CalcTextSize(emptyText);
        drawList.AddText(
            {(canvasMin.x + canvasMax.x - textSize.x) * 0.5F,
             (canvasMin.y + canvasMax.y - textSize.y) * 0.5F},
            ImGui::ColorConvertFloat4ToU32(MutedText),
            emptyText);
        return;
    }

    const auto spacing = canvasSize.x / static_cast<float>(enabledCount + 1U);
    auto visibleIndex = std::size_t{};
    const auto radius = std::clamp(draft.monsters.markerSize, 3.0F, 30.0F);
    for (const auto& monster : monsters) {
        if (!monster.enabled) {
            continue;
        }
        ++visibleIndex;
        const auto center = ImVec2{
            canvasMin.x + spacing * static_cast<float>(visibleIndex),
            canvasMin.y + 42.0F,
        };
        DrawMarker(
            drawList,
            monster.shape,
            center,
            radius,
            ImGui::ColorConvertFloat4ToU32(monster.color));

        const auto labelSize = ImGui::CalcTextSize(monster.label);
        drawList.AddText(
            {center.x - labelSize.x * 0.5F, canvasMin.y + 82.0F},
            ImGui::ColorConvertFloat4ToU32(Parchment),
            monster.label);
    }
}

void DrawImmunityPreview(const ImmunityOptions& immunities) {
    const auto colors = std::array<ImVec4, 6>{
        ToColor(immunities.physical),
        ToColor(immunities.fire),
        ToColor(immunities.cold),
        ToColor(immunities.lightning),
        ToColor(immunities.poison),
        ToColor(immunities.magic),
    };

    const auto canvasSize = ImVec2{ImGui::GetContentRegionAvail().x, 78.0F};
    ImGui::InvisibleButton("##immunity-preview", canvasSize);
    const auto canvasMin = ImGui::GetItemRectMin();
    const auto center = ImVec2{canvasMin.x + 42.0F, canvasMin.y + 38.0F};
    auto& drawList = *ImGui::GetWindowDrawList();

    drawList.AddCircleFilled(
        center,
        17.0F,
        ImGui::ColorConvertFloat4ToU32({0.28F, 0.12F, 0.09F, 1.0F}),
        24);
    for (auto index = std::size_t{}; index < colors.size(); ++index) {
        constexpr auto segment = (2.0F * Pi) / 6.0F;
        const auto start = -Pi * 0.5F + segment * static_cast<float>(index);
        const auto end = start + segment - 0.045F;
        drawList.PathArcTo(center, 25.0F, start, end, 10);
        drawList.PathStroke(
            ImGui::ColorConvertFloat4ToU32(colors[index]),
            ImDrawFlags_None,
            5.0F);
    }
    drawList.AddText(
        {canvasMin.x + 82.0F, canvasMin.y + 17.0F},
        ImGui::ColorConvertFloat4ToU32(Parchment),
        "Immunity ring preview");
    drawList.AddText(
        {canvasMin.x + 82.0F, canvasMin.y + 40.0F},
        ImGui::ColorConvertFloat4ToU32(MutedText),
        immunities.enabled ? "Enabled" : "Disabled");
}

void DrawMapTab(Config& draft) {
    DrawSectionTitle("MapSense overlay");
    ImGui::Checkbox("Enable MapSense", &draft.enabled);
    ImGui::Checkbox(
        "Show diagnostic witness scene",
        &draft.overlay.diagnosticPreview);
    ImGui::Checkbox(
        "Show map layer only with native automap",
        &draft.overlay.followNativeAutomap);
    ImGui::SliderFloat(
        "Overlay opacity",
        &draft.overlay.opacity,
        0.10F,
        1.0F,
        "%.2f");
    ImGui::SliderFloat(
        "Interface scale",
        &draft.overlay.scale,
        0.50F,
        2.0F,
        "%.2fx");

    auto frameRate = static_cast<int>(draft.overlay.frameRate);
    if (ImGui::SliderInt("Frame rate cap", &frameRate, 15, 240, "%d FPS")) {
        draft.overlay.frameRate = static_cast<std::int32_t>(frameRate);
    }

    DrawSectionTitle("Native automap");
    ImGui::TextWrapped(
        "Reveal Zone, Reveal Act, and Reveal All remain native MapSense "
        "commands and hotkey actions. This menu does not change their scope.");
}

void DrawMonstersTab(Config& draft) {
    DrawPlannedNotice(
        "Live monster collection is not connected. These controls affect "
        "the diagnostic preview and preserve the intended display settings.");
    DrawSectionTitle("Monster marker types");
    ImGui::Checkbox("Normal", &draft.monsters.normal);
    ImGui::Checkbox("Minion", &draft.monsters.minion);
    ImGui::Checkbox("Champion", &draft.monsters.champion);
    ImGui::Checkbox("Unique", &draft.monsters.unique);
    ImGui::Checkbox("Super Unique", &draft.monsters.superUnique);
    ImGui::SliderFloat(
        "Marker size",
        &draft.monsters.markerSize,
        3.0F,
        30.0F,
        "%.0f px");
    DrawSectionTitle("Preview");
    DrawMonsterPreview(draft);
}

void DrawImmunitiesTab(Config& draft) {
    DrawPlannedNotice(
        "Live resistance collection is not connected. The palette and ring "
        "below are active in the diagnostic preview only.");
    DrawSectionTitle("Immunity ring");
    ImGui::Checkbox("Enable immunity colors", &draft.immunities.enabled);
    DrawColorEditor("Physical", draft.immunities.physical);
    DrawColorEditor("Fire", draft.immunities.fire);
    DrawColorEditor("Cold", draft.immunities.cold);
    DrawColorEditor("Lightning", draft.immunities.lightning);
    DrawColorEditor("Poison", draft.immunities.poison);
    DrawColorEditor("Magic", draft.immunities.magic);
    DrawSectionTitle("Preview");
    DrawImmunityPreview(draft.immunities);
}

void DrawNavigationTab() {
    DrawPlannedNotice(
        "Navigation collectors and pathfinding are deliberately disabled "
        "until their runtime data and map alignment are proven.");
    ImGui::TextWrapped(
        "MapSense does not redraw players, hostile players, waypoints, "
        "portals, or standard symbols already supplied by D2R's native automap.");
    DrawSectionTitle("MapSense additions");
    DrawDisabledFeature("Named destinations and route-aware exits");
    DrawDisabledFeature("Unmarked quest objectives and bosses");
    DrawDisabledFeature("Unmarked or additionally identified shrines and wells");
    DrawDisabledFeature("Super chests");
    DrawDisabledFeature("Weapon and armor racks");
    DrawSectionTitle("Guidance");
    DrawDisabledFeature("Direction lines");
    DrawDisabledFeature("Computed paths");
    DrawDisabledFeature("Off-screen edge indicators");
}

void DrawSystemTab(Config& draft) {
    DrawSectionTitle("Settings launcher");
    ImGui::Checkbox("Show collapsed launcher", &draft.menu.showLauncher);
    ImGui::Checkbox("Start with settings expanded", &draft.menu.startExpanded);
    ImGui::Checkbox("Remember launcher position", &draft.menu.rememberPosition);
    ImGui::TextWrapped(
        "The launcher remains a small movable window while the settings "
        "panel is collapsed. The D2R Controls action can always toggle it.");

    DrawSectionTitle("Diagnostics");
    ImGui::Checkbox("Enable diagnostic logging", &draft.diagnostics);
    ImGui::TextWrapped(
        "Diagnostic logging is intended for development and runtime "
        "qualification. Disable it for normal play unless logs are needed.");

    DrawSectionTitle("Implementation status");
    ImGui::BulletText("Native map reveal: available");
    ImGui::BulletText("Overlay witness scene: available when renderer starts");
    ImGui::BulletText("Monster and immunity live collectors: planned - not live");
    ImGui::BulletText("Navigation and pathfinding: planned - not live");
}

} // namespace

void ApplyD2RStyle() noexcept {
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {14.0F, 12.0F};
    style.FramePadding = {8.0F, 5.0F};
    style.CellPadding = {7.0F, 5.0F};
    style.ItemSpacing = {9.0F, 7.0F};
    style.ItemInnerSpacing = {6.0F, 5.0F};
    style.ScrollbarSize = 13.0F;
    style.GrabMinSize = 10.0F;
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.TabBorderSize = 1.0F;
    style.WindowRounding = 2.0F;
    style.ChildRounding = 2.0F;
    style.FrameRounding = 1.0F;
    style.PopupRounding = 2.0F;
    style.ScrollbarRounding = 1.0F;
    style.GrabRounding = 1.0F;
    style.TabRounding = 1.0F;

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = Parchment;
    colors[ImGuiCol_TextDisabled] = MutedText;
    colors[ImGuiCol_WindowBg] = {0.025F, 0.018F, 0.014F, 0.98F};
    colors[ImGuiCol_ChildBg] = {0.035F, 0.024F, 0.017F, 0.94F};
    colors[ImGuiCol_PopupBg] = {0.045F, 0.030F, 0.020F, 0.99F};
    colors[ImGuiCol_Border] = MutedGold;
    colors[ImGuiCol_BorderShadow] = {0.0F, 0.0F, 0.0F, 0.65F};
    colors[ImGuiCol_FrameBg] = {0.12F, 0.065F, 0.038F, 0.96F};
    colors[ImGuiCol_FrameBgHovered] = {0.22F, 0.12F, 0.055F, 1.0F};
    colors[ImGuiCol_FrameBgActive] = {0.30F, 0.17F, 0.07F, 1.0F};
    colors[ImGuiCol_TitleBg] = {0.055F, 0.030F, 0.020F, 1.0F};
    colors[ImGuiCol_TitleBgActive] = {0.12F, 0.065F, 0.030F, 1.0F};
    colors[ImGuiCol_TitleBgCollapsed] = {0.035F, 0.020F, 0.015F, 0.92F};
    colors[ImGuiCol_MenuBarBg] = {0.07F, 0.038F, 0.023F, 1.0F};
    colors[ImGuiCol_ScrollbarBg] = {0.025F, 0.018F, 0.014F, 0.75F};
    colors[ImGuiCol_ScrollbarGrab] = {0.34F, 0.22F, 0.09F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = {0.50F, 0.34F, 0.13F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabActive] = {0.68F, 0.48F, 0.17F, 1.0F};
    colors[ImGuiCol_CheckMark] = Gold;
    colors[ImGuiCol_SliderGrab] = MutedGold;
    colors[ImGuiCol_SliderGrabActive] = Gold;
    colors[ImGuiCol_Button] = {0.18F, 0.09F, 0.045F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = {0.34F, 0.20F, 0.075F, 1.0F};
    colors[ImGuiCol_ButtonActive] = {0.48F, 0.30F, 0.095F, 1.0F};
    colors[ImGuiCol_Header] = {0.19F, 0.10F, 0.045F, 1.0F};
    colors[ImGuiCol_HeaderHovered] = {0.31F, 0.19F, 0.07F, 1.0F};
    colors[ImGuiCol_HeaderActive] = {0.43F, 0.28F, 0.10F, 1.0F};
    colors[ImGuiCol_Separator] = MutedGold;
    colors[ImGuiCol_SeparatorHovered] = Gold;
    colors[ImGuiCol_SeparatorActive] = {0.98F, 0.78F, 0.29F, 1.0F};
    colors[ImGuiCol_ResizeGrip] = {0.44F, 0.29F, 0.10F, 0.55F};
    colors[ImGuiCol_ResizeGripHovered] = {0.72F, 0.52F, 0.17F, 0.85F};
    colors[ImGuiCol_ResizeGripActive] = Gold;
    colors[ImGuiCol_Tab] = {0.10F, 0.055F, 0.030F, 1.0F};
    colors[ImGuiCol_TabHovered] = {0.34F, 0.20F, 0.075F, 1.0F};
    colors[ImGuiCol_TabSelected] = {0.24F, 0.135F, 0.050F, 1.0F};
    colors[ImGuiCol_TabSelectedOverline] = Gold;
    colors[ImGuiCol_TabDimmed] = {0.055F, 0.030F, 0.020F, 1.0F};
    colors[ImGuiCol_TabDimmedSelected] = {0.12F, 0.065F, 0.030F, 1.0F};
    colors[ImGuiCol_PlotLines] = MutedGold;
    colors[ImGuiCol_PlotLinesHovered] = Gold;
    colors[ImGuiCol_TextSelectedBg] = {0.45F, 0.28F, 0.08F, 0.55F};
    colors[ImGuiCol_NavHighlight] = Gold;
}

auto DrawSettingsSurface(
    Config& draft,
    bool& expanded,
    const SettingsSaveCallback saveCallback) noexcept -> SettingsSurfaceBounds {
    if (!expanded && !draft.menu.showLauncher) return {};

    const auto& io = ImGui::GetIO();
    const auto dpiScale = std::max(1.0F, io.FontGlobalScale);
    const auto desiredWidth = std::min(
        480.0F * dpiScale,
        std::max(1.0F, io.DisplaySize.x));
    const auto desiredHeight = std::min(
        720.0F * dpiScale,
        std::max(1.0F, io.DisplaySize.y * 0.82F));
    const ImVec2 expandedSize{desiredWidth, desiredHeight};
    const ImVec2 collapsedSize{
        std::min(240.0F * dpiScale, std::max(1.0F, io.DisplaySize.x)),
        ImGui::GetFrameHeight() + (8.0F * dpiScale),
    };
    const auto requestedSize = expanded ? expandedSize : collapsedSize;
    const auto availableX = std::max(0.0F, io.DisplaySize.x - requestedSize.x);
    const auto availableY = std::max(0.0F, io.DisplaySize.y - requestedSize.y);
    ImGui::SetNextWindowPos(
        {
            std::clamp(draft.menu.positionX, 0.0F, 1.0F) * availableX,
            std::clamp(draft.menu.positionY, 0.0F, 1.0F) * availableY,
        },
        ImGuiCond_Once);
    ImGui::SetNextWindowSize(requestedSize, ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(!expanded, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96F);

    constexpr auto windowFlags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings;
    const auto contentVisible = ImGui::Begin(
        "RuffnecKk MapSense",
        nullptr,
        windowFlags);
    expanded = !ImGui::IsWindowCollapsed();

    const auto windowPosition = ImGui::GetWindowPos();
    const auto windowSize = ImGui::GetWindowSize();
    const auto positionRangeX = std::max(0.0F, io.DisplaySize.x - windowSize.x);
    const auto positionRangeY = std::max(0.0F, io.DisplaySize.y - windowSize.y);
    if (draft.menu.rememberPosition) {
        draft.menu.positionX = positionRangeX > 0.0F
            ? std::clamp(windowPosition.x / positionRangeX, 0.0F, 1.0F)
            : 0.0F;
        draft.menu.positionY = positionRangeY > 0.0F
            ? std::clamp(windowPosition.y / positionRangeY, 0.0F, 1.0F)
            : 0.0F;
    }

    const SettingsSurfaceBounds bounds{
        .visible = true,
        .left = windowPosition.x,
        .top = windowPosition.y,
        .right = windowPosition.x + windowSize.x,
        .bottom = windowPosition.y + windowSize.y,
    };
    if (!contentVisible) {
        ImGui::End();
        return bounds;
    }

    ImGui::TextColored(Gold, "MAPSENSE");
    ImGui::SameLine();
    ImGui::TextColored(MutedText, "  Automap intelligence");
    ImGui::Separator();

    if (ImGui::CollapsingHeader(
            "Map & Reveal",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawMapTab(draft);
    }
    if (ImGui::CollapsingHeader("Monsters & Immunities")) {
        DrawMonstersTab(draft);
        ImGui::Spacing();
        DrawImmunitiesTab(draft);
    }
    if (ImGui::CollapsingHeader("Map Additions")) {
        DrawPlannedNotice(
            "Named destinations, super chests, racks, and off-screen markers "
            "will appear here after their live collectors are qualified.");
    }
    if (ImGui::CollapsingHeader("Missiles")) {
        DrawPlannedNotice(
            "Incoming projectile tracking will appear here after the live "
            "missile collector and trajectory filter are qualified.");
    }
    if (ImGui::CollapsingHeader("Lines & Pathfinding")) {
        DrawNavigationTab();
    }
    if (ImGui::CollapsingHeader("Interface & Diagnostics")) {
        DrawSystemTab(draft);
    }

    const auto footerHeight = ImGui::GetFrameHeightWithSpacing() + 8.0F;
    const auto remainingHeight = ImGui::GetContentRegionAvail().y;
    if (remainingHeight > footerHeight) {
        ImGui::Dummy({0.0F, remainingHeight - footerHeight});
    }
    ImGui::Separator();

    if (ImGui::Button("Apply & Save", {132.0F, 0.0F})) {
        if (saveCallback) {
            saveCallback(draft);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", {90.0F, 0.0F})) {
        draft = Config{};
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            "Restores defaults in this draft. Use Apply & Save to persist them.");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::TextColored(MutedText, "Reset does not save automatically.");

    ImGui::End();
    return bounds;
}

} // namespace RuffnecKk::MapSense
