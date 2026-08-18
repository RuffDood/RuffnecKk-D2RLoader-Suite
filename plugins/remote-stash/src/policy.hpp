#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace ruffneckk::remote_stash {

enum class InputDevice : std::uint8_t {
    Keyboard,
    Mouse,
};

struct Hotkey {
    std::uint32_t virtualKey{};
    InputDevice device{InputDevice::Keyboard};
    bool control{};
    bool shift{};
    bool alt{};
};

enum class HotkeyMode : std::uint8_t {
    RemoteOnly,
    RemoteAndInventory,
};

inline const char* HotkeyModeName(HotkeyMode mode) noexcept {
    return mode == HotkeyMode::RemoteAndInventory
        ? "remoteAndInventory"
        : "remoteOnly";
}

enum class ButtonPlacement : std::uint8_t {
    Automatic,
    Custom,
};

enum class ButtonAnchor : std::uint8_t {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

inline const char* ButtonPlacementName(ButtonPlacement placement) noexcept {
    return placement == ButtonPlacement::Custom ? "custom" : "auto";
}

inline const char* ButtonAnchorName(ButtonAnchor anchor) noexcept {
    switch (anchor) {
    case ButtonAnchor::TopLeft: return "topLeft";
    case ButtonAnchor::TopRight: return "topRight";
    case ButtonAnchor::BottomRight: return "bottomRight";
    default: return "bottomLeft";
    }
}

struct ButtonConfig {
    ButtonPlacement placement{ButtonPlacement::Automatic};
    ButtonAnchor anchor{ButtonAnchor::BottomLeft};
    std::int32_t offsetX{};
    std::int32_t offsetY{};
    std::int32_t width{176};
    std::int32_t height{112};
    std::string spriteFile{};
    std::string lowendSpriteFile{};
    std::uint32_t normalFrame{};
    std::uint32_t pressedFrame{2};
    std::uint32_t disabledFrame{1};
    std::uint32_t hoveredFrame{3};
};

struct HotkeyConfig {
    bool enabled{true};
    bool inventoryButtonEnabled{true};
    bool hotkeyEnabled{true};
    bool diagnostics{};
    Hotkey hotkey{'R', InputDevice::Keyboard, false, true, false};
    std::string hotkeyText{"SHIFT+R"};
    HotkeyMode mode{HotkeyMode::RemoteOnly};
    ButtonConfig button{};
};

struct WidgetRect {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

enum class PlacementFailure : std::uint8_t {
    None,
    InvalidPanel,
    InvalidGrid,
    InvalidFooter,
    InvalidButton,
    CoordinateOverflow,
    OutsidePanel,
    GridCollision,
    FooterCollision,
};

struct PlacementResult {
    bool valid{};
    WidgetRect rect{};
    PlacementFailure failure{PlacementFailure::InvalidPanel};
};

struct SpriteMetadata {
    std::int32_t atlasWidth{};
    std::int32_t height{};
    std::uint32_t frameCount{};
    std::int32_t frameWidth{};
};

inline constexpr bool HasUsableSize(const WidgetRect& rect) noexcept {
    return rect.width > 0 && rect.height > 0;
}

inline constexpr std::int64_t Right(const WidgetRect& rect) noexcept {
    return static_cast<std::int64_t>(rect.x) + rect.width;
}

inline constexpr std::int64_t Bottom(const WidgetRect& rect) noexcept {
    return static_cast<std::int64_t>(rect.y) + rect.height;
}

inline constexpr bool Contains(
    const WidgetRect& bounds,
    const WidgetRect& candidate
) noexcept {
    return HasUsableSize(bounds)
        && HasUsableSize(candidate)
        && candidate.x >= bounds.x
        && candidate.y >= bounds.y
        && Right(candidate) <= Right(bounds)
        && Bottom(candidate) <= Bottom(bounds);
}

inline constexpr bool Intersects(
    const WidgetRect& first,
    const WidgetRect& second
) noexcept {
    return HasUsableSize(first)
        && HasUsableSize(second)
        && first.x < Right(second)
        && second.x < Right(first)
        && first.y < Bottom(second)
        && second.y < Bottom(first);
}

inline constexpr WidgetRect UnionRect(
    const WidgetRect& first,
    const WidgetRect& second
) noexcept {
    if (!HasUsableSize(first)) return second;
    if (!HasUsableSize(second)) return first;
    const auto left = first.x < second.x ? first.x : second.x;
    const auto top = first.y < second.y ? first.y : second.y;
    const auto right = Right(first) > Right(second) ? Right(first) : Right(second);
    const auto bottom = Bottom(first) > Bottom(second) ? Bottom(first) : Bottom(second);
    const auto width = right - left;
    const auto height = bottom - top;
    if (width > std::numeric_limits<std::int32_t>::max()
        || height > std::numeric_limits<std::int32_t>::max()) {
        return {};
    }
    return {
        .x = left,
        .y = top,
        .width = static_cast<std::int32_t>(width),
        .height = static_cast<std::int32_t>(height),
    };
}

inline constexpr PlacementResult PlaceDesktopFooterLeft(
    const WidgetRect& panel,
    const WidgetRect& grid,
    const WidgetRect& goldButton,
    const WidgetRect& goldAmount,
    const WidgetRect& button
) noexcept {
    if (!HasUsableSize(panel)) {
        return {.failure = PlacementFailure::InvalidPanel};
    }
    if (!HasUsableSize(grid)) {
        return {.failure = PlacementFailure::InvalidGrid};
    }
    const auto footer = UnionRect(goldButton, goldAmount);
    if (!HasUsableSize(footer)) {
        return {.failure = PlacementFailure::InvalidFooter};
    }
    if (!HasUsableSize(button)) {
        return {.failure = PlacementFailure::InvalidButton};
    }

    const auto preferredY = static_cast<std::int64_t>(footer.y)
        + (static_cast<std::int64_t>(footer.height) - button.height) / 2;
    const auto minimumY = Bottom(grid);
    const auto maximumY = Bottom(panel) - button.height;
    if (minimumY > maximumY) {
        return {.failure = PlacementFailure::GridCollision};
    }
    auto y = preferredY < minimumY ? minimumY : preferredY;
    if (y > maximumY) y = maximumY;
    if (y < std::numeric_limits<std::int32_t>::min()
        || y > std::numeric_limits<std::int32_t>::max()) {
        return {.failure = PlacementFailure::CoordinateOverflow};
    }

    const WidgetRect candidate{
        .x = grid.x,
        .y = static_cast<std::int32_t>(y),
        .width = button.width,
        .height = button.height,
    };
    if (!Contains(panel, candidate)) {
        return {.failure = PlacementFailure::OutsidePanel};
    }
    if (Intersects(candidate, grid)) {
        return {.failure = PlacementFailure::GridCollision};
    }
    if (Intersects(candidate, footer)) {
        return {.failure = PlacementFailure::FooterCollision};
    }
    return {
        .valid = true,
        .rect = candidate,
        .failure = PlacementFailure::None,
    };
}

inline constexpr PlacementResult PlaceAtAnchor(
    const WidgetRect& panel,
    const WidgetRect& button,
    ButtonAnchor anchor,
    std::int32_t offsetX,
    std::int32_t offsetY,
    bool requireContainment
) noexcept {
    if (!HasUsableSize(panel)) {
        return {.failure = PlacementFailure::InvalidPanel};
    }
    if (!HasUsableSize(button)) {
        return {.failure = PlacementFailure::InvalidButton};
    }
    std::int64_t x = offsetX;
    std::int64_t y = offsetY;
    if (anchor == ButtonAnchor::TopRight
        || anchor == ButtonAnchor::BottomRight) {
        x += static_cast<std::int64_t>(panel.width) - button.width;
    }
    if (anchor == ButtonAnchor::BottomLeft
        || anchor == ButtonAnchor::BottomRight) {
        y += static_cast<std::int64_t>(panel.height) - button.height;
    }
    if (x < std::numeric_limits<std::int32_t>::min()
        || x > std::numeric_limits<std::int32_t>::max()
        || y < std::numeric_limits<std::int32_t>::min()
        || y > std::numeric_limits<std::int32_t>::max()) {
        return {.failure = PlacementFailure::CoordinateOverflow};
    }
    const WidgetRect candidate{
        .x = static_cast<std::int32_t>(x),
        .y = static_cast<std::int32_t>(y),
        .width = button.width,
        .height = button.height,
    };
    if (requireContainment
        && !Contains(WidgetRect{0, 0, panel.width, panel.height}, candidate)) {
        return {.failure = PlacementFailure::OutsidePanel};
    }
    return {
        .valid = true,
        .rect = candidate,
        .failure = PlacementFailure::None,
    };
}

inline bool InspectSpA1Sprite(
    const std::uint8_t* bytes,
    std::size_t byteCount,
    SpriteMetadata& metadata
) noexcept {
    constexpr std::size_t HeaderSize = 40;
    if (!bytes || byteCount < HeaderSize
        || bytes[0] != 'S'
        || (bytes[1] != 'p' && bytes[1] != 'P')
        || (bytes[2] != 'a' && bytes[2] != 'A')
        || bytes[3] != '1') {
        return false;
    }
    const auto read16 = [bytes](std::size_t offset) noexcept {
        return static_cast<std::uint16_t>(bytes[offset])
            | static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    };
    const auto read32 = [bytes](std::size_t offset) noexcept {
        return static_cast<std::uint32_t>(bytes[offset])
            | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
            | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
            | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    };
    if (read16(4) != 31) return false;
    const auto atlasWidth = static_cast<std::int32_t>(read32(8));
    const auto height = static_cast<std::int32_t>(read32(12));
    const auto frameCount = read32(20);
    if (atlasWidth <= 0 || height <= 0 || frameCount == 0
        || static_cast<std::uint32_t>(atlasWidth) % frameCount != 0) {
        return false;
    }
    const auto pixelCount = static_cast<std::uint64_t>(atlasWidth)
        * static_cast<std::uint64_t>(height);
    if (pixelCount > (std::numeric_limits<std::uint64_t>::max() - HeaderSize) / 4
        || HeaderSize + pixelCount * 4 > byteCount) {
        return false;
    }
    metadata = {
        .atlasWidth = atlasWidth,
        .height = height,
        .frameCount = frameCount,
        .frameWidth = atlasWidth / static_cast<std::int32_t>(frameCount),
    };
    return true;
}

inline bool ButtonFramesFit(
    const ButtonConfig& config,
    std::uint32_t frameCount
) noexcept {
    return frameCount > 0
        && config.normalFrame < frameCount
        && config.pressedFrame < frameCount
        && config.disabledFrame < frameCount
        && config.hoveredFrame < frameCount;
}

inline std::string BuildButtonLayoutJson(const ButtonConfig& config) {
    std::string json;
    json.reserve(640);
    json += "{\n  \"type\": \"ButtonWidget\",\n";
    json += "  \"name\": \"ruffneckk-remote-stash/inventory-button\",\n";
    json += "  \"fields\": {\n";
    json += "    \"rect\": { \"x\": -32000, \"y\": -32000, \"width\": ";
    json += std::to_string(config.width);
    json += ", \"height\": ";
    json += std::to_string(config.height);
    json += " },\n";
    json += "    \"filename\": \"D2RLoader\\\\ruffneckk-remote-stash\\\\inventory-button\",\n";
    json += "    \"normalFrame\": " + std::to_string(config.normalFrame) + ",\n";
    json += "    \"pressedFrame\": " + std::to_string(config.pressedFrame) + ",\n";
    json += "    \"disabledFrame\": " + std::to_string(config.disabledFrame) + ",\n";
    json += "    \"hoveredFrame\": " + std::to_string(config.hoveredFrame) + ",\n";
    json += "    \"tooltipString\": \"@OpenCurrentStashLegend\",\n";
    json += "    \"onClickMessage\": \"PanelManager:OpenPanel:RuffnecKkRemoteStash\"\n";
    json += "  }\n}\n";
    return json;
}

enum class HotkeyDispatch : std::uint8_t {
    Refuse,
    Open,
    Close,
};

enum class ToggleSource : std::uint8_t {
    Button,
    Hotkey,
};

struct RemoteTogglePlan {
    HotkeyDispatch dispatch{HotkeyDispatch::Refuse};
    bool closeCompanionInventoryAfterOpen{};
    bool closeInventoryAfterClose{};
    bool preserveInventoryAfterClose{};
    bool coupleInventory{};
};

struct RemoteOpenRollbackPlan {
    bool closeInventory{};
    bool openInventory{};
};

enum class CompanionInventoryCloseDecision : std::uint8_t {
    Wait,
    Close,
    Expire,
};

enum class PairedInterface : std::uint8_t {
    Other,
    Inventory,
    Stash,
};

enum class PairedCloseOrigin : std::uint8_t {
    Other,
    Movement,
    Server,
    StashButton,
    Escape,
    GeneralTeardown,
    Inventory,
};

struct PairedClosePlan {
    bool suppress{};
    bool deactivate{};
    bool notifyServer{};
    bool closeStash{};
    bool closeInventory{};
    bool preserveInventory{};
};

inline constexpr std::string_view RemoteStashUiTarget{"PanelManager"};
inline constexpr std::string_view RemoteStashUiCommand{"OpenPanel"};
inline constexpr std::string_view RemoteStashUiText{"RuffnecKkRemoteStash"};

inline std::string UpperTrim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    std::string result(value.substr(first, last - first + 1));
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return result;
}

inline bool ParseMainKey(
    const std::string& token,
    std::uint32_t& virtualKey,
    InputDevice& device
) {
    if (token.size() == 1) {
        const auto ch = static_cast<unsigned char>(token.front());
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            virtualKey = ch;
            device = InputDevice::Keyboard;
            return true;
        }
        if (ch == ';') {
            virtualKey = 0xBA; // VK_OEM_1
            device = InputDevice::Keyboard;
            return true;
        }
    }
    if (token.size() >= 2 && token.front() == 'F') {
        unsigned value{};
        for (std::size_t index = 1; index < token.size(); ++index) {
            if (token[index] < '0' || token[index] > '9') return false;
            const auto digit = static_cast<unsigned>(token[index] - '0');
            if (value > (24U - digit) / 10U) return false;
            value = value * 10 + digit;
        }
        if (value >= 1 && value <= 24) {
            virtualKey = 0x70U + value - 1U;
            device = InputDevice::Keyboard;
            return true;
        }
    }

    struct NamedKey {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedKey namedKeys[]{
        {"SPACE", 0x20}, {"TAB", 0x09}, {"INSERT", 0x2D},
        {"DELETE", 0x2E}, {"HOME", 0x24}, {"END", 0x23},
        {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22},
        {"SEMICOLON", 0xBA},
    };
    for (const auto& key : namedKeys) {
        if (token == key.name) {
            virtualKey = key.virtualKey;
            device = InputDevice::Keyboard;
            return true;
        }
    }

    struct NamedMouseButton {
        std::string_view name;
        std::uint32_t virtualKey;
    };
    constexpr NamedMouseButton mouseButtons[]{
        {"MOUSE3", 0x04}, {"MOUSE 3", 0x04}, {"MIDDLE", 0x04},
        {"MBUTTON", 0x04},
        {"MOUSE4", 0x05}, {"MOUSE 4", 0x05}, {"XBUTTON1", 0x05},
        {"MOUSE5", 0x06}, {"MOUSE 5", 0x06}, {"XBUTTON2", 0x06},
    };
    for (const auto& button : mouseButtons) {
        if (token == button.name) {
            virtualKey = button.virtualKey;
            device = InputDevice::Mouse;
            return true;
        }
    }
    return false;
}

inline bool ParseHotkey(std::string_view text, Hotkey& hotkey) {
    Hotkey parsed{};
    bool hasMainKey{};
    std::size_t begin{};
    while (begin <= text.size()) {
        const auto separator = text.find('+', begin);
        const auto token = UpperTrim(text.substr(
            begin,
            separator == std::string_view::npos
                ? text.size() - begin
                : separator - begin));
        if (token.empty()) return false;
        if (token == "CTRL" || token == "CONTROL") {
            if (parsed.control) return false;
            parsed.control = true;
        } else if (token == "SHIFT") {
            if (parsed.shift) return false;
            parsed.shift = true;
        } else if (token == "ALT") {
            if (parsed.alt) return false;
            parsed.alt = true;
        } else {
            if (hasMainKey || !ParseMainKey(token, parsed.virtualKey, parsed.device)) {
                return false;
            }
            hasMainKey = true;
        }
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    if (!hasMainKey || parsed.virtualKey == 0) return false;
    hotkey = parsed;
    return true;
}

inline bool IsMouseHotkey(const Hotkey& hotkey) noexcept {
    return hotkey.device == InputDevice::Mouse;
}

inline std::uint32_t ModifierCount(const Hotkey& hotkey) noexcept {
    return static_cast<std::uint32_t>(hotkey.control)
        + static_cast<std::uint32_t>(hotkey.shift)
        + static_cast<std::uint32_t>(hotkey.alt);
}

inline bool IsSdkInputCompatible(const Hotkey& hotkey) noexcept {
    return hotkey.device == InputDevice::Keyboard
        && ModifierCount(hotkey) <= 1;
}

// Mirrors D2RLoader Input::Modifier without exposing SDK headers to policy tests.
inline std::uint32_t SdkModifierValue(const Hotkey& hotkey) noexcept {
    if (hotkey.shift) return 1;
    if (hotkey.control) return 2;
    if (hotkey.alt) return 3;
    return 0;
}

inline bool ExactModifiersMatch(
    const Hotkey& hotkey,
    bool control,
    bool shift,
    bool alt
) noexcept {
    return hotkey.control == control
        && hotkey.shift == shift
        && hotkey.alt == alt;
}

inline RemoteTogglePlan ResolveRemoteTogglePlan(
    ToggleSource source,
    HotkeyMode mode,
    bool remoteSessionIsActive,
    bool knownInputIsBlocked,
    bool inventoryIsOpen
) noexcept {
    if (source == ToggleSource::Hotkey && knownInputIsBlocked) {
        return {};
    }

    RemoteTogglePlan plan{};
    plan.dispatch = remoteSessionIsActive
        ? HotkeyDispatch::Close
        : HotkeyDispatch::Open;
    const auto toggleInventoryTogether = source == ToggleSource::Hotkey
        && mode == HotkeyMode::RemoteAndInventory;
    plan.coupleInventory = toggleInventoryTogether;
    if (plan.dispatch == HotkeyDispatch::Open) {
        plan.closeCompanionInventoryAfterOpen = source == ToggleSource::Hotkey
            && !toggleInventoryTogether
            && !inventoryIsOpen;
    } else {
        plan.closeInventoryAfterClose = toggleInventoryTogether;
        plan.preserveInventoryAfterClose = !toggleInventoryTogether;
    }
    return plan;
}

inline RemoteOpenRollbackPlan ResolveRemoteOpenRollbackPlan(
    bool inventoryWasOpenBeforeOpen,
    bool inventoryIsOpenNow
) noexcept {
    return {
        .closeInventory = !inventoryWasOpenBeforeOpen && inventoryIsOpenNow,
        .openInventory = inventoryWasOpenBeforeOpen && !inventoryIsOpenNow,
    };
}

inline bool ShouldKeepInventoryOpenAfterRemoteOpen(
    bool inventoryIsCoupled,
    bool inventoryWasOpenBeforeOpen
) noexcept {
    return inventoryIsCoupled || inventoryWasOpenBeforeOpen;
}

inline bool ShouldRestoreIndependentInventory(
    bool inventoryWasOpenBeforeStashClose,
    bool inventoryIsOpenAfterStashClose
) noexcept {
    return inventoryWasOpenBeforeStashClose && !inventoryIsOpenAfterStashClose;
}

inline CompanionInventoryCloseDecision ResolveCompanionInventoryClose(
    std::uint64_t deadline,
    bool stashInterfaceIsOpening,
    std::uint64_t now
) noexcept {
    if (deadline == 0 || !stashInterfaceIsOpening) {
        return CompanionInventoryCloseDecision::Wait;
    }
    return now <= deadline
        ? CompanionInventoryCloseDecision::Close
        : CompanionInventoryCloseDecision::Expire;
}

inline bool IsRemoteStashUiMessage(
    std::string_view target,
    std::string_view command,
    std::string_view text
) noexcept {
    return target == RemoteStashUiTarget
        && command == RemoteStashUiCommand
        && text == RemoteStashUiText;
}

inline bool ResolveRemoteStashTransitionFlag(
    bool remoteHotkeyOpenIsScoped,
    bool requestedFlag
) noexcept {
    return remoteHotkeyOpenIsScoped ? false : requestedFlag;
}

inline bool ShouldSuppressHotkeyMouseReset(
    bool remoteHotkeyOpenIsScoped
) noexcept {
    return remoteHotkeyOpenIsScoped;
}

inline PairedClosePlan ResolvePairedClosePlan(
    bool remoteSessionIsActive,
    bool inventoryIsCoupled,
    bool stashInterfaceIsOpen,
    PairedInterface interfaceToClose,
    PairedCloseOrigin origin
) noexcept {
    if (!remoteSessionIsActive || interfaceToClose == PairedInterface::Other) {
        return {};
    }
    if (origin == PairedCloseOrigin::Movement) {
        if (interfaceToClose == PairedInterface::Inventory
            && !stashInterfaceIsOpen) {
            return {};
        }
        return {
            .suppress = true,
        };
    }
    if (interfaceToClose == PairedInterface::Inventory
        && !inventoryIsCoupled) {
        return {};
    }
    const auto preserveIndependentInventory = !inventoryIsCoupled
        && interfaceToClose == PairedInterface::Stash
        && origin != PairedCloseOrigin::GeneralTeardown;
    return {
        .suppress = false,
        .deactivate = true,
        // Even a native server-initiated UI close must acknowledge the custom
        // RuffnecKk session. The stock 0x77/0x14 path does not remove that
        // server-side map entry; the custom close request does, without
        // emitting another UI close.
        .notifyServer = true,
        .closeStash = true,
        .closeInventory = inventoryIsCoupled,
        .preserveInventory = preserveIndependentInventory,
    };
}

inline std::string_view Trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'
            || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'
            || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

inline std::string_view WithoutComment(std::string_view value) noexcept {
    bool quoted{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '"') quoted = !quoted;
        if (value[index] == '#' && !quoted) return value.substr(0, index);
    }
    return value;
}

inline bool SetError(
    std::string& error,
    std::size_t line,
    std::string_view message
) {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

inline bool ParseBoolean(std::string_view value, bool& output) noexcept {
    if (value == "true") {
        output = true;
        return true;
    }
    if (value == "false") {
        output = false;
        return true;
    }
    return false;
}

inline bool ParseQuotedString(
    std::string_view value,
    std::string& output
) {
    value = Trim(value);
    if (value.size() < 2 || value.front() != value.back()
        || (value.front() != '"' && value.front() != '\'')) {
        return false;
    }
    const auto content = value.substr(1, value.size() - 2);
    if (value.front() == '\'') {
        if (content.find('\'') != std::string_view::npos) return false;
        output.assign(content);
        return true;
    }

    std::string parsed;
    parsed.reserve(content.size());
    for (std::size_t index = 0; index < content.size(); ++index) {
        const auto ch = content[index];
        if (ch == '"') return false;
        if (ch != '\\') {
            parsed.push_back(ch);
            continue;
        }
        if (++index >= content.size()) return false;
        switch (content[index]) {
        case '"': parsed.push_back('"'); break;
        case '\\': parsed.push_back('\\'); break;
        case 'n': parsed.push_back('\n'); break;
        case 'r': parsed.push_back('\r'); break;
        case 't': parsed.push_back('\t'); break;
        default: return false;
        }
    }
    output = std::move(parsed);
    return true;
}

inline bool ParseInt32(std::string_view value, std::int32_t& output) noexcept {
    value = Trim(value);
    if (value.empty()) return false;
    std::int32_t parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return false;
    }
    output = parsed;
    return true;
}

inline bool ParseFrame(std::string_view value, std::uint32_t& output) noexcept {
    std::int32_t parsed{};
    if (!ParseInt32(value, parsed) || parsed < 0 || parsed > 255) return false;
    output = static_cast<std::uint32_t>(parsed);
    return true;
}

inline bool ParseConfig(
    std::string_view input,
    HotkeyConfig& output,
    std::string& error
) {
    enum class Section : std::uint8_t {
        Root,
        Button,
        Diagnostics,
    };

    HotkeyConfig parsed{};
    bool enabledSeen{};
    bool inventoryButtonEnabledSeen{};
    bool hotkeyEnabledSeen{};
    bool hotkeySeen{};
    bool hotkeyModeSeen{};
    bool diagnosticsSeen{};
    bool diagnosticsTableSeen{};
    bool buttonTableSeen{};
    bool placementSeen{};
    bool anchorSeen{};
    bool offsetXSeen{};
    bool offsetYSeen{};
    bool widthSeen{};
    bool heightSeen{};
    bool spriteFileSeen{};
    bool lowendSpriteFileSeen{};
    bool normalFrameSeen{};
    bool pressedFrameSeen{};
    bool disabledFrameSeen{};
    bool hoveredFrameSeen{};
    Section section{Section::Root};

    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = Trim(WithoutComment(input.substr(
            start,
            end == std::string_view::npos
                ? input.size() - start : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line == "[button]" && !buttonTableSeen) {
                buttonTableSeen = true;
                section = Section::Button;
            } else if (line == "[diagnostics]" && !diagnosticsTableSeen) {
                diagnosticsTableSeen = true;
                section = Section::Diagnostics;
            } else {
                return SetError(
                    error, lineNumber, "unknown or duplicate section");
            }
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string_view::npos
            || line.find('=', equal + 1) != std::string_view::npos) {
            return SetError(error, lineNumber,
                "expected one key/value assignment");
        }
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty()) {
            return SetError(error, lineNumber, "invalid key/value assignment");
        }

        bool valid{};
        bool* duplicate{};
        if (section == Section::Diagnostics) {
            if (key != "enabled") {
                return SetError(
                    error, lineNumber, "unknown diagnostics setting");
            }
            duplicate = &diagnosticsSeen;
            valid = ParseBoolean(value, parsed.diagnostics);
        } else if (section == Section::Button) {
            if (key == "placement") {
                duplicate = &placementSeen;
                std::string placement;
                valid = ParseQuotedString(value, placement);
                if (valid && placement == "auto") {
                    parsed.button.placement = ButtonPlacement::Automatic;
                } else if (valid && placement == "custom") {
                    parsed.button.placement = ButtonPlacement::Custom;
                } else if (valid) {
                    return SetError(error, lineNumber,
                        "button placement must be auto or custom");
                }
            } else if (key == "anchor") {
                duplicate = &anchorSeen;
                std::string anchor;
                valid = ParseQuotedString(value, anchor);
                if (valid && anchor == "topLeft") {
                    parsed.button.anchor = ButtonAnchor::TopLeft;
                } else if (valid && anchor == "topRight") {
                    parsed.button.anchor = ButtonAnchor::TopRight;
                } else if (valid && anchor == "bottomLeft") {
                    parsed.button.anchor = ButtonAnchor::BottomLeft;
                } else if (valid && anchor == "bottomRight") {
                    parsed.button.anchor = ButtonAnchor::BottomRight;
                } else if (valid) {
                    return SetError(error, lineNumber,
                        "button anchor must be topLeft, topRight, bottomLeft, or bottomRight");
                }
            } else if (key == "offset_x") {
                duplicate = &offsetXSeen;
                valid = ParseInt32(value, parsed.button.offsetX);
            } else if (key == "offset_y") {
                duplicate = &offsetYSeen;
                valid = ParseInt32(value, parsed.button.offsetY);
            } else if (key == "width") {
                duplicate = &widthSeen;
                valid = ParseInt32(value, parsed.button.width);
            } else if (key == "height") {
                duplicate = &heightSeen;
                valid = ParseInt32(value, parsed.button.height);
            } else if (key == "sprite_file") {
                duplicate = &spriteFileSeen;
                valid = ParseQuotedString(value, parsed.button.spriteFile);
            } else if (key == "lowend_sprite_file") {
                duplicate = &lowendSpriteFileSeen;
                valid = ParseQuotedString(value, parsed.button.lowendSpriteFile);
            } else if (key == "normal_frame") {
                duplicate = &normalFrameSeen;
                valid = ParseFrame(value, parsed.button.normalFrame);
            } else if (key == "pressed_frame") {
                duplicate = &pressedFrameSeen;
                valid = ParseFrame(value, parsed.button.pressedFrame);
            } else if (key == "disabled_frame") {
                duplicate = &disabledFrameSeen;
                valid = ParseFrame(value, parsed.button.disabledFrame);
            } else if (key == "hovered_frame") {
                duplicate = &hoveredFrameSeen;
                valid = ParseFrame(value, parsed.button.hoveredFrame);
            } else {
                return SetError(error, lineNumber, "unknown button setting");
            }
        } else if (key == "enabled") {
            duplicate = &enabledSeen;
            valid = ParseBoolean(value, parsed.enabled);
        } else if (key == "inventory_button_enabled") {
            duplicate = &inventoryButtonEnabledSeen;
            valid = ParseBoolean(value, parsed.inventoryButtonEnabled);
        } else if (key == "hotkey_enabled") {
            duplicate = &hotkeyEnabledSeen;
            valid = ParseBoolean(value, parsed.hotkeyEnabled);
        } else if (key == "hotkey") {
            duplicate = &hotkeySeen;
            valid = ParseQuotedString(value, parsed.hotkeyText);
            if (valid && !ParseHotkey(parsed.hotkeyText, parsed.hotkey)) {
                return SetError(error, lineNumber,
                    "hotkey is invalid or unsupported");
            }
        } else if (key == "hotkey_mode") {
            duplicate = &hotkeyModeSeen;
            std::string mode;
            valid = ParseQuotedString(value, mode);
            if (valid && mode == "remoteOnly") {
                parsed.mode = HotkeyMode::RemoteOnly;
            } else if (valid && mode == "remoteAndInventory") {
                parsed.mode = HotkeyMode::RemoteAndInventory;
            } else if (valid) {
                return SetError(error, lineNumber,
                    "hotkey_mode must be remoteOnly or remoteAndInventory");
            }
        } else if (key == "diagnostics") {
            duplicate = &diagnosticsSeen;
            valid = ParseBoolean(value, parsed.diagnostics);
        } else {
            return SetError(error, lineNumber, "unknown setting");
        }
        if (duplicate && *duplicate) {
            return SetError(error, lineNumber, "duplicate setting");
        }
        if (duplicate) *duplicate = true;
        if (!valid) return SetError(error, lineNumber, "invalid setting value");
    }

    if (!(enabledSeen && hotkeySeen)) {
        error = "one or more required settings are missing";
        return false;
    }
    // Before the Suite introduced a master switch, the root `enabled` key
    // controlled only the hotkey while the physical button stayed active.
    // The presence of either new surface switch identifies the new schema;
    // otherwise preserve the old meaning without requiring a migration.
    if (!inventoryButtonEnabledSeen && !hotkeyEnabledSeen) {
        parsed.hotkeyEnabled = parsed.enabled;
        parsed.enabled = true;
    }
    if (parsed.button.offsetX < -32768 || parsed.button.offsetX > 32767
        || parsed.button.offsetY < -32768 || parsed.button.offsetY > 32767) {
        error = "button offsets must be between -32768 and 32767";
        return false;
    }
    if (parsed.button.width < 1 || parsed.button.width > 4096
        || parsed.button.height < 1 || parsed.button.height > 4096) {
        error = "button width and height must be between 1 and 4096";
        return false;
    }
    if (parsed.button.spriteFile.size() > 1024
        || parsed.button.lowendSpriteFile.size() > 1024
        || parsed.button.spriteFile.find('\0') != std::string::npos
        || parsed.button.lowendSpriteFile.find('\0') != std::string::npos) {
        error = "button sprite paths are invalid or too long";
        return false;
    }
    output = parsed;
    error.clear();
    return true;
}

} // namespace ruffneckk::remote_stash
