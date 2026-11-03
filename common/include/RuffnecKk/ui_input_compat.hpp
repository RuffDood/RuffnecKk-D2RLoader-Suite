#pragma once
#include "quick_upgrade_witness.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

namespace RuffnecKk::UiInputCompat {
inline constexpr std::uintptr_t NativeRva = 0x843FC0;
inline constexpr std::array<std::uint8_t, 24> NativeBytes{
    0x48,0x8B,0x05,0xA9,0xC1,0xBF,0x02,0x48,0x85,0xC0,0x74,0x07,
    0xC6,0x80,0xB8,0,0,0,1,0xC3,0xCC,0xCC,0xCC,0xCC};

// Reader supplies safe read, executable-range and allocation-ownership checks.
// All consumers continue to call the canonical entry after admission.
inline bool RelativeTarget(std::uintptr_t next, std::int32_t offset,
        std::uintptr_t& target) noexcept {
    if (offset >= 0) {
        const auto distance = static_cast<std::uintptr_t>(offset);
        if (next > UINTPTR_MAX - distance) return false;
        target = next + distance;
    } else {
        const auto distance = static_cast<std::uintptr_t>(-static_cast<std::int64_t>(offset));
        if (next < distance) return false;
        target = next - distance;
    }
    return true;
}
template<class T, std::size_t N>
T Value(const std::array<std::uint8_t, N>& bytes, std::size_t offset) noexcept {
    T value{};
    if (offset <= N && sizeof(T) <= N - offset)
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
template<class Reader>
bool Validate(std::uintptr_t game, std::uintptr_t provider, const Reader& memory) noexcept {
    // The native RIP-relative global lives at game + 0x3440170.
    if (game == 0 || game > UINTPTR_MAX - 0x3440170U) return false;
    const auto entry = game + NativeRva;
    std::array<std::uint8_t, 24> actual{};
    if (!memory.code(entry, actual.size())
        || !memory.owned(entry, actual.size(), game)
        || !memory.read(entry, actual.data(), actual.size())) return false;
    if (actual == NativeBytes) return true;
    // Only the five-byte entry jump may differ from the native function.
    if (provider == 0 || provider > UINTPTR_MAX - 0x7D870U || actual[0] != 0xE9
        || !std::equal(actual.begin() + 5, actual.end(), NativeBytes.begin() + 5))
        return false;
    std::uintptr_t relay{};
    if (!RelativeTarget(entry + 5, Value<std::int32_t>(actual, 1), relay)) return false;
    std::array<std::uint8_t, 14> relayBytes{};
    constexpr std::array<std::uint8_t, 6> AbsoluteJump{0xFF,0x25,0,0,0,0};
    if (!memory.code(relay, relayBytes.size())
        || !memory.read(relay, relayBytes.data(), relayBytes.size())
        || !std::equal(AbsoluteJump.begin(), AbsoluteJump.end(), relayBytes.begin())
        || Value<std::uintptr_t>(relayBytes, 6) != provider + 0x14960U) return false;
    std::array<std::uint8_t, QuickUpgradeHook.size()> hook{};
    if (!memory.code(provider + 0x14960U, hook.size())
        || !memory.owned(provider + 0x14960U, hook.size(), provider)
        || !memory.read(provider + 0x14960U, hook.data(), hook.size())
        || hook != QuickUpgradeHook) return false;
    std::uintptr_t trampoline{}, providerGame{};
    if (!memory.owned(provider + 0x7D5E0U, sizeof(trampoline), provider)
        || !memory.read(provider + 0x7D5E0U, &trampoline, sizeof(trampoline))
        || !memory.owned(provider + 0x7D868U, sizeof(providerGame), provider)
        || !memory.read(provider + 0x7D868U, &providerGame, sizeof(providerGame))
        || providerGame != game || trampoline == 0 || trampoline > UINTPTR_MAX - 21U)
        return false;
    // MinHook relocates the complete seven-byte RIP-relative load and returns
    // through an absolute jump to the first undisplaced native instruction.
    std::array<std::uint8_t, 21> original{};
    std::uintptr_t global{};
    return memory.code(trampoline, original.size())
        && memory.read(trampoline, original.data(), original.size())
        && std::equal(NativeBytes.begin(), NativeBytes.begin() + 3, original.begin())
        && RelativeTarget(trampoline + 7, Value<std::int32_t>(original, 3), global)
        && global == game + 0x3440170U
        && std::equal(AbsoluteJump.begin(), AbsoluteJump.end(), original.begin() + 7)
        && Value<std::uintptr_t>(original, 13) == entry + 7;
}
}
