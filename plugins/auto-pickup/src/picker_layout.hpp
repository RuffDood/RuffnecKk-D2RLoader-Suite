#pragma once
#include <string>

namespace RuffnecKk::AutoPickup::Picker {
inline auto PanelLayout() -> std::string {
    std::string json = R"({"type":"Panel","name":"auto-pickup-anything/ItemPicker","fields":{"anchor":{"x":0.5,"y":0.5},"priority":8500},"children":[{"type":"ImageWidget","name":"Frame","fields":{"rect":{"x":-627,"y":-470,"width":1254,"height":940},"filename":"Panel\\Modals\\Modal_No_Title_BG"},"children":[)";
    auto label = [&](const std::string& id, int y, int height, const std::string& text) {
        json += "{\"type\":\"TextBoxWidget\",\"name\":\"" + id + "\",\"fields\":{\"rect\":{\"x\":110,\"y\":" + std::to_string(y)
            + ",\"width\":" + (id.starts_with("Item") ? "740" : "1010") + ",\"height\":" + std::to_string(height) + "},\"text\":\"" + text + "\",\"style\":\"$StyleModalDialogDescription\"}},";
    };
    auto button = [&](const std::string& id, int x, int y, const std::string& text) {
        json += "{\"type\":\"ButtonWidget\",\"name\":\"" + id + "\",\"fields\":{\"rect\":{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y)
            + "},\"filename\":\"Panel\\\\Modals\\\\ModalButton\",\"pressedFrame\":1,\"disabledFrame\":2,\"hoveredFrame\":3,\"textString\":\"" + text
            + "\",\"pointSize\":\"$MediumFontSize\",\"textColor\":\"$FontColorWhite\"}},";
    };
    label("Title", 40, 50, "Auto Pickup");
    label("Source", 90, 45, "Reading loaded item tables...");
    json += R"({"type":"InputTextBoxWidget","name":"Search","fields":{"rect":{"x":110,"y":145,"width":740,"height":45},"maxStringLength":80,"canInsertNewlines":false,"backgroundColor":"$FontColorTransparent","fontType":"12ptF","fontStyle":{"fontFace":"BlizzardGlobal","fontColor":"$FontColorWhite","pointSize":"$SmallFontSize"},"hint":{"text":"Search item name or code","style":"$StyleModalDialogDescription"}}},)";
    button("Filter", 910, 135, "All / Custom rules");
    button("Options", 910, 40, "Options / Items");
    // Eight reusable rows keep both the native widget count and panel height
    // bounded regardless of the number of vanilla/mod item definitions.
    for (int i = 0; i != 8; ++i) {
        label("Item" + std::to_string(i), 210 + i * 55, 50, "");
        button("Choice" + std::to_string(i), 910, 200 + i * 55, "Change");
    }
    button("Previous", 110, 660, "Previous");
    label("Page", 720, 40, "");
    button("Next", 910, 660, "Next");
    label("Status", 765, 55, "Default follows category switches. Exclude always wins.");
    button("Save", 270, 830, "Apply & Save");
    button("Close", 660, 830, "Close");
    json.pop_back();
    return json + "]}]}";
}
}
