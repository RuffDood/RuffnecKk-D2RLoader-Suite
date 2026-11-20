#include "imgui_settings_panel.hpp"
#include "ui_localization.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <iostream>

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

constexpr std::array Sections{UiTextId::Appearance, UiTextId::MapAndReveal,
    UiTextId::Monsters, UiTextId::Immunities, UiTextId::Missiles,
    UiTextId::Objects, UiTextId::Navigation};

auto Panel() -> ImGuiWindow* {
    return ImGui::FindWindowByName("MapSense###RuffnecKkMapSenseSettings");
}

auto Frame(Config& config, bool& expanded, std::uint64_t session = 1U)
        -> ImGuiSettingsBounds {
    ImGui::NewFrame();
    const auto result = DrawImGuiSettingsPanel(config, expanded, session, false, false, 1.0F, nullptr);
    ImGui::Render();
    return result;
}

void CheckCollapsedSections() {
    for (const auto section : Sections) {
        CHECK(Panel()->StateStorage.GetInt(Panel()->GetID(UiText(section)), 0) == 0);
    }
}
} // namespace

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1920.0F, 1080.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    CHECK(io.Fonts->Build());
    Config config{};
    bool expanded{};
    CHECK(!Frame(config, expanded).expanded);
    // Dear ImGui keeps the preceding game's header state in this context.
    for (const auto section : Sections) {
        Panel()->StateStorage.SetInt(Panel()->GetID(UiText(section)), 1);
    }
    expanded = true;
    CHECK(Frame(config, expanded).expanded);
    CheckCollapsedSections();

    // Opening sections during a game must work and stay open on later frames.
    for (const auto section : Sections) {
        Panel()->StateStorage.SetInt(Panel()->GetID(UiText(section)), 1);
    }
    CHECK(!Frame(config, expanded).saveRequested);
    for (const auto section : Sections) {
        CHECK(Panel()->StateStorage.GetInt(Panel()->GetID(UiText(section)), 0) == 1);
    }
    config.menu.theme = MenuTheme::ArcaneSanctuary;
    config.overlay.opacity = 0.43F;
    config.revealMap = true;
    config.menu.startExpanded = true; // Obsolete inputs cannot force open.
    config.overlay.startMenuOpen = true;
    const auto savedSettings = SerializeConfig(config);
    const auto nextGame = Frame(config, expanded, 2U);
    CHECK(!nextGame.expanded && !expanded && !nextGame.saveRequested);
    CHECK(SerializeConfig(config) == savedSettings);
    expanded = true;
    CHECK(Frame(config, expanded, 2U).expanded);
    CheckCollapsedSections();

    // Drive the real checkbox: a click changes the setting and asks for a save.
    const auto rowHeight = ImGui::GetFontSize()
        + ImGui::GetStyle().FramePadding.y * 2.0F + ImGui::GetStyle().ItemSpacing.y;
    const auto start = Panel()->DC.CursorStartPos;
    io.AddMousePosEvent(start.x + 8.0F, start.y + rowHeight + 8.0F);
    (void)Frame(config, expanded, 2U);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(!Frame(config, expanded, 2U).saveRequested);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(Frame(config, expanded, 2U).saveRequested);
    CHECK(!config.enabled);
    CHECK(!Frame(config, expanded, 2U).saveRequested);

    // A drag changes the live value, saves once on release, and cannot lose
    // that save when the panel closes before the edited item deactivates.
    Panel()->StateStorage.SetInt(Panel()->GetID(UiText(UiTextId::MapAndReveal)), 1);
    (void)Frame(config, expanded, 2U);
    const auto opacityBefore = config.overlay.opacity;
    io.AddMousePosEvent(start.x + 60.0F, start.y + rowHeight * 6.0F + 8.0F);
    (void)Frame(config, expanded, 2U);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(!Frame(config, expanded, 2U).saveRequested);
    io.AddMousePosEvent(start.x + 220.0F, start.y + rowHeight * 6.0F + 8.0F);
    CHECK(!Frame(config, expanded, 2U).saveRequested);
    CHECK(config.overlay.opacity != opacityBefore);
    expanded = false;
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(Frame(config, expanded, 2U).saveRequested);
    CHECK(!Frame(config, expanded, 2U).saveRequested);

    // New sessions reset even when the preceding frame was already collapsed.
    CHECK(!Frame(config, expanded, 3U).expanded);
    expanded = true;
    (void)Frame(config, expanded, 3U);
    CheckCollapsedSections();
    ImGui::DestroyContext();
    return Failures == 0 ? 0 : 1;
}
