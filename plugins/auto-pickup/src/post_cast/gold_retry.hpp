#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Reimagined::EmbeddedTeleportPickup {
struct GoldWallet {
    std::int32_t carried{};
    std::uint32_t limit{};
    constexpr auto Valid() const noexcept -> bool {
        return carried >= 0 && limit > 0 && limit <= static_cast<std::uint32_t>(INT32_MAX);
    }
    constexpr auto operator==(const GoldWallet&) const noexcept -> bool = default;
};

// Player IDs and amounts only: pile IDs can change when the game re-drops gold.
// The runtime serializes access and clears the guard on session changes/unload.
class GoldRetryGuard {
public:
    static constexpr std::size_t Capacity = 32;
    static constexpr std::uint64_t RetryDelayMs = 1000;
    enum class Decision { Ready, Full, CoolingDown, Unavailable };

    void Reset() noexcept { slots_ = {}; }
    auto Check(std::uint32_t player, GoldWallet wallet, std::uint64_t now) noexcept -> Decision {
        if (player == UINT32_MAX || !wallet.Valid()) return Decision::Unavailable;
        auto& slot = Observe(player, wallet, now);
        if (static_cast<std::uint32_t>(wallet.carried) >= wallet.limit) {
            slot.retryAt = 0;
            return Decision::Full;
        }
        return now < slot.retryAt ? Decision::CoolingDown : Decision::Ready;
    }
    void Attempt(std::uint32_t player, GoldWallet wallet, std::uint64_t now) noexcept {
        if (player == UINT32_MAX || !wallet.Valid()) return;
        auto& slot = Observe(player, wallet, now);
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        slot.retryAt = now > maximum - RetryDelayMs ? maximum : now + RetryDelayMs;
    }
private:
    struct Slot {
        bool used{};
        std::uint32_t player{};
        GoldWallet wallet{};
        std::uint64_t lastSeen{}, retryAt{};
    };
    auto Observe(std::uint32_t player, GoldWallet wallet, std::uint64_t now) noexcept -> Slot& {
        Slot* selected{};
        for (auto& slot : slots_) if (slot.used && slot.player == player) { selected = &slot; break; }
        if (!selected) for (auto& slot : slots_) if (!slot.used) { selected = &slot; break; }
        if (!selected) {
            selected = &slots_[0];
            for (auto& slot : slots_) if (slot.lastSeen < selected->lastSeen) selected = &slot;
        }
        if (!selected->used || selected->player != player || selected->wallet != wallet || now < selected->lastSeen)
            *selected = {true, player, wallet, now, 0};
        selected->lastSeen = now;
        return *selected;
    }
    std::array<Slot, Capacity> slots_{};
};
}
