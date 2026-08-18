#include "policy.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main(int argc, char** argv) {
    using namespace ruffneckk::remote_stash;

    Hotkey hotkey{};
    REQUIRE(ParseHotkey("F24", hotkey));
    REQUIRE(hotkey.virtualKey == 0x87 && !IsMouseHotkey(hotkey));
    REQUIRE(!ParseHotkey("F25", hotkey));
    REQUIRE(!ParseHotkey("F4294967297", hotkey));
    REQUIRE(ParseHotkey(";", hotkey));
    REQUIRE(hotkey.virtualKey == 0xBA && IsSdkInputCompatible(hotkey));
    REQUIRE(ParseHotkey("CTRL+SHIFT+F3", hotkey));
    REQUIRE(!IsSdkInputCompatible(hotkey));
    REQUIRE(ParseHotkey("MOUSE4", hotkey));
    REQUIRE(IsMouseHotkey(hotkey) && !IsSdkInputCompatible(hotkey));

    REQUIRE(argc == 5);
    std::ifstream configFile(argv[1], std::ios::binary);
    REQUIRE(configFile.good());
    std::ostringstream configStream;
    configStream << configFile.rdbuf();

    HotkeyConfig config{};
    std::string error;
    REQUIRE(ParseConfig(configStream.str(), config, error));
    REQUIRE(config.enabled);
    REQUIRE(config.inventoryButtonEnabled);
    REQUIRE(config.hotkeyEnabled);
    REQUIRE(!config.diagnostics);
    REQUIRE(config.hotkeyText == "SHIFT+R");
    REQUIRE(config.hotkey.virtualKey == 'R');
    REQUIRE(config.hotkey.shift);
    REQUIRE(IsSdkInputCompatible(config.hotkey));
    REQUIRE(config.mode == HotkeyMode::RemoteOnly);
    REQUIRE(config.button.placement == ButtonPlacement::Automatic);
    REQUIRE(config.button.anchor == ButtonAnchor::BottomLeft);
    REQUIRE(config.button.offsetX == 0 && config.button.offsetY == 0);
    REQUIRE(config.button.width == 176 && config.button.height == 112);
    REQUIRE(config.button.spriteFile.empty());
    REQUIRE(config.button.lowendSpriteFile.empty());
    REQUIRE(config.button.normalFrame == 0);
    REQUIRE(config.button.pressedFrame == 2);
    REQUIRE(config.button.disabledFrame == 1);
    REQUIRE(config.button.hoveredFrame == 3);

    HotkeyConfig enabled{};
    REQUIRE(ParseConfig(
        "enabled = true\nhotkey = \"ALT+F12\"\n",
        enabled,
        error));
    REQUIRE(enabled.enabled);
    REQUIRE(enabled.inventoryButtonEnabled);
    REQUIRE(enabled.hotkeyEnabled);
    REQUIRE(enabled.hotkey.virtualKey == 0x7B);
    REQUIRE(enabled.hotkey.alt);
    REQUIRE(SdkModifierValue(enabled.hotkey) == 3);
    REQUIRE(enabled.mode == HotkeyMode::RemoteOnly);

    auto invalid = configStream.str();
    invalid += "\nunknown = true\n";
    REQUIRE(!ParseConfig(invalid, config, error));
    REQUIRE(ParseConfig(
        "enabled = true\nhotkey = \";\"\nhotkey_mode = \"remoteAndInventory\"\n",
        config,
        error));
    REQUIRE(config.mode == HotkeyMode::RemoteAndInventory);
    REQUIRE(ParseConfig(
        "enabled = true\ninventory_button_enabled = false\n"
        "hotkey_enabled = true\nhotkey = \"SHIFT+R\"\n"
        "[diagnostics]\nenabled = true\n",
        config,
        error));
    REQUIRE(config.enabled);
    REQUIRE(!config.inventoryButtonEnabled);
    REQUIRE(config.hotkeyEnabled);
    REQUIRE(config.diagnostics);
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \"SHIFT+R\"\ndiagnostics = false\n"
        "[diagnostics]\nenabled = true\n",
        config,
        error));
    REQUIRE(ParseConfig(
        "enabled = false\nhotkey = \"SHIFT+R\"\n",
        config,
        error));
    REQUIRE(config.enabled);
    REQUIRE(config.inventoryButtonEnabled && !config.hotkeyEnabled);
    REQUIRE(ParseConfig(
        "enabled = false\ninventory_button_enabled = true\n"
        "hotkey_enabled = true\nhotkey = \"SHIFT+R\"\n",
        config,
        error));
    REQUIRE(!config.enabled);
    REQUIRE(config.inventoryButtonEnabled && config.hotkeyEnabled);
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \";\"\nhotkey_mode = \"paired\"\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \";\"\nhotkey_mode = \"remoteOnly\"\n"
        "hotkey_mode = \"remoteAndInventory\"\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \";\"\nhotkey = \"F1\"\n",
        config,
        error));
    REQUIRE(ParseConfig(
        "enabled = true\ninventory_button_enabled = true\n"
        "hotkey_enabled = false\nhotkey = \"SHIFT+R\"\n"
        "[button]\nplacement = \"custom\"\nanchor = \"topRight\"\n"
        "offset_x = -42\noffset_y = 17\nwidth = 222\nheight = 99\n"
        "sprite_file = 'sprites\\custom.sprite'\n"
        "lowend_sprite_file = \"sprites/custom.lowend.sprite\"\n"
        "normal_frame = 4\npressed_frame = 5\n"
        "disabled_frame = 6\nhovered_frame = 7\n",
        config,
        error));
    REQUIRE(config.button.placement == ButtonPlacement::Custom);
    REQUIRE(config.button.anchor == ButtonAnchor::TopRight);
    REQUIRE(config.button.offsetX == -42 && config.button.offsetY == 17);
    REQUIRE(config.button.width == 222 && config.button.height == 99);
    REQUIRE(config.button.spriteFile == "sprites\\custom.sprite");
    REQUIRE(config.button.lowendSpriteFile == "sprites/custom.lowend.sprite");
    REQUIRE(config.button.normalFrame == 4);
    REQUIRE(config.button.pressedFrame == 5);
    REQUIRE(config.button.disabledFrame == 6);
    REQUIRE(config.button.hoveredFrame == 7);
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \"SHIFT+R\"\n"
        "[button]\nwidth = 0\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \"SHIFT+R\"\n"
        "[button]\nhovered_frame = 256\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \"SHIFT+R\"\n"
        "[button]\nanchor = \"middle\"\n",
        config,
        error));
    REQUIRE(!ParseConfig(
        "enabled = true\n",
        config,
        error));

    auto plan = ResolveRemoteTogglePlan(
        ToggleSource::Hotkey,
        HotkeyMode::RemoteOnly,
        false,
        false,
        false);
    REQUIRE(plan.dispatch == HotkeyDispatch::Open);
    REQUIRE(plan.closeCompanionInventoryAfterOpen);
    REQUIRE(!plan.coupleInventory);

    plan = ResolveRemoteTogglePlan(
        ToggleSource::Hotkey,
        HotkeyMode::RemoteAndInventory,
        true,
        false,
        true);
    REQUIRE(plan.dispatch == HotkeyDispatch::Close);
    REQUIRE(plan.closeInventoryAfterClose);
    REQUIRE(!plan.preserveInventoryAfterClose);
    REQUIRE(plan.coupleInventory);

    plan = ResolveRemoteTogglePlan(
        ToggleSource::Button,
        HotkeyMode::RemoteAndInventory,
        true,
        true,
        true);
    REQUIRE(plan.dispatch == HotkeyDispatch::Close);
    REQUIRE(!plan.closeInventoryAfterClose);
    REQUIRE(plan.preserveInventoryAfterClose);
    REQUIRE(!plan.coupleInventory);

    plan = ResolveRemoteTogglePlan(
        ToggleSource::Hotkey,
        HotkeyMode::RemoteOnly,
        false,
        false,
        true);
    REQUIRE(plan.dispatch == HotkeyDispatch::Open);
    REQUIRE(!plan.closeCompanionInventoryAfterOpen);
    REQUIRE(!plan.coupleInventory);

    auto rollback = ResolveRemoteOpenRollbackPlan(false, false);
    REQUIRE(!rollback.closeInventory && !rollback.openInventory);
    rollback = ResolveRemoteOpenRollbackPlan(false, true);
    REQUIRE(rollback.closeInventory && !rollback.openInventory);
    rollback = ResolveRemoteOpenRollbackPlan(true, false);
    REQUIRE(!rollback.closeInventory && rollback.openInventory);
    rollback = ResolveRemoteOpenRollbackPlan(true, true);
    REQUIRE(!rollback.closeInventory && !rollback.openInventory);
    REQUIRE(!ShouldKeepInventoryOpenAfterRemoteOpen(false, false));
    REQUIRE(ShouldKeepInventoryOpenAfterRemoteOpen(false, true));
    REQUIRE(ShouldKeepInventoryOpenAfterRemoteOpen(true, false));
    REQUIRE(ShouldKeepInventoryOpenAfterRemoteOpen(true, true));

    REQUIRE(ShouldRestoreIndependentInventory(true, false));
    REQUIRE(!ShouldRestoreIndependentInventory(false, false));
    REQUIRE(!ShouldRestoreIndependentInventory(true, true));
    REQUIRE(ResolveCompanionInventoryClose(0, true, 10)
        == CompanionInventoryCloseDecision::Wait);
    REQUIRE(ResolveCompanionInventoryClose(20, true, 10)
        == CompanionInventoryCloseDecision::Close);
    REQUIRE(ResolveCompanionInventoryClose(20, true, 21)
        == CompanionInventoryCloseDecision::Expire);

    REQUIRE(IsRemoteStashUiMessage(
        "PanelManager", "OpenPanel", "RuffnecKkRemoteStash"));
    REQUIRE(!IsRemoteStashUiMessage(
        "PlayerInventoryPanelMessage", "DropGold", ""));
    REQUIRE(!IsRemoteStashUiMessage(
        "PanelManager", "OpenPanel", "SettingsPanel"));

    auto closePlan = ResolvePairedClosePlan(
        true,
        false,
        true,
        PairedInterface::Stash,
        PairedCloseOrigin::Movement);
    REQUIRE(closePlan.suppress);
    REQUIRE(!closePlan.deactivate);
    REQUIRE(!closePlan.notifyServer);
    REQUIRE(!closePlan.closeStash);
    REQUIRE(!closePlan.closeInventory);
    REQUIRE(static_cast<unsigned>(closePlan.notifyServer) == 0U);

    closePlan = ResolvePairedClosePlan(
        true,
        true,
        true,
        PairedInterface::Stash,
        PairedCloseOrigin::Server);
    REQUIRE(!closePlan.suppress);
    REQUIRE(closePlan.deactivate);
    REQUIRE(closePlan.notifyServer);
    REQUIRE(closePlan.closeStash);
    REQUIRE(closePlan.closeInventory);
    REQUIRE(static_cast<unsigned>(closePlan.notifyServer) == 1U);

    closePlan = ResolvePairedClosePlan(
        true,
        false,
        true,
        PairedInterface::Stash,
        PairedCloseOrigin::GeneralTeardown);
    REQUIRE(!closePlan.suppress);
    REQUIRE(closePlan.deactivate);
    REQUIRE(closePlan.notifyServer);
    REQUIRE(closePlan.closeStash);
    REQUIRE(!closePlan.closeInventory);
    REQUIRE(!closePlan.preserveInventory);
    REQUIRE(static_cast<unsigned>(closePlan.notifyServer) == 1U);

    closePlan = ResolvePairedClosePlan(
        true,
        false,
        true,
        PairedInterface::Inventory,
        PairedCloseOrigin::Inventory);
    REQUIRE(!closePlan.deactivate);
    REQUIRE(!closePlan.notifyServer);

    closePlan = ResolvePairedClosePlan(
        true,
        true,
        true,
        PairedInterface::Inventory,
        PairedCloseOrigin::Inventory);
    REQUIRE(closePlan.deactivate);
    REQUIRE(closePlan.notifyServer);
    REQUIRE(closePlan.closeStash && closePlan.closeInventory);

    closePlan = ResolvePairedClosePlan(
        true,
        false,
        true,
        PairedInterface::Stash,
        PairedCloseOrigin::StashButton);
    REQUIRE(closePlan.deactivate);
    REQUIRE(closePlan.closeStash);
    REQUIRE(!closePlan.closeInventory);
    REQUIRE(closePlan.preserveInventory);

    closePlan = ResolvePairedClosePlan(
        true,
        false,
        true,
        PairedInterface::Inventory,
        PairedCloseOrigin::Movement);
    REQUIRE(closePlan.suppress);
    REQUIRE(!closePlan.deactivate);

    closePlan = ResolvePairedClosePlan(
        true,
        false,
        false,
        PairedInterface::Inventory,
        PairedCloseOrigin::Movement);
    REQUIRE(!closePlan.suppress);
    REQUIRE(!closePlan.deactivate);

    // Deactivation makes a second close idempotent: only the first close plans a
    // server notification.
    closePlan = ResolvePairedClosePlan(
        false,
        false,
        false,
        PairedInterface::Inventory,
        PairedCloseOrigin::Other);
    REQUIRE(!closePlan.deactivate);
    REQUIRE(!closePlan.notifyServer);

    const auto readBytes = [](const char* path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::vector<std::uint8_t>{};
        return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    };
    const auto sprite = readBytes(argv[2]);
    const auto lowend = readBytes(argv[3]);
    REQUIRE(!sprite.empty() && !lowend.empty());
    SpriteMetadata spriteMetadata{};
    SpriteMetadata lowendMetadata{};
    REQUIRE(InspectSpA1Sprite(
        sprite.data(), sprite.size(), spriteMetadata));
    REQUIRE(InspectSpA1Sprite(
        lowend.data(), lowend.size(), lowendMetadata));
    REQUIRE(spriteMetadata.frameCount == 4);
    REQUIRE(spriteMetadata.frameWidth == 176);
    REQUIRE(spriteMetadata.height == 112);
    REQUIRE(lowendMetadata.frameCount == 4);
    REQUIRE(lowendMetadata.frameWidth == 88);
    REQUIRE(lowendMetadata.height == 56);

    const ButtonConfig defaultButton{};
    REQUIRE(ButtonFramesFit(defaultButton, spriteMetadata.frameCount));
    REQUIRE(!ButtonFramesFit(defaultButton, 3));
    const auto layout = BuildButtonLayoutJson(defaultButton);
    REQUIRE(layout.find("\"type\": \"ButtonWidget\"")
        != std::string::npos);
    REQUIRE(layout.find(
        "\"name\": \"ruffneckk-remote-stash/inventory-button\"")
        != std::string::npos);
    REQUIRE(layout.find("\"x\": -32000") != std::string::npos);
    REQUIRE(layout.find("\"width\": 176") != std::string::npos);
    REQUIRE(layout.find("\"height\": 112") != std::string::npos);
    REQUIRE(layout.find(
        "D2RLoader\\\\ruffneckk-remote-stash\\\\inventory-button")
        != std::string::npos);
    REQUIRE(layout.find("\"normalFrame\": 0") != std::string::npos);
    REQUIRE(layout.find("\"pressedFrame\": 2") != std::string::npos);
    REQUIRE(layout.find("\"disabledFrame\": 1") != std::string::npos);
    REQUIRE(layout.find("\"hoveredFrame\": 3") != std::string::npos);
    REQUIRE(layout.find("PanelManager:OpenPanel:RuffnecKkRemoteStash")
        != std::string::npos);
    REQUIRE(layout.find("@OpenCurrentStashLegend") != std::string::npos);
    REQUIRE(layout.find("DropGold") == std::string::npos);

    const WidgetRect panel{0, 0, 1000, 1600};
    const WidgetRect grid{100, 200, 700, 1100};
    const WidgetRect goldButton{780, 1380, 80, 80};
    const WidgetRect goldAmount{860, 1380, 120, 80};
    const WidgetRect button{0, 0, 176, 112};
    const auto automatic = PlaceDesktopFooterLeft(
        panel, grid, goldButton, goldAmount, button);
    REQUIRE(automatic.valid);
    REQUIRE(automatic.rect.x == grid.x);
    REQUIRE(!Intersects(automatic.rect, grid));
    REQUIRE(!Intersects(automatic.rect, UnionRect(goldButton, goldAmount)));
    auto custom = PlaceAtAnchor(
        panel, button, ButtonAnchor::BottomRight, -20, -30, true);
    REQUIRE(custom.valid);
    REQUIRE(custom.rect.x == 804 && custom.rect.y == 1458);
    custom = PlaceAtAnchor(
        panel, button, ButtonAnchor::TopLeft, -50, -60, false);
    REQUIRE(custom.valid && custom.rect.x == -50 && custom.rect.y == -60);

    std::ifstream runtimeFile(argv[4], std::ios::binary);
    REQUIRE(runtimeFile.good());
    std::ostringstream runtimeStream;
    runtimeStream << runtimeFile.rdbuf();
    const auto runtime = runtimeStream.str();
    REQUIRE(runtime.find(
        "if (!HotkeySettings.enabled) {") != std::string::npos);
    REQUIRE(runtime.find(
        "if (HotkeySettings.inventoryButtonEnabled) {")
        != std::string::npos);
    REQUIRE(runtime.find(
        "if (HotkeySettings.inventoryButtonEnabled && !RegisterSdkButtonListener())")
        != std::string::npos);
    REQUIRE(runtime.find("registerChildLayout") != std::string::npos);
    REQUIRE(runtime.find("registerResource") != std::string::npos);
    REQUIRE(runtime.find("HookConfigurePlayerInventory") != std::string::npos);
    REQUIRE(runtime.find("ruffneckk-remote-stash/inventory-button")
        != std::string::npos);
    REQUIRE(runtime.find("\"remote_stash\"") != std::string::npos);
    REQUIRE(runtime.find("\"ruffneckk_remote_stash_button\"")
        != std::string::npos);
    REQUIRE(runtime.find("FindNamedWidget(panel, \"gold_button\")")
        != std::string::npos);
    return 0;
}
