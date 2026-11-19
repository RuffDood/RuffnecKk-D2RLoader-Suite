#pragma once

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/shared_events.h>
#include "widget_service.hpp"

#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>

namespace MapSeedSelector {

[[nodiscard]] auto Initialize(const D2RL::PluginContext* context,
    std::string_view defaultConfig) noexcept -> bool;
[[nodiscard]] auto OwnsSeedInitializer(std::uintptr_t gameBase) noexcept -> bool;
[[nodiscard]] auto IsAvailable() noexcept -> bool;
void SetWidgetService(const MapSeedSelector::ExtendedWidgetService* service,
    bool nativeDropdownAvailable) noexcept;
void SetPanelGeneration(std::uint32_t generation) noexcept;
void SetOfflineFlowActive(bool active) noexcept;
[[nodiscard]] auto BuildLayoutChildren() -> std::string;
[[nodiscard]] auto BuildOptionsLayoutChildren() -> std::string;
void GetPanelPosition(std::int32_t& x, std::int32_t& y) noexcept;
void GetAddSeedButtonPosition(std::int32_t& x, std::int32_t& y) noexcept;
[[nodiscard]] auto GetPanelWidth() noexcept -> std::int32_t;
[[nodiscard]] auto GetPanelHeight() noexcept -> std::int32_t;
[[nodiscard]] auto GetOptionsPanelHeight() noexcept -> std::int32_t;
void HandleDropdownChanged() noexcept;
void SelectChoice(std::size_t choice) noexcept;
[[nodiscard]] auto GetLastObservedMapSeed(std::uint32_t& seed) noexcept -> bool;
[[nodiscard]] auto AddSeedEntry(std::string_view name,
    std::string_view seedText, std::string& message) noexcept -> bool;
[[nodiscard]] auto DeleteSeedEntry(std::size_t index,
    std::string& message) noexcept -> bool;
[[nodiscard]] auto GetSeedEntryCount() noexcept -> std::size_t;
[[nodiscard]] auto GetSeedEntryLabel(std::size_t index,
    std::string& label) noexcept -> bool;
[[nodiscard]] auto IsDropdownExpandedAbove() noexcept -> bool;
[[nodiscard]] auto GetDropdownRowHeight() noexcept -> std::int32_t;
[[nodiscard]] auto GetDropdownControlHeight() noexcept -> std::int32_t;
void RefreshUi() noexcept;
void Shutdown() noexcept;

} // namespace MapSeedSelector

extern "C" {
extern volatile long gLastObservedMapSeed;
extern volatile long gLastObservedMapSeedValid;
void MapDrlgSeedInitializationHook() noexcept;
void MapGameSeedIngressHook() noexcept;
void MapSavedSeedCopyAHook() noexcept;
void MapSavedSeedCopyBHook() noexcept;
void MapSavedSeedCopyCHook() noexcept;
}
