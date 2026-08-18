#include "policy.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main(int argc, char** argv) {
    using namespace RuffnecKk::TransmuteHotkey;

    Hotkey hotkey{};
    std::string error;
    REQUIRE(ParseHotkey("SHIFT+T", hotkey, error));
    REQUIRE(hotkey.virtualKey == 'T');
    REQUIRE(hotkey.modifier == Modifier::Shift);
    REQUIRE(ParseHotkey("T", hotkey, error));
    REQUIRE(hotkey.modifier == Modifier::None);
    REQUIRE(ParseHotkey("CTRL+F24", hotkey, error));
    REQUIRE(hotkey.virtualKey == 0x87);
    REQUIRE(hotkey.modifier == Modifier::Control);
    REQUIRE(ParseHotkey("ALT+PAGEUP", hotkey, error));
    REQUIRE(hotkey.virtualKey == 0x21);
    REQUIRE(!ParseHotkey("CTRL+SHIFT+T", hotkey, error));
    REQUIRE(error.find("zero or one modifier") != std::string::npos);
    REQUIRE(!ParseHotkey("MOUSE4", hotkey, error));
    REQUIRE(error.find("keyboard bindings only") != std::string::npos);
    REQUIRE(!ParseHotkey("GAMEPAD_A", hotkey, error));

    REQUIRE(argc == 3);
    std::ifstream file(argv[1], std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();

    Config config{};
    REQUIRE(ParseConfig(stream.str(), config, error));
    REQUIRE(config.enabled);
    REQUIRE(!config.diagnostics);
    REQUIRE(config.hotkeyText == "SHIFT+T");
    REQUIRE(config.hotkey.virtualKey == 'T');
    REQUIRE(config.hotkey.modifier == Modifier::Shift);

    auto invalid = stream.str();
    invalid += "\nunknown = true\n";
    REQUIRE(!ParseConfig(invalid, config, error));

    auto mouse = stream.str();
    const auto binding = mouse.find("hotkey = \"SHIFT+T\"");
    REQUIRE(binding != std::string::npos);
    mouse.replace(
        binding,
        std::string("hotkey = \"SHIFT+T\"").size(),
        "hotkey = \"MOUSE4\"");
    REQUIRE(!ParseConfig(mouse, config, error));

    auto legacyConsume = stream.str();
    legacyConsume += "\nconsume = true\n";
    REQUIRE(!ParseConfig(legacyConsume, config, error));

    auto legacyDiagnostics = stream.str();
    legacyDiagnostics += "\ndiagnostics = false\n";
    REQUIRE(!ParseConfig(legacyDiagnostics, config, error));

    REQUIRE(ParseConfig(
        "enabled = true\nhotkey = \"SHIFT+T\"\n"
        "[diagnostics]\nenabled = true\n",
        config,
        error));
    REQUIRE(config.diagnostics);
    REQUIRE(!ParseConfig(
        "enabled = true\nhotkey = \"SHIFT+T\"\n"
        "[diagnostics]\nenabled = false\nenabled = true\n",
        config,
        error));

    auto missing = stream.str();
    const auto enabled = missing.find("enabled = true");
    REQUIRE(enabled != std::string::npos);
    missing.erase(enabled, std::string("enabled = true").size());
    REQUIRE(!ParseConfig(missing, config, error));

    std::ifstream pluginFile(argv[2], std::ios::binary);
    REQUIRE(pluginFile.good());
    std::ostringstream pluginStream;
    pluginStream << pluginFile.rdbuf();
    const auto pluginSource = pluginStream.str();
    const auto callbackStart = pluginSource.find("auto __cdecl OnInputAction(");
    const auto callbackEnd = pluginSource.find(
        "auto RegisterInputAction()", callbackStart);
    REQUIRE(callbackStart != std::string::npos);
    REQUIRE(callbackEnd != std::string::npos);
    const auto callback = pluginSource.substr(
        callbackStart, callbackEnd - callbackStart);
    REQUIRE(callback.find("D2RL::Input::ActionResult::Ignored")
        != std::string::npos);
    REQUIRE(callback.find("D2RL::Input::ActionResult::Handled")
        == std::string::npos);
    REQUIRE(callback.find("HotkeyCaptured") == std::string::npos);
    REQUIRE(pluginSource.find("WH_KEYBOARD_LL") == std::string::npos);
    REQUIRE(pluginSource.find("GetAsyncKeyState") == std::string::npos);
    return 0;
}
