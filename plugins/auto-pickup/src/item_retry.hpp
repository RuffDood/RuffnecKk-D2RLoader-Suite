#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace RuffnecKk::AutoPickup {
struct PickupIdentity {
    std::uint32_t player{}, item{}, code{};
    constexpr auto Valid() const noexcept -> bool { return player != UINT32_MAX && item != UINT32_MAX; }
    constexpr auto operator==(const PickupIdentity&) const noexcept -> bool = default;
};

// Short-lived identities only. The caller serializes access; no native pointers
// survive a request. Do not evict live entries under pressure and re-enable spam.
class ItemRetryCooldown {
public:
    static constexpr std::size_t Capacity = 1024;
    static constexpr std::uint64_t DelayMs = 1000;
    void Reset() noexcept { entries_ = {}; lastNow_ = 0; }
    auto Ready(PickupIdentity key, std::uint64_t now) noexcept -> bool {
        ObserveClock(now);
        if (!key.Valid()) return false;
        for (const auto& entry : entries_)
            if (entry.used && entry.key == key) return now >= entry.retryAt;
        return true;
    }
    auto TryArm(PickupIdentity key, std::uint64_t now) noexcept -> bool {
        ObserveClock(now);
        if (!key.Valid()) return false;
        Entry* available{};
        for (auto& entry : entries_) {
            if (entry.used && entry.key == key) {
                if (now < entry.retryAt) return false;
                available = &entry; break;
            }
            if (!available && (!entry.used || now >= entry.retryAt)) available = &entry;
        }
        // A saturated cache pauses new requests until an entry expires rather
        // than discarding a still-active cooldown. Normal scans keep running.
        if (!available) return false;
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        *available = {true, key, now > maximum - DelayMs ? maximum : now + DelayMs};
        return true;
    }
private:
    struct Entry { bool used{}; PickupIdentity key{}; std::uint64_t retryAt{}; };
    void ObserveClock(std::uint64_t now) noexcept {
        if (now < lastNow_) Reset();
        lastNow_ = now;
    }
    std::array<Entry, Capacity> entries_{};
    std::uint64_t lastNow_{};
};
}
