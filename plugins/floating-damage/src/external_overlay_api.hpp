#pragma once

#include <Windows.h>

#include <cstdint>

namespace RuffnecKk::FloatingDamageOverlay {

inline constexpr std::uint32_t ApiVersion1 = 1;

using OverlayCallback = void(__cdecl*)(
    void* drawList,
    float displayWidth,
    float displayHeight,
    HWND window) noexcept;

using RegisterNamedOverlayFn = bool(__cdecl*)(
    const char* owner,
    OverlayCallback callback) noexcept;

using AddRectFn = void(__cdecl*)(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha,
    float thickness) noexcept;

using AddRectFilledFn = void(__cdecl*)(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha) noexcept;

struct ExternalOverlayApiV1 {
    std::uint32_t structSize{};
    std::uint32_t version{};
    RegisterNamedOverlayFn registerNamedOverlay{};
    AddRectFn addRect{};
    AddRectFilledFn addRectFilled{};
};

inline constexpr std::uint32_t ExternalOverlayApiV1Size =
    sizeof(ExternalOverlayApiV1);

using GetExternalOverlayApiFn = const ExternalOverlayApiV1* (__cdecl*)(
    std::uint32_t requestedVersion,
    std::uint32_t callerStructSize) noexcept;

} // namespace RuffnecKk::FloatingDamageOverlay
