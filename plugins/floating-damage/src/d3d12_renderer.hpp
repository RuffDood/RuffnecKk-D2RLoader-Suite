#pragma once

// Directly derived from locbones/D2RHUD-2.4 at
// b9373f8508282948ceb3e2b56f892d9eba475744. locbones authorized its use,
// modification, and redistribution on 2026-08-16. No upstream license is
// claimed or applied to this derivative notice.

#include <Windows.h>

#include <cstdint>

struct ImFont;

namespace D3D12 {

constexpr int kFloatingDamageFontCount = 13;
using DiagnosticLogCallback = void(__cdecl*)(const char* message) noexcept;

struct OverlayDiagnostics {
    std::uint64_t presentCalls{};
    std::uint64_t directQueueCaptures{};
    std::uint64_t rendererInitAttempts{};
    std::uint64_t rendererInitFailures{};
    std::uint64_t renderedFrames{};
    std::uint32_t lastInitFailureStage{};
    bool hooksInstalled{};
    bool commandQueueReady{};
    bool rendererInitialized{};
};

void SetDllModule(HMODULE module) noexcept;
void SetOptionalKodiaFontPath(const wchar_t* path) noexcept;
void SetDiagnosticLogCallback(DiagnosticLogCallback callback) noexcept;
bool InstallHooks() noexcept;
void RemoveHooks() noexcept;
OverlayDiagnostics GetOverlayDiagnostics() noexcept;
ImFont* GetFloatingDamageFont(int index) noexcept;
void GetDisplaySize(float& width, float& height) noexcept;

} // namespace D3D12
