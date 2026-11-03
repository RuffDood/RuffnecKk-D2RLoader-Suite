#pragma once

namespace players_hud_slider {

// A click needs a fresh press and release within the same button. A session,
// focus or visibility transition must not turn an already held key into a click.
class DirectClick {
public:
    struct Edges { bool pressed{}; bool released{}; bool toggle{}; };
    void Reset() noexcept { initialized_ = false; down_ = false; armed_ = false; }
    auto Sample(bool down, bool pointerReady, bool inside, bool eligible) noexcept -> Edges {
        if (!initialized_) {
            initialized_ = true; down_ = down; armed_ = false;
            return {};
        }
        Edges result{down && !down_, !down && down_, false};
        if (!eligible || !pointerReady) {
            armed_ = false;
            down_ = down;
            return {};
        }
        if (result.pressed) armed_ = inside;
        if (down && !inside) armed_ = false;
        if (result.released) {
            result.toggle = armed_ && inside;
            armed_ = false;
        }
        down_ = down;
        return result;
    }
private:
    bool initialized_{};
    bool down_{};
    bool armed_{};
};

} // namespace players_hud_slider
