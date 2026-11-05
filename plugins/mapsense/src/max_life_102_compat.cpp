#include "max_life_102_compat.hpp"
#include "max_life_102_witness.hpp"
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace RuffnecKk::MapSense::Detail {
namespace {
bool Read(std::uintptr_t at, void* out, std::size_t size) noexcept {
    SIZE_T count{};
    return at <= UINTPTR_MAX - size
        && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, size, &count)
        && count == size;
}
template<class T> bool Value(std::uintptr_t at, T& out) noexcept { return Read(at, &out, sizeof(out)); }
bool Code(std::uintptr_t at, std::size_t size, std::uintptr_t owner = 0) noexcept {
    MEMORY_BASIC_INFORMATION mbi{};
    if (at > UINTPTR_MAX - size || VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi)) != sizeof(mbi)
        || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
        || (owner != 0 && reinterpret_cast<std::uintptr_t>(mbi.AllocationBase) != owner)
        || at + size > reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) return false;
    const auto p = mbi.Protect & 0xFFU;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}
bool Match(std::uintptr_t at, std::span<const std::uint8_t> expected) noexcept {
    std::array<std::uint8_t, MaxLife102Code.size()> bytes{};
    return expected.size() <= bytes.size() && Read(at, bytes.data(), expected.size())
        && std::equal(expected.begin(), expected.end(), bytes.begin());
}
bool Relative(std::uintptr_t at, std::size_t displacementOffset,
        std::size_t instructionSize, std::uintptr_t& target) noexcept {
    std::int32_t displacement{};
    if (at > UINTPTR_MAX - instructionSize || !Value(at + displacementOffset, displacement)) return false;
    const auto after = at + instructionSize;
    if (displacement < 0) {
        const auto magnitude = static_cast<std::uintptr_t>(-static_cast<std::int64_t>(displacement));
        if (magnitude > after) return false;
        target = after - magnitude;
    } else {
        if (after > UINTPTR_MAX - static_cast<std::uintptr_t>(displacement)) return false;
        target = after + static_cast<std::uintptr_t>(displacement);
    }
    return true;
}
bool AbsoluteJump(std::uintptr_t at, std::uintptr_t target) noexcept {
    constexpr std::array<std::uint8_t, 6> prefix{0xFF, 0x25, 0, 0, 0, 0};
    std::uintptr_t actual{};
    return Match(at, prefix) && Value(at + 6, actual) && actual == target;
}
}

auto ValidateMaxLife102Hook(void* userData, std::uint32_t nativeRva,
        std::uintptr_t coreBase, std::uint32_t functionRva,
        std::span<const std::byte> expected) noexcept -> std::size_t {
    // Complete actual Core export includes its final RET (16 bytes).
    constexpr std::array<std::uint8_t, 16> exportBytes{
        0x48,0x83,0xEC,0x28,0xE8,0xE7,0x6D,0xBA,0xFF,0x89,0xC0,0x48,0x83,0xC4,0x28,0xC3};
    if (userData == nullptr || nativeRva != 0x2F5020 || functionRva != 0x831DE0
        || coreBase > UINTPTR_MAX - 0x831DF0 || expected.size() != exportBytes.size()
        || std::memcmp(expected.data(), exportBytes.data(), expected.size()) != 0) return 0;
    const auto entry = coreBase + functionRva;
    std::array<std::uint8_t, 9> patch{};
    std::uintptr_t relay{}, callback{};
    if (!Code(entry, 16, coreBase) || !Read(entry, patch.data(), patch.size())
        || patch[0] != 0xE9 || !std::all_of(patch.begin() + 5, patch.end(), [](auto x) { return x == 0x90; })
        || !Relative(entry, 1, 5, relay) || !Code(relay, 14)
        || !Value(relay + 6, callback) || !AbsoluteJump(relay, callback)) return 0;
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(callback), &module)) return 0;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (base > UINTPTR_MAX - 0xB608 || callback != base + 0x1040
        || !Code(base + 0x1000, MaxLife102Code.size(), base)
        || !Match(base + 0x1000, MaxLife102Code)) return 0;
    std::uintptr_t page{}, original{}, target{}, originalSlot{}, savedCallback{}, context{};
    std::uintptr_t ownerMain{}, callerMain{};
    std::uint8_t active{}, span{};
    if (!Value(base + 0xB600, page) || page != relay || page > UINTPTR_MAX - 0x1000
        || !Code(page, 0x1000, page) || !Value(base + 0xB3E8, original) || original != page + 0x200
        || !Value(base + 0xB050, target) || target != entry
        || !Value(base + 0xB030, originalSlot) || originalSlot != base + 0xB3E8
        || !Value(base + 0xB028, savedCallback) || savedCallback != callback
        || !Value(base + 0xB048, active) || active != 1
        || !Value(base + 0xB038, span) || span != 9
        || !Match(base + 0xB039, std::span(exportBytes).first(9))
        || !Match(base + 0xB058, patch)
        || !Value(base + 0xB428, context) || context == 0 || context > UINTPTR_MAX - sizeof(D2RL::PluginContext)
        || !Value(context + offsetof(D2RL::PluginContext, exeBase), ownerMain)
        || !Value(reinterpret_cast<std::uintptr_t>(userData) + offsetof(D2RL::PluginContext, exeBase), callerMain)
        || ownerMain == 0 || ownerMain != callerMain) return 0;
    // The original SUB and relocated CALL must reach the exact Core helper;
    // the trampoline must resume at the first untouched byte of this export.
    std::uintptr_t relocatedCall{};
    if (!Match(original, std::span(exportBytes).first(5))
        || !Relative(original + 4, 1, 5, relocatedCall) || relocatedCall != coreBase + 0x3D8BD0
        || !AbsoluteJump(original + 9, entry + 9)
        || !Match(entry + 9, std::span(exportBytes).subspan(9))) return 0;
    return 9;
}
}
