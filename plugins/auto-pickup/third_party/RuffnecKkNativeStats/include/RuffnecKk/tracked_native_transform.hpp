#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace RuffnecKk::TrackedNativeTransform {

enum class State { Unchanged, Tracked, Untracked };
enum class Kind { Unknown, BytePatch, InlineHook, Multiple };

class OwnerPluginId final {
public:
    static constexpr std::size_t Capacity = 127;

    constexpr OwnerPluginId() noexcept = default;
    constexpr OwnerPluginId(std::string_view value) noexcept {
        (void)Assign(value);
    }
    template <std::size_t Size>
    constexpr OwnerPluginId(const char (&value)[Size]) noexcept {
        static_assert(Size > 0);
        (void)Assign({value, Size - 1});
    }

    constexpr auto operator=(std::string_view value) noexcept
            -> OwnerPluginId& {
        (void)Assign(value);
        return *this;
    }
    template <std::size_t Size>
    constexpr auto operator=(const char (&value)[Size]) noexcept
            -> OwnerPluginId& {
        static_assert(Size > 0);
        (void)Assign({value, Size - 1});
        return *this;
    }

    [[nodiscard]] constexpr auto Assign(std::string_view value) noexcept
            -> bool {
        if (value.size() > Capacity) return false;
        for (std::size_t index{}; index < value.size(); ++index) {
            storage_[index] = value[index];
        }
        size_ = value.size();
        return true;
    }

    [[nodiscard]] constexpr auto View() const noexcept -> std::string_view {
        return {storage_.data(), size_};
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return View();
    }

    friend constexpr auto operator==(
            const OwnerPluginId& actual,
            std::string_view expected) noexcept -> bool {
        return actual.View() == expected;
    }

private:
    std::array<char, Capacity> storage_{};
    std::size_t size_{};
};

struct Observation {
    State state;
    Kind kind;
    std::uint32_t ownerCount;
    OwnerPluginId ownerPluginId;
    bool targetExecutable;
    bool structuralWitnessesMatch;
};

enum class Admission { Pristine, TrackedCompatible, Rejected };

constexpr auto Evaluate(const Observation& observation, bool pristineBytesMatch,
        std::string_view expectedOwner, Kind expectedKind) noexcept -> Admission {
    if (!observation.targetExecutable || !observation.structuralWitnessesMatch) {
        return Admission::Rejected;
    }
    if (observation.state == State::Unchanged) {
        return observation.ownerCount == 0 && pristineBytesMatch
            ? Admission::Pristine : Admission::Rejected;
    }
    if (observation.state == State::Tracked
        && observation.kind == expectedKind
        && observation.ownerCount == 1
        && observation.ownerPluginId == expectedOwner) {
        return Admission::TrackedCompatible;
    }
    return Admission::Rejected;
}

} // namespace RuffnecKk::TrackedNativeTransform
