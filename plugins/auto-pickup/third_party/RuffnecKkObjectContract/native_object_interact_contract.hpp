#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace RuffnecKk::MapSense::Detail {

// Complete accessors from the governed common native corpus. The split-byte
// extension was observed under Loader 1.2.2; no version selects this contract.
inline constexpr std::uintptr_t ObjectInteractGetterRva = 0x34AD40U;
inline constexpr std::uintptr_t ObjectInteractSetterRva = 0x34E9D0U;
inline constexpr std::size_t ObjectInteractGetterTail = 62U;
inline constexpr std::size_t ObjectInteractSetterTail = 74U;
inline constexpr std::array<std::uint8_t, 72U> ObjectInteractGetterBytes{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x84, 0xA8, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x83, 0x3B, 0x02, 0x74, 0x14, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
    0xE8, 0x3B, 0x9E, 0xFF, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x48, 0x8B, 0x43, 0x10, 0x0F, 0xB6,
    0x40, 0x08, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
inline constexpr std::array<std::uint8_t, 84U> ObjectInteractSetterBytes{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x0F, 0xB6, 0xFA, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24,
    0x38, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x5D,
    0x6E, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x83, 0x3B, 0x02, 0x74, 0x14, 0x48, 0x8D, 0x4C,
    0x24, 0x38, 0xC6, 0x44, 0x24, 0x38, 0x00, 0xE8,
    0x14, 0x61, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x8B, 0x5C,
    0x24, 0x30, 0x40, 0x88, 0x78, 0x08, 0x48, 0x83,
    0xC4, 0x20, 0x5F, 0xC3};

// Getter: EAX = low byte [+08] | high byte [+78] << 8; restore RSP.
// Setter: preserve the original byte write and clear [+78]; restore RSP.
// A direct rel32 JMP must follow each prefix and return to its exact epilogue.
inline constexpr std::array<std::uint8_t, 17U> ObjectInteractGetterRelay{
    0x0F, 0xB6, 0x48, 0x78, 0xC1, 0xE1, 0x08, 0x0F,
    0xB6, 0x40, 0x08, 0x0B, 0xC1, 0x48, 0x83, 0xC4, 0x20};
inline constexpr std::array<std::uint8_t, 12U> ObjectInteractSetterRelay{
    0x40, 0x88, 0x78, 0x08, 0xC6, 0x40, 0x78, 0x00,
    0x48, 0x83, 0xC4, 0x20};

enum class ObjectInteractContract : std::uint8_t {
    Unsupported,
    NativeByte,
    SplitByte16,
};

// Also used by the read-only qualification probe. Never grants write access,
// executes the sampled code, or follows a data pointer into a game object.
[[nodiscard]] inline auto ReadObjectInteractCode(
        HANDLE process, std::uintptr_t address,
        std::span<std::uint8_t> bytes) noexcept -> bool {
    constexpr std::uintptr_t UserLimit = 0x0000800000000000ULL;
    if (bytes.empty() || bytes.size() > 128U || address < 0x10000U
            || address >= UserLimit || bytes.size() > UserLimit - address) {
        return false;
    }
    const auto end = address + bytes.size();
    auto cursor = address;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQueryEx(process, reinterpret_cast<const void*>(cursor),
                &region, sizeof(region)) != sizeof(region)
                || region.State != MEM_COMMIT
                || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0U) {
            return false;
        }
        const auto access = region.Protect & 0xFFU;
        if (access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE
                && access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (start > cursor || region.RegionSize <= cursor - start) return false;
        const auto remaining = region.RegionSize - (cursor - start);
        cursor += (std::min)(remaining, end - cursor);
    }
    SIZE_T copied{};
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address),
            bytes.data(), bytes.size(), &copied) != FALSE
        && copied == bytes.size();
}

[[nodiscard]] inline auto ResolveObjectInteractJump(
        std::uintptr_t address, const std::uint8_t* bytes,
        std::uintptr_t& target) noexcept -> bool {
    constexpr auto Maximum = (std::numeric_limits<std::uintptr_t>::max)();
    if (bytes[0] != 0xE9U || address > Maximum - 5U) return false;
    std::uint32_t encoded{};
    for (std::size_t index = 0U; index < 4U; ++index) {
        encoded |= static_cast<std::uint32_t>(bytes[index + 1U]) << (index * 8U);
    }
    const auto displacement = std::bit_cast<std::int32_t>(encoded);
    const auto next = address + 5U;
    if (displacement >= 0) {
        const auto offset = static_cast<std::uintptr_t>(displacement);
        if (next > Maximum - offset) return false;
        target = next + offset;
    } else {
        const auto offset = static_cast<std::uintptr_t>(
            -static_cast<std::int64_t>(displacement));
        if (next < offset) return false;
        target = next - offset;
    }
    return target >= 0x10000U && target < 0x0000800000000000ULL;
}

template <std::size_t BodySize, std::size_t RelaySize, class Read>
[[nodiscard]] auto MatchesObjectInteractRelay(
        std::uintptr_t bodyAddress,
        const std::array<std::uint8_t, BodySize>& body,
        const std::array<std::uint8_t, BodySize>& canonical,
        std::size_t tail,
        const std::array<std::uint8_t, RelaySize>& prefix,
        Read& read) noexcept -> bool {
    for (std::size_t index = 0U; index < BodySize; ++index) {
        if (index >= tail && index < tail + 5U) continue;
        const auto expected = index >= tail + 5U && index < tail + 8U
            ? std::uint8_t{0x90U} : canonical[index];
        if (body[index] != expected) return false;
    }
    std::uintptr_t relayAddress{};
    if (!ResolveObjectInteractJump(bodyAddress + tail,
            body.data() + tail, relayAddress)) return false;
    std::array<std::uint8_t, RelaySize + 5U> relay{};
    if (!read(relayAddress, std::span<std::uint8_t>(relay))
            || !std::equal(prefix.begin(), prefix.end(), relay.begin())) {
        return false;
    }
    std::uintptr_t resume{};
    return ResolveObjectInteractJump(relayAddress + RelaySize,
            relay.data() + RelaySize, resume)
        && resume == bodyAddress + tail + 8U;
}

template <class Read>
[[nodiscard]] auto ValidateObjectInteractContract(
        std::uintptr_t base, Read&& read) noexcept -> ObjectInteractContract {
    if (base < 0x10000U || base > 0x0000800000000000ULL
            - ObjectInteractSetterRva - ObjectInteractSetterBytes.size()) {
        return ObjectInteractContract::Unsupported;
    }
    std::array<std::uint8_t, ObjectInteractGetterBytes.size()> getter{};
    std::array<std::uint8_t, ObjectInteractSetterBytes.size()> setter{};
    if (!read(base + ObjectInteractGetterRva, std::span<std::uint8_t>(getter))
            || !read(base + ObjectInteractSetterRva, std::span<std::uint8_t>(setter))) {
        return ObjectInteractContract::Unsupported;
    }
    if (getter == ObjectInteractGetterBytes && setter == ObjectInteractSetterBytes) {
        return ObjectInteractContract::NativeByte;
    }
    if (MatchesObjectInteractRelay(base + ObjectInteractGetterRva, getter,
            ObjectInteractGetterBytes, ObjectInteractGetterTail,
            ObjectInteractGetterRelay, read)
            && MatchesObjectInteractRelay(base + ObjectInteractSetterRva, setter,
                ObjectInteractSetterBytes, ObjectInteractSetterTail,
                ObjectInteractSetterRelay, read)) {
        return ObjectInteractContract::SplitByte16;
    }
    return ObjectInteractContract::Unsupported;
}

} // namespace RuffnecKk::MapSense::Detail
