#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace AutoInteract {
struct Config {
    bool enabled{}, validateOnly{}, chests{}, corpses{}, rocks{};
    bool weaponRacks{}, armorRacks{}, lockedChests{}, diagnostics{};
    std::uint32_t intervalMs{100}, retryMs{1000}, interactRange{}; // 0 = native reach; 1..20 uses explicit distance.
};
inline auto Trim(std::string_view s) noexcept -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
inline auto ParseConfig(std::string_view input, Config& output, std::string& error) -> bool {
    constexpr std::array<std::string_view, 12> keys{"enabled", "validate_only", "chests", "corpses", "rocks",
        "weapon_racks", "armor_racks", "locked_chests", "diagnostics_enabled", "interval_ms", "retry_ms", "interact_range"};
    std::array<bool, keys.size()> seen{};
    Config parsed;
    bool metadata{};
    while (!input.empty()) {
        auto end = input.find('\n');
        auto line = input.substr(0, end);
        input = end == std::string_view::npos ? std::string_view{} : input.substr(end + 1);
        line = Trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        if (line == "[d2rl]") {
            if (metadata) { error = "duplicate d2rl section"; return false; }
            metadata = true; continue;
        }
        if (line.front() == '[') { error = "unknown section"; return false; }
        if (metadata) continue; // Reserved loader-owned metadata.
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) { error = "expected key=value"; return false; }
        const auto key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        std::size_t k{};
        for (; k < keys.size() && keys[k] != key; ++k) {}
        if (k == keys.size() || seen[k]) { error = "unknown or duplicate key: " + std::string(key); return false; }
        seen[k] = true;
        if (k < 9) {
            if (value != "true" && value != "false") { error = "expected boolean: " + std::string(key); return false; }
            const bool v = value == "true";
            switch (k) {
            case 0: parsed.enabled = v; break; case 1: parsed.validateOnly = v; break;
            case 2: parsed.chests = v; break; case 3: parsed.corpses = v; break;
            case 4: parsed.rocks = v; break; case 5: parsed.weaponRacks = v; break;
            case 6: parsed.armorRacks = v; break; case 7: parsed.lockedChests = v; break;
            case 8: parsed.diagnostics = v; break;
            }
        } else {
            std::uint32_t v{};
            const auto [last, code] = std::from_chars(value.data(), value.data() + value.size(), v);
            if (value.empty() || code != std::errc{} || last != value.data() + value.size()) {
                error = "expected integer: " + std::string(key); return false;
            }
            if (k == 9) parsed.intervalMs = v; else if (k == 10) parsed.retryMs = v; else parsed.interactRange = v;
        }
    }
    if (parsed.intervalMs < 50 || parsed.intervalMs > 1000 || parsed.retryMs < 1000 || parsed.retryMs > 60000 || parsed.interactRange > 20) {
        error = "interval_ms must be 50..1000; retry_ms 1000..60000; interact_range 0..20 (0=native reach)"; return false;
    }
    output = parsed; error.clear(); return true;
}

enum class Kind : std::uint8_t { Excluded, Chest, Corpse, Rock, WeaponRack, ArmorRack };
// Stable, unlocalized ObjectsTxt Class keys supplement the verified dispatch
// pair for types sharing callback 14. No numeric object IDs or display names.
struct Semantics {
    std::string_view key;
    std::uint16_t init{}, operate{};
    std::uint8_t subclass{};
    bool selectable{}, door{}, verifiedCallbacks{};
};
constexpr auto Classify(const Semantics& s) noexcept -> Kind {
    if (!s.verifiedCallbacks || !s.selectable || s.door || (s.subclass & 0xF7) != 0) return Kind::Excluded;
    if ((s.init == 3 || s.init == 57) && s.operate == 4) return Kind::Chest;
    if (s.init == 2 && s.operate == 4 && s.key == "SewerChest") return Kind::Chest;
    if (s.init == 0 && s.operate == 19) return Kind::ArmorRack;
    if (s.init == 0 && s.operate == 20) return Kind::WeaponRack;
    // Ordinary scenery loot uses several callbacks, not only chest operation 4.
    // Keep explicit Class keys where callback/init pairs also serve other uses.
    if (s.operate == 3) {
        if (s.init == 33 && s.key == "DungeonRockPile") return Kind::Rock;
        if (s.init == 33 && s.key == "DungeonBasket") return Kind::Chest;
        if (s.init == 0 && s.key == "SewerDungeonBody") return Kind::Corpse;
        constexpr std::array breakables{"Urn1", "Urn2", "Urn3", "Urn4", "Urn5", "Basket1", "Basket2",
            "EJar1", "EJar2", "EJar3", "IceCaveJar1", "IceCaveJar2", "IceCaveJar3", "IceCaveJar4", "IceCaveJar5", "Box1", "Box2"};
        if (s.init == 2) for (auto key : breakables) if (s.key == key) return Kind::Chest;
        return Kind::Excluded;
    }
    if (s.operate == 5 && s.init == 0 && s.key == "Barrel") return Kind::Chest;
    if (s.operate == 51 && s.init == 2 && (s.key == "JunglePillar0" || s.key == "JunglePillar1"
        || s.key == "JunglePillar2" || s.key == "JunglePillar3")) return Kind::Chest;
    if (s.operate != 14 || (s.init != 0 && s.init != 2)) return Kind::Excluded;
    if (s.init == 0 && (s.key == "RockC" || s.key == "RockD")) return Kind::Rock;
    constexpr std::array containers{"Crate", "InnerHellObject1", "InnerHellObject2", "InnerHellObject3", "OuterHellObject1",
        "JugOutdoor1", "JugOutdoor2", "Rock", "HiddenStash", "HidingSpot", "Log", "DesertCoffin",
        "MephistoPillar1", "MephistoPillar2", "MephistoPillar3", "Pillar2", "Cocoon1", "Cocoon2",
        "SkullPile", "HellObject2", "SewerRatNest", "WUmPus", "HellBonePile", "Barrel3"};
    for (auto key : containers) if (s.key == key) return Kind::Chest;
    constexpr std::array corpses{"RogueCorpse1", "RogueCorpse2", "RogueRollingCorpse1", "RogueStakedCorpse1",
        "RogueStakedCorpse2", "DeadPalaceGuard", "Corpse3", "CorpseSkeleton", "GuardOnAStick",
        "GuardCorpse2Act2", "Corpse1Act3", "Corpse2Act3", "BurnedBody1Act1",
        "HaremGuard1", "HaremGuard2", "HaremGuard3", "HaremGuard4", "OuterHellSkeleton"};
    for (auto key : corpses) if (s.key == key) return Kind::Corpse;
    return Kind::Excluded;
}
constexpr auto Selected(Kind k, const Config& c) noexcept -> bool {
    switch (k) {
    case Kind::Chest: return c.chests; case Kind::Corpse: return c.corpses;
    case Kind::Rock: return c.rocks; case Kind::WeaponRack: return c.weaponRacks;
    case Kind::ArmorRack: return c.armorRacks; default: return false;
    }
}
// Scan only neutral/town-neutral players. Waiting until walking/casting finishes
// avoids changing a path, replacing a target or cancelling any current action.
constexpr auto PlayerReady(std::uint32_t mode) noexcept -> bool { return mode == 1 || mode == 5; }
}
