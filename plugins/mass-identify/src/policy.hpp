#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace RuffnecKk::MassIdentify {

inline constexpr std::size_t RequestPacketSize = 21;
inline constexpr std::uint8_t CainIdentifyOpcode = 0x34;
inline constexpr std::uint32_t RequestMarker = 0x3144494Du; // "MID1"
inline constexpr std::uint32_t RequestGuard = 0x314B4B52u;  // "RKK1"
inline constexpr std::uint32_t IdentifyTomeCode = 0x206B6269u; // "ibk "
inline constexpr std::uint32_t IdentifiedItemFlag = 0x00000010u;
inline constexpr std::uint32_t QuantityStat = 70;
inline constexpr std::uint8_t InventoryPage = 0;
inline constexpr std::uint8_t CubePage = 3;
inline constexpr std::uint8_t StashPage = 4;
inline constexpr std::uint8_t InvalidInventoryPage = 0xFF;
inline constexpr std::size_t ItemDataInventoryPageOffset = 0x55;

enum class TargetContainer : std::uint8_t {
    Inventory,
    Cube,
    PersonalStash,
    SharedStash,
};

struct TargetSelection {
    bool includeCube{};
    bool includePersonalStash{};
    bool includeSharedStash{};
};

struct Config {
    bool enabled{};
    bool freeIdentification{};
    TargetSelection targets{};
    bool diagnosticsEnabled{};
};

constexpr auto IncludesTarget(
    const TargetSelection& selection,
    TargetContainer container
) noexcept -> bool {
    switch (container) {
    case TargetContainer::Inventory:
        return true;
    case TargetContainer::Cube:
        return selection.includeCube;
    case TargetContainer::PersonalStash:
        return selection.includePersonalStash;
    case TargetContainer::SharedStash:
        return selection.includeSharedStash;
    }
    return false;
}

using RequestPacket = std::array<std::uint8_t, RequestPacketSize>;

constexpr auto ReadU32(
    const std::uint8_t* bytes,
    std::size_t offset
) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

constexpr void WriteU32(
    RequestPacket& packet,
    std::size_t offset,
    std::uint32_t value
) noexcept {
    packet[offset] = static_cast<std::uint8_t>(value);
    packet[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    packet[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    packet[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

constexpr auto MakeRequest(std::uint32_t tomeRuntimeId) noexcept
    -> RequestPacket {
    RequestPacket packet{};
    packet[0] = CainIdentifyOpcode;
    WriteU32(packet, 1, tomeRuntimeId);
    WriteU32(packet, 5, RequestMarker);
    WriteU32(packet, 9, RequestGuard);
    return packet;
}

constexpr auto IsPrivateRequest(
    const std::uint8_t* packet,
    std::int32_t size
) noexcept -> bool {
    return packet != nullptr
        && size == static_cast<std::int32_t>(RequestPacketSize)
        && packet[0] == CainIdentifyOpcode
        && ReadU32(packet, 5) == RequestMarker
        && ReadU32(packet, 9) == RequestGuard;
}

constexpr auto IsSupportedInventoryPage(std::uint8_t page) noexcept -> bool {
    return page == InventoryPage || page == CubePage;
}

inline auto ReadInventoryPageFromItemData(const void* itemData) noexcept
    -> std::uint8_t {
    if (itemData == nullptr) return InvalidInventoryPage;
    const auto* bytes = static_cast<const std::uint8_t*>(itemData);
    return bytes[ItemDataInventoryPageOffset];
}

constexpr auto ShouldCaptureGesture(
    bool enabled,
    bool shiftDown,
    bool rightClick,
    bool cursorEmpty,
    std::uint32_t itemCode,
    bool supportedTomeContainer
) noexcept -> bool {
    return enabled
        && shiftDown
        && rightClick
        && cursorEmpty
        && itemCode == IdentifyTomeCode
        && supportedTomeContainer;
}

constexpr auto IdentificationBudget(
    bool freeIdentification,
    std::int32_t quantity
) noexcept -> std::int32_t {
    if (freeIdentification) {
        return (std::numeric_limits<std::int32_t>::max)();
    }
    return quantity > 0 ? quantity : 0;
}

inline auto Trim(std::string_view value) noexcept -> std::string_view {
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

inline auto WithoutComment(std::string_view value) noexcept
    -> std::string_view {
    const auto comment = value.find('#');
    return comment == std::string_view::npos
        ? value
        : value.substr(0, comment);
}

inline auto ParseBoolean(std::string_view value, bool& output) noexcept
    -> bool {
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

inline auto SetConfigError(
    std::string& error,
    std::size_t line,
    std::string_view message
) -> bool {
    error = "line " + std::to_string(line) + ": " + std::string(message);
    return false;
}

inline auto ParseConfig(
    std::string_view input,
    Config& output,
    std::string& error
) -> bool {
    Config parsed{};
    std::string section;
    bool massSectionSeen{};
    bool diagnosticsSectionSeen{};
    bool enabledSeen{};
    bool freeIdentificationSeen{};
    bool includeCubeSeen{};
    bool includePersonalStashSeen{};
    bool includeSharedStashSeen{};
    bool diagnosticsEnabledSeen{};

    std::size_t lineNumber{};
    for (std::size_t start = 0; start <= input.size();) {
        ++lineNumber;
        const auto end = input.find('\n', start);
        auto line = Trim(WithoutComment(input.substr(
            start,
            end == std::string_view::npos
                ? input.size() - start
                : end - start)));
        start = end == std::string_view::npos ? input.size() + 1 : end + 1;
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']'
                || line.find('[', 1) != std::string_view::npos
                || line.find(']') != line.size() - 1) {
                return SetConfigError(
                    error, lineNumber, "invalid section header");
            }
            const auto name = Trim(line.substr(1, line.size() - 2));
            bool* seen{};
            if (name == "mass_identify") seen = &massSectionSeen;
            else if (name == "diagnostics") seen = &diagnosticsSectionSeen;
            else {
                return SetConfigError(error, lineNumber, "unknown section");
            }
            if (*seen) {
                return SetConfigError(error, lineNumber, "duplicate section");
            }
            *seen = true;
            section.assign(name);
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string_view::npos
            || line.find('=', equal + 1) != std::string_view::npos) {
            return SetConfigError(
                error, lineNumber, "expected one key/value assignment");
        }
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (section.empty() || key.empty() || value.empty()) {
            return SetConfigError(
                error, lineNumber, "invalid key/value assignment");
        }

        bool* seen{};
        bool* destination{};
        if (section == "diagnostics" && key == "enabled") {
            seen = &diagnosticsEnabledSeen;
            destination = &parsed.diagnosticsEnabled;
        } else if (section == "mass_identify" && key == "enabled") {
            seen = &enabledSeen;
            destination = &parsed.enabled;
        } else if (section == "mass_identify" && key == "freeIdentification") {
            seen = &freeIdentificationSeen;
            destination = &parsed.freeIdentification;
        } else if (section == "mass_identify" && key == "includeCube") {
            seen = &includeCubeSeen;
            destination = &parsed.targets.includeCube;
        } else if (section == "mass_identify" && key == "includePersonalStash") {
            seen = &includePersonalStashSeen;
            destination = &parsed.targets.includePersonalStash;
        } else if (section == "mass_identify" && key == "includeSharedStash") {
            seen = &includeSharedStashSeen;
            destination = &parsed.targets.includeSharedStash;
        } else {
            return SetConfigError(error, lineNumber, "unknown setting");
        }

        if (*seen) {
            return SetConfigError(error, lineNumber, "duplicate setting");
        }
        *seen = true;
        if (!ParseBoolean(value, *destination)) {
            return SetConfigError(error, lineNumber, "expected true or false");
        }
    }

    if (!(massSectionSeen && enabledSeen && freeIdentificationSeen
            && includeCubeSeen && includePersonalStashSeen
            && includeSharedStashSeen)
        || diagnosticsSectionSeen != diagnosticsEnabledSeen) {
        error = "one or more required settings are missing";
        return false;
    }

    output = parsed;
    error.clear();
    return true;
}

} // namespace RuffnecKk::MassIdentify
