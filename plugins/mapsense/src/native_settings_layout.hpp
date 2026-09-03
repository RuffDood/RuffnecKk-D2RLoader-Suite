#pragma once

#include <string_view>

namespace RuffnecKk::MapSense {

inline constexpr char NativeSettingsPanelLocalId[] = "MapSenseControls";
inline constexpr char NativeSettingsPanelQualifiedName[] =
    "ruffneckk-mapsense/MapSenseControls";
inline constexpr char NativeSettingsPanelResourcePath[] =
    "data/global/ui/layouts/ruffneckk-mapsense/MapSenseControlshd.json";

// Deliberately small SDK-v3 proof surface. Every action owns one stable native
// button for the entire panel lifetime; no clicked widget is hidden, replaced,
// or synchronized from inside its own UI-message callback.
inline constexpr char NativeSettingsPanelLayout[] = R"json({
  "type": "Panel",
  "name": "ruffneckk-mapsense/MapSenseControls",
  "fields": {
    "fitToParent": true,
    "priority": 8500
  },
  "children": [
    {
      "type": "RectangleWidget",
      "name": "DimBackground",
      "fields": {
        "fitToScreen": true,
        "color": [0.0, 0.0, 0.0, 0.72]
      },
      "children": [
        {
          "type": "ClickCatcherWidget",
          "name": "InputCatcher",
          "fields": {
            "fitToParent": true
          }
        },
        {
          "type": "Widget",
          "name": "ShellAnchor",
          "fields": {
            "anchor": { "x": 0.5, "y": 0.5 },
            "rect": "$SettingsPanelAnchorRect"
          },
          "children": [
            {
              "type": "ImageWidget",
              "name": "Frame",
              "fields": {
                "filename": "\\PANEL\\Options\\FrontEndOptionsBG"
              }
            },
            {
              "type": "TextBoxWidget",
              "name": "Heading",
              "fields": {
                "rect": { "x": 0, "y": 45, "width": 1950, "height": 103 },
                "text": "RUFFNECKK MAPSENSE",
                "style": "$StyleTitleBlock"
              }
            },
            {
              "type": "ButtonWidget",
              "name": "CloseButton",
              "fields": {
                "rect": { "x": 1868, "y": 8 },
                "filename": "PANEL\\closebtn_4x",
                "hoveredFrame": 3,
                "onClickMessage": "PanelManager:ClosePanel:ruffneckk-mapsense/MapSenseControls",
                "tooltipString": "Close",
                "sound": "cursor_close_window_hd",
                "acceptsEscKeyEverywhere": true,
                "action": "back"
              }
            }
          ]
        }
      ]
    },
    {
      "type": "ImageWidget",
      "name": "SettingsBackground",
      "fields": {
        "rect": "$SettingsPanelBackgroundRect",
        "anchor": { "x": 0.5 },
        "filename": "Controller/Panel/Options/Panel_Options_BG"
      }
    },
    {
      "type": "Widget",
      "name": "ControlsAnchor",
      "fields": {
        "anchor": { "x": 0.5, "y": 0.5 },
        "rect": "$SettingsPanelAnchorRect"
      },
      "children": [
        {
          "type": "ButtonWidget",
          "name": "RevealLevelButton",
          "fields": {
            "rect": { "x": 711, "y": 400 },
            "filename": "FrontEnd\\HD\\Final\\FrontEnd_ButtonMed",
            "textString": "REVEAL LEVEL",
            "onClickMessage": "PanelManager:OpenPanel:RuffnecKkMapSenseRevealLevel",
            "textColor": "$LightButtonTextColor",
            "text/style": "$StyleFEButtonText",
            "pointSize": "$MediumLargeFontSize",
            "hoveredFrame": 3,
            "disabledFrame": 2
          }
        },
        {
          "type": "ButtonWidget",
          "name": "RevealActButton",
          "fields": {
            "rect": { "x": 711, "y": 600 },
            "filename": "FrontEnd\\HD\\Final\\FrontEnd_ButtonMed",
            "textString": "REVEAL ACT",
            "onClickMessage": "PanelManager:OpenPanel:RuffnecKkMapSenseRevealAct",
            "textColor": "$LightButtonTextColor",
            "text/style": "$StyleFEButtonText",
            "pointSize": "$MediumLargeFontSize",
            "hoveredFrame": 3,
            "disabledFrame": 2
          }
        },
        {
          "type": "ButtonWidget",
          "name": "RevealAllButton",
          "fields": {
            "rect": { "x": 711, "y": 800 },
            "filename": "FrontEnd\\HD\\Final\\FrontEnd_ButtonMed",
            "textString": "REVEAL ALL",
            "onClickMessage": "PanelManager:OpenPanel:RuffnecKkMapSenseRevealAll",
            "textColor": "$LightButtonTextColor",
            "text/style": "$StyleFEButtonText",
            "pointSize": "$MediumLargeFontSize",
            "hoveredFrame": 3,
            "disabledFrame": 2
          }
        },
        {
          "type": "ButtonWidget",
          "name": "RevealOffButton",
          "fields": {
            "rect": { "x": 711, "y": 1000 },
            "filename": "FrontEnd\\HD\\Final\\FrontEnd_ButtonMed",
            "textString": "REVEAL ALL OFF",
            "onClickMessage": "PanelManager:OpenPanel:RuffnecKkMapSenseRevealOff",
            "textColor": "$LightButtonTextColor",
            "text/style": "$StyleFEButtonText",
            "pointSize": "$MediumLargeFontSize",
            "hoveredFrame": 3,
            "disabledFrame": 2
          }
        }
      ]
    }
  ]
})json";

inline constexpr std::string_view NativeSettingsPanelLayoutView{
    NativeSettingsPanelLayout,
    sizeof(NativeSettingsPanelLayout) - 1U,
};

} // namespace RuffnecKk::MapSense
