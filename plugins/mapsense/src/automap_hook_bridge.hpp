#pragma once

#include <D2RLPlugin/api.h>
#include <Windows.h>
#include <array>
#include <cstdint>
#include <span>

namespace RuffnecKk::MapSense::Detail {

using AutomapPass = void(__fastcall*)(void*, void*) noexcept;

// A bridge changes Pyre's original-call slot, never its code or the game hook.
// The byte-witnessed Pyre callback forwards both arguments through this slot
// once, before its own item-marker work. The slot is restored with compare/exchange.
struct AutomapHookRoute {
    void** slot{};
    void* original{};
    HMODULE ownerModule{};
};

inline constexpr std::uintptr_t AutomapEntryRva = 0xD76E0;
inline constexpr std::uintptr_t PyreAutomapCallbackRva = 0x4BA0;
inline constexpr std::uintptr_t PyreAutomapOriginalSlotRva = 0x1755A8;

auto QualifyAutomapHookRoute(const D2RL::PluginContext* context,
    std::span<const std::uint8_t> expected, AutomapHookRoute& route) noexcept -> bool;
auto InstallAutomapHookRoute(const D2RL::PluginContext* context,
    std::span<const std::uint8_t> expected, const AutomapHookRoute& route,
    AutomapPass callback, AutomapPass& original) noexcept -> bool;
void RestoreAutomapHookRoute() noexcept;

// Shared by the runtime and executable regression fixture. Failed ownership
// checks must never overwrite another component's pointer.
auto ExchangeAutomapSlot(void** slot, void* expected, void* replacement) noexcept -> bool;

} // namespace RuffnecKk::MapSense::Detail
