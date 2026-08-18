#pragma once

#include <D2RLPlugin/context.h>

#include "localization.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RuffnecKk::BulkSkillPointAllocation {

inline constexpr wchar_t GameplayConfigFileName[] =
    L"BulkSkillPointAllocation.json";
inline constexpr wchar_t StringsConfigFileName[] =
    L"BulkSkillPointAllocation.strings.json";
inline constexpr std::uintmax_t MaximumConfigBytes = 64U * 1024U;

inline constexpr std::uint32_t DefaultSkillPointsPerCtrlClick = 5;
inline constexpr std::uint32_t MaximumSkillPointsPerCtrlClick = 1'000;
inline constexpr std::uint16_t AssignAllSkillPointsExtra = 0xFFFF;
inline constexpr char DefaultShiftConfirmation[] =
    "Invest all currently usable skill points in this skill?";

inline constexpr bool RequiresPeerPlugin = false;

enum class AllocationMode : std::uint8_t {
    Single,
    CtrlBatch,
    ShiftAll,
};

struct Settings {
    bool enabled{};
    std::uint32_t skillPointsPerCtrlClick{
        DefaultSkillPointsPerCtrlClick};
    bool confirmShiftAllocation{};
    bool diagnostics{};
    std::string shiftConfirmationKey;
    std::string shiftConfirmationFallback{DefaultShiftConfirmation};
    std::array<std::string, SupportedLocaleCount> shiftConfirmations{
        DefaultShiftConfirmations()};
};

struct LoadedSettings {
    Settings settings{};
    std::optional<std::filesystem::path> gameplaySource;
    std::optional<std::filesystem::path> stringsSource;
};

constexpr auto ResolveMode(
    bool shiftPressed,
    bool controlPressed
) noexcept -> AllocationMode {
    if (controlPressed) return AllocationMode::CtrlBatch;
    if (shiftPressed) return AllocationMode::ShiftAll;
    return AllocationMode::Single;
}

constexpr auto NativeSkillPacketExtra(
    AllocationMode mode,
    std::uint32_t requestedPoints
) noexcept -> std::uint16_t {
    if (mode == AllocationMode::ShiftAll) {
        return AssignAllSkillPointsExtra;
    }
    if (requestedPoints <= 1) return 0;
    return static_cast<std::uint16_t>((std::min)(
        requestedPoints - 1,
        0xFFFEU));
}

inline auto IsUsableLocalizedString(
    std::string_view localized,
    std::string_view requestedKey,
    std::string_view localizedMissingString
) noexcept -> bool {
    return !localized.empty()
        && localized != requestedKey
        && (localizedMissingString.empty()
            || localized != localizedMissingString);
}

inline auto IsShiftConfirmationAcceptEvent(
    const char* target,
    const char* command
) noexcept -> bool {
    return target != nullptr
        && command != nullptr
        && std::string_view(target) == "CharacterStatsPanelMessage"
        && std::string_view(command) == "UseSkillPoint";
}

namespace Json {

enum class ScalarKind : std::uint8_t {
    Boolean,
    Integer,
    String,
};

struct Scalar {
    ScalarKind kind{};
    bool boolean{};
    std::int64_t integer{};
    std::string string;
};

using Object = std::unordered_map<std::string, Scalar>;

inline auto IsValidUtf8(std::string_view value) noexcept -> bool {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7F) {
            ++index;
            continue;
        }

        std::size_t length{};
        std::uint32_t codePoint{};
        std::uint32_t minimum{};
        if ((first & 0xE0U) == 0xC0U) {
            length = 2;
            codePoint = first & 0x1FU;
            minimum = 0x80;
        } else if ((first & 0xF0U) == 0xE0U) {
            length = 3;
            codePoint = first & 0x0FU;
            minimum = 0x800;
        } else if ((first & 0xF8U) == 0xF0U) {
            length = 4;
            codePoint = first & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (index + length > value.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            if ((byte & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (byte & 0x3FU);
        }
        if (codePoint < minimum
            || codePoint > 0x10FFFF
            || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            return false;
        }
        index += length;
    }
    return true;
}

inline void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(
            0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(
            0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(
            0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view input) noexcept : input_(input) {}

    auto ParseObject() -> Object {
        SkipTrivia();
        Expect('{');
        SkipTrivia();
        Object object;
        if (Consume('}')) {
            RequireEnd();
            return object;
        }

        while (true) {
            SkipTrivia();
            if (Peek() != '"') Fail("object keys must be JSON strings");
            auto key = ParseString();
            SkipTrivia();
            Expect(':');
            SkipTrivia();
            auto value = ParseScalar();
            if (!object.emplace(key, std::move(value)).second) {
                Fail("duplicate key '" + key + "'");
            }
            SkipTrivia();
            if (Consume('}')) break;
            Expect(',');
            SkipTrivia();
            if (Peek() == '}') Fail("trailing commas are not supported");
        }
        RequireEnd();
        return object;
    }

private:
    [[noreturn]] void Fail(const std::string& message) const {
        throw std::invalid_argument(
            message + " at byte " + std::to_string(position_));
    }

    auto Peek() const noexcept -> char {
        return position_ < input_.size() ? input_[position_] : '\0';
    }

    auto Consume(char expected) noexcept -> bool {
        if (Peek() != expected) return false;
        ++position_;
        return true;
    }

    void Expect(char expected) {
        if (!Consume(expected)) {
            Fail(std::string("expected '") + expected + "'");
        }
    }

    void RequireEnd() {
        SkipTrivia();
        if (position_ != input_.size()) {
            Fail("unexpected content after the root object");
        }
    }

    void SkipTrivia() {
        while (position_ < input_.size()) {
            const auto current = input_[position_];
            if (current == ' ' || current == '\t'
                || current == '\r' || current == '\n') {
                ++position_;
                continue;
            }
            if (current != '/' || position_ + 1 >= input_.size()) return;
            const auto next = input_[position_ + 1];
            if (next == '/') {
                position_ += 2;
                while (position_ < input_.size()
                    && input_[position_] != '\r'
                    && input_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            if (next == '*') {
                position_ += 2;
                const auto end = input_.find("*/", position_);
                if (end == std::string_view::npos) {
                    Fail("unterminated block comment");
                }
                position_ = end + 2;
                continue;
            }
            return;
        }
    }

    auto ParseHexQuad() -> std::uint32_t {
        if (position_ + 4 > input_.size()) {
            Fail("incomplete Unicode escape");
        }
        std::uint32_t value{};
        for (std::size_t index = 0; index < 4; ++index) {
            const auto digit = input_[position_++];
            value <<= 4U;
            if (digit >= '0' && digit <= '9') {
                value |= static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                Fail("invalid Unicode escape");
            }
        }
        return value;
    }

    auto ParseString() -> std::string {
        Expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const auto current = static_cast<unsigned char>(input_[position_++]);
            if (current == '"') {
                if (!IsValidUtf8(result)) Fail("string is not valid UTF-8");
                return result;
            }
            if (current < 0x20U) Fail("unescaped control character in string");
            if (current != '\\') {
                result.push_back(static_cast<char>(current));
                continue;
            }
            if (position_ >= input_.size()) Fail("incomplete escape sequence");
            const auto escape = input_[position_++];
            switch (escape) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                auto codePoint = ParseHexQuad();
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                    if (position_ + 2 > input_.size()
                        || input_[position_] != '\\'
                        || input_[position_ + 1] != 'u') {
                        Fail("high surrogate is missing its low surrogate");
                    }
                    position_ += 2;
                    const auto low = ParseHexQuad();
                    if (low < 0xDC00 || low > 0xDFFF) {
                        Fail("invalid low surrogate");
                    }
                    codePoint = 0x10000
                        + ((codePoint - 0xD800) << 10U)
                        + (low - 0xDC00);
                } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                    Fail("unexpected low surrogate");
                }
                AppendUtf8(result, codePoint);
                break;
            }
            default:
                Fail("unsupported escape sequence");
            }
        }
        Fail("unterminated string");
    }

    auto ParseInteger() -> std::int64_t {
        const auto start = position_;
        if (Consume('-') && (Peek() < '0' || Peek() > '9')) {
            Fail("minus sign must be followed by a digit");
        }
        if (Consume('0')) {
            if (Peek() >= '0' && Peek() <= '9') {
                Fail("integers cannot contain leading zeroes");
            }
        } else {
            if (Peek() < '1' || Peek() > '9') Fail("expected an integer");
            while (Peek() >= '0' && Peek() <= '9') ++position_;
        }
        if (Peek() == '.' || Peek() == 'e' || Peek() == 'E') {
            Fail("floating-point numbers are not supported");
        }
        std::int64_t value{};
        const auto* first = input_.data() + start;
        const auto* last = input_.data() + position_;
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last) {
            Fail("integer is outside the supported range");
        }
        return value;
    }

    auto ParseScalar() -> Scalar {
        if (Peek() == '"') {
            Scalar result{.kind = ScalarKind::String};
            result.string = ParseString();
            return result;
        }
        if (input_.substr(position_).starts_with("true")) {
            position_ += 4;
            return {.kind = ScalarKind::Boolean, .boolean = true};
        }
        if (input_.substr(position_).starts_with("false")) {
            position_ += 5;
            return {.kind = ScalarKind::Boolean, .boolean = false};
        }
        if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9')) {
            return {
                .kind = ScalarKind::Integer,
                .integer = ParseInteger(),
            };
        }
        Fail("only strings, booleans, and integers are supported");
    }

    std::string_view input_;
    std::size_t position_{};
};

inline auto ParseObject(std::string_view text) -> Object {
    return Parser(text).ParseObject();
}

inline auto RequireBoolean(
    const Scalar& scalar,
    std::string_view key
) -> bool {
    if (scalar.kind != ScalarKind::Boolean) {
        throw std::invalid_argument(std::string(key) + " must be a boolean");
    }
    return scalar.boolean;
}

inline auto RequireInteger(
    const Scalar& scalar,
    std::string_view key
) -> std::int64_t {
    if (scalar.kind != ScalarKind::Integer) {
        throw std::invalid_argument(std::string(key) + " must be an integer");
    }
    return scalar.integer;
}

inline auto RequireString(
    const Scalar& scalar,
    std::string_view key
) -> const std::string& {
    if (scalar.kind != ScalarKind::String) {
        throw std::invalid_argument(std::string(key) + " must be a string");
    }
    return scalar.string;
}

} // namespace Json

inline void ApplyGameplayConfig(
    const Json::Object& object,
    Settings& settings
) {
    for (const auto& [key, value] : object) {
        if (key == "enabled") {
            settings.enabled = Json::RequireBoolean(value, key);
        } else if (key == "skillPointsPerCtrlClick") {
            const auto points = Json::RequireInteger(value, key);
            if (points < 1
                || points > MaximumSkillPointsPerCtrlClick) {
                throw std::invalid_argument(
                    "skillPointsPerCtrlClick must be 1 through 1000");
            }
            settings.skillPointsPerCtrlClick =
                static_cast<std::uint32_t>(points);
        } else if (key == "confirmShiftAllocation") {
            settings.confirmShiftAllocation =
                Json::RequireBoolean(value, key);
        } else if (key == "diagnostics") {
            settings.diagnostics = Json::RequireBoolean(value, key);
        } else {
            throw std::invalid_argument(
                "gameplay configuration contains unknown key '" + key + "'");
        }
    }
}

inline void ApplyStringsConfig(
    const Json::Object& object,
    Settings& settings
) {
    for (const auto& [key, value] : object) {
        if (key == "shiftConfirmationKey") {
            settings.shiftConfirmationKey = Json::RequireString(value, key);
        } else if (key == "shiftConfirmationFallback") {
            settings.shiftConfirmationFallback =
                Json::RequireString(value, key);
        } else if (const auto locale = FindLocaleByCode(key)) {
            settings.shiftConfirmations[*locale] =
                Json::RequireString(value, key);
        } else {
            throw std::invalid_argument(
                "strings configuration contains unknown key '" + key + "'");
        }
    }
    if (settings.shiftConfirmationKey.size() > 255) {
        throw std::invalid_argument(
            "shiftConfirmationKey must contain at most 255 UTF-8 bytes");
    }
    if (settings.shiftConfirmationFallback.empty()
        || settings.shiftConfirmationFallback.size() > 1024) {
        throw std::invalid_argument(
            "shiftConfirmationFallback must contain 1 through 1024 UTF-8 bytes");
    }
    for (std::size_t index = 0; index < SupportedLocaleCount; ++index) {
        const auto& confirmation = settings.shiftConfirmations[index];
        if (confirmation.empty() || confirmation.size() > 1024) {
            throw std::invalid_argument(
                std::string(LocaleDefinitions[index].code)
                + " must contain 1 through 1024 UTF-8 bytes");
        }
    }
}

inline auto SamePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) -> bool {
    const auto& leftNative = left.native();
    const auto& rightNative = right.native();
    return leftNative.size() == rightNative.size()
        && std::equal(
            leftNative.begin(), leftNative.end(), rightNative.begin(),
            [](wchar_t lhs, wchar_t rhs) {
                if (lhs >= L'A' && lhs <= L'Z') lhs += L'a' - L'A';
                if (rhs >= L'A' && rhs <= L'Z') rhs += L'a' - L'A';
                return lhs == rhs;
            });
}

inline auto BuildConfigCandidates(
    const std::filesystem::path& activeModConfigDirectory,
    const std::filesystem::path& scopeConfigDirectory,
    const std::filesystem::path& globalConfigDirectory,
    const std::filesystem::path& fileName
) -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> candidates;
    const auto append = [&](const std::filesystem::path& directory) {
        if (directory.empty()) return;
        const auto path = (directory / fileName).lexically_normal();
        if (std::none_of(
                candidates.begin(), candidates.end(),
                [&](const auto& current) { return SamePath(current, path); })) {
            candidates.push_back(path);
        }
    };
    append(activeModConfigDirectory);
    append(scopeConfigDirectory);
    append(globalConfigDirectory);
    return candidates;
}

inline auto GameRootFromModSupportDirectory(
    const std::filesystem::path& modSupportDirectory
) -> std::filesystem::path {
    auto root = modSupportDirectory.parent_path();
    root = root.parent_path();
    root = root.parent_path();
    if (root.empty()) {
        throw std::runtime_error(
            "D2RLoader supplied an invalid mod support directory");
    }
    return root;
}

struct ConfigDirectories {
    std::filesystem::path activeMod;
    std::filesystem::path scope;
    std::filesystem::path global;
};

inline auto ResolveConfigDirectories(const D2RL::PluginContext* context)
    -> ConfigDirectories {
    if (context == nullptr) {
        throw std::runtime_error("D2RLoader did not supply a plugin context");
    }

    ConfigDirectories directories;
    std::filesystem::path gameRoot;
    if (context->activeMod != nullptr && context->activeMod[0] != '\0'
        && context->modSupportDirectory != nullptr
        && context->modSupportDirectory[0] != L'\0') {
        const std::filesystem::path support(context->modSupportDirectory);
        directories.activeMod = support / L"config";
        gameRoot = GameRootFromModSupportDirectory(support);
    }

    if (context->pluginConfigPath != nullptr
        && context->pluginConfigPath[0] != L'\0') {
        directories.scope =
            std::filesystem::path(context->pluginConfigPath).parent_path();
    } else if (context->pluginDirectory != nullptr
        && context->pluginDirectory[0] != L'\0') {
        directories.scope =
            std::filesystem::path(context->pluginDirectory).parent_path()
            / L"config";
    }

    if (gameRoot.empty() && context->scopeRootDirectory != nullptr
        && context->scopeRootDirectory[0] != L'\0') {
        const std::filesystem::path scopeRoot(context->scopeRootDirectory);
        if (context->loadScope == D2RL::LoadScope::Global) {
            gameRoot = scopeRoot;
        } else if (context->loadScope == D2RL::LoadScope::Mod) {
            gameRoot = scopeRoot.parent_path().parent_path();
        }
    }
    if (!gameRoot.empty()) {
        directories.global = gameRoot / L"d2rloader" / L"config";
    } else if (context->loadScope == D2RL::LoadScope::Global
        && !directories.scope.empty()) {
        directories.global = directories.scope;
    }
    if (directories.global.empty()) {
        throw std::runtime_error(
            "D2RLoader did not supply enough path context to locate global configuration");
    }
    return directories;
}

inline auto ResolveConfigCandidates(
    const D2RL::PluginContext* context,
    const std::filesystem::path& fileName
) -> std::vector<std::filesystem::path> {
    const auto directories = ResolveConfigDirectories(context);
    return BuildConfigCandidates(
        directories.activeMod,
        directories.scope,
        directories.global,
        fileName);
}

struct LoadedDocument {
    Json::Object object;
    std::filesystem::path source;
};

inline auto LoadFirstDocument(
    const std::vector<std::filesystem::path>& candidates
) -> std::optional<LoadedDocument> {
    for (const auto& path : candidates) {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            throw std::runtime_error(
                "could not inspect configuration file: " + error.message());
        }
        if (!exists) continue;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            throw std::runtime_error(
                "configuration path is not a readable regular file");
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            throw std::runtime_error(
                "configuration size could not be read: " + error.message());
        }
        if (size > MaximumConfigBytes) {
            throw std::runtime_error("configuration exceeds 64 KiB");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            throw std::runtime_error("configuration file could not be opened");
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        if (size != 0) {
            input.read(text.data(), static_cast<std::streamsize>(size));
        }
        if (!input || input.peek() != std::ifstream::traits_type::eof()) {
            throw std::runtime_error(
                "configuration file could not be read exactly");
        }
        return LoadedDocument{Json::ParseObject(text), path};
    }
    return std::nullopt;
}

inline auto LoadSettingsFromCandidates(
    const std::vector<std::filesystem::path>& gameplayCandidates,
    const std::vector<std::filesystem::path>& stringsCandidates
) -> LoadedSettings {
    LoadedSettings loaded;
    if (auto gameplay = LoadFirstDocument(gameplayCandidates)) {
        ApplyGameplayConfig(gameplay->object, loaded.settings);
        loaded.gameplaySource = std::move(gameplay->source);
    }
    if (auto strings = LoadFirstDocument(stringsCandidates)) {
        ApplyStringsConfig(strings->object, loaded.settings);
        loaded.stringsSource = std::move(strings->source);
    }
    return loaded;
}

inline auto LoadSettings(const D2RL::PluginContext* context)
    -> LoadedSettings {
    return LoadSettingsFromCandidates(
        ResolveConfigCandidates(context, GameplayConfigFileName),
        ResolveConfigCandidates(context, StringsConfigFileName));
}

} // namespace RuffnecKk::BulkSkillPointAllocation
