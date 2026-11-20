#include "../local_provider_windows.hpp"
#include <RuffnecKk/live_settings_windows.hpp>

#include "map_seed_selector.hpp"

#include <D2RLPlugin/logging.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

extern "C" {
__declspec(align(4)) volatile long gMapSeedOverrideActive{};
__declspec(align(4)) volatile long gSelectedMapSeed{};
// 0 = not classified yet, 1 = this entry needed the selected-seed override,
// 2 = D2R already supplied the selected seed from the character's saved map.
__declspec(align(4)) volatile long gMapSeedFlowState{};
__declspec(align(4)) volatile long gLastObservedMapSeed{};
__declspec(align(4)) volatile long gLastObservedMapSeedValid{};
void* gOriginalDrlgSeedInitialization{};
void* gOriginalGameSeedIngress{};
void* gOriginalSavedSeedCopyA{};
void* gOriginalSavedSeedCopyB{};
void* gOriginalSavedSeedCopyC{};
}

namespace MapSeedSelector {
namespace {

constexpr std::uintptr_t DrlgSeedInitializationWitnessRva = 0x326E89;
constexpr std::uintptr_t DrlgSeedInitializationRva = 0x326E9E;
constexpr std::size_t DrlgSeedInitializationHookOffset = 21;
constexpr std::size_t DrlgSeedInitializationOverwriteSize = 15;
constexpr std::uintptr_t GameSeedIngressRva = 0x48AADB;
constexpr std::array<std::uint8_t, 18> GameSeedIngressWitness{
    0x44, 0x8B, 0x87, 0x2C, 0x01, 0x00, 0x00,
    0x40, 0x0F, 0xB6, 0xD5,
    0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00,
};
constexpr std::uintptr_t SavedSeedCopyAWitnessRva = 0x4210BA;
constexpr std::uintptr_t SavedSeedCopyBWitnessRva = 0x532432;
constexpr std::uintptr_t SavedSeedCopyCWitnessRva = 0x532B63;
constexpr std::size_t SavedSeedCopyStoreOffset = 17;
constexpr std::size_t SavedSeedCopyStoreSize = 6;
constexpr std::uintptr_t SavedSeedCopyAHookRva =
    SavedSeedCopyAWitnessRva + SavedSeedCopyStoreOffset;
constexpr std::uintptr_t SavedSeedCopyBHookRva =
    SavedSeedCopyBWitnessRva + SavedSeedCopyStoreOffset;
constexpr std::uintptr_t SavedSeedCopyCHookRva =
    SavedSeedCopyCWitnessRva + SavedSeedCopyStoreOffset;
constexpr std::size_t MaximumConfigBytes = 65536;
constexpr std::size_t MaximumSeeds = 16;
constexpr std::size_t MaximumSeedNameLength = 48;
constexpr std::int32_t MaximumPanelCoordinate = 16384;
constexpr std::int32_t PickerWidth = 448;
constexpr std::int32_t AddSeedButtonWidth = 56;
constexpr std::int32_t LabelWidth = 160;
constexpr std::int32_t DropdownHeight = 56;
constexpr std::int32_t SelectorTitleHeight = 34;
constexpr std::int32_t OptionRowHeight = 43;
constexpr std::int32_t MenuGap = 8;
constexpr std::int32_t PopupCanvasWidth = 8192;
constexpr std::int32_t PopupCanvasHeight = 4320;
constexpr char SelectedSeedKey[] = "selected_seed";
// Keep the existing TOML sentinel so current user configs remain compatible.
constexpr char OffSelectionValue[] = "character";
std::string QualifiedPanelName{"ruffneckk-mapsense/MapSeeds0"};
std::string QualifiedOptionsPanelName{"ruffneckk-mapsense/SeedOptions0"};
constexpr char DropdownToggleMessage[] = "ruffneckk-mapsense/ToggleDropdown";
constexpr char DropdownWidgetName[] = "SeedDropdown";
constexpr char DropdownMessageTarget[] = "MapSeedSelector";
constexpr char DropdownMessageCommand[] = "SeedChanged";
constexpr char ChoiceLabelWidgetPrefix[] = "SeedChoiceLabel";
constexpr char ChoiceMessagePrefix[] = "ruffneckk-mapsense/Choose_";

// The DRLG initializer creates {seed, 0x29A}, advances the native LCG once,
// then stores the original and derived values at +0x840 and +0x860.
constexpr std::array<std::uint8_t, 129> DrlgSeedInitializationWitness{
    0x8B, 0xD3, 0x48, 0x89, 0x87, 0x58, 0x08, 0x00,
    0x00, 0x44, 0x88, 0xAF, 0x70, 0x08, 0x00, 0x00,
    0xE8, 0x72, 0x02, 0x04, 0x00, 0x8B, 0x07, 0x4C,
    0x69, 0xC8, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x47,
    0x04, 0x4C, 0x03, 0xC8, 0x89, 0x9F, 0x40, 0x08,
    0x00, 0x00, 0x8B, 0x85, 0x28, 0x08, 0x00, 0x00,
    0x49, 0x8B, 0xD1, 0x89, 0x87, 0x10, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0x85, 0x30, 0x08, 0x00, 0x00,
    0x48, 0x89, 0x87, 0x28, 0x01, 0x00, 0x00, 0x0F,
    0xB6, 0x85, 0x38, 0x08, 0x00, 0x00, 0x88, 0x87,
    0x30, 0x08, 0x00, 0x00, 0x48, 0x8B, 0x85, 0x40,
    0x08, 0x00, 0x00, 0x48, 0xC1, 0xEA, 0x20, 0x48,
    0x89, 0x87, 0x38, 0x08, 0x00, 0x00, 0x48, 0x8B,
    0x85, 0x48, 0x08, 0x00, 0x00, 0x48, 0x89, 0x87,
    0x78, 0x08, 0x00, 0x00, 0x89, 0x57, 0x04, 0x44,
    0x89, 0x0F, 0x44, 0x89, 0x8F, 0x60, 0x08, 0x00,
    0x00,
};

// The 3.2.92777 reverse-engineering notes identify these as saved-seed copies
// into Game+0x12C. All three complete witnesses matched the running 3.3.93847
// image before these guarded hooks were added.
constexpr std::array<std::uint8_t, 23> SavedSeedCopyAWitness{
    0xE8, 0x51, 0x4F, 0xC5, 0xFF, 0x40, 0x38, 0xB8,
    0x24, 0x0B, 0x00, 0x00, 0x75, 0x09, 0x8B, 0x45,
    0x1E, 0x89, 0x86, 0x2C, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 23> SavedSeedCopyBWitness{
    0xE8, 0xD9, 0x3B, 0xB4, 0xFF, 0x44, 0x38, 0xA8,
    0x24, 0x0B, 0x00, 0x00, 0x75, 0x09, 0x8B, 0x45,
    0x4B, 0x89, 0x87, 0x2C, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 23> SavedSeedCopyCWitness{
    0xE8, 0xA8, 0x34, 0xB4, 0xFF, 0x44, 0x38, 0xA8,
    0x24, 0x0B, 0x00, 0x00, 0x75, 0x09, 0x8B, 0x45,
    0x3B, 0x89, 0x87, 0x2C, 0x01, 0x00, 0x00,
};

struct SeedChoice {
    std::string name{};
    std::string selectionKey{};
    std::uint32_t value{};
};

enum class LabelPosition : std::uint8_t {
    Above,
    Below,
    Left,
    Right,
};

enum class ExpandDirection : std::uint8_t {
    Above,
    Below,
};

const D2RL::PluginContext* Context{};
const MapSeedSelector::ExtendedWidgetService* WidgetService{};
std::vector<SeedChoice> Seeds{};
std::atomic_size_t PublishedSeedCount{};
std::string ConfigText{};
std::filesystem::path ConfigPath;
std::array<std::uint8_t, DrlgSeedInitializationWitness.size()> InstalledSeedWitness{};
std::size_t SelectedChoice{};
std::int32_t PanelX{};
std::int32_t PanelY{};
std::int32_t AddSeedButtonX{944};
std::int32_t AddSeedButtonY{548};
LabelPosition SeedLabelPosition{LabelPosition::Above};
ExpandDirection SeedExpandDirection{ExpandDirection::Below};
bool NativeDropdownAvailable{};
std::atomic_bool SeedHookAvailable{};
std::atomic_bool OfflineFlowActive{};

auto Trim(std::string_view value) noexcept -> std::string_view {
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

auto IsReadableSeedName(std::string_view name) noexcept -> bool {
    name = Trim(name);
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](char character) {
        const auto value = static_cast<unsigned char>(character);
        return value >= 0x20 && value <= 0x7E;
    });
}

auto MakeSeedSelectionKey(const SeedChoice& choice) -> std::string {
    if (IsReadableSeedName(choice.name)) return choice.name;
    return "seed:" + std::to_string(choice.value);
}

auto RemoveTomlComment(std::string_view line) noexcept -> std::string_view {
    bool quoted{};
    bool escaped{};
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = false;
            }
        } else if (character == '"') {
            quoted = true;
        } else if (character == '#') {
            return line.substr(0, index);
        }
    }
    return line;
}

auto ParseBasicString(std::string_view value, std::string& result,
        bool allowNonAscii = false) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"') return false;
    result.clear();
    bool closed{};
    std::size_t index = 1;
    for (; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        if (character == '"') {
            closed = true;
            ++index;
            break;
        }
        if (character == '\\') {
            if (++index >= value.size()) return false;
            const char escaped = value[index];
            if (escaped != '\\' && escaped != '"') return false;
            result.push_back(escaped);
        } else {
            if (character < 0x20 || (!allowNonAscii && character > 0x7E))
                return false;
            result.push_back(static_cast<char>(character));
        }
    }
    return closed && Trim(value.substr(index)).empty();
}

auto ParseSeedValue(std::string_view value, std::uint32_t& output) -> bool {
    value = Trim(RemoveTomlComment(value));
    if (value.empty() || value.front() == '-') return false;
    std::string compact;
    compact.reserve(value.size());
    for (const char character : value) {
        if (character != '_') compact.push_back(character);
    }
    int base = 10;
    std::string_view digits(compact);
    if (digits.size() > 2 && digits[0] == '0'
            && (digits[1] == 'x' || digits[1] == 'X')) {
        base = 16;
        digits.remove_prefix(2);
    }
    std::uint64_t parsed{};
    const auto conversion = std::from_chars(
        digits.data(), digits.data() + digits.size(), parsed, base);
    if (digits.empty() || conversion.ec != std::errc{}
            || conversion.ptr != digits.data() + digits.size()
            || parsed > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    output = static_cast<std::uint32_t>(parsed);
    return true;
}

auto ParsePanelCoordinate(std::string_view value,
        std::int32_t& output) -> bool {
    value = Trim(RemoveTomlComment(value));
    if (value.empty()) return false;
    std::string compact;
    compact.reserve(value.size());
    for (const char character : value) {
        if (character != '_') compact.push_back(character);
    }
    if (compact.empty()) return false;
    std::int64_t parsed{};
    const auto conversion = std::from_chars(
        compact.data(), compact.data() + compact.size(), parsed, 10);
    if (conversion.ec != std::errc{}
            || conversion.ptr != compact.data() + compact.size()
            || parsed < 0 || parsed > MaximumPanelCoordinate) {
        return false;
    }
    output = static_cast<std::int32_t>(parsed);
    return true;
}

auto ParseLabelPosition(std::string_view value, LabelPosition& output) -> bool {
    std::string parsed;
    if (!ParseBasicString(value, parsed)) return false;
    if (parsed == "above") {
        output = LabelPosition::Above;
    } else if (parsed == "below") {
        output = LabelPosition::Below;
    } else if (parsed == "left") {
        output = LabelPosition::Left;
    } else if (parsed == "right") {
        output = LabelPosition::Right;
    } else {
        return false;
    }
    return true;
}

auto ParseExpandDirection(std::string_view value,
        ExpandDirection& output) -> bool {
    std::string parsed;
    if (!ParseBasicString(value, parsed)) return false;
    if (parsed == "above") {
        output = ExpandDirection::Above;
    } else if (parsed == "below") {
        output = ExpandDirection::Below;
    } else {
        return false;
    }
    return true;
}

auto IsHorizontalLabelPosition() noexcept -> bool {
    return SeedLabelPosition == LabelPosition::Left
        || SeedLabelPosition == LabelPosition::Right;
}

auto PanelWidth() noexcept -> std::int32_t {
    return IsHorizontalLabelPosition()
        ? PickerWidth + MenuGap + LabelWidth : PickerWidth;
}

auto DropdownLeft() noexcept -> std::int32_t {
    if (SeedLabelPosition == LabelPosition::Left) {
        return LabelWidth + MenuGap;
    }
    return 0;
}

auto DropdownCenterOffsetX() noexcept -> std::int32_t {
    return DropdownLeft() + PickerWidth / 2 - PanelWidth() / 2;
}

auto LabelLeft() noexcept -> std::int32_t {
    switch (SeedLabelPosition) {
    case LabelPosition::Left:
        return 0;
    case LabelPosition::Right:
        return PickerWidth + MenuGap;
    case LabelPosition::Above:
    case LabelPosition::Below:
        return (PanelWidth() - PickerWidth) / 2;
    }
    return 0;
}

auto LabelWidthForPosition() noexcept -> std::int32_t {
    return IsHorizontalLabelPosition() ? LabelWidth : PickerWidth;
}

auto LabelTop(std::int32_t dropdownTop) noexcept -> std::int32_t {
    switch (SeedLabelPosition) {
    case LabelPosition::Above:
        return dropdownTop - SelectorTitleHeight - MenuGap;
    case LabelPosition::Below:
        return dropdownTop + DropdownHeight + MenuGap;
    case LabelPosition::Left:
    case LabelPosition::Right:
        return dropdownTop + (DropdownHeight - SelectorTitleHeight) / 2;
    }
    return dropdownTop - SelectorTitleHeight - MenuGap;
}

auto ParseConfiguration(std::string_view config,
        std::vector<SeedChoice>& seeds, std::string& selectedName,
        std::int32_t& panelX,
        std::int32_t& panelY,
        std::int32_t& addSeedButtonX,
        std::int32_t& addSeedButtonY,
        LabelPosition& labelPosition,
        ExpandDirection& expandDirection) -> bool {
    seeds.clear();
    selectedName = OffSelectionValue;
    panelX = 856;
    panelY = 0;
    addSeedButtonX = 800;
    addSeedButtonY = 0;
    labelPosition = LabelPosition::Above;
    expandDirection = ExpandDirection::Below;
    SeedChoice pending{};
    bool inSeedTable{};
    bool inUiTable{};
    bool hasName{};
    bool hasSeed{};
    bool hasPositionX{};
    bool hasPositionY{};
    bool hasLabelPosition{};
    bool hasExpandDirection{};
    bool inAnyTable{};

    const auto finishSeed = [&]() -> bool {
        if (!inSeedTable) return true;
        if (!hasSeed || pending.name.size() > MaximumSeedNameLength
                || pending.name == OffSelectionValue
                || seeds.size() >= MaximumSeeds) {
            return false;
        }
        pending.selectionKey = MakeSeedSelectionKey(pending);
        const auto duplicate = std::find_if(seeds.begin(), seeds.end(),
            [&pending](const SeedChoice& existing) {
                return existing.selectionKey == pending.selectionKey;
            });
        if (duplicate != seeds.end()) return false;
        seeds.push_back(std::move(pending));
        pending = {};
        hasName = false;
        hasSeed = false;
        inSeedTable = false;
        return true;
    };

    std::size_t lineStart{};
    while (lineStart < config.size()) {
        const auto lineEnd = config.find('\n', lineStart);
        auto line = Trim(RemoveTomlComment(config.substr(lineStart,
            lineEnd == std::string_view::npos ? config.size() - lineStart
                : lineEnd - lineStart)));
        if (!line.empty()) {
            if (line == "[[seeds]]") {
                if (!finishSeed()) return false;
                inAnyTable = true;
                inUiTable = false;
                inSeedTable = true;
                pending = {};
                hasName = false;
                hasSeed = false;
            } else if (line.front() == '[') {
                if (!finishSeed()) return false;
                inAnyTable = true;
                inUiTable = line == "[ui]";
            } else {
                const auto equals = line.find('=');
                if (equals != std::string_view::npos) {
                    const auto key = Trim(line.substr(0, equals));
                    const auto value = Trim(line.substr(equals + 1));
                    if (inSeedTable && key == "name") {
                        if (hasName || !ParseBasicString(value, pending.name,
                                true)) return false;
                        hasName = true;
                    } else if (inSeedTable && key == "seed") {
                        if (hasSeed || !ParseSeedValue(value, pending.value)) return false;
                        hasSeed = true;
                    } else if (inUiTable && key == "position_x") {
                        if (hasPositionX
                                || !ParsePanelCoordinate(value,
                                    addSeedButtonX)) return false;
                        hasPositionX = true;
                    } else if (inUiTable && key == "position_y") {
                        if (hasPositionY
                                || !ParsePanelCoordinate(value,
                                    addSeedButtonY)) return false;
                        hasPositionY = true;
                    } else if (inUiTable && key == "text_position") {
                        if (hasLabelPosition
                                || !ParseLabelPosition(value, labelPosition)) return false;
                        hasLabelPosition = true;
                    } else if (inUiTable && key == "expand_direction") {
                        if (hasExpandDirection
                                || !ParseExpandDirection(value,
                                    expandDirection)) return false;
                        hasExpandDirection = true;
                    } else if (!inAnyTable && key == SelectedSeedKey) {
                        if (!ParseBasicString(value, selectedName)) return false;
                    }
                }
            }
        }
        if (lineEnd == std::string_view::npos) break;
        lineStart = lineEnd + 1;
    }
    if (!finishSeed()) return false;

    // The Add Seed button is the single position anchor for the whole group.
    panelX = addSeedButtonX + AddSeedButtonWidth;
    panelY = addSeedButtonY;
    return true;
}

auto ReadConfig(std::string& config) -> bool {
    try {
        config = RuffnecKk::LiveSettings::ReadFile(ConfigPath);
        return config.find('\0') == std::string::npos;
    } catch (...) {
        return false;
    }
}

auto WriteConfig(std::string_view config) noexcept -> bool {
    return config.size() < MaximumConfigBytes
        && RuffnecKk::LiveSettings::SaveFile(ConfigPath, config);
}

auto EscapeJson(std::string_view value) -> std::string {
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character >= 0x20) result.push_back(static_cast<char>(character));
            break;
        }
    }
    return result;
}

auto EscapeTomlBasicString(std::string_view value) -> std::string {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    return result;
}

auto FormatSeed(std::uint32_t seed) -> std::string {
    constexpr char HexDigits[] = "0123456789ABCDEF";
    std::string result("0x00000000");
    for (int index = 9; index >= 2; --index) {
        result[static_cast<std::size_t>(index)] = HexDigits[seed & 0xFU];
        seed >>= 4U;
    }
    return result;
}

auto ChoiceLabel(std::size_t index) -> std::string {
    if (index == 0) return "Off";
    const auto& choice = Seeds[index - 1];
    return IsReadableSeedName(choice.name)
        ? choice.name : FormatSeed(choice.value);
}

auto ChoiceIndexForDropdownRow(std::size_t visualRow) noexcept
        -> std::size_t {
    return SeedExpandDirection == ExpandDirection::Above
        ? Seeds.size() - visualRow : visualRow;
}

auto DropdownRowForChoice(std::size_t choiceIndex) noexcept
        -> std::size_t {
    return SeedExpandDirection == ExpandDirection::Above
        ? Seeds.size() - choiceIndex : choiceIndex;
}

auto ChoiceTooltip(std::size_t index) -> std::string {
    if (index == 0) return {};
    return "Seed: " + std::to_string(Seeds[index - 1].value);
}

auto CheckWitness(const D2RL::PluginContext* context,
        std::uintptr_t rva, const auto& bytes) noexcept -> bool {
    return context != nullptr && context->CheckExpectedBytes(
        rva, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

auto InstallEarlierSeedPathHooks(
        const D2RL::PluginContext* context) noexcept -> bool {
    const auto matchA = CheckWitness(context, SavedSeedCopyAWitnessRva,
        SavedSeedCopyAWitness);
    const auto matchB = CheckWitness(context, SavedSeedCopyBWitnessRva,
        SavedSeedCopyBWitness);
    const auto matchC = CheckWitness(context, SavedSeedCopyCWitnessRva,
        SavedSeedCopyCWitness);

    D2RL::LogInfoF(context,
        "MapSeedSelector: saved-seed copy fingerprint A at D2R+0x4210BA: %s",
        matchA ? "match" : "mismatch");
    D2RL::LogInfoF(context,
        "MapSeedSelector: saved-seed copy fingerprint B at D2R+0x532432: %s",
        matchB ? "match" : "mismatch");
    D2RL::LogInfoF(context,
        "MapSeedSelector: saved-seed copy fingerprint C at D2R+0x532B63: %s",
        matchC ? "match" : "mismatch");
    if (!matchA || !matchB || !matchC) {
        context->LogWarn(
            "MapSeedSelector: an earlier saved-seed copy fingerprint did not match; the seed picker is disabled without installing those hooks.");
        return false;
    }

    const auto* const copyAInstructions = SavedSeedCopyAWitness.data()
        + SavedSeedCopyStoreOffset;
    const auto copyAInstalled = context->InstallInlineHook(
        SavedSeedCopyAHookRva, copyAInstructions,
        static_cast<std::uint32_t>(SavedSeedCopyStoreSize),
        reinterpret_cast<void*>(MapSavedSeedCopyAHook),
        &gOriginalSavedSeedCopyA)
        && gOriginalSavedSeedCopyA != nullptr;
    if (!copyAInstalled) {
        context->LogWarn(
            "MapSeedSelector: saved-seed copy hook A could not be installed; seed selection is disabled.");
        return false;
    }

    const auto* const copyBInstructions = SavedSeedCopyBWitness.data()
        + SavedSeedCopyStoreOffset;
    const auto copyBInstalled = context->InstallInlineHook(
        SavedSeedCopyBHookRva, copyBInstructions,
        static_cast<std::uint32_t>(SavedSeedCopyStoreSize),
        reinterpret_cast<void*>(MapSavedSeedCopyBHook),
        &gOriginalSavedSeedCopyB)
        && gOriginalSavedSeedCopyB != nullptr;
    if (!copyBInstalled) {
        context->LogWarn(
            "MapSeedSelector: saved-seed copy hook B could not be installed; seed selection is disabled.");
        return false;
    }

    const auto* const copyCInstructions = SavedSeedCopyCWitness.data()
        + SavedSeedCopyStoreOffset;
    const auto copyCInstalled = context->InstallInlineHook(
        SavedSeedCopyCHookRva, copyCInstructions,
        static_cast<std::uint32_t>(SavedSeedCopyStoreSize),
        reinterpret_cast<void*>(MapSavedSeedCopyCHook),
        &gOriginalSavedSeedCopyC)
        && gOriginalSavedSeedCopyC != nullptr;
    if (!copyCInstalled) {
        context->LogWarn(
            "MapSeedSelector: saved-seed copy hook C could not be installed; seed selection is disabled.");
        return false;
    }

    context->LogInfo(
        "MapSeedSelector: all three guarded saved-seed copy hooks were installed; they preserve matching character seeds and inject the selected seed when it differs.");
    return true;
}

auto InstallGameSeedIngressHook(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!CheckWitness(context, GameSeedIngressRva,
                GameSeedIngressWitness)) {
        context->LogWarn(
            "MapSeedSelector: Game+0x12C seed-consumer fingerprint did not match; seed selection is disabled without modifying that path.");
        return false;
    }
    const auto installed = context->InstallInlineHook(
        GameSeedIngressRva, GameSeedIngressWitness.data(),
        static_cast<std::uint32_t>(GameSeedIngressWitness.size()),
        reinterpret_cast<void*>(MapGameSeedIngressHook),
        &gOriginalGameSeedIngress)
        && gOriginalGameSeedIngress != nullptr;
    if (!installed) {
        context->LogWarn(
            "MapSeedSelector: Game+0x12C seed-consumer hook could not be installed.");
        return false;
    }
    context->LogInfo(
        "MapSeedSelector: guarded Game+0x12C seed-consumer hook installed at D2R+0x48AADB; it avoids rewriting an already-matching saved seed.");
    return true;
}

auto SeedHookTargetsPicker(std::uintptr_t gameBase) noexcept -> bool {
    using namespace RuffnecKk::LocalProviderCompat;
    const Memory memory{Read};
    const auto picker = reinterpret_cast<std::uintptr_t>(MapDrlgSeedInitializationHook);
    std::uintptr_t target{};
    return Relative(memory, gameBase + DrlgSeedInitializationRva, 0xE9, target)
        && (target == picker || Absolute(memory, target, picker));
}

auto InstallSeedHook(const D2RL::PluginContext* context) noexcept -> bool {
    if (!CheckWitness(context, DrlgSeedInitializationWitnessRva,
                DrlgSeedInitializationWitness)) {
        return false;
    }
    if (!InstallEarlierSeedPathHooks(context)) return false;
    if (!InstallGameSeedIngressHook(context)) return false;

    const auto* const seedInstructions = DrlgSeedInitializationWitness.data()
        + DrlgSeedInitializationHookOffset;
    const auto primaryInstalled = context->InstallInlineHook(DrlgSeedInitializationRva,
                seedInstructions,
                static_cast<std::uint32_t>(
                    DrlgSeedInitializationOverwriteSize),
                reinterpret_cast<void*>(MapDrlgSeedInitializationHook),
                &gOriginalDrlgSeedInitialization)
            && gOriginalDrlgSeedInitialization != nullptr;
    if (!primaryInstalled || !RuffnecKk::LocalProviderCompat::Read(
            context->exeBase + DrlgSeedInitializationWitnessRva,
            InstalledSeedWitness.data(), InstalledSeedWitness.size())) {
        return false;
    }
    constexpr auto hookEnd =
        DrlgSeedInitializationHookOffset + DrlgSeedInitializationOverwriteSize;
    return SeedHookTargetsPicker(context->exeBase)
        && std::equal(InstalledSeedWitness.begin(),
            InstalledSeedWitness.begin() + DrlgSeedInitializationHookOffset,
            DrlgSeedInitializationWitness.begin())
        && std::equal(InstalledSeedWitness.begin() + hookEnd,
            InstalledSeedWitness.end(), DrlgSeedInitializationWitness.begin() + hookEnd);
}

auto FindRootSelectedSeedLine(std::string& config,
        std::size_t& lineStart, std::size_t& lineEnd) noexcept -> bool {
    std::size_t cursor{};
    while (cursor < config.size()) {
        const auto end = config.find('\n', cursor);
        const auto actualEnd = end == std::string::npos ? config.size() : end;
        const auto raw = std::string_view(config).substr(cursor, actualEnd - cursor);
        const auto line = Trim(RemoveTomlComment(raw));
        if (!line.empty() && line.front() == '[') return false;
        if (!line.empty()) {
            const auto equals = line.find('=');
            if (equals != std::string_view::npos
                    && Trim(line.substr(0, equals)) == SelectedSeedKey) {
                lineStart = cursor;
                lineEnd = actualEnd;
                return true;
            }
        }
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    return false;
}

auto SetSelectedSeedLine(std::string& config,
        std::string_view selectedName) -> bool {
    std::string replacement = "selected_seed = \"";
    for (const char character : selectedName) {
        if (character == '\\' || character == '"') replacement.push_back('\\');
        replacement.push_back(character);
    }
    replacement.push_back('"');

    std::size_t lineStart{};
    std::size_t lineEnd{};
    if (FindRootSelectedSeedLine(config, lineStart, lineEnd)) {
        config.replace(lineStart, lineEnd - lineStart, replacement);
    } else {
        const auto firstTable = config.find("\n[");
        if (firstTable == std::string::npos) {
            if (!config.empty() && config.back() != '\n') config.push_back('\n');
            config += replacement;
            config.push_back('\n');
        } else {
            config.insert(firstTable + 1, replacement + "\n");
        }
    }
    return true;
}

auto PersistSelection(std::string_view selectedName) noexcept -> bool {
    if (Context == nullptr) return false;
    try {
        std::string config;
        if (!ReadConfig(config)
                || !SetSelectedSeedLine(config, selectedName)) {
            return false;
        }
        return WriteConfig(config);
    } catch (...) {
        return false;
    }
}

auto SelectedSeedName() noexcept -> std::string_view {
    return SelectedChoice == 0 ? std::string_view(OffSelectionValue)
        : std::string_view(Seeds[SelectedChoice - 1].selectionKey);
}

std::atomic_bool PickerEnabled{true};

void ApplySelectedChoice() noexcept {
    // Re-evaluate whether this entry already has the selected seed whenever
    // the player changes the choice or enters/leaves Offline character select.
    InterlockedExchange(&gMapSeedFlowState, 0);
    // Off leaves the hooks installed but sends every seed copy through the
    // game's original trampoline, so map creation behaves as if this plugin
    // were absent while the character-selection panel remains visible.
    if (!PickerEnabled.load(std::memory_order_acquire) || SelectedChoice == 0
            || !OfflineFlowActive.load(std::memory_order_acquire)) {
        InterlockedExchange(&gMapSeedOverrideActive, 0);
        return;
    }
    InterlockedExchange(&gSelectedMapSeed,
        static_cast<long>(Seeds[SelectedChoice - 1].value));
    InterlockedExchange(&gMapSeedOverrideActive, 1);
}

auto FindWidget(const char* name,
        D2RL::Widgets::WidgetHandle& handle) noexcept -> bool {
    if (Context == nullptr || WidgetService == nullptr
            || WidgetService->findPanel == nullptr
            || WidgetService->findWidget == nullptr) {
        return false;
    }
    D2RL::Widgets::WidgetHandle panel{};
    return WidgetService->findPanel(Context, QualifiedPanelName.c_str(), &panel)
            == D2RL::Widgets::Result::Success
        && WidgetService->findWidget(Context, panel, name, &handle)
            == D2RL::Widgets::Result::Success;
}

auto PanelHeight() noexcept -> std::int32_t {
    if (!NativeDropdownAvailable) {
        if (SeedLabelPosition == LabelPosition::Above
                || SeedLabelPosition == LabelPosition::Below) {
            return SelectorTitleHeight + DropdownHeight + MenuGap;
        }
        return (std::max)(SelectorTitleHeight, DropdownHeight);
    }
    const auto optionCount = static_cast<std::int32_t>(Seeds.size() + 1);
    // Center the collapsed control and leave enough room below it for the
    // native dropdown's expanded rows.
    return 2 * (DropdownHeight / 2 + MenuGap
        + optionCount * OptionRowHeight);
}

auto DropdownTop() noexcept -> std::int32_t {
    if (!NativeDropdownAvailable) {
        switch (SeedLabelPosition) {
        case LabelPosition::Above:
            return SelectorTitleHeight + MenuGap;
        case LabelPosition::Below:
            return 0;
        case LabelPosition::Left:
        case LabelPosition::Right:
            return (PanelHeight() - DropdownHeight) / 2;
        }
    }
    return PanelHeight() / 2 - DropdownHeight / 2;
}

auto OptionsPanelHeight() noexcept -> std::int32_t {
    return PopupCanvasHeight;
}

auto ChoiceLabelWidgetName(std::size_t index) -> std::string {
    return std::string(ChoiceLabelWidgetPrefix) + std::to_string(index);
}

auto BuildFallbackSelectorChildren(bool includeOptions) -> std::string {
    std::string result;
    result.reserve((Seeds.size() + 1) * 360 + 1024);
    if (!includeOptions) {
        result += R"json({
      "type": "TextBoxWidget",
      "name": "SeedPickerTitle",
      "fields": {
        "rect": { "x": )json";
        result += std::to_string(LabelLeft());
        result += R"json(, "y": )json";
        result += std::to_string(LabelTop(DropdownTop()));
        result += R"json(, "width": )json";
        result += std::to_string(LabelWidthForPosition());
        result += R"json(, "height": )json";
        result += std::to_string(SelectorTitleHeight);
        result += R"json( },
        "text": "Map Seed",
        "style": {
          "fontColor": "$FontColorGold",
          "pointSize": "$MediumFontSize",
          "alignment": { "h": "center", "v": "center" }
        }
      }
    })json";

        result += R"json(,
    {
      "type": "ButtonWidget",
      "name": "SeedDropdown",
      "fields": {
        "rect": { "x": )json";
        result += std::to_string(DropdownLeft());
        result += R"json(, "y": )json";
        result += std::to_string(DropdownTop());
        result += R"json(, "width": )json";
        result += std::to_string(PickerWidth);
        result += R"json(, "height": )json";
        result += std::to_string(DropdownHeight);
        result += R"json( },
        "filename": "d2rloader\\ruffneckk-mapsense\\dropdown",
        "normalFrame": 0,
        "pressedFrame": 1,
        "disabledFrame": 2,
        "hoveredFrame": 3,
        "sound": "cursor_button_click_hd",
        "onClickMessage": "PanelManager:ClosePanel:)json";
        result += DropdownToggleMessage;
        result += R"json("
      }
    })json";

        for (std::size_t index = 0; index <= Seeds.size(); ++index) {
            result += R"json(,
    {
      "type": "TextBoxWidget",
      "name": ")json";
            result += ChoiceLabelWidgetPrefix;
            result += std::to_string(index);
            result += R"json(",
      "fields": {
        "rect": { "x": )json";
            result += std::to_string(DropdownLeft() + 14);
            result += R"json(, "y": )json";
            result += std::to_string(DropdownTop());
            result += R"json(, "width": )json";
            result += std::to_string(PickerWidth - 76);
            result += R"json(, "height": )json";
            result += std::to_string(DropdownHeight);
            result += R"json( },
        "text": ")json";
            result += EscapeJson(ChoiceLabel(index));
            result += R"json(",
        "style": {
          "fontColor": "$FontColorLightGold",
          "pointSize": "$MediumFontSize",
          "dropShadow": "$DefaultDropShadow",
          "spacing": "$ReducedSpacing",
          "alignment": { "h": "center", "v": "center" }
        }
      }
    })json";
        }
    } else {
        // A transparent screen-sized hit target closes the popup on outside
        // clicks. The option controls below it win hit-testing over this row.
        result += R"json({
      "type": "ButtonWidget",
      "name": "SeedDropdownOutsideClick",
      "fields": {
        "rect": { "x": 0, "y": 0, "width": )json";
        result += std::to_string(PopupCanvasWidth);
        result += R"json(, "height": )json";
        result += std::to_string(PopupCanvasHeight);
        result += R"json( },
        "filename": "d2rloader\\ruffneckk-mapsense\\blank",
        "normalFrame": 0,
        "hoveredFrame": 0,
        "pressedFrame": 0,
        "disabledFrame": 0,
        "sound": "cursor_menu_select_hd",
        "onClickMessage": "PanelManager:ClosePanel:)json";
        result += DropdownToggleMessage;
        result += R"json("
      }
    })json";
        const auto optionCount = static_cast<std::int32_t>(Seeds.size() + 1);
        const auto listOffset = SeedExpandDirection == ExpandDirection::Above
            ? -optionCount * OptionRowHeight : DropdownHeight;
        for (std::size_t visualRow = 0; visualRow <= Seeds.size();
                ++visualRow) {
            const auto index = ChoiceIndexForDropdownRow(visualRow);
            std::int32_t panelX{};
            std::int32_t panelY{};
            GetPanelPosition(panelX, panelY);
            const auto rowY = panelY + DropdownTop() + listOffset
                + static_cast<std::int32_t>(visualRow) * OptionRowHeight;
            const auto rowX = panelX + DropdownLeft();
            result += R"json(,
    {
      "type": "ButtonWidget",
      "name": "SeedOption)json";
            result += std::to_string(index);
            result += R"json(",
      "fields": {
        "rect": { "x": )json";
            result += std::to_string(rowX);
            result += R"json(, "y": )json";
            result += std::to_string(rowY);
            result += R"json(, "width": )json";
            result += std::to_string(PickerWidth);
            result += R"json(, "height": )json";
            result += std::to_string(OptionRowHeight);
            result += R"json( },
        "filename": "d2rloader\\ruffneckk-mapsense\\optiontile",
        "normalFrame": 0,
        "pressedFrame": 1,
        "disabledFrame": 0,
        "hoveredFrame": 1,
        "sound": "cursor_menu_select_hd",
        "tooltipString": ")json";
            result += EscapeJson(ChoiceTooltip(index));
            result += R"json(",
        "tooltipStyle": "$StyHlpTt",
        "onClickMessage": "PanelManager:ClosePanel:)json";
            result += ChoiceMessagePrefix;
            result += std::to_string(index);
            result += R"json("
      }
    })json";

            result += R"json(,
    {
      "type": "TextBoxWidget",
      "name": "SeedOptionLabel)json";
            result += std::to_string(index);
            result += R"json(",
      "fields": {
        "rect": { "x": )json";
            result += std::to_string(rowX + 12);
            result += R"json(, "y": )json";
            result += std::to_string(rowY);
            result += R"json(, "width": )json";
            result += std::to_string(PickerWidth - 24);
            result += R"json(, "height": )json";
            result += std::to_string(OptionRowHeight);
            result += R"json( },
        "text": ")json";
            result += EscapeJson(ChoiceLabel(index));
            result += R"json(",
        "style": {
          "fontColor": ")json";
            result += "$FontColorWhite";
            result += R"json(",
          "pointSize": "$MediumFontSize",
          "dropShadow": "$DefaultDropShadow",
          "spacing": "$ReducedSpacing",
          "alignment": { "h": "center", "v": "center" }
        }
      }
    })json";
        }
    }
    return result;
}

} // namespace

auto Initialize(const D2RL::PluginContext* context,
        std::string_view defaultConfig) noexcept -> bool {
    Context = context;
    Seeds.clear();
    PublishedSeedCount.store(0, std::memory_order_release);
    SelectedChoice = 0;
    PanelX = 856;
    PanelY = 0;
    AddSeedButtonX = 800;
    AddSeedButtonY = 0;
    SeedLabelPosition = LabelPosition::Above;
    SeedExpandDirection = ExpandDirection::Below;
    SeedHookAvailable.store(false, std::memory_order_release);
    OfflineFlowActive.store(false, std::memory_order_release);
    NativeDropdownAvailable = false;
    InterlockedExchange(&gMapSeedOverrideActive, 0);
    InterlockedExchange(&gSelectedMapSeed, 0);
    InterlockedExchange(&gMapSeedFlowState, 0);
    InterlockedExchange(&gLastObservedMapSeed, 0);
    InterlockedExchange(&gLastObservedMapSeedValid, 0);
    if (context == nullptr) return false;

    try {
        if (context->pluginConfigPath == nullptr || *context->pluginConfigPath == L'\0') {
            return false;
        }
        ConfigPath = std::filesystem::path(context->pluginConfigPath).parent_path()
            / L"map-seed-selector.toml";
        if (!std::filesystem::exists(ConfigPath) && !WriteConfig(defaultConfig)) {
            return false;
        }
        if (!ReadConfig(ConfigText)) {
            context->LogWarn("MapSeedSelector: map-seed settings could not be read; the seed picker is disabled.");
            return true;
        }
        std::string selectedName;
        if (!ParseConfiguration(ConfigText, Seeds, selectedName,
                PanelX, PanelY, AddSeedButtonX, AddSeedButtonY,
                SeedLabelPosition,
                SeedExpandDirection)) {
            Seeds.clear();
            PublishedSeedCount.store(0, std::memory_order_release);
            context->LogWarn("MapSeedSelector: invalid seed or UI configuration; the seed picker is disabled.");
            return true;
        }
        PublishedSeedCount.store(Seeds.size(), std::memory_order_release);
        if (selectedName != OffSelectionValue) {
            const auto selected = std::find_if(Seeds.begin(), Seeds.end(),
                [&selectedName](const SeedChoice& choice) {
                    return choice.selectionKey == selectedName;
                });
            if (selected != Seeds.end()) {
                SelectedChoice = static_cast<std::size_t>(selected - Seeds.begin()) + 1;
            } else {
                context->LogWarn("MapSeedSelector: selected_seed no longer matches a configured seed; using Off and the character's saved map.");
            }
        }
        Seeds.reserve(MaximumSeeds);
        if (!InstallSeedHook(context)) {
            context->LogWarn("MapSeedSelector: a required saved-seed or DRLG fingerprint/hook could not be qualified; the map-seed picker is disabled.");
            return true;
        }
        SeedHookAvailable.store(true, std::memory_order_release);
        ApplySelectedChoice();
        context->LogInfo("MapSeedSelector: map-seed selection is available; the DRLG seed initializer matched its runtime fingerprint.");
        return true;
    } catch (...) {
        Seeds.clear();
        PublishedSeedCount.store(0, std::memory_order_release);
        context->LogWarn("MapSeedSelector: map-seed configuration could not be parsed; the seed picker is disabled.");
        return true;
    }
}

auto OwnsSeedInitializer(std::uintptr_t gameBase) noexcept -> bool {
    return SeedHookAvailable.load(std::memory_order_acquire)
        && SeedHookTargetsPicker(gameBase)
        && RuffnecKk::LocalProviderCompat::Equal(
            {RuffnecKk::LocalProviderCompat::Read},
            gameBase + DrlgSeedInitializationWitnessRva, InstalledSeedWitness);
}

auto IsAvailable() noexcept -> bool {
    return SeedHookAvailable.load(std::memory_order_acquire);
}

void SetWidgetService(const MapSeedSelector::ExtendedWidgetService* service,
        bool nativeDropdownAvailable) noexcept {
    WidgetService = service;
    NativeDropdownAvailable = nativeDropdownAvailable;
}

void SetPanelGeneration(std::uint32_t generation) noexcept {
    try {
        const auto slot = std::to_string(generation);
        QualifiedPanelName = "ruffneckk-mapsense/MapSeeds" + slot;
        QualifiedOptionsPanelName = "ruffneckk-mapsense/SeedOptions" + slot;
    } catch (...) {
    }
}

void SetOfflineFlowActive(bool active) noexcept {
    OfflineFlowActive.store(active, std::memory_order_release);
    ApplySelectedChoice();
}

void SetPickerPreference(bool enabled) noexcept {
    if (PickerEnabled.exchange(enabled, std::memory_order_acq_rel) != enabled) {
        ApplySelectedChoice();
    }
}

auto IsPickerEnabled() noexcept -> bool {
    return PickerEnabled.load(std::memory_order_acquire);
}

void GetPanelPosition(std::int32_t& x, std::int32_t& y) noexcept {
    x = PanelX;
    y = PanelY;
}

void GetAddSeedButtonPosition(std::int32_t& x, std::int32_t& y) noexcept {
    x = AddSeedButtonX;
    y = AddSeedButtonY;
}

auto GetPanelWidth() noexcept -> std::int32_t {
    return PanelWidth();
}

auto GetPanelHeight() noexcept -> std::int32_t {
    return PanelHeight();
}

auto GetOptionsPanelHeight() noexcept -> std::int32_t {
    return OptionsPanelHeight();
}

auto BuildLayoutChildren() -> std::string {
    if (!IsAvailable()) return {};
    if (Seeds.empty()) return {};
    if (!NativeDropdownAvailable) {
        return BuildFallbackSelectorChildren(false);
    }
    std::string result;
    const auto optionCount = Seeds.size() + 1;
    const auto dropdownTop = DropdownTop();
    result.reserve(optionCount * 96 + 256);
    result += R"json({
      "type": "TextBoxWidget",
      "name": "SeedPickerTitle",
      "fields": {
        "rect": { "x": )json";
    result += std::to_string(LabelLeft());
    result += R"json(, "y": )json";
    result += std::to_string(LabelTop(dropdownTop));
    result += R"json(, "width": )json";
    result += std::to_string(LabelWidthForPosition());
    result += R"json(, "height": )json";
    result += std::to_string(SelectorTitleHeight);
    result += R"json( },
        "text": "Map Seed",
        "style": {
          "fontColor": "$FontColorGold",
          "pointSize": "$MediumFontSize",
          "alignment": { "h": "center", "v": "center" }
        }
      }
    })json";
    result += R"json(,
    {
      "type": "DropdownListWidget",
      "name": "SeedDropdown",
      "fields": {
        "rect": { "x": )json";
    result += std::to_string(DropdownLeft());
    result += R"json(, "y": )json";
    result += std::to_string(dropdownTop);
    result += R"json(, "width": )json";
    result += std::to_string(PickerWidth);
    result += R"json(, "height": )json";
    result += std::to_string(DropdownHeight);
    result += R"json( },
        "states": [
)json";
    for (std::size_t visualRow = 0; visualRow < optionCount; ++visualRow) {
        if (visualRow != 0) result += ",\n";
        const auto index = ChoiceIndexForDropdownRow(visualRow);
        result += "          \"";
        result += EscapeJson(ChoiceLabel(index));
        result += "\"";
    }
    result += R"json(
        ]
      }
    })json";
    return result;
}

auto BuildOptionsLayoutChildren() -> std::string {
    if (!IsAvailable() || NativeDropdownAvailable) return {};
    return BuildFallbackSelectorChildren(true);
}

void SelectChoice(std::size_t choice) noexcept {
    if (!IsAvailable() || Context == nullptr || choice > Seeds.size()) return;
    if (SelectedChoice != choice) {
        SelectedChoice = choice;
        ApplySelectedChoice();
        if (!PersistSelection(SelectedSeedName())) {
            Context->LogWarn("MapSeedSelector: selected map seed is active for this session, but selected_seed could not be saved to TOML.");
        }
        if (SelectedChoice == 0) {
            Context->LogInfo("MapSeedSelector: Off selected; seed hooks are passing through to the game's original map-seed logic.");
        } else {
            D2RL::LogInfoF(Context,
                "MapSeedSelector: map seed selected name=%s seed=%s",
                Seeds[SelectedChoice - 1].name.c_str(),
                FormatSeed(Seeds[SelectedChoice - 1].value).c_str());
        }
    }
    if (!NativeDropdownAvailable) RefreshUi();
}

auto GetLastObservedMapSeed(std::uint32_t& seed) noexcept -> bool {
    if (InterlockedCompareExchange(&gLastObservedMapSeedValid, 0, 0) == 0) {
        return false;
    }
    seed = static_cast<std::uint32_t>(
        InterlockedCompareExchange(&gLastObservedMapSeed, 0, 0));
    return true;
}

auto AddSeedEntry(std::string_view name, std::string_view seedText,
        std::string& message) noexcept -> bool {
    message.clear();
    if (Context == nullptr || !IsAvailable()) {
        message = "The seed selector is unavailable.";
        return false;
    }
    try {
        name = Trim(name);
        seedText = Trim(seedText);
        if (name.empty()) {
            message = "Enter a name for this seed.";
            return false;
        }
        if (name.size() > MaximumSeedNameLength) {
            message = "The name is too long (maximum 48 characters).";
            return false;
        }
        if (name == OffSelectionValue) {
            message = "That name is reserved for Off.";
            return false;
        }
        if (std::any_of(name.begin(), name.end(), [](char character) {
                const auto value = static_cast<unsigned char>(character);
                return value < 0x20 || value == 0x7F;
            })) {
            message = "The name contains an unsupported control character.";
            return false;
        }
        std::uint32_t seedValue{};
        if (!ParseSeedValue(seedText, seedValue)) {
            message = "Enter an unsigned 32-bit seed in decimal or 0x hexadecimal.";
            return false;
        }
        if (Seeds.size() >= MaximumSeeds) {
            message = "The seed list is full (maximum 16 entries).";
            return false;
        }

        SeedChoice pending{
            .name = std::string(name),
            .selectionKey = {},
            .value = seedValue,
        };
        pending.selectionKey = MakeSeedSelectionKey(pending);
        const auto duplicate = std::find_if(Seeds.begin(), Seeds.end(),
            [&pending](const SeedChoice& existing) {
                return existing.selectionKey == pending.selectionKey;
            });
        if (duplicate != Seeds.end()) {
            message = "A seed with that name is already in the list.";
            return false;
        }

        std::string config;
        if (!ReadConfig(config)) {
            message = "The plugin TOML could not be read.";
            return false;
        }
        if (!config.empty() && config.back() != '\n') config.push_back('\n');
        config += "\n[[seeds]]\nname = \"";
        config += EscapeTomlBasicString(name);
        config += "\"\nseed = ";
        config += std::to_string(seedValue);
        config.push_back('\n');
        if (!SetSelectedSeedLine(config, pending.selectionKey)
                || config.size() >= MaximumConfigBytes) {
            message = "The plugin TOML is too large to update safely.";
            return false;
        }
        if (!WriteConfig(config)) {
            message = "The new seed could not be saved to the plugin TOML.";
            return false;
        }

        Seeds.push_back(std::move(pending));
        PublishedSeedCount.store(Seeds.size(), std::memory_order_release);
        ConfigText = std::move(config);
        SelectedChoice = Seeds.size();
        ApplySelectedChoice();
        D2RL::LogInfoF(Context,
            "MapSeedSelector: newly added seed selected name=%s seed=%s",
            Seeds.back().name.c_str(), FormatSeed(Seeds.back().value).c_str());
        message = "Seed added and selected.";
        return true;
    } catch (...) {
        message = "The new seed could not be added.";
        return false;
    }
}

auto DeleteSeedEntry(std::size_t index,
        std::string& message) noexcept -> bool {
    message.clear();
    if (Context == nullptr || !IsAvailable()) {
        message = "The seed selector is unavailable.";
        return false;
    }
    if (index >= Seeds.size()) {
        message = "The selected seed no longer exists.";
        return false;
    }
    try {
        std::string config;
        if (!ReadConfig(config)) {
            message = "The plugin TOML could not be read.";
            return false;
        }

        std::size_t cursor{};
        std::size_t seedTableIndex{};
        std::size_t removeStart = std::string::npos;
        std::size_t removeEnd = config.size();
        while (cursor < config.size()) {
            const auto newline = config.find('\n', cursor);
            const auto lineEnd = newline == std::string::npos
                ? config.size() : newline;
            const auto line = Trim(RemoveTomlComment(
                std::string_view(config).substr(cursor, lineEnd - cursor)));
            if (!line.empty() && line.front() == '[') {
                if (removeStart != std::string::npos) {
                    removeEnd = cursor;
                    break;
                }
                if (line == "[[seeds]]") {
                    if (seedTableIndex == index) removeStart = cursor;
                    ++seedTableIndex;
                }
            }
            if (newline == std::string::npos) break;
            cursor = newline + 1;
        }
        if (removeStart == std::string::npos || removeEnd < removeStart) {
            message = "The saved seed block could not be located in the plugin TOML.";
            return false;
        }

        config.erase(removeStart, removeEnd - removeStart);
        const auto removedChoice = index + 1;
        std::size_t nextChoice = SelectedChoice;
        std::string_view nextSelection{OffSelectionValue};
        if (SelectedChoice != removedChoice) {
            if (SelectedChoice > removedChoice) --nextChoice;
            // Resolve the name before erasing: when an earlier row is deleted,
            // the selected choice's array index shifts but its selection key
            // remains the same.
            if (SelectedChoice > 0 && SelectedChoice <= Seeds.size()) {
                nextSelection = Seeds[SelectedChoice - 1].selectionKey;
            } else {
                nextChoice = 0;
            }
        } else {
            nextChoice = 0;
        }
        if (!SetSelectedSeedLine(config, nextSelection)) {
            message = "The selected seed setting could not be updated.";
            return false;
        }
        if (!WriteConfig(config)) {
            message = "The saved seed could not be removed from the plugin TOML.";
            return false;
        }

        const auto deletedName = Seeds[index].name;
        Seeds.erase(Seeds.begin() + static_cast<std::ptrdiff_t>(index));
        SelectedChoice = nextChoice;
        PublishedSeedCount.store(Seeds.size(), std::memory_order_release);
        ConfigText = std::move(config);
        ApplySelectedChoice();
        D2RL::LogInfoF(Context,
            "MapSeedSelector: deleted saved seed name=%s",
            deletedName.c_str());
        message = "Saved seed deleted.";
        return true;
    } catch (...) {
        message = "The saved seed could not be deleted.";
        return false;
    }
}

auto GetSeedEntryCount() noexcept -> std::size_t {
    return PublishedSeedCount.load(std::memory_order_acquire);
}

auto GetSeedEntryLabel(std::size_t index, std::string& label) noexcept -> bool {
    label.clear();
    if (index >= Seeds.size()) return false;
    try {
        label = ChoiceLabel(index + 1);
        return true;
    } catch (...) {
        label.clear();
        return false;
    }
}

auto IsDropdownExpandedAbove() noexcept -> bool {
    return SeedExpandDirection == ExpandDirection::Above;
}

auto GetDropdownRowHeight() noexcept -> std::int32_t {
    return OptionRowHeight;
}

auto GetDropdownControlHeight() noexcept -> std::int32_t {
    return DropdownHeight;
}

void HandleDropdownChanged() noexcept {
    if (!IsAvailable() || Context == nullptr || WidgetService == nullptr
            || WidgetService->getDropdownSelection == nullptr) {
        return;
    }
    D2RL::Widgets::WidgetHandle handle{};
    if (!FindWidget(DropdownWidgetName, handle)) {
        Context->LogWarn("MapSeedSelector: native seed dropdown could not be found after its change event.");
        return;
    }
    std::uint32_t choice{};
    const auto result = WidgetService->getDropdownSelection(Context, handle,
        &choice);
    if (result != D2RL::Widgets::Result::Success || choice > Seeds.size()) {
        D2RL::LogWarnF(Context,
            "MapSeedSelector: native dropdown selection could not be read (result=%u, index=%u).",
            static_cast<unsigned>(result), choice);
        return;
    }
    const auto selectedChoice = ChoiceIndexForDropdownRow(
        static_cast<std::size_t>(choice));
    if (SelectedChoice == selectedChoice) return;

    SelectedChoice = selectedChoice;
    ApplySelectedChoice();
    if (!PersistSelection(SelectedSeedName())) {
        Context->LogWarn("MapSeedSelector: selected map seed is active for this session, but selected_seed could not be saved to TOML.");
    }
    if (SelectedChoice == 0) {
        Context->LogInfo("MapSeedSelector: Off selected; seed hooks are passing through to the game's original map-seed logic.");
    } else {
        D2RL::LogInfoF(Context,
            "MapSeedSelector: map seed selected name=%s seed=%s",
            Seeds[SelectedChoice - 1].name.c_str(),
            FormatSeed(Seeds[SelectedChoice - 1].value).c_str());
    }
}

void RefreshUi() noexcept {
    if (!IsAvailable() || Seeds.empty() || Context == nullptr
            || WidgetService == nullptr) {
        return;
    }
    if (!NativeDropdownAvailable) {
        bool failed{};
        for (std::size_t index = 0; index <= Seeds.size(); ++index) {
            D2RL::Widgets::WidgetHandle handle{};
            const auto name = ChoiceLabelWidgetName(index);
            if (!FindWidget(name.c_str(), handle)
                    || WidgetService->setWidgetVisible == nullptr
                    || WidgetService->setWidgetVisible(Context, handle,
                        index == SelectedChoice)
                        != D2RL::Widgets::Result::Success) {
                failed = true;
            }
        }
        if (failed) {
            Context->LogWarn("MapSeedSelector: the D2R-styled seed selector opened, but its selected label could not be synchronized.");
        }
        return;
    }
    if (WidgetService->setDropdownSelection == nullptr
            || WidgetService->setDropdownUpdateMessage == nullptr) return;
    D2RL::Widgets::WidgetHandle handle{};
    if (!FindWidget(DropdownWidgetName, handle)) {
        Context->LogWarn("MapSeedSelector: native seed dropdown could not be found when the panel opened.");
        return;
    }
    const auto selectionResult = WidgetService->setDropdownSelection(Context,
        handle, static_cast<std::uint32_t>(
            DropdownRowForChoice(SelectedChoice)));
    const auto messageResult = WidgetService->setDropdownUpdateMessage(Context,
        handle, DropdownMessageTarget, DropdownMessageCommand);
    if (selectionResult != D2RL::Widgets::Result::Success
            || messageResult != D2RL::Widgets::Result::Success) {
        D2RL::LogWarnF(Context,
            "MapSeedSelector: native dropdown setup failed (selection=%u, message=%u).",
            static_cast<unsigned>(selectionResult),
            static_cast<unsigned>(messageResult));
    }
}

void Shutdown() noexcept {
    SeedHookAvailable.store(false, std::memory_order_release);
    OfflineFlowActive.store(false, std::memory_order_release);
    InterlockedExchange(&gMapSeedOverrideActive, 0);
    InterlockedExchange(&gSelectedMapSeed, 0);
    Context = nullptr;
    WidgetService = nullptr;
    NativeDropdownAvailable = false;
    Seeds.clear();
    ConfigText.clear();
    ConfigPath.clear();
    SelectedChoice = 0;
    PanelX = 856;
    PanelY = 0;
    AddSeedButtonX = 800;
    AddSeedButtonY = 0;
    SeedLabelPosition = LabelPosition::Above;
    SeedExpandDirection = ExpandDirection::Below;
    QualifiedPanelName = "ruffneckk-mapsense/MapSeeds0";
    QualifiedOptionsPanelName = "ruffneckk-mapsense/SeedOptions0";
}

} // namespace MapSeedSelector
