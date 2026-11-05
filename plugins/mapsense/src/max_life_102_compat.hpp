#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace RuffnecKk::MapSense::Detail {
auto ValidateMaxLife102Hook(void* userData, std::uint32_t nativeRva,
    std::uintptr_t coreBase, std::uint32_t functionRva,
    std::span<const std::byte> expected) noexcept -> std::size_t;
}
