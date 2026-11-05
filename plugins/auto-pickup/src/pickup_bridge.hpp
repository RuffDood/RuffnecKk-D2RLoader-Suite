#pragma once
#include <cstdint>
#include <type_traits>

// Independent consumer/provider copies must remain byte-identical.
// Calls are synchronous on the authoritative game thread only. The caller
// holds a module reference for the entire call; no native pointer is retained.
namespace Reimagined::PickupBridge {
inline constexpr std::uint32_t Version = 1;
inline constexpr std::uint32_t RangeVersion = 2;
inline constexpr std::uint32_t MaximumPickupDistance = 20;
inline constexpr char ExportName[] = "AutoPickupAnythingGetScanApi";
using ScanFn = bool(__cdecl*)(void* player) noexcept;
struct ApiV1 {
    std::uint32_t structSize;
    std::uint32_t version;
    ScanFn scan;
};
using GetApiFn = const ApiV1*(__cdecl*)(std::uint32_t version, std::uint32_t size) noexcept;
struct ApiV2 {
    std::uint32_t structSize;
    std::uint32_t version;
    bool(__cdecl* scan)(void* player, std::uint32_t distance) noexcept;
};
using GetRangeApiFn = const ApiV2*(__cdecl*)(std::uint32_t version, std::uint32_t size) noexcept;
inline auto ValidApi(const ApiV2* api) noexcept -> bool {
    return api && api->structSize >= sizeof(ApiV2) && api->version == RangeVersion && api->scan;
}
inline auto ValidApi(const ApiV1* api) noexcept -> bool {
    return api && api->structSize >= sizeof(ApiV1) && api->version == Version && api->scan;
}
inline constexpr auto SupportedRequest(std::uint32_t version, std::uint32_t size) noexcept -> bool {
    return version == Version && size >= sizeof(ApiV1);
}
static_assert(sizeof(ApiV1) == 16 && std::is_trivially_copyable_v<ApiV1>);
static_assert(sizeof(ApiV2) == 16 && std::is_trivially_copyable_v<ApiV2>);
}
