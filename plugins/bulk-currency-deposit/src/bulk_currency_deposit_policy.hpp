#pragma once

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ruffneckk::bulk_currency_deposit {

inline constexpr std::uint8_t MainInventoryPage = 0;
inline constexpr std::uint8_t AdvancedStashPage = 4;
inline constexpr std::uint8_t InvalidInventoryPage = 0xFF;
inline constexpr std::size_t ItemDataInventoryPageOffset = 0x55;
inline constexpr std::uint32_t ItemUnitType = 4;
inline constexpr std::uint32_t MinimumItemDelayMs = 50;
inline constexpr std::uint32_t MaximumItemDelayMs = 1000;
inline constexpr std::uint32_t DefaultItemDelayMs = 100;
inline constexpr std::int32_t DefaultButtonX = 3;
inline constexpr std::int32_t DefaultButtonY = 813;
inline constexpr std::int32_t MinimumButtonCoordinate = -32768;
inline constexpr std::int32_t MaximumButtonCoordinate = 32767;
inline constexpr std::string_view DefaultButtonTooltip{"Deposit Currency"};
inline constexpr std::size_t MaximumButtonTooltipBytes = 256;

constexpr std::uint64_t PackActionBinding(
        std::uint32_t key,
        std::uint32_t modifier) noexcept {
    return key == 0
        ? 0
        : (static_cast<std::uint64_t>(modifier) << 32) | key;
}

class BindingCaptureSet {
public:
    bool Contains(std::uint64_t binding) const noexcept {
        if (binding == 0) return false;
        for (const auto& slot : slots_) {
            if (slot.load(std::memory_order_acquire) == binding) return true;
        }
        return false;
    }

    bool Capture(std::uint64_t binding) noexcept {
        if (binding == 0) return false;
        for (;;) {
            if (Contains(binding)) return true;
            bool sawAvailableSlot{};
            for (auto& slot : slots_) {
                std::uint64_t expected{};
                if (slot.compare_exchange_strong(
                        expected,
                        binding,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return true;
                }
                if (expected == binding) return true;
                sawAvailableSlot = sawAvailableSlot || expected == 0;
            }
            if (!sawAvailableSlot) return false;
        }
    }

    bool Release(std::uint64_t binding) noexcept {
        if (binding == 0) return false;
        for (auto& slot : slots_) {
            auto expected = binding;
            if (slot.compare_exchange_strong(
                    expected,
                    0,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }

    void Reset() noexcept {
        for (auto& slot : slots_) {
            slot.store(0, std::memory_order_release);
        }
    }

private:
    std::array<std::atomic<std::uint64_t>, 2> slots_{};
};

class CallbackRundownState {
public:
    void Reset() noexcept {
        active_.store(0, std::memory_order_relaxed);
        stopping_.store(false, std::memory_order_release);
    }

    bool Enter() noexcept {
        active_.fetch_add(1, std::memory_order_acq_rel);
        return !stopping_.load(std::memory_order_acquire);
    }

    void Leave() noexcept {
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }

    void Stop() noexcept {
        stopping_.store(true, std::memory_order_release);
    }

    bool CanProcess() const noexcept {
        return !stopping_.load(std::memory_order_acquire);
    }

    std::uint32_t ActiveCount() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool stopping_{};
    std::atomic<std::uint32_t> active_{};
};

enum class UiStateEntryStatus : std::uint8_t {
    Unchanged,
    TrackedInlineHook,
    Other,
};

constexpr bool AcceptUiStateEntry(
        UiStateEntryStatus status,
        bool vanillaSignatureMatches,
        bool entryIsExecutable,
        std::uint32_t ownerCount,
        std::string_view ownerPluginId) noexcept {
    if (status == UiStateEntryStatus::Unchanged) {
        return vanillaSignatureMatches;
    }
    return status == UiStateEntryStatus::TrackedInlineHook
        && entryIsExecutable
        && ownerCount == 1
        && ownerPluginId == "ruffneckk-remote-stash";
}

struct ButtonConfig {
    std::int32_t x{DefaultButtonX};
    std::int32_t y{DefaultButtonY};
    std::string tooltip{DefaultButtonTooltip};
};

struct Config {
    bool enabled{true};
    bool inventoryButtonEnabled{false};
    std::uint32_t itemDelayMs{DefaultItemDelayMs};
    std::vector<std::uint32_t> includeItemCodes;
    std::vector<std::uint32_t> excludeItemCodes;
    ButtonConfig button{};
};

inline constexpr std::string_view DepositUiTarget{"PanelManager"};
inline constexpr std::string_view DepositUiCommand{"OpenPanel"};
inline constexpr std::string_view DepositUiText{
    "RuffnecKkBulkCurrencyDeposit"};

inline bool IsDepositUiMessage(
        std::string_view target,
        std::string_view command,
        std::string_view text) noexcept {
    return target == DepositUiTarget
        && command == DepositUiCommand
        && text == DepositUiText;
}

inline std::string EscapeJsonString(std::string_view text) {
    static constexpr char HexDigits[]{"0123456789ABCDEF"};
    std::string escaped;
    escaped.reserve(text.size());
    for (const unsigned char byte : text) {
        switch (byte) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20) {
                escaped += "\\u00";
                escaped.push_back(HexDigits[(byte >> 4) & 0x0F]);
                escaped.push_back(HexDigits[byte & 0x0F]);
            } else {
                escaped.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    return escaped;
}

inline std::string BuildButtonLayoutJson(const ButtonConfig& config) {
    std::string json;
    json.reserve(900);
    json += "{\n  \"type\": \"ImageWidget\",\n";
    json += "  \"name\": \"bulk-currency-deposit/inventory-button\",\n";
    json += "  \"fields\": {\n";
    json += "    \"rect\": { \"x\": " + std::to_string(config.x);
    json += ", \"y\": " + std::to_string(config.y);
    json += ", \"width\": 54, \"height\": 141 },\n";
    json += "    \"filename\": \"d2rloader/bulk-currency-deposit/button-mold\"\n";
    json += "  },\n  \"children\": [\n    {\n";
    json += "      \"type\": \"ButtonWidget\",\n";
    json += "      \"name\": \"bulk-currency-deposit/deposit-button\",\n";
    json += "      \"fields\": {\n";
    json += "        \"rect\": { \"x\": 1, \"y\": 39 },\n";
    json += "        \"filename\": \"d2rloader/bulk-currency-deposit/deposit-button\",\n";
    json += "        \"hoveredFrame\": 3,\n";
    json += "        \"onClickMessage\": \"PanelManager:OpenPanel:RuffnecKkBulkCurrencyDeposit\",\n";
    json += "        \"pressLabelOffset\": { \"x\": 0, \"y\": 2 },\n";
    json += "        \"tooltipString\": \"";
    json += EscapeJsonString(config.tooltip);
    json += "\"\n";
    json += "      }\n    }\n  ]\n}\n";
    return json;
}

constexpr bool IsFreshRequest(
        std::uint64_t now,
        std::uint64_t requestedAt,
        std::uint64_t maximumAge) noexcept {
    return requestedAt != 0
        && now >= requestedAt
        && now - requestedAt <= maximumAge;
}

inline std::optional<std::uint32_t> PackItemCode(
        std::string_view code) noexcept {
    if (code.empty() || code.size() > 4) return std::nullopt;
    std::uint32_t packed{};
    for (std::size_t index = 0; index < code.size(); ++index) {
        const auto character = static_cast<unsigned char>(code[index]);
        if (character <= 0x20 || character > 0x7E) return std::nullopt;
        packed |= static_cast<std::uint32_t>(character) << (index * 8);
    }
    return packed;
}

inline std::string UnpackItemCode(std::uint32_t packed) {
    std::string code;
    for (unsigned index = 0; index < 4; ++index) {
        const auto character = static_cast<char>((packed >> (index * 8)) & 0xFF);
        if (character == '\0') break;
        code.push_back(character);
    }
    return code;
}

inline bool ContainsCode(
        const std::vector<std::uint32_t>& codes,
        std::uint32_t itemCode) noexcept {
    return std::find(codes.begin(), codes.end(), itemCode) != codes.end();
}

inline bool MatchesItemCodeFilter(
        const Config& config,
        std::uint32_t itemCode) noexcept {
    if (!config.includeItemCodes.empty()
            && !ContainsCode(config.includeItemCodes, itemCode)) {
        return false;
    }
    return !ContainsCode(config.excludeItemCodes, itemCode);
}

inline std::uint8_t ReadInventoryPageFromItemData(
        const void* itemData) noexcept {
    if (!itemData) return InvalidInventoryPage;
    return static_cast<const std::uint8_t*>(itemData)[
        ItemDataInventoryPageOffset];
}

inline std::vector<std::filesystem::path> BuildConfigCandidates(
        const std::filesystem::path& activeModConfigDirectory,
        const std::filesystem::path& scopeConfigDirectory,
        const std::filesystem::path& globalConfigDirectory,
        const std::filesystem::path& fileName) {
    std::vector<std::filesystem::path> candidates;
    const auto append = [&](const std::filesystem::path& directory) {
        if (directory.empty()) return;
        const auto candidate = (directory / fileName).lexically_normal();
        if (std::find(candidates.begin(), candidates.end(), candidate)
                == candidates.end()) {
            candidates.emplace_back(candidate);
        }
    };
    append(activeModConfigDirectory);
    append(scopeConfigDirectory);
    append(globalConfigDirectory);
    return candidates;
}

inline bool ParseToml(
        std::string_view input,
        Config& result,
        std::string& error) {
    try {
        const auto root = toml::parse(input);
        for (const auto& [key, value] : root) {
            (void)value;
            if (key != "deposit" && key != "button") {
                error = "unknown top-level setting or section: "
                    + std::string(key.str());
                return false;
            }
        }
        const auto* deposit = root["deposit"].as_table();
        if (!deposit) {
            error = "missing [deposit] section";
            return false;
        }
        constexpr std::string_view allowedKeys[]{
            "enabled",
            "inventory_button_enabled",
            "item_delay_ms",
            "include_item_codes",
            "exclude_item_codes",
        };
        for (const auto& [key, value] : *deposit) {
            (void)value;
            const auto known = std::find(
                std::begin(allowedKeys), std::end(allowedKeys), key.str())
                != std::end(allowedKeys);
            if (!known) {
                error = "unknown setting: deposit." + std::string(key.str());
                return false;
            }
        }

        Config parsed{};
        const auto readBool = [&](const char* key, bool& destination) {
            const auto* node = deposit->get(key);
            if (!node) return true;
            const auto value = node->value<bool>();
            if (!value) {
                error = std::string("deposit.") + key
                    + " must be true or false";
                return false;
            }
            destination = *value;
            return true;
        };
        if (!readBool("enabled", parsed.enabled)
                || !readBool(
                    "inventory_button_enabled",
                    parsed.inventoryButtonEnabled)) {
            return false;
        }

        if (const auto* node = deposit->get("item_delay_ms")) {
            const auto value = node->value<std::int64_t>();
            if (!value
                    || *value < MinimumItemDelayMs
                    || *value > MaximumItemDelayMs) {
                error = "deposit.item_delay_ms must be an integer from 50 to 1000";
                return false;
            }
            parsed.itemDelayMs = static_cast<std::uint32_t>(*value);
        }

        const auto readCodes = [&](
                const char* key,
                std::vector<std::uint32_t>& destination) {
            const auto* node = deposit->get(key);
            if (!node) return true;
            const auto* array = node->as_array();
            if (!array) {
                error = std::string("deposit.") + key
                    + " must be a TOML array";
                return false;
            }
            for (const auto& entry : *array) {
                const auto text = entry.value<std::string>();
                if (!text) {
                    error = std::string("deposit.") + key
                        + " entries must be quoted strings";
                    return false;
                }
                const auto packed = PackItemCode(*text);
                if (!packed) {
                    error = std::string("deposit.") + key
                        + " entries must contain one to four printable non-space ASCII characters";
                    return false;
                }
                if (ContainsCode(destination, *packed)) {
                    error = std::string("deposit.") + key
                        + " must not contain duplicate item codes";
                    return false;
                }
                destination.push_back(*packed);
            }
            return true;
        };
        if (!readCodes("include_item_codes", parsed.includeItemCodes)
                || !readCodes(
                    "exclude_item_codes", parsed.excludeItemCodes)) {
            return false;
        }
        if (const auto* buttonNode = root.get("button")) {
            const auto* button = buttonNode->as_table();
            if (!button) {
                error = "button must be a TOML table";
                return false;
            }
            constexpr std::string_view allowedButtonKeys[]{
                "x", "y", "tooltip"};
            for (const auto& [key, value] : *button) {
                (void)value;
                const auto known = std::find(
                    std::begin(allowedButtonKeys),
                    std::end(allowedButtonKeys),
                    key.str()) != std::end(allowedButtonKeys);
                if (!known) {
                    error = "unknown setting: button." + std::string(key.str());
                    return false;
                }
            }
            const auto readCoordinate = [&error, button](
                    const char* key,
                    std::int32_t& destination) {
                const auto* node = button->get(key);
                if (!node) return true;
                const auto value = node->value<std::int64_t>();
                if (!value
                        || *value < MinimumButtonCoordinate
                        || *value > MaximumButtonCoordinate) {
                    error = std::string("button.") + key
                        + " must be an integer from -32768 to 32767";
                    return false;
                }
                destination = static_cast<std::int32_t>(*value);
                return true;
            };
            if (!readCoordinate("x", parsed.button.x)
                    || !readCoordinate("y", parsed.button.y)) {
                return false;
            }
            if (const auto* node = button->get("tooltip")) {
                const auto value = node->value<std::string>();
                if (!value
                        || value->empty()
                        || value->size() > MaximumButtonTooltipBytes
                        || value->find('\0') != std::string::npos) {
                    error = "button.tooltip must be a non-empty UTF-8 string of at most 256 bytes";
                    return false;
                }
                parsed.button.tooltip = *value;
            }
        }
        result = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace ruffneckk::bulk_currency_deposit
