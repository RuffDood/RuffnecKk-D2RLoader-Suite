#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Canonical ABI: Suite plugins/cast-triggers/src/damage_cleanup_api.hpp.
// The DollExplosion incubation carries a byte-identical consumer copy.
namespace RuffnecKk::DamageCleanup {
inline constexpr std::uint32_t Version = 1;
inline constexpr std::uint64_t DamageLayout = 0x4432444D47313830ULL;
inline constexpr std::uint32_t DamageBytes = 0x180;
inline constexpr char ExportName[] = "RuffnecKkCastTriggersGetDamageCleanupApi";

enum class Result : std::uint32_t {
    Rejected = 0,       // Operation and cleanup were NOT called.
    Completed = 1,      // Operation succeeded; cleanup completed once.
    OperationFailed = 2, // Operation returned false; cleanup completed once.
    Fault = 3,         // SEH fault; no retry, cleanup may already have run.
};
using OperationFn = bool(__cdecl*)(void* userData, void* damage) noexcept;
struct RequestV1 {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t damageLayout;
    const void* nativeDestructor;
    const std::uint8_t* nativeExpected;
    std::uint32_t nativeExpectedSize;
    std::uint32_t reserved;
};
using AcceptsFn = bool(__cdecl*)(const RequestV1*) noexcept;
using RunFn = Result(__cdecl*)(const RequestV1*, void*, OperationFn, void*) noexcept;
struct ApiV1 {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t damageLayout;
    AcceptsFn accepts;
    RunFn run;
};
using GetApiFn = const ApiV1*(__cdecl*)(std::uint32_t, std::uint32_t) noexcept;

inline auto ValidApi(const ApiV1* api) noexcept -> bool {
    return api && api->structSize >= sizeof(ApiV1) && api->version == Version
        && api->damageLayout == DamageLayout && api->accepts && api->run;
}
inline auto ValidRequest(const RequestV1* request) noexcept -> bool {
    return request && request->structSize == sizeof(RequestV1)
        && request->version == Version && request->damageLayout == DamageLayout
        && request->nativeDestructor && request->nativeExpected
        && request->nativeExpectedSize == 32 && request->reserved == 0;
}
static_assert(sizeof(RequestV1) == 40 && sizeof(ApiV1) == 32);
static_assert(std::is_standard_layout_v<RequestV1>
    && std::is_trivially_copyable_v<RequestV1>);
} // namespace RuffnecKk::DamageCleanup
