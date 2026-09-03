#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneckk::armageddon_ctc {

inline constexpr std::int32_t ArmageddonSkillId = 249;
inline constexpr std::int32_t HurricaneSkillId = 250;

struct Config {
    bool enabled{true};
    bool armageddon{true};
    bool hurricane{true};
    bool diagnostics{};
};

constexpr bool IsSupportedSkill(std::int32_t skillId) noexcept {
    return skillId == ArmageddonSkillId || skillId == HurricaneSkillId;
}

constexpr bool IsSkillEnabled(
        const Config& config,
        std::int32_t skillId) noexcept {
    return (skillId == ArmageddonSkillId && config.armageddon)
        || (skillId == HurricaneSkillId && config.hurricane);
}

constexpr bool ShouldEraseExpiredSeedBeforeActive(
        bool statePresent) noexcept {
    return !statePresent;
}

constexpr bool ShouldEraseExpiredSeedAfterActive(
        std::int32_t callbackResult,
        bool statePresent) noexcept {
    return callbackResult == 0 && !statePresent;
}

inline bool MatchesFingerprint(
        const std::uint8_t* actual,
        std::span<const std::uint8_t> expected) noexcept {
    return actual && !expected.empty()
        && std::equal(expected.begin(), expected.end(), actual);
}

inline std::vector<std::filesystem::path> BuildConfigCandidates(
        const std::vector<std::filesystem::path>& directories,
        const std::filesystem::path& fileName) {
    std::vector<std::filesystem::path> result;
    for (const auto& directory : directories) {
        if (directory.empty()) continue;
        const auto candidate = (directory / fileName).lexically_normal();
        if (std::find(result.begin(), result.end(), candidate) == result.end()) {
            result.emplace_back(candidate);
        }
    }
    return result;
}

inline bool ReadBoolean(
        const toml::table& table,
        const char* key,
        bool& destination,
        std::string& error,
        std::string_view qualifiedName) {
    const auto* node = table.get(key);
    if (!node) return true;
    const auto value = node->value<bool>();
    if (!value) {
        error = std::string(qualifiedName) + " must be true or false";
        return false;
    }
    destination = *value;
    return true;
}

inline bool ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) {
    try {
        const auto root = toml::parse(input);
        for (const auto& [key, value] : root) {
            (void)value;
            if (key != "enabled" && key != "skills"
                    && key != "diagnostics") {
                error = "unknown top-level setting or section: "
                    + std::string(key.str());
                return false;
            }
        }

        Config parsed{};
        if (!ReadBoolean(root, "enabled", parsed.enabled, error, "enabled")) {
            return false;
        }

        if (const auto* skillsNode = root.get("skills")) {
            const auto* skills = skillsNode->as_table();
            if (!skills) {
                error = "skills must be a TOML table";
                return false;
            }
            for (const auto& [key, value] : *skills) {
                (void)value;
                if (key != "armageddon" && key != "hurricane") {
                    error = "unknown setting: skills."
                        + std::string(key.str());
                    return false;
                }
            }
            if (!ReadBoolean(
                    *skills,
                    "armageddon",
                    parsed.armageddon,
                    error,
                    "skills.armageddon")
                    || !ReadBoolean(
                        *skills,
                        "hurricane",
                        parsed.hurricane,
                        error,
                        "skills.hurricane")) {
                return false;
            }
        }

        if (const auto* diagnosticsNode = root.get("diagnostics")) {
            const auto* diagnostics = diagnosticsNode->as_table();
            if (!diagnostics) {
                error = "diagnostics must be a TOML table";
                return false;
            }
            for (const auto& [key, value] : *diagnostics) {
                (void)value;
                if (key != "enabled") {
                    error = "unknown setting: diagnostics."
                        + std::string(key.str());
                    return false;
                }
            }
            if (!ReadBoolean(
                    *diagnostics,
                    "enabled",
                    parsed.diagnostics,
                    error,
                    "diagnostics.enabled")) {
                return false;
            }
        }

        result = parsed;
        error.clear();
        return true;
    } catch (const toml::parse_error& exception) {
        error = exception.description();
        return false;
    }
}

} // namespace ruffneckk::armageddon_ctc
