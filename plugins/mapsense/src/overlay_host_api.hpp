#pragma once

#include <Windows.h>

#include <cstdint>

namespace RuffnecKk::OverlayHost {

// Both DLLs pin ocornut/imgui commit f401021d5a5d56fe2304056c391e78f81c8d4b8f.
// The fingerprint intentionally makes an ImGui ABI mismatch fail closed.
inline constexpr std::uint32_t ApiVersion2 = 2;
inline constexpr std::uint32_t ImGuiVersionNumber = 19150;
inline constexpr std::uint64_t ImGuiAbiFingerprint =
    0xF401021D19150002ULL;

struct FrameContextV2 {
    std::uint32_t structSize{};
    std::uint32_t version{};
    void* imguiContext{};
    HWND window{};
    float displayWidth{};
    float displayHeight{};
};

inline constexpr std::uint32_t FrameContextV2Size =
    sizeof(FrameContextV2);

using BeforeFrameCallbackV2 = void(__cdecl*)(
    const FrameContextV2* frame,
    void* userData) noexcept;

using RenderCallbackV2 = void(__cdecl*)(
    const FrameContextV2* frame,
    void* userData) noexcept;

// Context callbacks let a client attach resources such as fonts to the one
// ImGui atlas owned by MapSense. hostStopped is emitted only after MapSense
// has removed its D3D12 hooks, so an autonomous client may safely resume.
using ContextCallbackV2 = void(__cdecl*)(
    const FrameContextV2* frame,
    void* userData) noexcept;

using HostStoppedCallbackV2 = void(__cdecl*)(void* userData) noexcept;

struct ClientV2 {
    std::uint32_t structSize{};
    std::uint32_t version{};
    const char* owner{};
    std::uint64_t imguiAbiFingerprint{};
    ContextCallbackV2 contextCreated{};
    ContextCallbackV2 contextDestroying{};
    HostStoppedCallbackV2 hostStopped{};
    BeforeFrameCallbackV2 beforeFrame{};
    RenderCallbackV2 render{};
    void* userData{};
};

inline constexpr std::uint32_t ClientV2Size = sizeof(ClientV2);

using RegisterClientV2Fn = bool(__cdecl*)(const ClientV2* client) noexcept;
using UnregisterClientV2Fn = bool(__cdecl*)(const char* owner) noexcept;

struct HostApiV2 {
    std::uint32_t structSize{};
    std::uint32_t version{};
    std::uint64_t imguiAbiFingerprint{};
    RegisterClientV2Fn registerClient{};
    UnregisterClientV2Fn unregisterClient{};
};

inline constexpr std::uint32_t HostApiV2Size = sizeof(HostApiV2);

using GetHostApiV2Fn = const HostApiV2* (__cdecl*)(
    std::uint32_t requestedVersion,
    std::uint32_t callerStructSize) noexcept;

} // namespace RuffnecKk::OverlayHost
