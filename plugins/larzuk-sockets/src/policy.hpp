#pragma once

#include <D2RLPlugin/context.h>
#include <json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RuffnecKk::LarzukSockets {

inline constexpr wchar_t ConfigFileName[] = L"ForceLarzukSockets.json";
inline constexpr std::uintmax_t MaximumConfigBytes = 64U * 1024U;

enum class ItemQuality : std::int32_t {
    Magic = 4,
    Set = 5,
    Rare = 6,
    Unique = 7,
    Crafted = 8,
};

struct SocketRule {
    std::uint8_t minSockets{};
    std::uint8_t maxSockets{};
};

inline constexpr std::size_t DifficultyCount = 3;
inline constexpr std::size_t QualityCount = 5;
using RuleMatrix = std::array<
    std::array<std::optional<SocketRule>, QualityCount>,
    DifficultyCount>;

struct Config {
    bool enabled{};
    RuleMatrix rules{};
    bool diagnostics{};
};

struct LoadedConfig {
    Config config{};
    std::filesystem::path source;
};

inline constexpr std::array<std::string_view, DifficultyCount>
    DifficultyNames{"normal", "nightmare", "hell"};
inline constexpr std::array<std::string_view, QualityCount>
    QualityNames{"magic", "rare", "set", "unique", "crafted"};

constexpr auto QualityIndex(std::int32_t quality) noexcept
    -> std::optional<std::size_t> {
    switch (static_cast<ItemQuality>(quality)) {
    case ItemQuality::Magic: return 0;
    case ItemQuality::Rare: return 1;
    case ItemQuality::Set: return 2;
    case ItemQuality::Unique: return 3;
    case ItemQuality::Crafted: return 4;
    }
    return std::nullopt;
}

constexpr auto IsValidRule(SocketRule rule) noexcept -> bool {
    return rule.minSockets >= 1
        && rule.maxSockets <= 6
        && rule.minSockets <= rule.maxSockets;
}

constexpr auto EffectiveLegalMaximum(
    std::uint8_t engineMaximum,
    std::uint8_t inventoryWidth,
    std::uint8_t inventoryHeight
) noexcept -> std::uint8_t {
    const auto area = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(inventoryWidth)
        * static_cast<std::uint16_t>(inventoryHeight));
    if (area == 0) return 0;
    const auto footprintMaximum = static_cast<std::uint8_t>(
        area < std::uint16_t{6} ? area : std::uint16_t{6});
    return engineMaximum < footprintMaximum
        ? engineMaximum
        : footprintMaximum;
}

constexpr auto LimitedRandomIndex(
    std::uint32_t rawRoll,
    std::uint32_t range
) noexcept -> std::uint32_t {
    if (range <= 1) return 0;
    return (range & (range - 1)) == 0
        ? (rawRoll & (range - 1))
        : (rawRoll % range);
}

constexpr auto ResolveSockets(
    SocketRule rule,
    std::uint8_t legalMaximum,
    std::uint32_t rawRoll
) noexcept -> std::uint8_t {
    if (legalMaximum == 0) return 0;
    const auto minimum = (std::min)(rule.minSockets, legalMaximum);
    const auto maximum = (std::min)(rule.maxSockets, legalMaximum);
    const auto range = static_cast<std::uint32_t>(maximum - minimum) + 1;
    return static_cast<std::uint8_t>(
        minimum + LimitedRandomIndex(rawRoll, range));
}

constexpr auto FindRule(
    const RuleMatrix& rules,
    std::uint8_t difficulty,
    std::int32_t quality
) noexcept -> const std::optional<SocketRule>* {
    const auto qualityIndex = QualityIndex(quality);
    if (difficulty >= DifficultyCount || !qualityIndex) return nullptr;
    return &rules[difficulty][*qualityIndex];
}

constexpr auto HasRules(const RuleMatrix& rules) noexcept -> bool {
    for (const auto& difficulty : rules) {
        for (const auto& rule : difficulty) {
            if (rule.has_value()) return true;
        }
    }
    return false;
}

inline auto SamePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) -> bool {
    const auto& leftNative = left.native();
    const auto& rightNative = right.native();
    return leftNative.size() == rightNative.size()
        && std::equal(
            leftNative.begin(),
            leftNative.end(),
            rightNative.begin(),
            [](wchar_t lhs, wchar_t rhs) {
                if (lhs >= L'A' && lhs <= L'Z') lhs += L'a' - L'A';
                if (rhs >= L'A' && rhs <= L'Z') rhs += L'a' - L'A';
                return lhs == rhs;
            });
}

inline auto BuildConfigCandidates(
    const std::filesystem::path& activeModConfigDirectory,
    const std::filesystem::path& scopeConfigDirectory,
    const std::filesystem::path& globalConfigDirectory
) -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> candidates;
    const auto append = [&](const std::filesystem::path& directory) {
        if (directory.empty()) return;
        const auto path = (directory / ConfigFileName).lexically_normal();
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

inline auto ResolveConfigCandidates(const D2RL::PluginContext* context)
    -> std::vector<std::filesystem::path> {
    if (context == nullptr) {
        throw std::runtime_error("D2RLoader did not supply a plugin context");
    }

    std::filesystem::path activeModConfigDirectory;
    std::filesystem::path scopeConfigDirectory;
    std::filesystem::path globalConfigDirectory;
    std::filesystem::path gameRoot;

    if (context->activeMod != nullptr && context->activeMod[0] != '\0'
        && context->modSupportDirectory != nullptr
        && context->modSupportDirectory[0] != L'\0') {
        const std::filesystem::path support(context->modSupportDirectory);
        activeModConfigDirectory = support / L"config";
        gameRoot = GameRootFromModSupportDirectory(support);
    }

    if (context->pluginConfigPath != nullptr
        && context->pluginConfigPath[0] != L'\0') {
        scopeConfigDirectory =
            std::filesystem::path(context->pluginConfigPath).parent_path();
    } else if (context->pluginDirectory != nullptr
        && context->pluginDirectory[0] != L'\0') {
        scopeConfigDirectory =
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
        globalConfigDirectory = gameRoot / L"d2rloader" / L"config";
    } else if (context->loadScope == D2RL::LoadScope::Global
        && !scopeConfigDirectory.empty()) {
        globalConfigDirectory = scopeConfigDirectory;
    }

    if (globalConfigDirectory.empty()) {
        throw std::runtime_error(
            "D2RLoader did not supply enough path context to locate the global configuration");
    }

    return BuildConfigCandidates(
        activeModConfigDirectory,
        scopeConfigDirectory,
        globalConfigDirectory);
}

inline void RequireAllowedKeys(
    const nlohmann::json& object,
    std::initializer_list<std::string_view> allowed,
    std::string_view path
) {
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), std::string_view(key))
            == allowed.end()) {
            throw std::invalid_argument(
                std::string(path) + " contains unknown key '" + key + "'");
        }
    }
}

inline auto ParseJson(std::string_view text) -> nlohmann::json {
    std::vector<std::unordered_set<std::string>> objectKeys;
    bool duplicateKey{};
    std::string duplicateName;
    const auto callback = [&](int, nlohmann::json::parse_event_t event,
                              nlohmann::json& value) {
        switch (event) {
        case nlohmann::json::parse_event_t::object_start:
            objectKeys.emplace_back();
            break;
        case nlohmann::json::parse_event_t::object_end:
            if (!objectKeys.empty()) objectKeys.pop_back();
            break;
        case nlohmann::json::parse_event_t::key:
            if (!objectKeys.empty()) {
                const auto key = value.get<std::string>();
                if (!objectKeys.back().insert(key).second && !duplicateKey) {
                    duplicateKey = true;
                    duplicateName = key;
                }
            }
            break;
        default:
            break;
        }
        return true;
    };

    auto parsed = nlohmann::json::parse(
        text.begin(), text.end(), callback, true, true);
    if (duplicateKey) {
        throw std::invalid_argument(
            "configuration contains duplicate key '" + duplicateName + "'");
    }
    return parsed;
}

inline auto ParseRule(
    const nlohmann::json& value,
    std::string_view path
) -> SocketRule {
    if (!value.is_object()) {
        throw std::invalid_argument(
            std::string(path) + " must be an object or null");
    }
    RequireAllowedKeys(value, {"minSockets", "maxSockets"}, path);
    if (!value.contains("minSockets") || !value.contains("maxSockets")) {
        throw std::invalid_argument(
            std::string(path)
            + " must define both minSockets and maxSockets");
    }
    if (!value.at("minSockets").is_number_integer()
        || !value.at("maxSockets").is_number_integer()) {
        throw std::invalid_argument(
            std::string(path) + " bounds must be integers from 1 through 6");
    }
    const auto minimum = value.at("minSockets").get<std::int64_t>();
    const auto maximum = value.at("maxSockets").get<std::int64_t>();
    if (minimum < 1 || maximum > 6 || minimum > maximum) {
        throw std::invalid_argument(
            std::string(path)
            + " requires 1 <= minSockets <= maxSockets <= 6");
    }
    return {
        static_cast<std::uint8_t>(minimum),
        static_cast<std::uint8_t>(maximum),
    };
}

inline auto ParseConfig(const nlohmann::json& root) -> Config {
    if (!root.is_object()) {
        throw std::invalid_argument("configuration root must be an object");
    }
    RequireAllowedKeys(
        root,
        {"enabled", "normal", "nightmare", "hell", "diagnostics"},
        "configuration root");

    if (root.contains("enabled") && !root.at("enabled").is_boolean()) {
        throw std::invalid_argument("enabled must be true or false");
    }

    if (!root.contains("diagnostics")
        || !root.at("diagnostics").is_boolean()) {
        throw std::invalid_argument("diagnostics must be true or false");
    }

    Config parsed{};
    if (root.contains("enabled")) {
        parsed.enabled = root.at("enabled").get<bool>();
    }
    parsed.diagnostics = root.at("diagnostics").get<bool>();
    for (std::size_t difficulty = 0;
         difficulty < DifficultyNames.size();
         ++difficulty) {
        const auto difficultyName = DifficultyNames[difficulty];
        if (!root.contains(difficultyName)) {
            throw std::invalid_argument(
                "configuration root must define "
                + std::string(difficultyName));
        }
        const auto& difficultyConfig = root.at(difficultyName);
        if (!difficultyConfig.is_object()) {
            throw std::invalid_argument(
                std::string(difficultyName) + " must be an object");
        }
        RequireAllowedKeys(
            difficultyConfig,
            {"magic", "rare", "set", "unique", "crafted"},
            difficultyName);

        for (std::size_t quality = 0;
             quality < QualityNames.size();
             ++quality) {
            const auto qualityName = QualityNames[quality];
            if (!difficultyConfig.contains(qualityName)) {
                throw std::invalid_argument(
                    std::string(difficultyName) + " must define "
                    + std::string(qualityName)
                    + " (use null for vanilla behavior)");
            }
            const auto& value = difficultyConfig.at(qualityName);
            if (value.is_null()) continue;
            parsed.rules[difficulty][quality] = ParseRule(
                value,
                std::string(difficultyName) + "."
                    + std::string(qualityName));
        }
    }
    return parsed;
}

inline auto LoadConfigFromCandidates(
    const std::vector<std::filesystem::path>& candidates
) -> LoadedConfig {
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
            throw std::runtime_error("configuration file could not be read exactly");
        }
        return {ParseConfig(ParseJson(text)), path};
    }
    throw std::runtime_error(
        "ForceLarzukSockets.json was not found in active-mod, plugin-scope, or global configuration");
}

} // namespace RuffnecKk::LarzukSockets
