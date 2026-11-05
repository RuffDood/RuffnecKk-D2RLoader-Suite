#pragma once
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace Reimagined::PickupCoexist {
// Matching RCDATA markers are readable before DLL initialization. No code is
// executed when probing a candidate file. Keep this header identical in both repos.
enum class Role : std::uint32_t { PickitOwner = 1, YieldingTeleport = 2 };
inline constexpr wchar_t ResourceName[] = L"D2R_PICKUP_COEXIST";
struct Presence { bool present{}, cooperative{}, legacy{}; };
inline auto Supports(HMODULE module, Role role) noexcept -> bool {
    const auto resource = FindResourceW(module, ResourceName, MAKEINTRESOURCEW(10));
    if (!resource || SizeofResource(module, resource) != 12) return false;
    const auto data = LoadResource(module, resource);
    const auto* bytes = data ? LockResource(data) : nullptr;
    if (!bytes) return false;
    std::array<std::uint32_t, 3> marker{};
    std::memcpy(marker.data(), bytes, sizeof(marker));
    return marker[0] == 0x5049434B && marker[1] == 1 && marker[2] == static_cast<std::uint32_t>(role);
}
inline auto Inspect(const D2RL::PluginContext* context, const wchar_t* filename, Role role) noexcept -> Presence {
    Presence result;
    auto record = [&](bool cooperative) { result.present = true; result.cooperative |= cooperative; result.legacy |= !cooperative; };
    if (const auto module = GetModuleHandleW(filename)) record(Supports(module, role));
    auto inspectFolder = [&](const std::filesystem::path& folder) {
        if (folder.empty()) return;
        const auto path = folder / filename;
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return;
        const auto image = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        record(image && Supports(image, role));
        if (image) FreeLibrary(image);
    };
    try {
        if (context->pluginDirectory) inspectFolder(context->pluginDirectory);
        if (context->scopeRootDirectory) inspectFolder(std::filesystem::path(context->scopeRootDirectory) / L"plugins");
        if (context->modDirectory) inspectFolder(std::filesystem::path(context->modDirectory) / L"d2rloader" / L"plugins");
        std::array<wchar_t, 32768> exe{};
        const auto length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length && length < exe.size()) inspectFolder(std::filesystem::path(exe.data()).parent_path() / L"d2rloader" / L"plugins");
    } catch (...) { record(false); }
    return result;
}
inline auto LegacyTeleportPresent(const D2RL::PluginContext* context) noexcept -> bool {
    return Inspect(context, L"d2rl-teleport-auto-pickup.dll", Role::YieldingTeleport).legacy;
}
inline auto YieldToPickit(const D2RL::PluginContext* context) noexcept -> bool {
    return Inspect(context, L"d2rl-auto-pickup-anything.dll", Role::PickitOwner).cooperative && !LegacyTeleportPresent(context);
}
}
