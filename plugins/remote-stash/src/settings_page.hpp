#pragma once
#include <RuffnecKk/live_settings.hpp>
#include <array>
#include <string>
#include <sstream>
#include "policy.hpp"
namespace ruffneckk::remote_stash::LivePage {
inline bool RestartRequired(std::string_view key) {
 return key=="enabled" || key=="button.sprite_file" || key=="button.lowend_sprite_file"
     || key=="button.normal_frame" || key=="button.pressed_frame"
     || key=="button.disabled_frame" || key=="button.hovered_frame";
}
inline bool Known(std::string_view key){
 return key=="enabled" || key=="inventory_button_enabled" || key=="hotkey_enabled" || key=="hotkey" || key=="close_remote_stash_and_inventory_together" || key=="button.placement" || key=="button.anchor" || key=="button.offset_x" || key=="button.offset_y" || key=="button.width" || key=="button.height" || key=="button.sprite_file" || key=="button.lowend_sprite_file" || key=="button.normal_frame" || key=="button.pressed_frame" || key=="button.disabled_frame" || key=="button.hovered_frame" || key=="diagnostics.enabled";
}
inline std::string Value(const HotkeyConfig& c,std::string_view key){
 using RuffnecKk::LiveSettings::Quote;
 if(key=="enabled") return std::string(c.enabled ? "true" : "false");
 if(key=="inventory_button_enabled") return std::string(c.inventoryButtonEnabled ? "true" : "false");
 if(key=="hotkey_enabled") return std::string(c.hotkeyEnabled ? "true" : "false");
 if(key=="hotkey") return Quote(c.hotkeyText);
 if(key=="close_remote_stash_and_inventory_together") return std::string(c.closeRemoteStashAndInventoryTogether ? "true" : "false");
 if(key=="button.placement") return Quote(c.button.placement==ButtonPlacement::Automatic ? "auto" : "custom");
 if(key=="button.anchor") return Quote(c.button.anchor==ButtonAnchor::TopLeft ? "topLeft" : c.button.anchor==ButtonAnchor::TopRight ? "topRight" : c.button.anchor==ButtonAnchor::BottomRight ? "bottomRight" : "bottomLeft");
 if(key=="button.offset_x") return std::to_string(c.button.offsetX);
 if(key=="button.offset_y") return std::to_string(c.button.offsetY);
 if(key=="button.width") return std::to_string(c.button.width);
 if(key=="button.height") return std::to_string(c.button.height);
 if(key=="button.sprite_file") return Quote(c.button.spriteFile);
 if(key=="button.lowend_sprite_file") return Quote(c.button.lowendSpriteFile);
 if(key=="button.normal_frame") return std::to_string(c.button.normalFrame);
 if(key=="button.pressed_frame") return std::to_string(c.button.pressedFrame);
 if(key=="button.disabled_frame") return std::to_string(c.button.disabledFrame);
 if(key=="button.hovered_frame") return std::to_string(c.button.hoveredFrame);
 if(key=="diagnostics.enabled") return std::string(c.diagnostics ? "true" : "false");
 throw std::invalid_argument("Unknown setting.");
}
inline void Copy(HotkeyConfig& to, const HotkeyConfig& from, std::string_view key){
 if(key=="inventory_button_enabled") { to.inventoryButtonEnabled=from.inventoryButtonEnabled; return; }
 if(key=="hotkey_enabled") { to.hotkeyEnabled=from.hotkeyEnabled; return; }
 if(key=="hotkey") { to.hotkeyText=from.hotkeyText; to.hotkey=from.hotkey; return; }
 if(key=="close_remote_stash_and_inventory_together") { to.closeRemoteStashAndInventoryTogether=from.closeRemoteStashAndInventoryTogether; return; }
 if(key=="diagnostics.enabled") { to.diagnostics=from.diagnostics; return; }
 if(key=="button.placement") { to.button.placement=from.button.placement; return; }
 if(key=="button.anchor") { to.button.anchor=from.button.anchor; return; }
 if(key=="button.offset_x") { to.button.offsetX=from.button.offsetX; return; }
 if(key=="button.offset_y") { to.button.offsetY=from.button.offsetY; return; }
 if(key=="button.width") { to.button.width=from.button.width; return; }
 if(key=="button.height") { to.button.height=from.button.height; return; }
 if(key=="button.sprite_file") { to.button.spriteFile=from.button.spriteFile; return; }
 if(key=="button.lowend_sprite_file") { to.button.lowendSpriteFile=from.button.lowendSpriteFile; return; }
 if(key=="button.normal_frame") { to.button.normalFrame=from.button.normalFrame; return; }
 if(key=="button.pressed_frame") { to.button.pressedFrame=from.button.pressedFrame; return; }
 if(key=="button.disabled_frame") { to.button.disabledFrame=from.button.disabledFrame; return; }
 if(key=="button.hovered_frame") { to.button.hoveredFrame=from.button.hoveredFrame; return; }
 if(key!="enabled") throw std::invalid_argument("Unknown setting.");
}
inline std::string Schema(bool overridden=false){
 std::string out="title = \"Remote Stash\"\n";
 out += "[[setting]]\ntype = \"text\"\nlabel = \"Changes save immediately. Starred settings apply after restart.\"\n";
 out += "[[setting]]\ntype = \"text\"\nlabel = \"Button geometry updates when Inventory opens; sprites are cached until restart.\"\n";
 if(overridden) out += "[[setting]]\ntype = \"text\"\nlabel = \"This mod owns button appearance. Button fields are read-only here.\"\n";
 out += "[[setting]]\nkey = \"enabled\"\ntype = \"bool\"\nlabel = \"Enable at next start\"\nrestart = true\n";
 out += "[[setting]]\nkey = \"inventory_button_enabled\"\ntype = \"bool\"\nlabel = \"Inventory button\"\n";
 out += "[[setting]]\nkey = \"hotkey_enabled\"\ntype = \"bool\"\nlabel = \"Hotkey enabled\"\n";
 out += "[[setting]]\nkey = \"hotkey\"\ntype = \"string\"\nlabel = \"Hotkey\"\n";
 out += "[[setting]]\nkey = \"close_remote_stash_and_inventory_together\"\ntype = \"bool\"\nlabel = \"Close Inventory with Remote Stash\"\n";
 out += "[[setting]]\nkey = \"button.placement\"\ntype = \"enum\"\nlabel = \"Button placement\"\nvalues = [\"auto\", \"custom\"]\n";
 out += "[[setting]]\nkey = \"button.anchor\"\ntype = \"enum\"\nlabel = \"Button anchor\"\nvalues = [\"topLeft\", \"topRight\", \"bottomLeft\", \"bottomRight\"]\n";
 out += "[[setting]]\nkey = \"button.offset_x\"\ntype = \"int\"\nlabel = \"Horizontal offset\"\nmin = -4096\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.offset_y\"\ntype = \"int\"\nlabel = \"Vertical offset\"\nmin = -4096\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.width\"\ntype = \"int\"\nlabel = \"Button width\"\nmin = 1\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.height\"\ntype = \"int\"\nlabel = \"Button height\"\nmin = 1\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.sprite_file\"\nrestart = true\ntype = \"string\"\nlabel = \"Custom sprite path\"\n";
 out += "[[setting]]\nkey = \"button.lowend_sprite_file\"\nrestart = true\ntype = \"string\"\nlabel = \"Low-end sprite path\"\n";
 out += "[[setting]]\nkey = \"button.normal_frame\"\nrestart = true\ntype = \"int\"\nlabel = \"Normal frame\"\nmin = 0\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.pressed_frame\"\nrestart = true\ntype = \"int\"\nlabel = \"Pressed frame\"\nmin = 0\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.disabled_frame\"\nrestart = true\ntype = \"int\"\nlabel = \"Disabled frame\"\nmin = 0\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"button.hovered_frame\"\nrestart = true\ntype = \"int\"\nlabel = \"Hovered frame\"\nmin = 0\nmax = 4096\nstep = 1\n";
 out += "[[setting]]\nkey = \"diagnostics.enabled\"\ntype = \"bool\"\nlabel = \"Diagnostics\"\n";
 return out;
}
}
