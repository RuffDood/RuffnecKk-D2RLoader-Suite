#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneckk::burn_damage_fix {

inline constexpr std::int64_t ConfigVersion = 1;
inline constexpr std::int32_t StatRegenerationEvent = 3;
inline constexpr std::int32_t BurningState = 115;
inline constexpr std::int32_t FireHitOverlay = 81;
inline constexpr std::uint16_t NativeBurningOverlay = 224;
inline constexpr std::uint16_t EmptyStateOverlay = 0xFFFF;
inline constexpr std::int32_t UnitDoOverlayStat = 178;
inline constexpr std::int32_t PreservePositiveResistanceAndImmunity = 0;
inline constexpr std::int32_t DefaultOverlayRepeatFrames = 10;
inline constexpr std::int32_t MinimumOverlayRepeatFrames = 1;
inline constexpr std::int32_t MaximumOverlayRepeatFrames = 250;

struct FireResistanceMetadata {
    std::int32_t resistanceStat;
    std::int32_t maximumResistanceStat;
    std::int32_t pierceStat;
    std::int32_t immunityPierceStat;
    std::int32_t absorbPercentStat;
    std::int32_t absorbFlatStat;
    std::int32_t damageReductionIndex;
    std::int32_t attackerGate;
    std::int32_t flags28;
    std::int32_t reserved2C;
    std::uint8_t logFlag;
};

inline constexpr FireResistanceMetadata BurnFireResistance{
    .resistanceStat = 39,
    .maximumResistanceStat = 40,
    .pierceStat = 333,
    .immunityPierceStat = 189,
    .absorbPercentStat = -1,
    .absorbFlatStat = -1,
    .damageReductionIndex = 2,
    .attackerGate = 0,
    .flags28 = 1,
    .reserved2C = 0,
    .logFlag = 8,
};

struct Config {
    bool enabled{true};
    bool diagnostics{false};

    static constexpr bool normalizeGenericBurn{true};
    static constexpr bool applyFireResistance{true};
    static constexpr bool replayFireHit{true};
    static constexpr bool suppressNativeBurning{true};
    static constexpr std::int32_t overlayRepeatFrames{
        DefaultOverlayRepeatFrames};
};

inline auto ShouldResolveBurn(
        const Config& config,
        std::int32_t burnDamage,
        std::int32_t burnLength) noexcept -> bool {
    return config.enabled && config.applyFireResistance
        && burnDamage > 0 && burnLength > 0;
}

inline auto ShouldWitnessBurningState(
        const Config& config,
        std::int32_t resolvedBurnDamage,
        std::int32_t burnLength) noexcept -> bool {
    return config.enabled && config.diagnostics
        && resolvedBurnDamage > 0 && burnLength > 0;
}

inline auto ShouldReplayFireHit(
        const Config& config,
        std::int32_t eventType,
        std::uint32_t gameFrame) noexcept -> bool {
    return config.enabled && config.replayFireHit
        && eventType == StatRegenerationEvent
        && config.overlayRepeatFrames >= MinimumOverlayRepeatFrames
        && config.overlayRepeatFrames <= MaximumOverlayRepeatFrames
        && gameFrame
            % static_cast<std::uint32_t>(config.overlayRepeatFrames) == 0;
}

inline auto ShouldSuppressNativeBurning(const Config& config) noexcept
        -> bool {
    return config.enabled && config.replayFireHit
        && config.suppressNativeBurning;
}

enum class NativeBurningOverlayAction {
    Suppress,
    AlreadySuppressed,
    PreserveCustom,
};

inline auto ClassifyNativeBurningOverlay(
        std::uint16_t overlay) noexcept -> NativeBurningOverlayAction {
    if (overlay == NativeBurningOverlay) {
        return NativeBurningOverlayAction::Suppress;
    }
    if (overlay == EmptyStateOverlay) {
        return NativeBurningOverlayAction::AlreadySuppressed;
    }
    return NativeBurningOverlayAction::PreserveCustom;
}

enum class ResistanceResolverStatus {
    Unchanged,
    TrackedInlineHook,
    Other,
};

inline auto AcceptResistanceResolver(
        ResistanceResolverStatus status,
        bool vanillaSignatureMatches,
        bool liveEntryIsExecutable,
        std::uint32_t ownerCount,
        std::string_view ownerPluginId) noexcept -> bool {
    if (status == ResistanceResolverStatus::Unchanged) {
        return vanillaSignatureMatches && liveEntryIsExecutable
            && ownerCount == 0;
    }
    return status == ResistanceResolverStatus::TrackedInlineHook
        && liveEntryIsExecutable
        && ownerCount == 1
        && ownerPluginId == "monsterdisplay";
}

inline auto CalculatePercentage(
        std::int32_t value,
        std::int32_t percentage) noexcept -> std::int64_t {
    return static_cast<std::int64_t>(value)
        * static_cast<std::int64_t>(percentage) / 100;
}

inline auto NormalizeGenericNumerator(
        std::int32_t scaledExistingBurn,
        std::int32_t burningMin,
        std::int32_t burningMax,
        std::int32_t fireMastery,
        std::uint32_t advancedRandom) noexcept -> std::int32_t {
    std::int64_t addedBurn{};
    if (burningMin > 0 && burningMax > 0) {
        if (burningMin > burningMax) std::swap(burningMin, burningMax);
        auto minimum = static_cast<std::int64_t>(burningMin)
            + CalculatePercentage(burningMin, fireMastery);
        auto maximum = static_cast<std::int64_t>(burningMax)
            + CalculatePercentage(burningMax, fireMastery);
        minimum = std::clamp<std::int64_t>(
            minimum, 0, std::numeric_limits<std::int32_t>::max());
        maximum = std::clamp<std::int64_t>(
            maximum, 0, std::numeric_limits<std::int32_t>::max());
        if (minimum > maximum) std::swap(minimum, maximum);
        addedBurn = minimum;
        const auto range = static_cast<std::uint32_t>(maximum - minimum);
        if (range != 0) {
            const auto offset = (range & (range - 1U)) == 0
                ? advancedRandom & (range - 1U)
                : advancedRandom % range;
            addedBurn += offset;
        }
    }

    const auto result = static_cast<std::int64_t>(scaledExistingBurn) + addedBurn;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        result, 0, std::numeric_limits<std::int32_t>::max()));
}

inline auto ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) -> bool {
    try {
        const auto root = toml::parse(input);
        for (const auto& [key, value] : root) {
            (void)value;
            if (key != "config_version" && key != "enabled"
                    && key != "diagnostics") {
                error = "unknown top-level setting or section: "
                    + std::string(key.str());
                return false;
            }
        }

        const auto* versionNode = root.get("config_version");
        const auto* enabledNode = root.get("enabled");
        const auto* diagnosticsNode = root.get("diagnostics");
        const auto configVersion = versionNode
            ? versionNode->value<std::int64_t>()
            : std::optional<std::int64_t>{};
        if (!versionNode || !versionNode->is_integer() || !configVersion
                || *configVersion != ConfigVersion) {
            error = "config_version must be integer 1";
            return false;
        }
        if (!enabledNode || !enabledNode->is_boolean()) {
            error = "enabled must be a boolean";
            return false;
        }
        const auto* diagnostics = diagnosticsNode
            ? diagnosticsNode->as_table() : nullptr;
        if (!diagnostics) {
            error = "diagnostics must be a table";
            return false;
        }
        for (const auto& [key, value] : *diagnostics) {
            (void)value;
            if (key != "enabled") {
                error = "unknown diagnostics setting: " + std::string(key.str());
                return false;
            }
        }
        const auto* diagnosticsEnabledNode = diagnostics->get("enabled");
        if (!diagnosticsEnabledNode || !diagnosticsEnabledNode->is_boolean()) {
            error = "diagnostics.enabled must be a boolean";
            return false;
        }

        Config parsed{};
        parsed.enabled = *enabledNode->value<bool>();
        parsed.diagnostics = *diagnosticsEnabledNode->value<bool>();
        result = parsed;
        error.clear();
        return true;
    } catch (const toml::parse_error& exception) {
        error = exception.description();
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

inline auto BuildConfigCandidates(
        const std::filesystem::path& activeModDirectory,
        const std::filesystem::path& scopeDirectory,
        const std::filesystem::path& globalDirectory,
        const std::filesystem::path& fileName)
        -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> result;
    for (const auto& directory : {
            activeModDirectory, scopeDirectory, globalDirectory}) {
        if (directory.empty()) continue;
        const auto candidate = directory / fileName;
        if (std::find(result.begin(), result.end(), candidate) == result.end()) {
            result.push_back(candidate);
        }
    }
    return result;
}

inline auto CanEncodeRel32(
        std::uintptr_t instruction,
        std::uintptr_t target) noexcept -> bool {
    const auto next = static_cast<std::int64_t>(instruction) + 5;
    const auto displacement = static_cast<std::int64_t>(target) - next;
    return displacement >= std::numeric_limits<std::int32_t>::min()
        && displacement <= std::numeric_limits<std::int32_t>::max();
}

} // namespace ruffneckk::burn_damage_fix
