#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Canonical ABI: plugins/stack-manager/src/auto_pickup_route_api.hpp.
// Auto Pickup carries a byte-identical consumer copy so both plugins remain
// independently packageable and tolerate the other DLL being absent.
namespace RuffnecKk::AutoPickupRoute {

inline constexpr std::uint32_t Version = 1;
inline constexpr char ExportName[] =
    "RuffnecKkStackManagerGetAutoPickupRouteApi";

enum class Destination : std::uint32_t {
    None = 0,
    Belt = 1,
    Inventory = 2,
};

enum class ProviderState : std::uint32_t {
    Inactive = 0,
    Ready = 1,
};

struct RequestV1 {
    std::uint32_t structSize;
    std::uint32_t version;
    void* inventory;
    void* item;
    std::uint32_t itemGuid;
    std::array<std::uint8_t, 4> beltColumns;
    std::uint8_t beltColumnCount;
    std::uint8_t allowInventory;
    std::uint16_t reserved;
};

struct PlanV1 {
    std::uint32_t structSize;
    std::uint32_t version;
    Destination destination;
    std::int32_t beltSlot;
};

using PlanFn = bool(__cdecl*)(const RequestV1*, PlanV1*) noexcept;
using BeginFn = bool(__cdecl*)(const RequestV1*, const PlanV1*) noexcept;
using EndFn = void(__cdecl*)() noexcept;
using StateFn = ProviderState(__cdecl*)() noexcept;

struct ApiV1 {
    std::uint32_t structSize;
    std::uint32_t version;
    StateFn state;
    PlanFn plan;
    BeginFn begin;
    EndFn end;
};

using GetApiFn = const ApiV1*(__cdecl*)(
    std::uint32_t requestedVersion,
    std::uint32_t callerStructSize) noexcept;

inline auto ValidRequest(const RequestV1* request) noexcept -> bool {
    if (!request || request->structSize != sizeof(RequestV1)
        || request->version != Version || !request->inventory || !request->item
        || request->itemGuid == UINT32_MAX
        || request->beltColumnCount > request->beltColumns.size()
        || request->allowInventory > 1 || request->reserved != 0) {
        return false;
    }
    std::array<bool, 4> seen{};
    for (std::uint8_t index = 0; index < request->beltColumnCount; ++index) {
        const auto column = request->beltColumns[index];
        if (column < 1 || column > 4 || seen[column - 1]) return false;
        seen[column - 1] = true;
    }
    return true;
}

inline auto ValidPlan(const PlanV1* plan) noexcept -> bool {
    if (!plan || plan->structSize != sizeof(PlanV1)
        || plan->version != Version) {
        return false;
    }
    if (plan->destination == Destination::Belt) {
        return plan->beltSlot >= 0 && plan->beltSlot < 16;
    }
    return plan->beltSlot == -1
        && (plan->destination == Destination::None
            || plan->destination == Destination::Inventory);
}

inline auto ValidApi(const ApiV1* api) noexcept -> bool {
    return api && api->structSize >= sizeof(ApiV1)
        && api->version == Version && api->state && api->plan
        && api->begin && api->end;
}

static_assert(sizeof(RequestV1) == 40 && sizeof(PlanV1) == 16
    && sizeof(ApiV1) == 40);
static_assert(std::is_standard_layout_v<RequestV1>
    && std::is_trivially_copyable_v<RequestV1>
    && std::is_standard_layout_v<PlanV1>
    && std::is_trivially_copyable_v<PlanV1>);

} // namespace RuffnecKk::AutoPickupRoute
