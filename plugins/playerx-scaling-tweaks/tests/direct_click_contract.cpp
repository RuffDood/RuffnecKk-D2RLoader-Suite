// Exercise the exact production pointer handler with a recording panel adapter.
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include "hud/hud_player_slider_config.hpp"
#include "hud/direct_click.hpp"
#include "hud/slider_policy.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace players_hud_slider {
namespace {
int checks{}, applyCalls{}, lastApplied{};
bool allowed = true, prepareWorks = true;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL direct click: %s\n", label); std::exit(1); }
}
bool Allowed() noexcept { return allowed; }
bool Prepare() noexcept { return prepareWorks; }
bool Apply(std::int32_t value) noexcept { ++applyCalls; lastApplied = value; return true; }
bool RectContains(POINT p, int x, int y, int width, int height) noexcept {
    return p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
}
struct RecordingPanel {
    bool open{}, buttonVisible{true}, openWorks{true};
    int opens{}, closes{}, preview{}, previews{};
    bool IsOpen() const noexcept { return open; }
    bool IsHudButtonOpen() const noexcept { return buttonVisible; }
    bool Open() noexcept { ++opens; open = openWorks; return open; }
    void Close() noexcept { ++closes; open = false; }
    void SetTooltipValue(int value) noexcept { preview = value; }
    void QueueTooltipValue(int value) noexcept { preview = value; ++previews; }
};
class PointerHarness {
public:
    HudPlayerSliderConfig config_;
    int maximumPlayers_{24};
    const D2RL::PluginContext* context_{};
    bool (*canApply_)() noexcept = Allowed;
    bool (*prepare_)() noexcept = Prepare;
    bool (*apply_)(std::int32_t) noexcept = Apply;
    RecordingPanel nativePanel_;
    std::atomic_int32_t currentPlayers_{64};
    std::atomic_bool active_{true};
    bool sessionReady_{true}, sliderOpen_{}, nativeDragActive_{};
    std::int32_t selectedPlayers_{1};
    DirectClick directClick_;
    void ToggleSlider() noexcept;
    void ProcessPointerSample(bool, bool, POINT, bool) noexcept;
    bool IsSliderPoint(POINT) const noexcept;
    double MarkerLeft(std::int32_t) const noexcept;
    double MarkerCenter(std::int32_t) const noexcept;
    std::int32_t ResolvePlayerCount(POINT) const noexcept;
    POINT Button() const noexcept { return {config_.button.x + 10, config_.button.y + 10}; }
    POINT Track(int count) const noexcept { return {static_cast<LONG>(std::lround(MarkerCenter(count))), config_.track.y+5}; }
    void Sample(bool down, POINT p, bool foreground = true, bool ready = true) noexcept { ProcessPointerSample(down, ready, p, foreground); }
    void Click() noexcept { Sample(false, Button()); Sample(true, Button()); Sample(false, Button()); }
};
#define PLAYERX_HUD_POINTER_CLASS PointerHarness
#include "hud/pointer_interaction.inc"
#undef PLAYERX_HUD_POINTER_CLASS
}

void RunDirectClickContract() {
    PointerHarness h;
    h.Sample(false, h.Button());
    h.Sample(true, h.Button());
    Check(!h.nativePanel_.open && applyCalls == 0, "press only arms; it does not open or apply");
    for (int i = 0; i < 100; ++i) h.Sample(true, h.Button());
    Check(h.nativePanel_.opens == 0, "holding never repeats the button");
    h.Sample(false, h.Button());
    Check(h.nativePanel_.open && h.nativePanel_.opens == 1, "one release opens exactly once");
    Check(applyCalls == 0 && h.nativePanel_.preview == 64 && h.currentPlayers_.load() == 64, "opening preserves actual p64 without applying");
    h.Sample(false, h.Button());
    Check(h.nativePanel_.opens == 1 && applyCalls == 0, "idle release does not repeat or apply");
    h.Click(); Check(!h.nativePanel_.open && applyCalls == 0, "diamond closes without applying");
    h.Sample(true, h.Button()); h.Sample(false, {0,0});
    Check(!h.nativePanel_.open, "release outside cancels the button");
    h.Sample(true, {0,0}); h.Sample(true, h.Button()); h.Sample(false, h.Button());
    Check(!h.nativePanel_.open, "press outside then enter cannot arm the button");
    h.Sample(true, h.Button()); h.Sample(true, {0,0}); h.Sample(true, h.Button()); h.Sample(false, h.Button());
    Check(!h.nativePanel_.open, "leaving the button cancels the original press");
    h.Sample(true, h.Button()); h.Sample(false, h.Button(), false); h.Sample(false, h.Button());
    Check(!h.nativePanel_.open, "focus loss cancels button release");
    h.Sample(true, h.Button()); h.Sample(false, h.Button(), true, false); h.Sample(false, h.Button());
    Check(!h.nativePanel_.open, "missing pointer cancels button release");
    h.Sample(true, h.Button()); h.nativePanel_.buttonVisible = false; h.Sample(false, h.Button()); h.nativePanel_.buttonVisible = true;
    Check(!h.nativePanel_.open, "hiding HUD cancels an armed click");
    h.Sample(true, h.Button()); allowed = false; h.Sample(false, h.Button()); allowed = true;
    Check(!h.nativePanel_.open, "eligibility change cancels an armed click");
    h.directClick_.Reset(); h.Sample(true, h.Button()); h.Sample(false, h.Button());
    Check(!h.nativePanel_.open, "session reset cannot reinterpret a held button as a fresh click");
    prepareWorks = false; h.Click(); Check(!h.nativePanel_.open && applyCalls == 0, "preparation failure does not apply"); prepareWorks = true;
    h.nativePanel_.openWorks = false; h.Click(); Check(!h.nativePanel_.open && applyCalls == 0, "open failure does not apply"); h.nativePanel_.openWorks = true;
    for (int count : {1,8,16,24}) {
        h.Click(); Check(h.nativePanel_.open, "diamond opens before drag");
        h.Sample(true, h.Track(count));
        const int before = applyCalls;
        for (int i = 0; i < 10; ++i) h.Sample(true, h.Track(count));
        Check(applyCalls == before && h.nativePanel_.preview == count, "drag only previews");
        h.Sample(false, h.Track(count));
        Check(applyCalls == before+1 && lastApplied == count && !h.nativePanel_.open, "release applies exact count once and closes");
        h.Sample(false, h.Track(count)); Check(applyCalls == before+1, "idle does not reapply");
    }
    h.Click(); h.Sample(true, h.Track(8)); const int before = applyCalls;
    h.Sample(false, h.Track(16), false); Check(applyCalls == before && !h.nativePanel_.open, "focus loss during drag cancels application");
    h.Click(); h.Sample(true, h.Track(8)); h.Sample(false, h.Track(16), true, false);
    Check(applyCalls == before && !h.nativePanel_.open, "lost pointer during release cancels application");
    h.Click(); h.Sample(true, {0,0}); h.Sample(true, h.Track(8)); h.Sample(false, h.Track(8));
    Check(applyCalls == before, "held button entering track is not a fresh drag");
    h.Sample(true, h.Track(8)); h.sessionReady_ = false; h.Sample(false, h.Track(8));
    Check(applyCalls == before && !h.nativePanel_.open, "session end cancels drag");
    h.sessionReady_ = true; h.active_.store(false); h.Click();
    Check(!h.nativePanel_.open && applyCalls == before, "inactive plugin ignores clicks");
    std::printf("%d direct-click production-handler checks passed\n", checks);
}
} // namespace players_hud_slider
