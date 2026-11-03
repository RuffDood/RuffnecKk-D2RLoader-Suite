// Adapted from MadMike616/players-hud-slider, MIT (2026 MadMike616). See LICENSE.
#include "native_slider_panel.hpp"
#include "slider_policy.hpp"

#include "resource_ids.h"

#include <D2RLPlugin/resources.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace players_hud_slider {
namespace {

// D2R's native-panel transform renders layout units at 5/8 of the HUD
// coordinate size. Compensate once here so widgets line up with HUD geometry.
constexpr double NativePanelScale = 1.6;

auto LoadEmbeddedResource(int resourceId, std::vector<std::uint8_t>& output)
        noexcept -> bool {
    const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
    const auto resource = FindResourceW(
        module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr) return false;
    const auto size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = loaded != nullptr
        ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    if (bytes == nullptr || size == 0) return false;
    try {
        output.assign(bytes, bytes + size);
        return true;
    } catch (...) {
        return false;
    }
}

void AppendNumber(std::string& output, std::int32_t value) {
    output += std::to_string(value);
}

auto ScaleLayoutX(std::int32_t value) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(std::lround(
        static_cast<double>(value) * (2560.0 / 3200.0)
            * NativePanelScale));
}

auto ScaleLayoutY(std::int32_t value) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(std::lround(
        static_cast<double>(value) * (1440.0 / 1801.0)
            * NativePanelScale));
}

} // namespace

auto NativeSliderPanel::RegisterResource(
        const char* path,
        const void* bytes,
        std::uint64_t byteCount,
        D2RL::Resources::RegistrationHandle& handle) noexcept -> bool {
    if (resources_ == nullptr || resources_->registerResource == nullptr
            || path == nullptr || bytes == nullptr || byteCount == 0) {
        return false;
    }
    const D2RL::Resources::ResourceRegistration registration{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags = 0,
        .path = path,
        .bytes = bytes,
        .byteCount = byteCount,
    };
    return resources_->registerResource(context_, &registration, &handle)
            == D2RL::Resources::Result::Success
        && handle != D2RL::Resources::InvalidHandle;
}

auto NativeSliderPanel::BuildLayout(
        const HudPlayerSliderConfig& config,
        std::int32_t maximumPlayers,
        std::string& output) noexcept -> bool {
    try {
        // HUD TOML uses a 3200x1801 design canvas. Native D2R panels use a
        // 2560x1440 canvas. Anchor the panel itself at the configured HUD
        // location; child coordinates are then local to the panel.
        const auto containerX = ScaleLayoutX(config.track.x);
        const auto containerY = ScaleLayoutY(config.marker.y);
        const auto panelOffsetX = containerX - ScaleLayoutX(1600);
        const auto panelOffsetY = containerY - ScaleLayoutY(1801);
        const auto trackWidth = ScaleLayoutX(config.track.width);
        const auto trackHeight = ScaleLayoutY(config.track.height);
        const auto markerWidth = ScaleLayoutX(config.marker.width);
        const auto markerHeight = ScaleLayoutY(config.marker.height);
        const auto panelHeight = markerHeight;
        const auto trackY = ScaleLayoutY(config.track.y - config.marker.y);
        const auto tooltipY = ScaleLayoutY(config.tooltip.y);
        const auto tooltipWidth = ScaleLayoutX(config.tooltip.width);
        const auto tooltipHeight = ScaleLayoutY(config.tooltip.height);
        const auto tooltipTextWidth = ScaleLayoutX(config.tooltipText.width);
        const auto tooltipTextHeight = ScaleLayoutY(config.tooltipText.height);
        const auto tickWidth = ScaleLayoutX(config.tick.width);
        const auto tickHeight = ScaleLayoutY(config.tick.height);
        const auto tickCount = maximumPlayers - config.minimumPlayers + 1;

        output.clear();
        output.reserve(8192U + static_cast<std::size_t>(tickCount) * 256U
            + static_cast<std::size_t>(maximumPlayers) * 512U);
        output += R"json({
  "type": "Panel",
  "name": "ruffneckk-playerx-scaling-tweaks/PlayerCountSlider",
  "fields": {
    "priority": 8500,
    "anchor": { "x": 0.5, "y": 1.0 },
    "rect": { "x": )json";
        AppendNumber(output, panelOffsetX);
        output += R"json(, "y": )json";
        AppendNumber(output, panelOffsetY);
        output += R"json(, "width": )json";
        AppendNumber(output, trackWidth);
        output += R"json(, "height": )json";
        AppendNumber(output, panelHeight);
        output += R"json( }
  },
  "children": [
    {
      "type": "Widget", "name": "SliderAnchor",
      "fields": {
        "anchor": { "x": 0.0, "y": 0.0 },
        "rect": { "x": )json";
        AppendNumber(output, 0);
        output += R"json(, "y": 0, "width": )json";
        AppendNumber(output, trackWidth);
        output += R"json(, "height": )json";
        AppendNumber(output, panelHeight);
        output += R"json( }
      },
      "children": [
)json";
        if (tickCount > 1) {
            const auto firstCenter = ScaleLayoutX(config.marker.x
                - config.track.x + config.marker.width / 2);
            const auto lastCenter = ScaleLayoutX(config.track.width
                - config.marker.width / 2);
            for (std::int32_t i = 0; i < tickCount; ++i) {
                const auto numerator = static_cast<long long>(i)
                    * (lastCenter - firstCenter);
                const auto center = firstCenter
                    + static_cast<std::int32_t>(numerator / (tickCount - 1));
                const auto x = center + ScaleLayoutX(config.tick.x)
                    - tickWidth / 2;
                const auto y = trackY + trackHeight / 2
                    + ScaleLayoutY(config.tick.y) - tickHeight / 2;
                output += R"json(        {
          "type": "RectangleWidget", "name": "SliderTick)json";
                AppendNumber(output, i);
                output += R"json(",
          "fields": {
            "rect": { "x": )json";
                AppendNumber(output, x);
                output += R"json(, "y": )json";
                AppendNumber(output, y);
                output += R"json(, "width": )json";
                AppendNumber(output, tickWidth);
                output += R"json(, "height": )json";
                AppendNumber(output, tickHeight);
                output += R"json( },
            "color": [0.57, 0.54, 0.47, 0.80]
          }
        },
)json";
            }
        }

        output += "        {\"type\":\"ButtonWidget\",\"name\":\"PlayerXSliderTrack\",\"fields\":{\"rect\":{\"x\":0,\"y\":"
            + std::to_string(trackY) + ",\"width\":" + std::to_string(trackWidth)
            + ",\"height\":" + std::to_string(trackHeight)
            + "},\"filename\":\"d2rloader/ruffneckk-playerx-scaling-tweaks/native-track\",\"normalFrame\":0,\"pressedFrame\":0,\"hoveredFrame\":0,\"disabledFrame\":0}}\n      ]\n    },\n";
        const auto firstCenter = ScaleLayoutX(config.marker.x
            - config.track.x + config.marker.width / 2);
        const auto lastCenter = ScaleLayoutX(config.track.width
            - config.marker.width / 2);
        for (std::int32_t players = config.minimumPlayers; players <= maximumPlayers; ++players) {
            const auto numerator = static_cast<long long>(players - config.minimumPlayers)
                * (lastCenter - firstCenter);
            const auto center = maximumPlayers > config.minimumPlayers
                ? firstCenter + static_cast<std::int32_t>(
                    numerator / (maximumPlayers - config.minimumPlayers))
                : firstCenter;
            const auto frameX = center - markerWidth / 2
                + ScaleLayoutX(config.tooltip.x);
            const auto textX = frameX + ScaleLayoutX(config.tooltipText.x);
            const auto textY = tooltipY + ScaleLayoutY(config.tooltipText.y);

            output += "        {\"type\":\"ImageWidget\",\"name\":\"SliderMarker" + std::to_string(players)
                + "\",\"fields\":{\"rect\":{\"x\":" + std::to_string(center - markerWidth / 2)
                + ",\"y\":0,\"width\":" + std::to_string(markerWidth) + ",\"height\":" + std::to_string(markerHeight)
                + "},\"filename\":\"d2rloader/ruffneckk-playerx-scaling-tweaks/native-marker\",\"visible\":false}},\n";
            output += R"json(        {
          "type": "ImageWidget", "name": "SliderTooltipFrame)json";
            AppendNumber(output, players);
            output += R"json(",
          "fields": {
            "rect": { "x": )json";
            AppendNumber(output, frameX);
            output += R"json(, "y": )json";
            AppendNumber(output, tooltipY);
            output += R"json(, "width": )json";
            AppendNumber(output, tooltipWidth);
            output += R"json(, "height": )json";
            AppendNumber(output, tooltipHeight);
            output += R"json( },
            "filename": "d2rloader/ruffneckk-playerx-scaling-tweaks/native-tooltip",
            "visible": false
          }
        },
        {
          "type": "TextBoxWidget", "name": "SliderTooltipValue)json";
            AppendNumber(output, players);
            output += R"json(",
          "fields": {
            "rect": { "x": )json";
            AppendNumber(output, textX);
            output += R"json(, "y": )json";
            AppendNumber(output, textY);
            output += R"json(, "width": )json";
            AppendNumber(output, tooltipTextWidth);
            output += R"json(, "height": )json";
            AppendNumber(output, tooltipTextHeight);
            output += R"json( },
            "text": ")json";
            AppendNumber(output, players);
            output += R"json(",
            "visible": false,
            "style": {
              "fontFace": "Exocet",
              "fontColor": "$FontColorWhite",
              "pointSize": "$SmallFontSize",
              "alignment": { "h": "center", "v": "center" },
              "dropShadow": "$DefaultDropShadow"
            }
          }
        })json";
            output += players < maximumPlayers ? ",\n" : "\n";
        }
        output += ",\n";
        for (int digit = 0; digit < 5; ++digit) for (int value = 0; value < 10; ++value) {
            output += "{\"type\":\"TextBoxWidget\",\"name\":\"ActualDigit" + std::to_string(digit) + std::to_string(value)
                + "\",\"fields\":{\"rect\":{\"x\":" + std::to_string(lastCenter - 30 + digit * 12)
                + ",\"y\":" + std::to_string(tooltipY) + ",\"width\":12,\"height\":" + std::to_string(tooltipTextHeight)
                + "},\"text\":\"" + std::to_string(value) + "\",\"visible\":false,\"style\":{\"fontFace\":\"Exocet\",\"fontColor\":\"$FontColorWhite\",\"pointSize\":\"$SmallFontSize\"}}}";
            output += digit == 4 && value == 9 ? "\n" : ",\n";
        }
        output += R"json(  ]
})json";
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

auto NativeSliderPanel::BuildHudButtonLayout(
        const HudPlayerSliderConfig& config,
        std::string& output) noexcept -> bool {
    try {
        const auto buttonX = ScaleLayoutX(config.button.x);
        const auto buttonY = ScaleLayoutY(config.button.y);
        const auto buttonWidth = ScaleLayoutX(config.button.width);
        const auto buttonHeight = ScaleLayoutY(config.button.height);

        output.clear();
        output += R"json({
  "type": "Panel",
  "name": "ruffneckk-playerx-scaling-tweaks/HudButton",
  "fields": {
    "priority": 10000,
    "anchor": { "x": 0.5, "y": 1.0 },
    "rect": { "x": )json";
        AppendNumber(output, -ScaleLayoutX(1600));
        // The root is bottom-anchored, so move its full-screen canvas up by
        // its height. Without this offset, the button child is laid out below
        // the visible screen even though the panel reports Open.
        output += R"json(, "y": )json";
        AppendNumber(output, -ScaleLayoutY(1801));
        output += R"json(, "width": )json";
        AppendNumber(output, ScaleLayoutX(3200));
        output += R"json(, "height": )json";
        AppendNumber(output, ScaleLayoutY(1801));
        output += R"json( }
  },
  "children": [
    {
      "type": "ButtonWidget",
      "name": "OpenPlayersSlider",
      "fields": {
        "rect": { "x": )json";
        AppendNumber(output, buttonX);
        output += R"json(, "y": )json";
        AppendNumber(output, buttonY);
        output += R"json(, "width": )json";
        AppendNumber(output, buttonWidth);
        output += R"json(, "height": )json";
        AppendNumber(output, buttonHeight);
        output += R"json( },
        "filename": "d2rloader/ruffneckk-playerx-scaling-tweaks/native-button",
        "normalFrame": 0,
        "pressedFrame": 2,
        "hoveredFrame": 1,
        "disabledFrame": 3
      }
    }
  ]
})json";
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

auto NativeSliderPanel::Start(
        const D2RL::PluginContext* context,
        const HudPlayerSliderConfig& config,
        std::int32_t maximumPlayers) noexcept -> bool {
    Stop();
    if (context == nullptr || config.minimumPlayers < 1
        || config.minimumPlayers > SelectionMaximum(maximumPlayers)) return false;
    context_ = context;
    if (context_->QueryService(D2RL::ServiceId::Resource,
            D2RL::ResourceServiceV1Version, &resources_)
                != D2RL::ServiceQueryResult::Success
            || !D2RL::HasResourceServiceV1Field(resources_,
                D2RL::ResourceServiceV1RequiredSize)
            || resources_->registerResource == nullptr
            || resources_->unregisterResource == nullptr) {
        context_->LogWarn(
            "Players HUD Slider: ResourceService is unavailable for the native slider panel.");
        Cleanup();
        return false;
    }
    if (context_->QueryService(D2RL::ServiceId::Panel,
            D2RL::PanelServiceV1Version, &panels_)
                != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPanelServiceV1Field(panels_,
                D2RL::PanelServiceV1RequiredSize)
            || panels_->registerPanel == nullptr
            || panels_->unregisterPanel == nullptr
            || panels_->getPanelInfo == nullptr
            || panels_->openPanel == nullptr
            || panels_->closePanel == nullptr) {
        context_->LogWarn(
            "Players HUD Slider: PanelService is unavailable for the native slider panel.");
        Cleanup();
        return false;
    }
    if (context_->QueryService(D2RL::ServiceId::Widget,
            D2RL::WidgetServiceV1Version, &widgets_)
                != D2RL::ServiceQueryResult::Success
            || !D2RL::HasWidgetServiceV1Field(widgets_,
                D2RL::WidgetServiceV1RequiredSize)
            || widgets_->findPanel == nullptr
            || widgets_->findWidget == nullptr
            || widgets_->setWidgetVisible == nullptr) {
        context_->LogWarn(
            "Players HUD Slider: WidgetService is unavailable for the value tooltip.");
        Cleanup();
        return false;
    }
    if (context_->QueryService(D2RL::ServiceId::Thread,
            D2RL::ThreadServiceV1Version, &threads_)
                != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceV1Field(threads_,
                D2RL::ThreadServiceV1RequiredSize)
            || threads_->runOnUiThread == nullptr) {
        threads_ = nullptr;
        context_->LogWarn(
            "Players HUD Slider: ThreadService is unavailable; tooltip updates will use the caller thread.");
    }
    minimumPlayers_ = config.minimumPlayers;
    maximumPlayers_ = std::clamp(maximumPlayers, 1,
        HudSelectionMaximum);

    std::array<std::vector<std::uint8_t>, 4> sprites;
    constexpr std::array resourceIds{
        PLAYERS_HUD_NATIVE_TRACK_RESOURCE_ID,
        PLAYERS_HUD_NATIVE_MARKER_RESOURCE_ID,
        PLAYERS_HUD_NATIVE_TOOLTIP_RESOURCE_ID,
        PLAYERS_HUD_NATIVE_BUTTON_RESOURCE_ID,
    };
    for (std::size_t i = 0; i < resourceIds.size(); ++i) {
        if (!LoadEmbeddedResource(resourceIds[i], sprites[i])) {
            context_->LogWarn(
                "Players HUD Slider: native panel sprite could not be loaded from the DLL.");
            Cleanup();
            return false;
        }
    }
    constexpr std::array spritePaths{
        TrackSpriteResourcePath,
        MarkerSpriteResourcePath,
        TooltipSpriteResourcePath,
        HudButtonSpriteResourcePath,
    };
    for (std::size_t i = 0; i < spritePaths.size(); ++i) {
        if (!RegisterResource(spritePaths[i], sprites[i].data(),
                sprites[i].size(), spriteHandles_[i])) {
            context_->LogWarn(
                "Players HUD Slider: native panel sprite registration failed.");
            Cleanup();
            return false;
        }
    }

    std::string layout;
    if (!BuildLayout(config, maximumPlayers_, layout)
            || !RegisterResource(LayoutResourcePath, layout.data(),
                layout.size(), layoutHandle_)) {
        context_->LogWarn(
            "Players HUD Slider: native panel layout registration failed.");
        Cleanup();
        return false;
    }

    std::string hudButtonLayout;
    if (!BuildHudButtonLayout(config, hudButtonLayout)
            || !RegisterResource(HudButtonLayoutResourcePath,
                hudButtonLayout.data(), hudButtonLayout.size(),
                hudButtonLayoutHandle_)) {
        context_->LogWarn(
            "Players HUD Slider: native HUD button layout registration failed.");
        Cleanup();
        return false;
    }

    const D2RL::Panels::PanelRegistration panel{
        .structSize = D2RL::Panels::PanelRegistrationSize,
        .flags = D2RL::Panels::PanelFlags::CloseOnEscape,
        .localId = PanelLocalId,
    };
    if (panels_->registerPanel(context_, &panel, &panelHandle_)
                != D2RL::Panels::Result::Success
            || panelHandle_ == D2RL::Panels::InvalidHandle) {
        context_->LogWarn(
            "Players HUD Slider: native player-count panel registration failed.");
        Cleanup();
        return false;
    }

    const D2RL::Panels::PanelRegistration hudButtonPanel{
        .structSize = D2RL::Panels::PanelRegistrationSize,
        .flags = D2RL::Panels::PanelFlags::None,
        .localId = HudButtonPanelLocalId,
    };
    if (panels_->registerPanel(context_, &hudButtonPanel,
            &hudButtonPanelHandle_) != D2RL::Panels::Result::Success
            || hudButtonPanelHandle_ == D2RL::Panels::InvalidHandle) {
        context_->LogWarn(
            "Players HUD Slider: native HUD button panel registration failed.");
        Cleanup();
        return false;
    }

    started_ = true;
    context_->LogInfo(
        "Players HUD Slider: native D2R slider and HUD button panels registered.");
    return true;
}

void NativeSliderPanel::Cleanup() noexcept {
    tooltipValue_ = 0;
    pendingTooltipValue_.store(0, std::memory_order_release);
    pendingHudButtonVisible_.store(false, std::memory_order_release);
    tooltipUpdateQueued_.store(false, std::memory_order_release);
    hudButtonUpdateQueued_.store(false, std::memory_order_release);
    tooltipWidgetsInitialized_ = false;
    if (context_ != nullptr && panels_ != nullptr
            && hudButtonPanelHandle_ != D2RL::Panels::InvalidHandle) {
        if (panels_->closePanel != nullptr) {
            (void)panels_->closePanel(context_, hudButtonPanelHandle_);
        }
        if (panels_->unregisterPanel != nullptr) {
            (void)panels_->unregisterPanel(context_, hudButtonPanelHandle_);
        }
        hudButtonPanelHandle_ = D2RL::Panels::InvalidHandle;
    }
    if (context_ != nullptr && panels_ != nullptr
            && panelHandle_ != D2RL::Panels::InvalidHandle) {
        if (panels_->closePanel != nullptr) {
            (void)panels_->closePanel(context_, panelHandle_);
        }
        if (panels_->unregisterPanel != nullptr) {
            (void)panels_->unregisterPanel(context_, panelHandle_);
        }
        panelHandle_ = D2RL::Panels::InvalidHandle;
    }
    if (context_ != nullptr && resources_ != nullptr
            && resources_->unregisterResource != nullptr) {
        if (layoutHandle_ != D2RL::Resources::InvalidHandle) {
            (void)resources_->unregisterResource(context_, layoutHandle_);
            layoutHandle_ = D2RL::Resources::InvalidHandle;
        }
        if (hudButtonLayoutHandle_ != D2RL::Resources::InvalidHandle) {
            (void)resources_->unregisterResource(context_,
                hudButtonLayoutHandle_);
            hudButtonLayoutHandle_ = D2RL::Resources::InvalidHandle;
        }
        for (auto handle = spriteHandles_.rbegin();
                handle != spriteHandles_.rend(); ++handle) {
            if (*handle != D2RL::Resources::InvalidHandle) {
                (void)resources_->unregisterResource(context_, *handle);
                *handle = D2RL::Resources::InvalidHandle;
            }
        }
    }
    started_ = false;
    tooltipUpdateQueued_.store(false);
    hudButtonUpdateQueued_.store(false);
    threads_ = nullptr;
    widgets_ = nullptr;
    panels_ = nullptr;
    resources_ = nullptr;
    context_ = nullptr;
    maximumPlayers_ = 1;
}

void NativeSliderPanel::Stop() noexcept {
    Cleanup();
}

auto NativeSliderPanel::Open() noexcept -> bool {
    tooltipValue_ = 0;
    tooltipWidgetsInitialized_ = false;
    pendingTooltipValue_.store(0, std::memory_order_release);
    const bool opened = started_ && context_ != nullptr && panels_ != nullptr
        && panelHandle_ != D2RL::Panels::InvalidHandle
        && panels_->openPanel(context_, panelHandle_)
            == D2RL::Panels::Result::Success;
    if (opened) { HideAllTooltipWidgets(); tooltipWidgetsInitialized_ = true; }
    return opened;
}

void NativeSliderPanel::Close() noexcept {
    QueueTooltipValue(0);
    if (started_ && context_ != nullptr && panels_ != nullptr
            && panelHandle_ != D2RL::Panels::InvalidHandle
            && panels_->closePanel != nullptr) {
        (void)panels_->closePanel(context_, panelHandle_);
    }
}

void NativeSliderPanel::SetTooltipWidgetsVisible(
        std::int32_t players,
        bool visible) noexcept {
    if (context_ == nullptr || widgets_ == nullptr
            || widgets_->findPanel == nullptr
            || widgets_->findWidget == nullptr
            || widgets_->setWidgetVisible == nullptr) {
        return;
    }
    D2RL::Widgets::WidgetHandle panel{};
    if (widgets_->findPanel(context_, PanelQualifiedName, &panel)
            != D2RL::Widgets::Result::Success) {
        return;
    }
    try {
        const auto suffix = std::to_string(players);
        const auto setVisible = [&](const std::string& name) {
            D2RL::Widgets::WidgetHandle widget{};
            if (widgets_->findWidget(context_, panel, name.c_str(), &widget)
                    == D2RL::Widgets::Result::Success) {
                (void)widgets_->setWidgetVisible(context_, widget, visible);
            }
        };
        setVisible("SliderMarker" + suffix);
        setVisible("SliderTooltipFrame" + suffix);
        setVisible("SliderTooltipValue" + suffix);
    } catch (...) {
    }
}

void NativeSliderPanel::HideAllTooltipWidgets() noexcept {
    if (context_ == nullptr || widgets_ == nullptr
            || widgets_->findPanel == nullptr
            || widgets_->findWidget == nullptr
            || widgets_->setWidgetVisible == nullptr) {
        return;
    }
    D2RL::Widgets::WidgetHandle panel{};
    if (widgets_->findPanel(context_, PanelQualifiedName, &panel)
            != D2RL::Widgets::Result::Success) {
        return;
    }
    try {
        for (std::int32_t players = 1; players <= maximumPlayers_; ++players) {
            const auto suffix = std::to_string(players);
            const auto hide = [&](const std::string& name) {
                D2RL::Widgets::WidgetHandle widget{};
                if (widgets_->findWidget(context_, panel, name.c_str(), &widget)
                        == D2RL::Widgets::Result::Success) {
                    (void)widgets_->setWidgetVisible(context_, widget, false);
                }
            };
            hide("SliderMarker" + suffix);
            hide("SliderTooltipFrame" + suffix);
            hide("SliderTooltipValue" + suffix);
        }
        for (int digit = 0; digit < 5; ++digit) for (int value = 0; value < 10; ++value) {
            const auto name = "ActualDigit" + std::to_string(digit) + std::to_string(value);
            D2RL::Widgets::WidgetHandle widget{};
            if (widgets_->findWidget(context_, panel, name.c_str(), &widget) == D2RL::Widgets::Result::Success)
                (void)widgets_->setWidgetVisible(context_, widget, false);
        }
    } catch (...) {
    }
}

void NativeSliderPanel::SetTooltipValue(std::int32_t players) noexcept try {
    players = DisplayCount(players);
    if (!tooltipWidgetsInitialized_) {
        HideAllTooltipWidgets();
        tooltipWidgetsInitialized_ = true;
        tooltipValue_ = 0;
    }
    if (players == tooltipValue_) return;
    if (tooltipValue_ > 0) SetTooltipWidgetsVisible(std::clamp(tooltipValue_, minimumPlayers_, maximumPlayers_), false);
    SetActualDigits(tooltipValue_, false);
    tooltipValue_ = players;
    SetTooltipWidgetsVisible(std::clamp(players, minimumPlayers_, maximumPlayers_), true);
    if (players < minimumPlayers_ || players > maximumPlayers_) {
        // Hide the endpoint's normal label so the actual value is unambiguous.
        D2RL::Widgets::WidgetHandle panel{}, widget{};
        if (widgets_->findPanel(context_, PanelQualifiedName, &panel) == D2RL::Widgets::Result::Success) {
            const auto name = "SliderTooltipValue" + std::to_string(std::clamp(players, minimumPlayers_, maximumPlayers_));
            if (widgets_->findWidget(context_, panel, name.c_str(), &widget) == D2RL::Widgets::Result::Success)
                (void)widgets_->setWidgetVisible(context_, widget, false);
        }
        SetActualDigits(players, true);
    }
} catch (...) {}
void NativeSliderPanel::SetActualDigits(std::int32_t players, bool visible) noexcept try {
    if ((players >= minimumPlayers_ && players <= maximumPlayers_) || !widgets_) return;
    D2RL::Widgets::WidgetHandle panel{};
    if (widgets_->findPanel(context_, PanelQualifiedName, &panel) != D2RL::Widgets::Result::Success) return;
    char digits[6]{};
    std::snprintf(digits, sizeof(digits), "%05d", DisplayCount(players));
    bool started = false;
    for (int i = 0; i < 5; ++i) {
        if (!started && digits[i] == '0' && i < 4) continue;
        started = true;
        const auto name = "ActualDigit" + std::to_string(i) + digits[i];
        D2RL::Widgets::WidgetHandle widget{};
        if (widgets_->findWidget(context_, panel, name.c_str(), &widget) == D2RL::Widgets::Result::Success)
            (void)widgets_->setWidgetVisible(context_, widget, visible);
    }
} catch (...) {}

void NativeSliderPanel::QueueTooltipValue(std::int32_t players) noexcept {
    pendingTooltipValue_.store(players, std::memory_order_release);
    if (threads_ == nullptr || threads_->runOnUiThread == nullptr) {
        return;
    }
    bool expected = false;
    if (!tooltipUpdateQueued_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;
    }
    if (threads_->runOnUiThread(context_, OnQueuedTooltipValue, this)
            != D2RL::Threads::Result::Success) {
        tooltipUpdateQueued_.store(false, std::memory_order_release);
    }
}

void __cdecl NativeSliderPanel::OnQueuedTooltipValue(
        const D2RL::PluginContext* context,
        void* userData) noexcept {
    auto* self = static_cast<NativeSliderPanel*>(userData);
    if (self == nullptr || !self->started_ || context != self->context_) {
        return;
    }
    const auto players = self->pendingTooltipValue_.load(
        std::memory_order_acquire);
    self->SetTooltipValue(players);
    self->tooltipUpdateQueued_.store(false, std::memory_order_release);
    if (self->pendingTooltipValue_.load(std::memory_order_acquire) != players) {
        self->QueueTooltipValue(self->pendingTooltipValue_.load(
            std::memory_order_acquire));
    }
}

auto NativeSliderPanel::IsOpen() const noexcept -> bool {
    if (!started_ || context_ == nullptr || panels_ == nullptr
            || panelHandle_ == D2RL::Panels::InvalidHandle
            || panels_->getPanelInfo == nullptr) {
        return false;
    }
    D2RL::Panels::PanelInfo info{
        .structSize = D2RL::Panels::PanelInfoSize,
    };
    return panels_->getPanelInfo(context_, panelHandle_, &info)
            == D2RL::Panels::Result::Success
        && info.presentationState == D2RL::Panels::PresentationState::Open;
}

void NativeSliderPanel::ShowHudButton() noexcept {
    QueueHudButtonVisibility(true);
}

void NativeSliderPanel::HideHudButton() noexcept {
    QueueHudButtonVisibility(false);
}

auto NativeSliderPanel::IsHudButtonOpen() const noexcept -> bool {
    if (!started_ || context_ == nullptr || panels_ == nullptr
            || hudButtonPanelHandle_ == D2RL::Panels::InvalidHandle
            || panels_->getPanelInfo == nullptr) {
        return false;
    }
    D2RL::Panels::PanelInfo info{
        .structSize = D2RL::Panels::PanelInfoSize,
    };
    return panels_->getPanelInfo(context_, hudButtonPanelHandle_, &info)
            == D2RL::Panels::Result::Success
        && info.presentationState == D2RL::Panels::PresentationState::Open;
}

void NativeSliderPanel::SetHudButtonVisible(bool visible) noexcept {
    if (!started_ || context_ == nullptr || panels_ == nullptr
            || hudButtonPanelHandle_ == D2RL::Panels::InvalidHandle) {
        return;
    }
    const bool currentlyOpen = IsHudButtonOpen();
    if (currentlyOpen == visible) return;
    const auto result = visible
        ? panels_->openPanel(context_, hudButtonPanelHandle_)
        : panels_->closePanel(context_, hudButtonPanelHandle_);
    if (result != D2RL::Panels::Result::Success) {
        context_->LogWarn(visible
            ? "Players HUD Slider: opening the HUD button panel failed."
            : "Players HUD Slider: closing the HUD button panel failed.");
        return;
    }
    if (visible) {
        context_->LogInfo(
            "Players HUD Slider: HUD button panel opened at priority 10000.");
    }
}

void NativeSliderPanel::QueueHudButtonVisibility(bool visible) noexcept {
    pendingHudButtonVisible_.store(visible, std::memory_order_release);
    if (threads_ == nullptr || threads_->runOnUiThread == nullptr) {
        return;
    }
    bool expected = false;
    if (!hudButtonUpdateQueued_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;
    }
    if (threads_->runOnUiThread(context_, OnQueuedHudButtonVisibility, this)
            != D2RL::Threads::Result::Success) {
        hudButtonUpdateQueued_.store(false, std::memory_order_release);
    }
}

void __cdecl NativeSliderPanel::OnQueuedHudButtonVisibility(
        const D2RL::PluginContext* context,
        void* userData) noexcept {
    auto* self = static_cast<NativeSliderPanel*>(userData);
    if (self == nullptr || !self->started_ || context != self->context_) {
        return;
    }
    const bool visible = self->pendingHudButtonVisible_.load(
        std::memory_order_acquire);
    self->SetHudButtonVisible(visible);
    self->hudButtonUpdateQueued_.store(false, std::memory_order_release);
    if (self->pendingHudButtonVisible_.load(std::memory_order_acquire)
            != visible) {
        self->QueueHudButtonVisibility(
            self->pendingHudButtonVisible_.load(std::memory_order_acquire));
    }
}

} // namespace players_hud_slider
