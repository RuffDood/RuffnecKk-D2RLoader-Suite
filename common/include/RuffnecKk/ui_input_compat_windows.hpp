#pragma once
#include "ui_input_compat.hpp"
#include <Windows.h>

namespace RuffnecKk::UiInputCompat {
struct ProcessMemory {
    bool read(std::uintptr_t address, void* output, std::size_t length) const noexcept {
        if (length > UINTPTR_MAX - address) return false;
        SIZE_T count{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            output, length, &count) != FALSE && count == length;
    }
    bool region(std::uintptr_t address, std::size_t length,
            MEMORY_BASIC_INFORMATION& info) const noexcept {
        if (length == 0 || length > UINTPTR_MAX - address
            || VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info)
            || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        return address >= begin && address - begin <= info.RegionSize
            && length <= info.RegionSize - (address - begin);
    }
    bool code(std::uintptr_t address, std::size_t length) const noexcept {
        MEMORY_BASIC_INFORMATION info{};
        if (!region(address, length, info)) return false;
        const auto protect = info.Protect & 0xFFU;
        return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ
            || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
    }
    bool owned(std::uintptr_t address, std::size_t length, std::uintptr_t owner) const noexcept {
        MEMORY_BASIC_INFORMATION info{};
        return region(address, length, info)
            && reinterpret_cast<std::uintptr_t>(info.AllocationBase) == owner;
    }
};

inline bool ValidateCurrentProcess(std::uintptr_t game) noexcept {
    const auto provider = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(L"d2rl-rune-gem-quick-upgrade.dll"));
    return Validate(game, provider, ProcessMemory{});
}
}
