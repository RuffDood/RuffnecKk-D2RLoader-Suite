#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace RuffnecKk::PotionRouting {

inline constexpr std::size_t MaximumInventoryCodes = 256;
inline constexpr std::size_t MaximumInventoryTypes = 128;
inline constexpr std::uint16_t NoInventoryMatch = UINT16_MAX;

enum class Family : std::uint8_t { Health, Mana, Rejuvenation, Unknown };

struct Item {
  std::string_view code;
  Family family;
  std::uint8_t tier;
};

inline constexpr std::array Items{
    Item{"hp1", Family::Health, 1},       Item{"hp2", Family::Health, 2},
    Item{"hp3", Family::Health, 3},       Item{"hp4", Family::Health, 4},
    Item{"hp5", Family::Health, 5},       Item{"mp1", Family::Mana, 1},
    Item{"mp2", Family::Mana, 2},         Item{"mp3", Family::Mana, 3},
    Item{"mp4", Family::Mana, 4},         Item{"mp5", Family::Mana, 5},
    Item{"rvs", Family::Rejuvenation, 1}, Item{"rvl", Family::Rejuvenation, 2},
};

constexpr auto PackItemCode(std::string_view code) noexcept -> std::uint32_t {
  std::uint32_t packed = 0x20202020U;
  for (std::size_t index = 0; index < 4 && index < code.size(); ++index) {
    packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[index]))
              << static_cast<std::uint32_t>(index * 8U);
  }
  return packed;
}

constexpr auto Classify(std::string_view code) noexcept -> Item {
  for (const auto &item : Items)
    if (item.code == code)
      return item;
  return {code, Family::Unknown, 0};
}

struct Policy {
  bool enabled{};
  std::array<bool, 6> tiers{};
  std::array<bool, 6> inventoryFallback{};
  std::array<std::uint8_t, 4> columns{};
  std::uint8_t columnCount{};

  constexpr auto Accepts(Item item) const noexcept -> bool {
    return enabled && item.family != Family::Unknown &&
           item.tier < tiers.size() && tiers[item.tier];
  }
  constexpr auto AllowsInventory(Item item) const noexcept -> bool {
    return Accepts(item) && item.tier < inventoryFallback.size() &&
           inventoryFallback[item.tier];
  }
};

struct BeltSlot {
  bool occupied{};
  Family family{Family::Unknown};
};

struct RoutingToken {
  static constexpr std::uint32_t InvalidGuid =
      std::numeric_limits<std::uint32_t>::max();
  std::uint32_t itemGuid{InvalidGuid};
  constexpr auto Matches(std::uint32_t actual) const noexcept -> bool {
    return itemGuid != InvalidGuid && itemGuid == actual;
  }
  constexpr void Reset() noexcept { itemGuid = InvalidGuid; }
};

constexpr auto ChooseBeltSlot(const Policy &policy, Item item,
                              const std::array<BeltSlot, 16> &slots,
                              std::uint8_t capacity) noexcept -> std::int8_t {
  if (!policy.Accepts(item) || capacity < 4 || capacity > slots.size() ||
      capacity % 4 != 0)
    return -1;
  const auto rows = static_cast<std::uint8_t>(capacity / 4);
  for (std::uint8_t index = 0; index < policy.columnCount; ++index) {
    const auto column = policy.columns[index];
    if (column < 1 || column > 4)
      continue;
    const auto bottom = static_cast<std::uint8_t>(column - 1);
    if (!slots[bottom].occupied || slots[bottom].family != item.family)
      continue;
    for (std::uint8_t row = 1; row < rows; ++row) {
      const auto slot = static_cast<std::uint8_t>(bottom + row * 4);
      if (!slots[slot].occupied)
        return static_cast<std::int8_t>(slot);
    }
  }
  for (std::uint8_t index = 0; index < policy.columnCount; ++index) {
    const auto column = policy.columns[index];
    if (column >= 1 && column <= 4 && !slots[column - 1].occupied) {
      return static_cast<std::int8_t>(column - 1);
    }
  }
  return -1;
}

struct Route {
  std::array<std::uint8_t, 4> columns{};
  std::uint8_t count{};
  bool inventoryFallback{};
  bool operator==(const Route &) const = default;
};
struct Config {
  std::array<Route, 3> families{};
  bool operator==(const Config &) const = default;
};
inline constexpr std::array<const char *, 3> Sections{
    "potion_routing.health", "potion_routing.mana",
    "potion_routing.rejuvenation"};
inline constexpr std::array<const char *, 3> FamilyNames{"Healing", "Mana",
                                                         "Rejuvenation"};
inline auto ClassifyPackedCode(std::uint32_t packed) noexcept -> Item {
  for (const auto &item : Items)
    if (PackItemCode(item.code) == packed)
      return item;
  return {{}, Family::Unknown, 0};
}
inline auto AsPolicy(const Route &route) noexcept -> Policy {
  Policy p;
  p.enabled = true;
  p.tiers.fill(true);
  p.inventoryFallback.fill(route.inventoryFallback);
  p.columns = route.columns;
  p.columnCount = route.count;
  return p;
}
inline auto ColumnsText(const Route &route) -> std::string {
  std::string out = "[";
  for (std::uint8_t i = 0; i < route.count; ++i) {
    if (i)
      out += ", ";
    out += std::to_string(route.columns[i]);
  }
  return out + "]";
}
inline auto ParseColumns(std::string_view value, Route &route) -> bool {
  auto trim = [](std::string_view s) {
    while (!s.empty() &&
           (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
      s.remove_prefix(1);
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
      s.remove_suffix(1);
    return s;
  };
  value = trim(value);
  if (value.size() < 2 || value.front() != '[' || value.back() != ']')
    return false;
  value = trim(value.substr(1, value.size() - 2));
  Route next;
  next.inventoryFallback = route.inventoryFallback;
  std::array<bool, 5> seen{};
  while (!value.empty()) {
    const auto comma = value.find(',');
    const auto part = trim(value.substr(0, comma));
    if (part.size() != 1 || part[0] < '1' || part[0] > '4' || next.count == 4)
      return false;
    const auto column = static_cast<std::uint8_t>(part[0] - '0');
    if (seen[column])
      return false;
    seen[column] = true;
    next.columns[next.count++] = column;
    if (comma == std::string_view::npos)
      break;
    value = trim(value.substr(comma + 1));
    if (value.empty())
      return false;
  }
  route = next;
  return true;
}
inline auto ToggleValue(const Config &config, std::size_t index) -> bool {
  if (index >= 15)
    return false;
  const auto &r = config.families[index / 5];
  const auto column = index % 5;
  if (column == 4)
    return r.inventoryFallback;
  for (std::uint8_t i = 0; i < r.count; ++i)
    if (r.columns[i] == column + 1)
      return true;
  return false;
}
inline void SetToggle(Config &config, std::size_t index, bool on) {
  if (index >= 15)
    return;
  auto &r = config.families[index / 5];
  const auto column = static_cast<std::uint8_t>(index % 5 + 1);
  if (column == 5) {
    r.inventoryFallback = on;
    return;
  }
  std::uint8_t found = r.count;
  for (std::uint8_t i = 0; i < r.count; ++i)
    if (r.columns[i] == column)
      found = i;
  if (on && found == r.count && r.count < 4)
    r.columns[r.count++] = column;
  if (!on && found < r.count) {
    for (auto i = found; i + 1 < r.count; ++i)
      r.columns[i] = r.columns[i + 1];
    r.columns[--r.count] = 0;
  }
}
inline auto Label(std::size_t index) -> std::string {
  return std::string(FamilyNames[index / 5]) +
         (index % 5 == 4 ? " - allow inventory overflow"
                         : " - belt column " + std::to_string(index % 5 + 1));
}
} // namespace RuffnecKk::PotionRouting
