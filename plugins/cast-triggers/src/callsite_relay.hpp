#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace ruffneckk::cast_triggers::callsites {
struct Site {
    std::uintptr_t rva;
    std::uintptr_t entry;
    std::size_t relay;
    // Eight bytes before CALL, five CALL bytes, eight bytes after it.
    std::array<std::uint8_t, 21> witness;
    std::size_t witnessSize{21};
};

inline bool EncodeCall(std::uintptr_t source, std::uintptr_t target,
        std::array<std::uint8_t, 5>& result) noexcept {
    const auto distance = static_cast<std::int64_t>(target)
        - static_cast<std::int64_t>(source) - 5;
    if (distance < (std::numeric_limits<std::int32_t>::min)()
            || distance > (std::numeric_limits<std::int32_t>::max)()) return false;
    const auto displacement = static_cast<std::int32_t>(distance);
    result[0] = 0xE8;
    std::memcpy(result.data() + 1, &displacement, sizeof displacement);
    return true;
}

inline bool Validate(const std::uint8_t* base, std::span<const Site> sites) noexcept {
    for (const auto& site : sites) {
        std::array<std::uint8_t, 5> expected{};
        if (site.rva < 8 || site.relay >= 4 || site.witnessSize < 13
                || site.witnessSize > site.witness.size()
                || !EncodeCall(site.rva, site.entry, expected)
                || std::memcmp(expected.data(), site.witness.data() + 8, 5) != 0
                || std::memcmp(base + site.rva - 8, site.witness.data(), site.witnessSize) != 0)
            return false;
    }
    return true;
}

// Four 16-byte leaf tail-jumps: FF25 [RIP+0], absolute wrapper address.
// No stack/register changes and no unwind frame of their own.
inline auto Allocate(std::uintptr_t base, const std::array<void*, 4>& wrappers)
        noexcept -> std::uint8_t* {
    constexpr std::uintptr_t granularity = 0x10000;
    const auto limit = base + 0x70000000;
    auto cursor = (base + granularity - 1) & ~(granularity - 1);
    while (cursor < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof region)) break;
        if (region.State == MEM_FREE) {
            auto* page = static_cast<std::uint8_t*>(VirtualAlloc(
                reinterpret_cast<void*>(cursor), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (page) {
                for (std::size_t index = 0; index < wrappers.size(); ++index) {
                    const std::array<std::uint8_t, 6> jump{0xFF, 0x25, 0, 0, 0, 0};
                    std::memcpy(page + index * 16, jump.data(), jump.size());
                    std::memcpy(page + index * 16 + 6, &wrappers[index], sizeof(void*));
                }
                DWORD previous{};
                if (VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &previous)
                        && FlushInstructionCache(GetCurrentProcess(), page, 64)) return page;
                VirtualFree(page, 0, MEM_RELEASE);
                return nullptr;
            }
        }
        const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        const auto next = (end + granularity - 1) & ~(granularity - 1);
        if (next <= cursor) break;
        cursor = next;
    }
    return nullptr;
}
} // namespace ruffneckk::cast_triggers::callsites
