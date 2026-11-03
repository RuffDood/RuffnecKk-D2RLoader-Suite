// Adapted from MadMike616/players-hud-slider, MIT (2026 MadMike616). See LICENSE.
#include "player_hud_slider.hpp"
#include "slider_policy.hpp"

#include <D2RLPlugin/logging.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace players_hud_slider {
namespace {

constexpr wchar_t TimerWindowClassName[] = L"STATIC";
constexpr std::int32_t MaximumHudTicks = 32;

struct HostWindowCandidate {
    HWND window{};
    long long area{};
    bool hasGameTitle{};
};

BOOL CALLBACK FindHostWindowCallback(HWND window, LPARAM parameter) noexcept {
    auto* candidate = reinterpret_cast<HostWindowCandidate*>(parameter);
    DWORD processId{};
    (void)GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(window)
            || IsIconic(window)) {
        return TRUE;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const long long width = client.right - client.left;
    const long long height = client.bottom - client.top;
    const long long area = width * height;
    if (area < 640LL * 480LL) return TRUE;

    wchar_t title[256]{};
    (void)GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    const std::wstring_view titleView(title);
    const bool gameTitle = titleView.find(L"Diablo") != std::wstring_view::npos
        || titleView.find(L"D2R") != std::wstring_view::npos;
    if ((gameTitle && !candidate->hasGameTitle)
            || (gameTitle == candidate->hasGameTitle && area > candidate->area)) {
        candidate->window = window;
        candidate->area = area;
        candidate->hasGameTitle = gameTitle;
    }
    return TRUE;
}

auto FindHostWindow() noexcept -> HWND {
    HWND foreground = GetForegroundWindow();
    DWORD processId{};
    (void)GetWindowThreadProcessId(foreground, &processId);
    if (processId == GetCurrentProcessId() && IsWindowVisible(foreground)) {
        RECT client{};
        if (GetClientRect(foreground, &client)
                && client.right - client.left >= 640
                && client.bottom - client.top >= 480) {
            return foreground;
        }
    }
    HostWindowCandidate candidate{};
    (void)EnumWindows(FindHostWindowCallback,
        reinterpret_cast<LPARAM>(&candidate));
    return candidate.window;
}

auto RectContains(
        POINT point,
        std::int32_t x,
        std::int32_t y,
        std::int32_t width,
        std::int32_t height) noexcept -> bool {
    return point.x >= x && point.y >= y
        && point.x < x + width && point.y < y + height;
}

} // namespace

auto PlayerHudSlider::Start(
        const D2RL::PluginContext* context,
        const HudPlayerSliderConfig& config,
        std::int32_t maximumPlayers,
        Read read,
        CanApply canApply,
        Prepare prepare,
        Apply apply) noexcept -> bool {
    Stop();
    if (context == nullptr) return false;
    context_ = context;
    config_ = config;
    maximumPlayers_ = std::clamp(
        maximumPlayers, 1, HudSelectionMaximum);
    read_ = read;
    canApply_ = canApply;
    prepare_ = prepare;
    apply_ = apply;
    selectedPlayers_ = currentPlayers_.load(std::memory_order_acquire);
    active_.store(true, std::memory_order_release);
    if (context_->QueryService(D2RL::ServiceId::Thread, 1, &threads_) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceV1Field(threads_, D2RL::ThreadServiceV1RequiredSize)
            || !threads_->runOnUiThread) { Stop(); return false; }
    if (!RegisterListeners()
            || !nativePanel_.Start(context_, config_, maximumPlayers_)) {
        Stop();
        return false;
    }
    return true;
}

void PlayerHudSlider::Stop() noexcept {
    active_.store(false, std::memory_order_release);
    sessionReady_ = false;
    directClick_.Reset();
    DestroyInteractionTimer();
    nativePanel_.Stop();
    if (context_ != nullptr && lifecycle_ != nullptr
            && lifecycle_->unregisterGameplayEventListener != nullptr) {
        for (auto& listener : listeners_) {
            if (listener != D2RL::Lifecycle::InvalidHandle) {
                (void)lifecycle_->unregisterGameplayEventListener(context_, listener);
                listener = D2RL::Lifecycle::InvalidHandle;
            }
        }
    }
    lifecycle_ = nullptr;
    context_ = nullptr;
    read_ = nullptr;
    canApply_ = nullptr;
    prepare_ = nullptr;
    apply_ = nullptr;
}

void PlayerHudSlider::SetCurrentPlayers(std::int32_t players) noexcept {
    const auto clamped = DisplayCount(players);
    currentPlayers_.store(clamped, std::memory_order_release);
}

auto PlayerHudSlider::RegisterListeners() noexcept -> bool {
    if (context_->QueryService(D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version, &lifecycle_)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(lifecycle_,
                D2RL::LifecycleServiceV1RequiredSize)
            || lifecycle_->registerGameplayEventListener == nullptr
            || lifecycle_->unregisterGameplayEventListener == nullptr) {
        lifecycle_ = nullptr;
        context_->LogWarn(
            "Players HUD slider: LifecycleService v1 is unavailable.");
        return false;
    }

    constexpr std::array kinds{
        D2RL::Lifecycle::GameplayEventKind::GameJoined,
        D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
        D2RL::Lifecycle::GameplayEventKind::GameLeft,
    };
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .flags = 0,
            .kind = kinds[i],
            .reserved = 0,
            .callback = OnGameplay,
            .userData = this,
        };
        if (lifecycle_->registerGameplayEventListener(context_, &listener,
                &listeners_[i]) != D2RL::Lifecycle::Result::Success
                || listeners_[i] == D2RL::Lifecycle::InvalidHandle) {
            context_->LogWarn(
                "Players HUD slider: could not register gameplay lifecycle events.");
            return false;
        }
    }
    return true;
}

void __cdecl PlayerHudSlider::OnGameplay(
        const D2RL::PluginContext* context,
        const D2RL::Lifecycle::GameplayEvent* event,
        void* userData) noexcept {
    if (context == nullptr || event == nullptr || userData == nullptr
            || event->structSize < D2RL::Lifecycle::GameplayEventRequiredSize) {
        return;
    }
    auto& self = *static_cast<PlayerHudSlider*>(userData);
    if (context != self.context_ || !self.active_.load(std::memory_order_acquire)) {
        return;
    }
    self.pendingReady_.store(event->kind == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, std::memory_order_release);
    if (self.threads_) (void)self.threads_->runOnUiThread(context, OnSessionTransition, &self);
}
void __cdecl PlayerHudSlider::OnSessionTransition(const D2RL::PluginContext* context, void* data) noexcept {
    auto* self = static_cast<PlayerHudSlider*>(data);
    if (!self || context != self->context_ || !self->active_.load(std::memory_order_acquire)) return;
    if (self->pendingReady_.load(std::memory_order_acquire)) self->OnSessionReady();
    else self->OnSessionEnded();
}

void PlayerHudSlider::OnSessionReady() noexcept {
    if (!active_.load(std::memory_order_acquire)) return;
    sessionReady_ = true;
    directClick_.Reset();
    if (read_) SetCurrentPlayers(read_());
    nativePanel_.Close();
    sliderOpen_ = false;
    nativeDragActive_ = false;
    selectedPlayers_ = std::clamp(
        currentPlayers_.load(std::memory_order_acquire), config_.minimumPlayers, maximumPlayers_);
    if (!CreateInteractionTimer()) {
        if (context_ != nullptr) {
            context_->LogWarn(
                "Players HUD slider: could not create its interaction timer.");
        }
        return;
    }
    if (config_.minimumPlayers > maximumPlayers_ || (canApply_ != nullptr && !canApply_())) return;
    nativePanel_.ShowHudButton();
}

void PlayerHudSlider::OnSessionEnded() noexcept {
    sessionReady_ = false;
    directClick_.Reset();
    nativePanel_.Close();
    nativePanel_.HideHudButton();
    sliderOpen_ = false;
    nativeDragActive_ = false;
    DestroyInteractionTimer();
}

auto PlayerHudSlider::CreateInteractionTimer() noexcept -> bool {
    if (timerWindow_ != nullptr && IsWindow(timerWindow_)) return true;
    hostWindow_ = FindHostWindow();
    if (hostWindow_ == nullptr) return false;

    timerWindow_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TimerWindowClassName, L"", WS_POPUP, 0, 0, 1, 1,
        hostWindow_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (timerWindow_ == nullptr) {
        hostWindow_ = nullptr;
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    previousWindowProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        timerWindow_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(WindowProc)));
    if (previousWindowProc_ == nullptr && GetLastError() != ERROR_SUCCESS) {
        (void)DestroyWindow(timerWindow_);
        timerWindow_ = nullptr;
        hostWindow_ = nullptr;
        return false;
    }
    (void)SetWindowLongPtrW(timerWindow_, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(this));
    if (SetTimer(timerWindow_, RefreshTimerId, 250, nullptr) == 0
            || SetTimer(timerWindow_, NativeDragTimerId, 10, nullptr) == 0) {
        DestroyInteractionTimer();
        return false;
    }
    return true;
}

void PlayerHudSlider::DestroyInteractionTimer() noexcept {
    const HWND window = timerWindow_;
    if (window != nullptr && IsWindow(window)) {
        if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId()) {
            (void)SendMessageW(window, DetachMessage, 0, 0);
            if (IsWindow(window)) (void)DestroyWindow(window);
        } else {
            (void)SendMessageW(window, DetachMessage, 0, 0);
            if (IsWindow(window)) (void)PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
    timerWindow_ = nullptr;
    previousWindowProc_ = nullptr;
    hostWindow_ = nullptr;
}

void PlayerHudSlider::PollPointer() noexcept {
    const auto foreground = GetForegroundWindow();
    const bool hostForeground = foreground == hostWindow_
        || (foreground != nullptr && hostWindow_ != nullptr && IsChild(hostWindow_, foreground));
    POINT screen{}, logical{};
    const bool ready = GetCursorPos(&screen) && LogicalPoint(screen, logical);
    ProcessPointerSample((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, ready, logical, hostForeground);
}

auto PlayerHudSlider::LogicalPoint(
        POINT screenPoint,
        POINT& output) const noexcept -> bool {
    if (hostWindow_ == nullptr || !IsWindow(hostWindow_)) return false;
    if (!ScreenToClient(hostWindow_, &screenPoint)) return false;
    RECT client{};
    if (!GetClientRect(hostWindow_, &client)) return false;
    const int height = client.bottom - client.top;
    if (height <= 0) return false;
    const double scale = static_cast<double>(height) / VirtualLayoutHeight;
    output.x = static_cast<LONG>(std::lround(screenPoint.x / scale));
    output.y = static_cast<LONG>(std::lround(screenPoint.y / scale));
    return true;
}

#define PLAYERX_HUD_POINTER_CLASS PlayerHudSlider
#include "pointer_interaction.inc"
#undef PLAYERX_HUD_POINTER_CLASS

LRESULT CALLBACK PlayerHudSlider::WindowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept {
    auto* self = reinterpret_cast<PlayerHudSlider*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_TIMER && wParam == RefreshTimerId) {
        if (self->read_) self->SetCurrentPlayers(self->read_());
        const HWND foreground = GetForegroundWindow();
        const bool hostIsForeground = foreground == self->hostWindow_
            || (foreground != nullptr && self->hostWindow_ != nullptr
                && IsChild(self->hostWindow_, foreground));
        const bool shouldShow = self->active_.load(std::memory_order_acquire)
            && self->sessionReady_ && self->config_.minimumPlayers <= self->maximumPlayers_ && hostIsForeground
            && (self->canApply_ == nullptr || self->canApply_());
        if (shouldShow != self->nativePanel_.IsHudButtonOpen()) {
            if (shouldShow) self->nativePanel_.ShowHudButton();
            else self->nativePanel_.HideHudButton();
        }
        self->sliderOpen_ = self->nativePanel_.IsOpen();
        if (self->sliderOpen_ && !self->nativeDragActive_)
            self->nativePanel_.QueueTooltipValue(self->currentPlayers_.load(std::memory_order_acquire));
        if (!self->sliderOpen_) self->nativeDragActive_ = false;
        return 0;
    }
    if (message == WM_TIMER && wParam == NativeDragTimerId) {
        self->PollPointer();
        return 0;
    }
    if (message == DetachMessage) {
        (void)KillTimer(window, RefreshTimerId);
        (void)KillTimer(window, NativeDragTimerId);
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (self->previousWindowProc_ != nullptr) {
            (void)SetWindowLongPtrW(window, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(self->previousWindowProc_));
        }
        self->timerWindow_ = nullptr;
        self->previousWindowProc_ = nullptr;
        (void)PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        const auto previous = self->previousWindowProc_;
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->timerWindow_ = nullptr;
        self->previousWindowProc_ = nullptr;
        return previous != nullptr
            ? CallWindowProcW(previous, window, message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    return self->previousWindowProc_ != nullptr
        ? CallWindowProcW(self->previousWindowProc_, window,
            message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

} // namespace players_hud_slider
