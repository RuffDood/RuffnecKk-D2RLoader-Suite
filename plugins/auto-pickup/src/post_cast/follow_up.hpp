#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace RuffnecKk::AutoPickup {
// Pure scheduling state, used under the runtime mutex. Keep player identities,
// never player pointers. Queued callbacks belong to the SDK's current session.
class FollowUpWindow {
public:
    static constexpr std::size_t Capacity = 32;
    static constexpr std::uint32_t InvalidId = UINT32_MAX;
    struct PollResult {
        std::uintptr_t game{};
        std::array<std::uint32_t, Capacity> players{};
        std::size_t count{};
        bool again{};
    };
    void Reset(std::uint64_t generation, bool active) noexcept {
        ++ticket_; generation_ = generation; active_ = active;
        queued_ = false; game_ = 0; nextScan_ = 0; slots_ = {};
    }
    void Cancel() noexcept { Reset(generation_, active_); }
    void Enter(std::uint64_t generation) noexcept {
        if (!active_ || generation != generation_) Reset(generation, true);
    }
    auto Arm(std::uintptr_t game, std::uint32_t player, std::uint64_t now,
             std::uint32_t interval, std::uint32_t window) noexcept -> std::uint64_t {
        if (!active_ || !game || player == InvalidId) return 0;
        if (game_ && game_ != game) Reset(generation_, true);
        Slot* chosen{};
        for (auto& slot : slots_) if (slot.until > now && slot.id == player) { chosen = &slot; break; }
        if (!chosen) for (auto& slot : slots_) if (slot.until <= now) { chosen = &slot; break; }
        if (!chosen) return 0;
        *chosen = {player, now + window}; game_ = game;
        if (queued_) return 0;
        queued_ = true; nextScan_ = now + interval;
        return ticket_;
    }
    auto Poll(std::uint64_t ticket, std::uint64_t now, std::uint32_t interval) noexcept -> PollResult {
        PollResult result{};
        if (!active_ || !queued_ || ticket != ticket_) return result;
        result.game = game_;
        for (auto& slot : slots_) if (slot.until > now) {
            result.again = true;
            if (now >= nextScan_) result.players[result.count++] = slot.id;
        }
        if (!result.again) { queued_ = false; game_ = 0; }
        else if (result.count) nextScan_ = now + interval;
        return result;
    }
    void QueueFailed(std::uint64_t ticket) noexcept {
        if (ticket == ticket_) queued_ = false;
    }
    auto IsCurrent(std::uint64_t ticket) const noexcept -> bool {
        return active_ && queued_ && ticket == ticket_;
    }
    auto IsActive() const noexcept -> bool { return active_; }
private:
    struct Slot { std::uint32_t id{}; std::uint64_t until{}; };
    std::array<Slot, Capacity> slots_{};
    std::uintptr_t game_{};
    std::uint64_t ticket_{1}, generation_{}, nextScan_{};
    bool active_{}, queued_{};
};
}
