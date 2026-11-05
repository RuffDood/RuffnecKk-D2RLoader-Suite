#pragma once
#include "policy.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace AutoInteract {
struct Identity {
    std::uint32_t player{UINT32_MAX}, object{UINT32_MAX}, objectClass{UINT32_MAX};
    constexpr auto operator==(const Identity&) const noexcept -> bool = default;
    constexpr auto Valid() const noexcept -> bool { return player != UINT32_MAX && object != UINT32_MAX && objectClass != UINT32_MAX; }
};
struct Object {
    Identity id;
    Kind kind{};
    std::uint32_t mode{};
    std::int32_t distance{-1}; // Native units; 0 config retains shape reach, 1..20 uses explicit range.
    bool inReach{}, blocked{true}, locked{}, keysAvailable{}, validTrap{};
    bool targetable{true};
};
// Caller serializes state with lifecycle/unload. All persistent data are values,
// never native object/player pointers. Refusals, faults and no-progress successes
// share the same pre-call cooldown. A full cache never evicts a live cooldown.
class Engine {
public:
    static constexpr std::size_t Capacity = 1024;
    struct Counters { std::uint64_t attempts{}, completed{}, noProgress{}, cooling{}, saturated{}; } metrics;
    void Reset() noexcept { entries_ = {}; index_.fill(UINT16_MAX); lastNow_ = 0; }
    auto Choose(std::span<const Object> objects, const Config& config, std::uint64_t now) noexcept -> Identity {
        if (now < lastNow_) Reset();
        lastNow_ = now;
        index_.fill(UINT16_MAX);
        for (std::size_t i = 0; i < entries_.size(); ++i) if (entries_[i].used) {
            auto slot = Hash(entries_[i].id);
            while (index_[slot] != UINT16_MAX) slot = (slot + 1) % index_.size();
            index_[slot] = static_cast<std::uint16_t>(i);
        }
        // Confirm completion only from the same observed object/class, not the
        // return value of a native request. Missing streamed objects are unknown.
        for (const auto& o : objects) if (o.mode != 0) if (auto* e = Find(o.id); e && e->awaiting) { e->awaiting = false; ++metrics.completed; }
        for (auto& e : entries_) if (e.used && e.awaiting && now >= e.retryAt) { e.awaiting = false; ++metrics.noProgress; }
        Identity best;
        auto nearest = INT32_MAX;
        for (const auto& o : objects) {
            if (!Eligible(o, config)) continue;
            if (const auto* e = Find(o.id); e && now < e->retryAt) { ++metrics.cooling; continue; }
            if (o.distance < nearest || (o.distance == nearest && o.id.object < best.object)) {
                best = o.id; nearest = o.distance;
            }
        }
        return best;
    }
    auto Arm(Identity id, std::uint64_t now, std::uint32_t delayMs) noexcept -> bool {
        if (!id.Valid()) return false;
        Entry* free{};
        for (auto& e : entries_) {
            if (e.used && e.id == id) {
                if (now < e.retryAt) return false;
                free = &e; break;
            }
            if (!free && (!e.used || now >= e.retryAt)) free = &e;
        }
        if (!free) { ++metrics.saturated; return false; }
        auto max = (std::numeric_limits<std::uint64_t>::max)();
        *free = {id, now > max - delayMs ? max : now + delayMs, true, true};
        ++metrics.attempts; return true;
    }
    static auto Eligible(const Object& o, const Config& c) noexcept -> bool {
        return c.enabled && !c.validateOnly && o.id.Valid() && Selected(o.kind, c)
            && o.mode == 0 && o.distance >= 0 && o.targetable && !o.blocked && o.validTrap
            && (c.interactRange ? static_cast<std::uint32_t>(o.distance) <= c.interactRange : o.inReach)
            && (!o.locked || (o.kind == Kind::Chest && c.lockedChests && o.keysAvailable));
    }
private:
    struct Entry { Identity id; std::uint64_t retryAt{}; bool used{}, awaiting{}; };
    static constexpr std::size_t IndexSize = Capacity * 2;
    static auto Hash(Identity id) noexcept -> std::size_t {
        std::uint64_t h = std::uint64_t{id.player} * 0x9E3779B185EBCA87ULL ^ std::uint64_t{id.object} * 0xC2B2AE3D27D4EB4FULL ^ id.objectClass;
        h ^= h >> 33; return static_cast<std::size_t>(h) % IndexSize;
    }
    auto Find(Identity id) noexcept -> Entry* {
        auto slot = Hash(id);
        for (std::size_t probe = 0; probe < index_.size(); ++probe) {
            const auto index = index_[slot]; if (index == UINT16_MAX) return nullptr;
            if (entries_[index].id == id) return &entries_[index];
            slot = (slot + 1) % index_.size();
        }
        return nullptr;
    }
    std::array<Entry, Capacity> entries_{};
    std::array<std::uint16_t, IndexSize> index_{}; // Rebuilt once per scan; no allocations or object-pointer cache.
    std::uint64_t lastNow_{};
};
// One self-requeueing SDK callback per live session. Queue failures retire the
// ticket; a later lifecycle event can retry. Tickets prevent stale callbacks
// from touching a replacement session even when it reuses the same player ID.
class Schedule {
public:
    auto Enter(std::uint64_t generation, std::uint32_t player) noexcept -> std::uint64_t {
        if (active_ && generation_ == generation && player_ == player) return 0;
        Stop(); generation_ = generation; player_ = player;
        if (player == UINT32_MAX) return 0;
        active_ = true; return ticket_;
    }
    void Stop() noexcept { active_ = false; next_ = 0; ++ticket_; if (!ticket_) ++ticket_; }
    auto Current(std::uint64_t ticket) const noexcept -> bool { return active_ && ticket == ticket_; }
    auto Due(std::uint64_t ticket, std::uint64_t now, std::uint32_t interval) noexcept -> bool {
        if (!Current(ticket) || (now < next_ && now >= last_)) return false;
        last_ = now;
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        next_ = now > max - interval ? max : now + interval;
        return true;
    }
    auto Player() const noexcept -> std::uint32_t { return player_; }
private:
    bool active_{};
    std::uint64_t generation_{}, ticket_{1}, next_{}, last_{};
    std::uint32_t player_{UINT32_MAX};
};
}
