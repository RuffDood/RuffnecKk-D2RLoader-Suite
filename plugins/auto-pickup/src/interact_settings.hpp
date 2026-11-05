#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

// Internal settings model and callbacks, owned by Pickit. No DLL export or lookup.
namespace Reimagined::InteractSettings {
inline constexpr std::uint32_t Version = 1;
enum Flag : std::uint32_t { Enabled = 1, Chests = 2, Corpses = 4, Rocks = 8, WeaponRacks = 16, ArmorRacks = 32, LockedChests = 64 };
enum State : std::uint32_t { Live = 1, ValidationOnly = 2, RestartRequired = 4, StartupBlocked = 8 };
struct Values {
    std::uint32_t structSize{sizeof(Values)}, flags{}, range{}, intervalMs{100}, retryMs{1000}, state{};
    std::uint64_t revision{};
};
enum class Result : std::uint32_t { Success, Unavailable, Invalid, Conflict, WriteFailed, Incompatible, RestartRequired };
struct Api {
    std::uint32_t structSize, version;
    Result(__cdecl* read)(Values*) noexcept;
    Result(__cdecl* save)(const Values* expected, const Values* draft, char* error, std::uint32_t capacity) noexcept;
};
inline constexpr std::array<const char*, 7> ToggleLabels{"Enable Auto Interact", "Loot containers", "Searchable corpses",
    "Rocks and rock piles", "Weapon racks", "Armor racks", "Open locked chests (uses keys)"};
inline constexpr auto Valid(const Values& v) noexcept -> bool {
    return v.structSize >= sizeof(Values) && !(v.flags & ~127U) && v.range <= 20
        && v.intervalMs >= 50 && v.intervalMs <= 1000 && v.retryMs >= 1000 && v.retryMs <= 60000;
}
inline constexpr auto Same(const Values& a, const Values& b) noexcept -> bool {
    return a.flags == b.flags && a.range == b.range && a.intervalMs == b.intervalMs && a.retryMs == b.retryMs;
}
inline auto Valid(const Api* api) noexcept -> bool { return api && api->structSize >= sizeof(Api) && api->version == Version && api->read && api->save; }
static_assert(sizeof(Values) == 32 && sizeof(Api) == 24 && std::is_trivially_copyable_v<Values>);
}
