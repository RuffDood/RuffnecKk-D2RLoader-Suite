#include "module.hpp"
#include "auto_pickup_route_api.hpp"
#include <Windows.h>
#include <atomic>
namespace RuffnecKk::PotionRouting {
namespace {
constexpr std::uintptr_t UnitIdRva = 0x34A330;
constexpr std::uintptr_t PickupRva = 0x471950;
constexpr std::uintptr_t GetItemCodeRva = 0x36EF50;
constexpr std::uintptr_t GetInventoryRva = 0x34A360;
constexpr std::uintptr_t ResolveOccupancyGridRva = 0x38B070;
constexpr std::uintptr_t GetBeltTypeRva = 0x349720;
constexpr std::uintptr_t GetFreeBeltSlotRva = 0x3862D0;
constexpr std::uintptr_t BodyGridInfoRva = 0x237B620;
constexpr std::uintptr_t BeltGridInfoRva = 0x237B638;
constexpr std::uint8_t BeltBodySlot = 8;

constexpr std::array<std::uint8_t, 32> GetFreeBeltSlotExpected{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x81, 0xEC, 0x70, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xDF,
    0x4F, 0x64, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24,
};
constexpr std::array<std::uint8_t, 32> ResolveOccupancyGridExpected{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x57, 0x48, 0x83, 0xEC,
    0x30, 0x49, 0x8B, 0xF8, 0x48, 0x39, 0x51, 0x28, 0x0F, 0x86, 0x01,
    0x01, 0x00, 0x00, 0x49, 0x89, 0x73, 0x18, 0x48, 0x8D, 0x71,
};
constexpr std::array<std::uint8_t, 32> GetInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xF1, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30,
    0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetBeltTypeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x15, 0x88, 0x4C, 0x24, 0x30,
    0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xE0, 0xC0, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 42> OccupancyLayoutExpected{
    0x0F, 0xB6, 0x07, 0x88, 0x43, 0x10, 0x0F, 0xB6, 0x47, 0x01, 0x88,
    0x43, 0x11, 0xE8, 0x96, 0x5A, 0x69, 0x00, 0x0F, 0xB6, 0x4B, 0x10,
    0x0F, 0xB6, 0x53, 0x11, 0x48, 0x0F, 0xAF, 0xD1, 0x48, 0x8B, 0xC8,
    0xE8, 0x72, 0x7B, 0xFF, 0xFF, 0x48, 0x89, 0x43, 0x18,
};
constexpr std::array<std::uint8_t, 35> BodyGridLayoutExpected{
    0x4C, 0x8D, 0x05, 0xDC, 0x51, 0xFF, 0x01, 0x33, 0xD2, 0x48, 0x8B, 0xCF,
    0xE8, 0x22, 0x4C, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x75, 0x05, 0x8D, 0x58,
    0x02, 0xEB, 0x19, 0x48, 0x8B, 0x40, 0x18, 0x48, 0x8B, 0x48, 0x40,
};
constexpr std::array<std::uint8_t, 45> BeltGridLayoutExpected{
    0x4C, 0x8D, 0x05, 0x8A, 0x51, 0xFF, 0x01, 0xBA, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xCF, 0xE8, 0xB5, 0x4B, 0x00, 0x00, 0x4C, 0x8B, 0xF0, 0x48,
    0x85, 0xC0, 0x0F, 0x84, 0x52, 0xFE, 0xFF, 0xFF, 0x33, 0xF6, 0x8B, 0xDE,
    0x8B, 0xFE, 0x0F, 0x1F, 0x00, 0x49, 0x8B, 0x4E, 0x18,
};

using UnitIntFn = std::uint32_t(__fastcall *)(void *);
using PickupFn = bool(__fastcall *)(void *, std::uint32_t, bool, std::uint32_t,
                                    bool, bool);
using GetItemCodeFn = std::uint32_t(__fastcall *)(void *);
using GetInventoryFn = void *(__fastcall *)(void *);
using GetBeltTypeFn = std::int32_t(__fastcall *)(void *);
using ResolveOccupancyGridFn = void *(__fastcall *)(void *, std::uint64_t,
                                                    const void *);
using GetFreeBeltSlotFn = std::int32_t(__fastcall *)(void *, void *,
                                                     std::int32_t *,
                                                     bool) noexcept;

const D2RL::PluginContext *Context{};
std::uint8_t *Base{};
GetFreeBeltSlotFn OriginalGetFreeBeltSlot{};
GetInventoryFn GetInventory{};
GetBeltTypeFn GetBeltType{};
ResolveOccupancyGridFn ResolveOccupancyGrid{};
GetItemCodeFn GetItemCode{};
UnitIntFn UnitId{};
PickupFn NativePickup{};
struct {
  std::atomic<std::uint64_t> routeMatches{}, routeMismatches{};
} Metrics;
bool Ready{};
thread_local bool Inside{};
thread_local void *ForcedInventory{};
thread_local RoutingToken ForcedRoute{};
thread_local std::int32_t ForcedBeltSlot{-1};
thread_local bool ForceInventoryFallback{};
thread_local HMODULE RouteAdvisorModule{};
template <class F> auto At(std::uintptr_t rva) noexcept -> F {
  return reinterpret_cast<F>(Base + rva);
}
void ResetRoutingScope() noexcept {
  Inside = false;
  ForcedInventory = nullptr;
  ForcedRoute.Reset();
  ForcedBeltSlot = -1;
  ForceInventoryFallback = false;
}
enum class RouteAdvisorStatus : std::uint8_t {
  Absent,
  Inactive,
  Ready,
  Incompatible,
};

struct RouteAdvisorResolution {
  RouteAdvisorStatus status{RouteAdvisorStatus::Absent};
  const AutoPickupRoute::ApiV1 *api{};
};

auto ResolveRouteAdvisor() noexcept -> RouteAdvisorResolution {
  HMODULE module{};
  if (!GetModuleHandleExW(0, L"d2rl-ruffneckk-stack-manager.dll", &module)) {
    return {};
  }
  // Hold the provider module until the enclosing scan has completed every
  // plan/begin/end call, including structured-exception cleanup.
  RouteAdvisorModule = module;
  const auto getApi = reinterpret_cast<AutoPickupRoute::GetApiFn>(
      GetProcAddress(module, AutoPickupRoute::ExportName));
  if (!getApi)
    return {RouteAdvisorStatus::Incompatible, nullptr};
  const auto *api =
      getApi(AutoPickupRoute::Version, sizeof(AutoPickupRoute::ApiV1));
  if (!AutoPickupRoute::ValidApi(api)) {
    return {RouteAdvisorStatus::Incompatible, nullptr};
  }
  const auto state = api->state();
  if (state == AutoPickupRoute::ProviderState::Ready) {
    return {RouteAdvisorStatus::Ready, api};
  }
  if (state == AutoPickupRoute::ProviderState::Inactive) {
    return {RouteAdvisorStatus::Inactive, api};
  }
  return {RouteAdvisorStatus::Incompatible, nullptr};
}

void ReleaseRouteAdvisor() noexcept {
  const auto module = RouteAdvisorModule;
  RouteAdvisorModule = nullptr;
  if (module)
    FreeLibrary(module);
}

auto ReadBeltState(void *inventory, std::array<BeltSlot, 16> &slots,
                   std::uint8_t &capacity) noexcept -> bool {
  __try {
    if (!inventory || !GetBeltType || !ResolveOccupancyGrid || !GetItemCode)
      return false;
    auto *bodyGrid = static_cast<std::uint8_t *>(
        ResolveOccupancyGrid(inventory, 0, Base + BodyGridInfoRva));
    if (!bodyGrid)
      return false;
    const auto bodyCells = static_cast<std::uint32_t>(bodyGrid[0x10]) *
                           static_cast<std::uint32_t>(bodyGrid[0x11]);
    if (bodyCells <= BeltBodySlot || bodyCells > 64)
      return false;
    auto **bodyItems = *reinterpret_cast<void ***>(bodyGrid + 0x18);
    if (!bodyItems)
      return false;
    switch (const auto beltType = bodyItems[BeltBodySlot]
                                      ? GetBeltType(bodyItems[BeltBodySlot])
                                      : 2) {
    case 0:
    case 5:
      capacity = 12;
      break;
    case 1:
    case 4:
      capacity = 8;
      break;
    case 2:
      capacity = 4;
      break;
    case 3:
    case 6:
      capacity = 16;
      break;
    default:
      return false;
    }
    auto *beltGrid = static_cast<std::uint8_t *>(
        ResolveOccupancyGrid(inventory, 1, Base + BeltGridInfoRva));
    if (!beltGrid)
      return false;
    const auto cells = static_cast<std::uint32_t>(beltGrid[0x10]) *
                       static_cast<std::uint32_t>(beltGrid[0x11]);
    if (cells < capacity || cells > slots.size())
      return false;
    auto **items = *reinterpret_cast<void ***>(beltGrid + 0x18);
    if (!items)
      return false;
    for (std::uint8_t index = 0; index < capacity; ++index)
      if (items[index]) {
        slots[index].occupied = true;
        slots[index].family =
            ClassifyPackedCode(GetItemCode(items[index])).family;
      }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
auto ReadUnitId(void *unit) noexcept -> std::uint32_t {
  __try {
    return unit && UnitId ? UnitId(unit) : RoutingToken::InvalidGuid;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return RoutingToken::InvalidGuid;
  }
}
auto __fastcall HookGetFreeBeltSlot(void *inventory, void *item,
                                    std::int32_t *freeSlot,
                                    bool allowAnyBeltable) noexcept
    -> std::int32_t {
  if (Inside) {
    const auto itemGuid = ReadUnitId(item);
    if (inventory == ForcedInventory && ForcedRoute.Matches(itemGuid)) {
      Metrics.routeMatches.fetch_add(1, std::memory_order_relaxed);
      if (ForceInventoryFallback) {
        if (freeSlot)
          *freeSlot = -1;
        return 0;
      }
      if (freeSlot && ForcedBeltSlot >= 0 && ForcedBeltSlot < 16) {
        *freeSlot = ForcedBeltSlot;
        return 1;
      }
      return 0;
    }
    Metrics.routeMismatches.fetch_add(1, std::memory_order_relaxed);
  }
  return OriginalGetFreeBeltSlot(inventory, item, freeSlot, allowAnyBeltable);
}

auto Validate() noexcept -> bool {
  bool valid = true;
  valid = Context->CheckExpectedBytes(
              GetFreeBeltSlotRva, GetFreeBeltSlotExpected.data(),
              static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size())) &&
          valid;
  valid =
      Context->CheckExpectedBytes(
          ResolveOccupancyGridRva, ResolveOccupancyGridExpected.data(),
          static_cast<std::uint32_t>(ResolveOccupancyGridExpected.size())) &&
      valid;
  valid = Context->CheckExpectedBytes(
              0x38B108, OccupancyLayoutExpected.data(),
              static_cast<std::uint32_t>(OccupancyLayoutExpected.size())) &&
          valid;
  valid = Context->CheckExpectedBytes(
              0x38643D, BodyGridLayoutExpected.data(),
              static_cast<std::uint32_t>(BodyGridLayoutExpected.size())) &&
          valid;
  valid = Context->CheckExpectedBytes(
              0x3864A7, BeltGridLayoutExpected.data(),
              static_cast<std::uint32_t>(BeltGridLayoutExpected.size())) &&
          valid;
  valid = Context->CheckExpectedBytes(
              GetInventoryRva, GetInventoryExpected.data(),
              static_cast<std::uint32_t>(GetInventoryExpected.size())) &&
          valid;
  return Context->CheckExpectedBytes(
             GetBeltTypeRva, GetBeltTypeExpected.data(),
             static_cast<std::uint32_t>(GetBeltTypeExpected.size())) &&
         valid;
}

struct Plan {
  void *inventory{};
  std::uint32_t guid{};
  std::int32_t slot{-1};
  bool fallback{};
  const AutoPickupRoute::ApiV1 *advisor{};
  AutoPickupRoute::PlanV1 native{};
};
auto Build(void *player, void *item, const Route &route, Plan &plan) -> bool {
  if (!Ready || !player || !item)
    return false;
  const auto potion = ClassifyPackedCode(GetItemCode(item));
  if (potion.family == Family::Unknown)
    return false;
  plan.inventory = GetInventory(player);
  plan.guid = ReadUnitId(item);
  if (!plan.inventory || plan.guid == RoutingToken::InvalidGuid)
    return false;
  const auto resolution = ResolveRouteAdvisor();
  if (resolution.status == RouteAdvisorStatus::Incompatible)
    return false;
  if (resolution.status == RouteAdvisorStatus::Ready) {
    plan.advisor = resolution.api;
    const AutoPickupRoute::RequestV1 request{
        sizeof(AutoPickupRoute::RequestV1),
        AutoPickupRoute::Version,
        plan.inventory,
        item,
        plan.guid,
        route.columns,
        route.count,
        static_cast<std::uint8_t>(route.inventoryFallback),
        0};
    if (!plan.advisor->plan(&request, &plan.native) ||
        !AutoPickupRoute::ValidPlan(&plan.native))
      return false;
    plan.slot = plan.native.destination == AutoPickupRoute::Destination::Belt
                    ? plan.native.beltSlot
                    : -1;
    plan.fallback =
        plan.native.destination == AutoPickupRoute::Destination::Inventory;
  } else {
    std::array<BeltSlot, 16> belt{};
    std::uint8_t capacity{};
    if (!ReadBeltState(plan.inventory, belt, capacity))
      return false;
    plan.slot = ChooseBeltSlot(AsPolicy(route), potion, belt, capacity);
    plan.fallback = plan.slot < 0 && route.inventoryFallback;
  }
  return plan.slot >= 0 || plan.fallback;
}
} // namespace
auto Prepare(const D2RL::PluginContext *context) noexcept -> bool {
  Stop();
  Context = context;
  Base = reinterpret_cast<std::uint8_t *>(context->exeBase);
  if (!Base || !Validate())
    return false;
  GetInventory = At<GetInventoryFn>(GetInventoryRva);
  GetBeltType = At<GetBeltTypeFn>(GetBeltTypeRva);
  ResolveOccupancyGrid = At<ResolveOccupancyGridFn>(ResolveOccupancyGridRva);
  GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
  UnitId = At<UnitIntFn>(UnitIdRva);
  NativePickup = At<PickupFn>(PickupRva);
  return true;
}
auto Activate() noexcept -> bool {
  Ready = Context->InstallInlineHook(
      GetFreeBeltSlotRva, GetFreeBeltSlotExpected.data(),
      static_cast<std::uint32_t>(GetFreeBeltSlotExpected.size()),
      HookGetFreeBeltSlot, &OriginalGetFreeBeltSlot);
  return Ready;
}
void Stop() noexcept {
  Ready = false;
  ResetRoutingScope();
  ReleaseRouteAdvisor();
  OriginalGetFreeBeltSlot = nullptr;
  Context = nullptr;
  Base = nullptr;
  GetInventory = nullptr;
  GetBeltType = nullptr;
  ResolveOccupancyGrid = nullptr;
  GetItemCode = nullptr;
  UnitId = nullptr;
  NativePickup = nullptr;
}
auto CanRoute(void *player, void *item, const Route &route) noexcept -> bool {
  bool result{};
  __try {
    __try {
      Plan p;
      result = Build(player, item, route, p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      result = false;
    }
  } __finally {
    ReleaseRouteAdvisor();
  }
  return result;
}
auto Pickup(void *player, void *item, std::uint32_t guid,
            std::uint32_t distance, const Route &route) noexcept -> bool {
  bool result{}, begun{};
  Plan p;
  __try {
    __try {
      if (!Build(player, item, route, p) || p.guid != guid)
        __leave;
      if (p.advisor) {
        const AutoPickupRoute::RequestV1 request{
            sizeof(AutoPickupRoute::RequestV1),
            AutoPickupRoute::Version,
            p.inventory,
            item,
            guid,
            route.columns,
            route.count,
            static_cast<std::uint8_t>(route.inventoryFallback),
            0};
        begun = p.advisor->begin(&request, &p.native);
        if (!begun)
          __leave;
      }
      Inside = true;
      ForcedInventory = p.inventory;
      ForcedRoute.itemGuid = guid;
      ForcedBeltSlot = p.slot;
      ForceInventoryFallback = p.fallback;
      result = NativePickup(player, guid, true, distance, true, false);
    } __finally {
      ResetRoutingScope();
      if (begun)
        p.advisor->end();
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    result = false;
  }
  ReleaseRouteAdvisor();
  return result;
}
} // namespace RuffnecKk::PotionRouting
