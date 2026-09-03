#include "overlay_window.hpp"

#include "automap_visibility.hpp"
#include "overlay_scene.hpp"
#include "settings_menu.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gl/GL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace RuffnecKk::MapSense {
namespace {

constexpr wchar_t OverlayWindowClass[] = L"RuffnecKkMapSenseWglOverlay";

constexpr int WglDrawToWindowArb = 0x2001;
constexpr int WglAccelerationArb = 0x2003;
constexpr int WglSupportOpenGlArb = 0x2010;
constexpr int WglDoubleBufferArb = 0x2011;
constexpr int WglPixelTypeArb = 0x2013;
constexpr int WglColorBitsArb = 0x2014;
constexpr int WglAlphaBitsArb = 0x201B;
constexpr int WglFullAccelerationArb = 0x2027;
constexpr int WglTypeRgbaArb = 0x202B;
constexpr int WglContextMajorVersionArb = 0x2091;
constexpr int WglContextMinorVersionArb = 0x2092;
constexpr int WglContextProfileMaskArb = 0x9126;
constexpr int WglContextCoreProfileBitArb = 0x00000001;
constexpr GLenum GlMajorVersion = 0x821B;
constexpr GLenum GlMinorVersion = 0x821C;

using WglChoosePixelFormatArb = BOOL(WINAPI*)(
    HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using WglCreateContextAttribsArb = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using WglGetPixelFormatAttribivArb = BOOL(WINAPI*)(
    HDC, int, int, UINT, const int*, int*);
using WglSwapIntervalExt = BOOL(WINAPI*)(int);

std::mutex LifecycleLock;
std::thread OverlayThread;
std::atomic_bool StopRequested{};
std::atomic_bool ThreadRunning{};
std::atomic_bool AttachedToGame{};
std::atomic_bool DeviceReady{};
std::atomic_bool MenuOpen{};
std::atomic_uint64_t PresentedFrames{};
std::atomic<HWND> OverlayWindowHandle{};

auto ToImColor(Rgba8 color, float opacity = 1.0F) noexcept -> ImU32 {
    const auto alpha = static_cast<std::uint8_t>(std::lround(
        static_cast<float>(color.alpha)
        * std::clamp(opacity, 0.0F, 1.0F)));
    return IM_COL32(color.red, color.green, color.blue, alpha);
}

auto ToSceneColor(RgbaColor color) noexcept -> Rgba8 {
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return {
        channel(color.red),
        channel(color.green),
        channel(color.blue),
        channel(color.alpha),
    };
}

auto ConfiguredElementColor(
        const Config& config,
        Element element) noexcept -> Rgba8 {
    switch (element) {
        case Element::Physical: return ToSceneColor(config.immunities.physical);
        case Element::Magic: return ToSceneColor(config.immunities.magic);
        case Element::Fire: return ToSceneColor(config.immunities.fire);
        case Element::Lightning: return ToSceneColor(config.immunities.lightning);
        case Element::Cold: return ToSceneColor(config.immunities.cold);
        case Element::Poison: return ToSceneColor(config.immunities.poison);
    }
    return ScenePalette::Physical;
}

auto MonsterEnabled(
        const MonsterOptions& options,
        MonsterRank rank) noexcept -> bool {
    switch (rank) {
        case MonsterRank::Normal: return options.normal;
        case MonsterRank::Minion: return options.minion;
        case MonsterRank::Champion: return options.champion;
        case MonsterRank::Unique: return options.unique;
        case MonsterRank::SuperUnique: return options.superUnique;
    }
    return false;
}

template <typename Function>
auto LoadWglFunction(const char* name) noexcept -> Function {
    const auto procedure = wglGetProcAddress(name);
    const auto address = reinterpret_cast<std::uintptr_t>(procedure);
    if (address == 0U
        || address == 1U
        || address == 2U
        || address == 3U
        || address == std::numeric_limits<std::uintptr_t>::max()) {
        return nullptr;
    }
    return std::bit_cast<Function>(procedure);
}

class OverlayRenderer final {
public:
    OverlayRenderer(
        Config config,
        OverlaySaveCallback saveCallback,
        OverlayLogCallback infoCallback,
        OverlayLogCallback warningCallback) noexcept
        : settings_(std::move(config)),
          saveCallback_(saveCallback),
          infoCallback_(infoCallback),
          warningCallback_(warningCallback) {
        if (!settings_.menu.rememberPosition) {
            const Config defaults{};
            settings_.menu.positionX = defaults.menu.positionX;
            settings_.menu.positionY = defaults.menu.positionY;
        }
    }

    OverlayRenderer(const OverlayRenderer&) = delete;
    auto operator=(const OverlayRenderer&) -> OverlayRenderer& = delete;

    void Run() noexcept {
        ThreadRunning.store(true, std::memory_order_release);
        const auto oldDpiContext = SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::uint32_t consecutiveFailures{};
        while (!StopRequested.load(std::memory_order_acquire)) {
            const auto attemptStarted = std::chrono::steady_clock::now();
            const auto framesBeforeAttempt = PresentedFrames.load(
                std::memory_order_relaxed);
            try {
                RunGuarded();
            } catch (...) {
                Warn("MapSense: the WGL renderer stopped after an unexpected failure.");
            }
            const auto reachedReadyState = DeviceReady.load(
                std::memory_order_acquire);
            const auto stableAttempt = reachedReadyState
                && PresentedFrames.load(std::memory_order_relaxed)
                    > framesBeforeAttempt
                && (std::chrono::steady_clock::now() - attemptStarted)
                    >= std::chrono::seconds(5);
            Cleanup();
            if (StopRequested.load(std::memory_order_acquire)) break;

            consecutiveFailures = stableAttempt
                ? 1U
                : std::min(consecutiveFailures + 1U, 10U);
            const auto retryDelay = std::chrono::milliseconds(
                std::min<std::uint32_t>(5'000U, consecutiveFailures * 500U));
            Warn(
                "MapSense: the WGL renderer will retry cleanly; reveal "
                "controls remain available during recovery.");
            const auto retryAt = std::chrono::steady_clock::now() + retryDelay;
            while (!StopRequested.load(std::memory_order_acquire)
                && std::chrono::steady_clock::now() < retryAt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        if (oldDpiContext != nullptr) {
            (void)SetThreadDpiAwarenessContext(oldDpiContext);
        }
        ThreadRunning.store(false, std::memory_order_release);
    }

private:
    struct WindowSearch {
        HWND result{};
        std::int64_t largestArea{};
    };

    static constexpr std::uint32_t MaxMouseButtons = 5U;
    static constexpr std::uint32_t MaxInteractionRects = 32U;
    static constexpr std::size_t FrameSampleCapacity = 300U;

    struct ScreenRect {
        LONG left{};
        LONG top{};
        LONG right{};
        LONG bottom{};
    };


    static auto CALLBACK FindWindowCallback(HWND window, LPARAM parameter) -> BOOL {
        auto* search = reinterpret_cast<WindowSearch*>(parameter);
        if (search == nullptr || !IsWindowVisible(window)) return TRUE;

        DWORD processId{};
        (void)GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId()) return TRUE;
        if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;

        wchar_t className[64]{};
        (void)GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (std::wstring_view(className) == OverlayWindowClass) return TRUE;

        RECT client{};
        if (!GetClientRect(window, &client)) return TRUE;
        const auto width = std::max<LONG>(0, client.right - client.left);
        const auto height = std::max<LONG>(0, client.bottom - client.top);
        const auto area = static_cast<std::int64_t>(width)
            * static_cast<std::int64_t>(height);
        if (area > search->largestArea) {
            search->largestArea = area;
            search->result = window;
        }
        return TRUE;
    }

    static auto IsGameWindowCandidate(HWND window) noexcept -> bool {
        if (window == nullptr || !IsWindow(window)) return false;
        DWORD processId{};
        (void)GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId()
            || GetWindow(window, GW_OWNER) != nullptr) {
            return false;
        }
        wchar_t className[64]{};
        (void)GetClassNameW(
            window,
            className,
            static_cast<int>(std::size(className)));
        if (std::wstring_view(className) == OverlayWindowClass) return false;

        RECT client{};
        if (!GetClientRect(window, &client)) return false;
        const auto width = std::max<LONG>(0, client.right - client.left);
        const auto height = std::max<LONG>(0, client.bottom - client.top);
        return static_cast<std::int64_t>(width)
            * static_cast<std::int64_t>(height) >= (640LL * 360LL);
    }

    static auto CALLBACK WindowProcedure(
            HWND window,
            UINT message,
            WPARAM wordParameter,
            LPARAM longParameter) -> LRESULT {
        OverlayRenderer* renderer{};
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(longParameter);
            renderer = static_cast<OverlayRenderer*>(create->lpCreateParams);
            (void)SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(renderer));
        } else {
            renderer = reinterpret_cast<OverlayRenderer*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (renderer != nullptr) {
            switch (message) {
                case WM_DWMCOMPOSITIONCHANGED:
                    if (!renderer->ApplyDwmTransparency(window)) {
                        renderer->Warn(
                            "MapSense: DWM transparency could not be reapplied.");
                    }
                    return 0;
                case WM_NCHITTEST:
                    return renderer->windowInteractive_
                        ? HTCLIENT
                        : HTTRANSPARENT;
                case WM_MOUSEACTIVATE:
                    return renderer->windowInteractive_
                        ? MA_ACTIVATE
                        : MA_NOACTIVATE;
                case WM_SETCURSOR:
                    // The overlay never supplies a Windows cursor. Keeping
                    // this message handled preserves the cursor last chosen
                    // by D2R instead of letting DefWindowProc select an arrow.
                    if (renderer->windowInteractive_) return TRUE;
                    break;
                case WM_KILLFOCUS:
                    renderer->ClearImGuiMouseState(true);
                    break;
                case WM_SETFOCUS:
                    renderer->AddImGuiFocusEvent(true);
                    break;
                case WM_ACTIVATEAPP:
                    if (wordParameter == FALSE) {
                        renderer->ClearImGuiMouseState(true);
                    }
                    break;
                case WM_ERASEBKGND:
                    return 1;
                case WM_CLOSE:
                case WM_DESTROY:
                    return 0;
                case WM_NCDESTROY:
                    (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                    break;
                default:
                    if (const auto mouseResult =
                            renderer->HandleWindowMouseMessage(
                                window,
                                message,
                                wordParameter,
                                longParameter)) {
                        return *mouseResult;
                    }
                    break;
            }
        }
        return DefWindowProcW(window, message, wordParameter, longParameter);
    }

    void RunGuarded() {
        Info("MapSense WGL stage 1/8: locating the D2R client window.");
        while (!StopRequested.load(std::memory_order_acquire)) {
            gameWindow_ = FindGameWindow();
            if (gameWindow_ != nullptr) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (StopRequested.load(std::memory_order_acquire)) return;

        if (!RegisterOverlayClass()
            || !CreateBootstrapContext()
            || !CreateOverlayWindow()
            || !CreateHardwareContext()
            || !InitializeImGui()) {
            Warn(
                "MapSense: the hardware WGL overlay could not be initialized; "
                "reveal controls remain available.");
            return;
        }

        DeviceReady.store(true, std::memory_order_release);
        Info(
            "MapSense WGL stage 8/8: one transparent window and one ImGui "
            "context are ready for cursor-scoped native interaction.");

        auto nextFrame = std::chrono::steady_clock::now();
        while (!StopRequested.load(std::memory_order_acquire)) {
            UpdateInteractionFromCursor();
            PumpMessages();
            if (StopRequested.load(std::memory_order_acquire)) break;

            if (!UpdateGameGeometry()) {
                if (overlayWindow_ == nullptr || !IsWindow(overlayWindow_)) {
                    Warn(
                        "MapSense: the owned overlay HWND was destroyed; "
                        "the WGL renderer will be rebuilt.");
                    return;
                }
                SetWindowInteractive(false, false);
                ClearInteractionRects();
                SetOverlayVisible(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                nextFrame = std::chrono::steady_clock::now();
                continue;
            }

            if (!RenderFrame()) {
                Warn("MapSense: the WGL overlay could not present a frame.");
                return;
            }

            const auto framesPerSecond = std::clamp(
                settings_.overlay.frameRate, 15, 240);
            const auto frameDuration = std::chrono::microseconds(
                1'000'000 / framesPerSecond);
            nextFrame += frameDuration;
            const auto now = std::chrono::steady_clock::now();
            if (nextFrame > now) {
                std::this_thread::sleep_until(nextFrame);
            } else {
                nextFrame = now;
            }
        }
    }

    auto FindGameWindow() noexcept -> HWND {
        WindowSearch search{};
        (void)EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&search));
        return search.largestArea >= (640LL * 360LL) ? search.result : nullptr;
    }

    auto RegisterOverlayClass() noexcept -> bool {
        Info("MapSense WGL stage 2/8: registering the plugin-owned Win32 class.");
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&WindowProcedure),
                &module_)) {
            Warn("MapSense: the plugin module handle could not be resolved.");
            return false;
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = module_;
        windowClass.hCursor = nullptr;
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = OverlayWindowClass;
        windowClassRegistered_ = RegisterClassExW(&windowClass) != 0;
        if (!windowClassRegistered_) {
            Warn("MapSense: the WGL window class could not be registered cleanly.");
        }
        return windowClassRegistered_;
    }

    static auto BasicPixelFormat() noexcept -> PIXELFORMATDESCRIPTOR {
        PIXELFORMATDESCRIPTOR descriptor{};
        descriptor.nSize = sizeof(descriptor);
        descriptor.nVersion = 1;
        descriptor.dwFlags = PFD_DRAW_TO_WINDOW
            | PFD_SUPPORT_OPENGL
            | PFD_DOUBLEBUFFER;
        descriptor.iPixelType = PFD_TYPE_RGBA;
        descriptor.cColorBits = 32;
        descriptor.cAlphaBits = 8;
        descriptor.iLayerType = PFD_MAIN_PLANE;
        return descriptor;
    }

    auto CreateBootstrapContext() noexcept -> bool {
        Info("MapSense WGL stage 3/8: loading WGL extensions from a bootstrap context.");
        // Place the bootstrap on the same monitor as D2R so hybrid-GPU systems
        // do not source extension pointers from a different display ICD.
        if (!QueryGameClientGeometry()) return false;
        bootstrapWindow_ = CreateWindowExW(
            0,
            OverlayWindowClass,
            L"MapSense WGL bootstrap",
            WS_OVERLAPPED,
            gameOrigin_.x,
            gameOrigin_.y,
            1,
            1,
            nullptr,
            nullptr,
            module_,
            this);
        if (bootstrapWindow_ == nullptr) return false;

        bootstrapDc_ = GetDC(bootstrapWindow_);
        if (bootstrapDc_ == nullptr) return false;
        auto descriptor = BasicPixelFormat();
        const auto pixelFormat = ChoosePixelFormat(bootstrapDc_, &descriptor);
        if (pixelFormat == 0
            || !SetPixelFormat(bootstrapDc_, pixelFormat, &descriptor)) {
            return false;
        }

        bootstrapContext_ = wglCreateContext(bootstrapDc_);
        if (bootstrapContext_ == nullptr
            || !wglMakeCurrent(bootstrapDc_, bootstrapContext_)) {
            return false;
        }

        choosePixelFormatArb_ = LoadWglFunction<WglChoosePixelFormatArb>(
            "wglChoosePixelFormatARB");
        createContextAttribsArb_ = LoadWglFunction<WglCreateContextAttribsArb>(
            "wglCreateContextAttribsARB");
        getPixelFormatAttribivArb_ =
            LoadWglFunction<WglGetPixelFormatAttribivArb>(
                "wglGetPixelFormatAttribivARB");
        swapIntervalExt_ = LoadWglFunction<WglSwapIntervalExt>(
            "wglSwapIntervalEXT");
        if (choosePixelFormatArb_ == nullptr
            || createContextAttribsArb_ == nullptr
            || getPixelFormatAttribivArb_ == nullptr) {
            Warn("MapSense: required WGL_ARB extensions are unavailable.");
            return false;
        }
        return true;
    }

    auto QueryGameClientGeometry() noexcept -> bool {
        RECT client{};
        POINT origin{};
        if (!GetClientRect(gameWindow_, &client)
            || !ClientToScreen(gameWindow_, &origin)) {
            return false;
        }
        gameOrigin_ = origin;
        gameWidth_ = std::max<LONG>(1, client.right - client.left);
        gameHeight_ = std::max<LONG>(1, client.bottom - client.top);
        gameDpiScale_ = std::max(
            1.0F,
            static_cast<float>(GetDpiForWindow(gameWindow_)) / 96.0F);
        return true;
    }

    auto CreateOverlayWindow() noexcept -> bool {
        Info("MapSense WGL stage 4/8: creating one DWM-composed click-through window.");
        if (!QueryGameClientGeometry()) return false;

        constexpr DWORD extendedStyle = WS_EX_TOPMOST
            | WS_EX_TOOLWINDOW
            | WS_EX_NOACTIVATE
            | WS_EX_TRANSPARENT
            | WS_EX_LAYERED;
        overlayWindow_ = CreateWindowExW(
            extendedStyle,
            OverlayWindowClass,
            L"RuffnecKk MapSense",
            WS_POPUP,
            gameOrigin_.x,
            gameOrigin_.y,
            gameWidth_,
            gameHeight_,
            gameWindow_,
            nullptr,
            module_,
            this);
        if (overlayWindow_ == nullptr) {
            return false;
        }
        // PrimeMH 0.5.7 reaches the same styles through Notan/winit 0.28.7,
        // but winit only enables DWM blur. Win32 requires a layered window to
        // be initialized by SetLayeredWindowAttributes or UpdateLayeredWindow
        // before it is made visible. Match GLFW's opaque window-opacity path:
        // a full global alpha initializes the layered HWND while DWM and the
        // WGL framebuffer alpha still provide per-pixel transparency.
        if (!SetLayeredWindowAttributes(
                overlayWindow_,
                0,
                255,
                LWA_ALPHA)) {
            Warn("MapSense: the layered WGL window could not be initialized.");
            return false;
        }
        if (!ApplyDwmTransparency(overlayWindow_)) {
            return false;
        }
        OverlayWindowHandle.store(overlayWindow_, std::memory_order_release);
        return true;
    }

    auto ApplyDwmTransparency(HWND window) const noexcept -> bool {
        const auto region = CreateRectRgn(0, 0, -1, -1);
        if (region == nullptr) return false;
        DWM_BLURBEHIND blur{};
        blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        blur.fEnable = TRUE;
        blur.hRgnBlur = region;
        const auto result = DwmEnableBlurBehindWindow(window, &blur);
        (void)DeleteObject(region);
        return SUCCEEDED(result);
    }

    auto CreateHardwareContext() noexcept -> bool {
        Info("MapSense WGL stage 5/8: selecting a full-acceleration alpha pixel format.");
        overlayDc_ = GetDC(overlayWindow_);
        if (overlayDc_ == nullptr) return false;

        constexpr int pixelAttributes[]{
            WglDrawToWindowArb, TRUE,
            WglSupportOpenGlArb, TRUE,
            WglDoubleBufferArb, TRUE,
            WglPixelTypeArb, WglTypeRgbaArb,
            WglColorBitsArb, 32,
            WglAlphaBitsArb, 8,
            WglAccelerationArb, WglFullAccelerationArb,
            0,
        };
        int pixelFormat{};
        UINT formatCount{};
        if (!choosePixelFormatArb_(
                overlayDc_, pixelAttributes, nullptr, 1, &pixelFormat, &formatCount)
            || formatCount == 0U) {
            Warn("MapSense: no full-acceleration RGBA WGL pixel format was found.");
            return false;
        }

        PIXELFORMATDESCRIPTOR descriptor{};
        if (DescribePixelFormat(
                overlayDc_, pixelFormat, sizeof(descriptor), &descriptor) == 0
            || !SetPixelFormat(overlayDc_, pixelFormat, &descriptor)) {
            return false;
        }

        constexpr std::array<int, 3> proofAttributes{
            WglAccelerationArb,
            WglAlphaBitsArb,
            WglDoubleBufferArb,
        };
        std::array<int, proofAttributes.size()> proofValues{};
        if (!getPixelFormatAttribivArb_(
                overlayDc_,
                pixelFormat,
                0,
                static_cast<UINT>(proofAttributes.size()),
                proofAttributes.data(),
                proofValues.data())
            || proofValues[0] != WglFullAccelerationArb
            || proofValues[1] < 8
            || proofValues[2] == 0
            || (descriptor.dwFlags & PFD_GENERIC_FORMAT) != 0) {
            Warn("MapSense: the selected pixel format did not prove hardware alpha rendering.");
            return false;
        }

        constexpr int contextAttributes[]{
            WglContextMajorVersionArb, 3,
            WglContextMinorVersionArb, 3,
            WglContextProfileMaskArb, WglContextCoreProfileBitArb,
            0,
        };
        overlayContext_ = createContextAttribsArb_(
            overlayDc_, nullptr, contextAttributes);
        if (overlayContext_ == nullptr
            || !wglMakeCurrent(overlayDc_, overlayContext_)) {
            Warn("MapSense: an OpenGL 3.3 core context could not be created.");
            return false;
        }
        DestroyBootstrapContext();

        GLint major{};
        GLint minor{};
        glGetIntegerv(GlMajorVersion, &major);
        glGetIntegerv(GlMinorVersion, &minor);
        const auto* vendorBytes = glGetString(GL_VENDOR);
        const auto* rendererBytes = glGetString(GL_RENDERER);
        const auto* versionBytes = glGetString(GL_VERSION);
        if (vendorBytes == nullptr
            || rendererBytes == nullptr
            || versionBytes == nullptr
            || major < 3
            || (major == 3 && minor < 3)) {
            Warn("MapSense: the active OpenGL context is below the required 3.3 baseline.");
            return false;
        }

        const auto* vendor = reinterpret_cast<const char*>(vendorBytes);
        const auto* renderer = reinterpret_cast<const char*>(rendererBytes);
        const auto* version = reinterpret_cast<const char*>(versionBytes);
        std::string rendererLower(renderer);
        std::transform(
            rendererLower.begin(),
            rendererLower.end(),
            rendererLower.begin(),
            [](char value) {
                if (value >= 'A' && value <= 'Z') {
                    return static_cast<char>(value - 'A' + 'a');
                }
                return value;
            });
        if (rendererLower.find("gdi generic") != std::string::npos
            || rendererLower.find("swiftshader") != std::string::npos
            || rendererLower.find("llvmpipe") != std::string::npos
            || rendererLower.find("softpipe") != std::string::npos) {
            Warn("MapSense: a software OpenGL renderer was rejected.");
            return false;
        }

        char hardwareLog[512]{};
        (void)std::snprintf(
            hardwareLog,
            sizeof(hardwareLog),
            "MapSense WGL stage 6/8: hardware ICD proven; vendor='%s', renderer='%s', version='%s', alpha=%d.",
            vendor,
            renderer,
            version,
            proofValues[1]);
        Info(hardwareLog);

        if (swapIntervalExt_ != nullptr) {
            const auto disabled = swapIntervalExt_(0) != FALSE;
            Info(disabled
                ? "MapSense: WGL swap interval is 0; the explicit frame cap owns pacing."
                : "MapSense: the driver refused swap interval 0; SwapBuffers may pace frames.");
        } else {
            Info("MapSense: WGL_EXT_swap_control is unavailable; the explicit cap remains active.");
        }
        return true;
    }

    void DestroyBootstrapContext() noexcept {
        if (wglGetCurrentContext() == bootstrapContext_) {
            (void)wglMakeCurrent(nullptr, nullptr);
        }
        if (bootstrapContext_ != nullptr) {
            (void)wglDeleteContext(bootstrapContext_);
            bootstrapContext_ = nullptr;
        }
        if (bootstrapDc_ != nullptr && bootstrapWindow_ != nullptr) {
            (void)ReleaseDC(bootstrapWindow_, bootstrapDc_);
            bootstrapDc_ = nullptr;
        }
        if (bootstrapWindow_ != nullptr) {
            (void)DestroyWindow(bootstrapWindow_);
            bootstrapWindow_ = nullptr;
        }
    }

    auto InitializeImGui() noexcept -> bool {
        Info("MapSense WGL stage 7/8: initializing one backend-free ImGui platform context.");
        IMGUI_CHECKVERSION();
        imguiContext_ = ImGui::CreateContext();
        if (imguiContext_ == nullptr) return false;
        ImGui::SetCurrentContext(imguiContext_);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = {
            static_cast<float>(gameWidth_),
            static_cast<float>(gameHeight_),
        };
        io.DisplayFramebufferScale = {1.0F, 1.0F};
        ApplyD2RStyle();
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) return false;
        imguiOpenGlReady_ = true;
        menuExpanded_ = settings_.menu.startExpanded
            || settings_.overlay.startMenuOpen;
        MenuOpen.store(menuExpanded_, std::memory_order_release);
        lastImGuiFrame_ = std::chrono::steady_clock::now();
        return true;
    }

    void PumpMessages() noexcept {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                StopRequested.store(true, std::memory_order_release);
                return;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    auto UpdateGameGeometry() noexcept -> bool {
        if (!IsWindow(overlayWindow_)) {
            AttachedToGame.store(false, std::memory_order_release);
            ClearImGuiMouseState(true);
            return false;
        }
        if (!IsGameWindowCandidate(gameWindow_)) {
            SetWindowInteractive(false, false);
            ClearInteractionRects();
            gameWindow_ = FindGameWindow();
            if (gameWindow_ == nullptr) {
                AttachedToGame.store(false, std::memory_order_release);
                return false;
            }
            (void)SetWindowLongPtrW(
                overlayWindow_,
                GWLP_HWNDPARENT,
                reinterpret_cast<LONG_PTR>(gameWindow_));
            geometryPositioned_ = false;
        }

        const auto foreground = GetForegroundWindow();
        if (!IsWindowVisible(gameWindow_)
            || IsIconic(gameWindow_)
            || (foreground != gameWindow_ && foreground != overlayWindow_)) {
            AttachedToGame.store(false, std::memory_order_release);
            SetWindowInteractive(false, false);
            ClearInteractionRects();
            return false;
        }

        const auto oldOrigin = gameOrigin_;
        const auto oldWidth = gameWidth_;
        const auto oldHeight = gameHeight_;
        if (!QueryGameClientGeometry()) {
            AttachedToGame.store(false, std::memory_order_release);
            SetWindowInteractive(false, foreground == overlayWindow_);
            ClearInteractionRects();
            return false;
        }

        const auto geometryChanged = !geometryPositioned_
            || oldOrigin.x != gameOrigin_.x
            || oldOrigin.y != gameOrigin_.y
            || oldWidth != gameWidth_
            || oldHeight != gameHeight_;
        if (geometryChanged) {
            if (!SetWindowPos(
                    overlayWindow_,
                    HWND_TOPMOST,
                    gameOrigin_.x,
                    gameOrigin_.y,
                    gameWidth_,
                    gameHeight_,
                    SWP_NOACTIVATE)) {
                AttachedToGame.store(false, std::memory_order_release);
                SetWindowInteractive(false, foreground == overlayWindow_);
                ClearInteractionRects();
                return false;
            }
            SetWindowInteractive(false, foreground == overlayWindow_);
            ClearInteractionRects();
            geometryPositioned_ = true;
            if (oldWidth != gameWidth_ || oldHeight != gameHeight_) {
                preview_.reset();
            }
        }

        AttachedToGame.store(true, std::memory_order_release);
        return true;
    }

    auto RenderFrame() -> bool {
        if (!IsWindow(overlayWindow_)
            || overlayDc_ == nullptr
            || overlayContext_ == nullptr) {
            ClearImGuiMouseState(true);
            return false;
        }
        const auto foreground = GetForegroundWindow();
        if (!IsWindow(gameWindow_)
            || (foreground != gameWindow_ && foreground != overlayWindow_)) {
            SetWindowInteractive(false, false);
            ClearInteractionRects();
            SetOverlayVisible(false);
            return true;
        }

        const auto requestedExpanded = MenuOpen.load(std::memory_order_acquire);
        if (requestedExpanded != menuExpanded_) {
            menuExpanded_ = requestedExpanded;
            ClearImGuiMouseState(false);
        }
        const auto nativeAutomapVisible = IsNativeAutomapVisible();
        const auto canvasWanted = settings_.enabled
            && settings_.overlay.diagnosticPreview
            && (!settings_.overlay.followNativeAutomap || nativeAutomapVisible);
        const auto menuWanted = menuExpanded_ || settings_.menu.showLauncher;
        if (!canvasWanted && !menuWanted) {
            ClearInteractionRects();
            SetWindowInteractive(false, foreground == overlayWindow_);
            SetOverlayVisible(false);
            return true;
        }

        const auto frameStart = std::chrono::steady_clock::now();
        ImGui::SetCurrentContext(imguiContext_);
        ImGui_ImplOpenGL3_NewFrame();
        auto& io = ImGui::GetIO();
        io.DisplaySize = {
            static_cast<float>(gameWidth_),
            static_cast<float>(gameHeight_),
        };
        io.DisplayFramebufferScale = {1.0F, 1.0F};
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::max(
            0.0001F,
            std::chrono::duration<float>(now - lastImGuiFrame_).count());
        lastImGuiFrame_ = now;
        io.FontGlobalScale = gameDpiScale_;
        FeedMousePosition(io);
        ImGui::NewFrame();

        if (canvasWanted) {
            if (!preview_) {
                preview_ = BuildDiagnosticPreview(
                    static_cast<std::uint32_t>(gameWidth_),
                    static_cast<std::uint32_t>(gameHeight_),
                    ++previewSequence_);
            }
            DrawScene(*preview_);
        }

        SettingsSurfaceBounds menuBounds{};
        if (menuWanted) {
            auto expanded = menuExpanded_;
            menuBounds = DrawSettingsSurface(
                settings_, expanded, saveCallback_);
            if (expanded != menuExpanded_) {
                menuExpanded_ = expanded;
                MenuOpen.store(expanded, std::memory_order_release);
                ClearImGuiMouseState(false);
            }
        }

        const auto finalMenuWanted = menuExpanded_ || settings_.menu.showLauncher;
        if (finalMenuWanted && menuBounds.visible) {
            CollectInteractiveWindows(menuBounds);
            UpdateInteractionFromCursor();
        } else {
            ClearInteractionRects();
            SetWindowInteractive(false, foreground == overlayWindow_);
        }

        ImGui::Render();
        glViewport(0, 0, gameWidth_, gameHeight_);
        glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!SwapBuffers(overlayDc_)) return false;

        SetOverlayVisible(true);
        const auto frames = PresentedFrames.fetch_add(
            1,
            std::memory_order_relaxed) + 1U;
        RecordFrameTiming(frameStart, std::chrono::steady_clock::now(), frames);
        return true;
    }

    void FeedMousePosition(ImGuiIO& io) const noexcept {
        POINT position{};
        const auto foreground = GetForegroundWindow();
        if ((foreground == gameWindow_ || foreground == overlayWindow_)
            && GetCursorPos(&position)) {
            io.AddMousePosEvent(
                static_cast<float>(position.x - gameOrigin_.x),
                static_cast<float>(position.y - gameOrigin_.y));
        } else {
            const auto unavailable = -std::numeric_limits<float>::max();
            io.AddMousePosEvent(unavailable, unavailable);
        }
    }

    static auto MessageClientPoint(LPARAM parameter) noexcept -> POINT {
        return {
            GET_X_LPARAM(parameter),
            GET_Y_LPARAM(parameter),
        };
    }

    void AddMousePosition(ImGuiIO& io, POINT point) const noexcept {
        io.AddMousePosEvent(
            static_cast<float>(point.x),
            static_cast<float>(point.y));
    }

    auto HandleWindowMouseMessage(
            HWND window,
            UINT message,
            WPARAM wordParameter,
            LPARAM longParameter) noexcept -> std::optional<LRESULT> {
        if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
            if (pressedButtons_ != 0U) ClearImGuiMouseState(false);
            return 0;
        }
        if (!windowInteractive_ || imguiContext_ == nullptr) return std::nullopt;

        ImGui::SetCurrentContext(imguiContext_);
        auto& io = ImGui::GetIO();
        const auto buttonEvent = [
                this,
                window,
                &io,
                longParameter](
                std::uint32_t button,
                bool down) {
            const auto point = MessageClientPoint(longParameter);
            AddMousePosition(io, point);
            const auto mask = 1U << button;
            if (down) {
                pressedButtons_ |= mask;
                if (GetCapture() != window) (void)SetCapture(window);
            } else {
                pressedButtons_ &= ~mask;
            }
            io.AddMouseButtonEvent(static_cast<int>(button), down);
            if (!down
                && pressedButtons_ == 0U
                && GetCapture() == window) {
                (void)ReleaseCapture();
            }
        };

        switch (message) {
            case WM_MOUSEMOVE:
                AddMousePosition(io, MessageClientPoint(longParameter));
                return 0;
            case WM_LBUTTONDOWN:
                buttonEvent(0U, true);
                return 0;
            case WM_LBUTTONUP:
                buttonEvent(0U, false);
                return 0;
            case WM_RBUTTONDOWN:
                buttonEvent(1U, true);
                return 0;
            case WM_RBUTTONUP:
                buttonEvent(1U, false);
                return 0;
            case WM_MBUTTONDOWN:
                buttonEvent(2U, true);
                return 0;
            case WM_MBUTTONUP:
                buttonEvent(2U, false);
                return 0;
            case WM_XBUTTONDOWN:
                buttonEvent(
                    HIWORD(wordParameter) == XBUTTON1 ? 3U : 4U,
                    true);
                return TRUE;
            case WM_XBUTTONUP:
                buttonEvent(
                    HIWORD(wordParameter) == XBUTTON1 ? 3U : 4U,
                    false);
                return TRUE;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL: {
                POINT point{
                    GET_X_LPARAM(longParameter),
                    GET_Y_LPARAM(longParameter),
                };
                if (!ScreenToClient(window, &point)) return 0;
                AddMousePosition(io, point);
                const auto delta = static_cast<float>(
                    GET_WHEEL_DELTA_WPARAM(wordParameter))
                    / static_cast<float>(WHEEL_DELTA);
                io.AddMouseWheelEvent(
                    message == WM_MOUSEHWHEEL ? -delta : 0.0F,
                    message == WM_MOUSEWHEEL ? delta : 0.0F);
                return 0;
            }
            default:
                return std::nullopt;
        }
    }

    void ClearImGuiMouseState(bool focusLost) noexcept {
        pressedButtons_ = 0U;
        if (GetCapture() == overlayWindow_) (void)ReleaseCapture();
        if (imguiContext_ == nullptr) return;
        ImGui::SetCurrentContext(imguiContext_);
        auto& io = ImGui::GetIO();
        io.ClearEventsQueue();
        for (std::uint32_t button{}; button < MaxMouseButtons; ++button) {
            io.AddMouseButtonEvent(static_cast<int>(button), false);
        }
        io.ClearInputMouse();
        if (focusLost) io.AddFocusEvent(false);
    }

    void AddImGuiFocusEvent(bool focused) noexcept {
        if (imguiContext_ == nullptr) return;
        ImGui::SetCurrentContext(imguiContext_);
        ImGui::GetIO().AddFocusEvent(focused);
    }

    void ClearInteractionRects() noexcept {
        interactionRectCount_ = 0U;
    }

    auto IsInsideInteractionRects(POINT point) const noexcept -> bool {
        for (std::uint32_t index{}; index < interactionRectCount_; ++index) {
            const auto& rect = interactionRects_[index];
            if (point.x >= rect.left
                && point.x < rect.right
                && point.y >= rect.top
                && point.y < rect.bottom) {
                return true;
            }
        }
        return false;
    }

    auto SetWindowInteractive(
            bool interactive,
            bool returnFocusToGame) noexcept -> bool {
        if (overlayWindow_ == nullptr || !IsWindow(overlayWindow_)) {
            windowInteractive_ = false;
            ClearImGuiMouseState(true);
            return false;
        }

        if (windowInteractive_ == interactive) {
            if (!interactive
                && returnFocusToGame
                && GetForegroundWindow() == overlayWindow_
                && IsWindow(gameWindow_)) {
                (void)SetForegroundWindow(gameWindow_);
            }
            return true;
        }

        if (!interactive) ClearImGuiMouseState(true);

        const auto oldStyle = GetWindowLongPtrW(overlayWindow_, GWL_EXSTYLE);
        auto newStyle = oldStyle;
        if (interactive) {
            newStyle &= ~static_cast<LONG_PTR>(
                WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        } else {
            newStyle |= static_cast<LONG_PTR>(
                WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        }

        SetLastError(ERROR_SUCCESS);
        const auto previous = SetWindowLongPtrW(
            overlayWindow_,
            GWL_EXSTYLE,
            newStyle);
        if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
            Warn("MapSense: the overlay interaction style could not be changed.");
            return false;
        }
        if (!SetWindowPos(
                overlayWindow_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE
                    | SWP_NOSIZE
                    | SWP_NOACTIVATE
                    | SWP_FRAMECHANGED)) {
            (void)SetWindowLongPtrW(
                overlayWindow_,
                GWL_EXSTYLE,
                oldStyle);
            (void)SetWindowPos(
                overlayWindow_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE
                    | SWP_NOSIZE
                    | SWP_NOACTIVATE
                    | SWP_FRAMECHANGED);
            Warn("MapSense: the overlay interaction style could not be applied.");
            return false;
        }

        windowInteractive_ = interactive;
        if (interactive) {
            if (imguiContext_ != nullptr) {
                ImGui::SetCurrentContext(imguiContext_);
                auto& io = ImGui::GetIO();
                io.ClearEventsQueue();
                io.ClearInputMouse();
                io.AddFocusEvent(true);
            }
        } else if (returnFocusToGame
            && GetForegroundWindow() == overlayWindow_
            && IsWindow(gameWindow_)) {
            (void)SetForegroundWindow(gameWindow_);
        }
        return true;
    }

    void UpdateInteractionFromCursor() noexcept {
        if (overlayWindow_ == nullptr || !IsWindow(overlayWindow_)) return;
        const auto foreground = GetForegroundWindow();
        if (foreground != gameWindow_ && foreground != overlayWindow_) {
            (void)SetWindowInteractive(false, false);
            return;
        }

        POINT cursor{};
        const auto cursorAvailable = GetCursorPos(&cursor) != FALSE;
        const auto inside = cursorAvailable
            && interactionRectCount_ != 0U
            && IsInsideInteractionRects(cursor);
        const auto shouldInteract = inside || pressedButtons_ != 0U;
        (void)SetWindowInteractive(
            shouldInteract,
            !shouldInteract && foreground == overlayWindow_);
    }

    void CollectInteractiveWindows(
            const SettingsSurfaceBounds& bounds) noexcept {
        interactionRectCount_ = 0U;
        const auto append = [this](ScreenRect rect) {
            if (rect.right <= rect.left || rect.bottom <= rect.top) return;
            for (std::uint32_t index{}; index < interactionRectCount_; ++index) {
                const auto& existing = interactionRects_[index];
                if (existing.left == rect.left
                    && existing.top == rect.top
                    && existing.right == rect.right
                    && existing.bottom == rect.bottom) {
                    return;
                }
            }
            if (interactionRectCount_ < MaxInteractionRects) {
                interactionRects_[interactionRectCount_++] = rect;
                return;
            }
            auto& overflow = interactionRects_[MaxInteractionRects - 1U];
            overflow.left = std::min(overflow.left, rect.left);
            overflow.top = std::min(overflow.top, rect.top);
            overflow.right = std::max(overflow.right, rect.right);
            overflow.bottom = std::max(overflow.bottom, rect.bottom);
        };

        if (bounds.visible) {
            append({
                .left = gameOrigin_.x + static_cast<LONG>(std::floor(bounds.left)),
                .top = gameOrigin_.y + static_cast<LONG>(std::floor(bounds.top)),
                .right = gameOrigin_.x + static_cast<LONG>(std::ceil(bounds.right)),
                .bottom = gameOrigin_.y + static_cast<LONG>(std::ceil(bounds.bottom)),
            });
        }

        const auto* context = ImGui::GetCurrentContext();
        if (context == nullptr) return;
        for (const auto* window : context->Windows) {
            if (window == nullptr
                || !window->Active
                || window->Hidden
                || (window->Flags & ImGuiWindowFlags_NoInputs) != 0) {
                continue;
            }
            const auto rect = window->Rect();
            append({
                .left = gameOrigin_.x
                    + static_cast<LONG>(std::floor(rect.Min.x)),
                .top = gameOrigin_.y
                    + static_cast<LONG>(std::floor(rect.Min.y)),
                .right = gameOrigin_.x
                    + static_cast<LONG>(std::ceil(rect.Max.x)),
                .bottom = gameOrigin_.y
                    + static_cast<LONG>(std::ceil(rect.Max.y)),
            });
        }
    }

    void SetOverlayVisible(bool visible) noexcept {
        if (overlayWindow_ == nullptr || overlayVisible_ == visible) return;
        overlayVisible_ = visible;
        if (visible) {
            (void)SetWindowPos(
                overlayWindow_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE
                    | SWP_NOSIZE
                    | SWP_NOACTIVATE
                    | SWP_SHOWWINDOW);
        } else {
            ShowWindow(overlayWindow_, SW_HIDE);
        }
    }

    auto TransformPoint(Vec2 point) const noexcept -> ImVec2 {
        const auto centerX = static_cast<float>(gameWidth_) * 0.5F;
        const auto centerY = static_cast<float>(gameHeight_) * 0.5F;
        return {
            centerX + ((point.x - centerX) * settings_.overlay.scale),
            centerY + ((point.y - centerY) * settings_.overlay.scale),
        };
    }

    void DrawScene(const SceneSnapshot& scene) {
        auto* drawList = ImGui::GetForegroundDrawList();
        const auto opacity = settings_.overlay.opacity;
        for (const auto& primitive : scene.primitives) {
            std::visit(
                [this, drawList, opacity](const auto& value) {
                    using Value = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Value, LinePrimitive>) {
                        drawList->AddLine(
                            TransformPoint(value.start),
                            TransformPoint(value.end),
                            ToImColor(value.color, opacity),
                            value.thickness * settings_.overlay.scale);
                    } else if constexpr (std::is_same_v<Value, CirclePrimitive>) {
                        const auto center = TransformPoint(value.center);
                        const auto radius = value.radius * settings_.overlay.scale;
                        if (value.fill) {
                            drawList->AddCircleFilled(
                                center,
                                radius,
                                ToImColor(*value.fill, opacity));
                        }
                        drawList->AddCircle(
                            center,
                            radius,
                            ToImColor(value.stroke, opacity),
                            0,
                            value.thickness * settings_.overlay.scale);
                    } else if constexpr (std::is_same_v<Value, PolygonPrimitive>) {
                        std::vector<ImVec2> points;
                        points.reserve(value.points.size());
                        for (const auto point : value.points) {
                            points.push_back(TransformPoint(point));
                        }
                        if (value.fill) {
                            drawList->AddConvexPolyFilled(
                                points.data(),
                                static_cast<int>(points.size()),
                                ToImColor(*value.fill, opacity));
                        }
                        drawList->AddPolyline(
                            points.data(),
                            static_cast<int>(points.size()),
                            ToImColor(value.stroke, opacity),
                            ImDrawFlags_Closed,
                            value.thickness * settings_.overlay.scale);
                    } else if constexpr (std::is_same_v<Value, MonsterMarker>) {
                        if (!MonsterEnabled(settings_.monsters, value.rank)) return;
                        auto marker = value;
                        marker.radius = settings_.monsters.markerSize;
                        const auto outline = BuildMonsterOutline(marker);
                        std::vector<ImVec2> points;
                        points.reserve(outline.size());
                        for (const auto point : outline) {
                            points.push_back(TransformPoint(point));
                        }
                        drawList->AddConvexPolyFilled(
                            points.data(),
                            static_cast<int>(points.size()),
                            ToImColor(marker.fill, opacity));
                        drawList->AddPolyline(
                            points.data(),
                            static_cast<int>(points.size()),
                            ToImColor(marker.stroke, opacity),
                            ImDrawFlags_Closed,
                            marker.thickness * settings_.overlay.scale);
                    } else if constexpr (std::is_same_v<Value, ImmunityRing>) {
                        if (!settings_.immunities.enabled) return;
                        const auto center = TransformPoint(value.center);
                        const auto middleRadius =
                            ((value.innerRadius + value.outerRadius) * 0.5F)
                            * settings_.overlay.scale;
                        const auto thickness =
                            (value.outerRadius - value.innerRadius)
                            * settings_.overlay.scale;
                        for (const auto& arc : value.arcs) {
                            drawList->PathClear();
                            drawList->PathArcTo(
                                center,
                                middleRadius,
                                arc.startRadians,
                                arc.endRadians,
                                24);
                            drawList->PathStroke(
                                ToImColor(
                                    ConfiguredElementColor(settings_, arc.element),
                                    opacity),
                                0,
                                thickness);
                        }
                    } else if constexpr (std::is_same_v<Value, MissileMarker>) {
                        const auto start = TransformPoint(value.center);
                        const auto magnitude = std::sqrt(
                            (value.direction.x * value.direction.x)
                            + (value.direction.y * value.direction.y));
                        const Vec2 endPoint{
                            value.center.x
                                + ((value.direction.x / magnitude) * value.length),
                            value.center.y
                                + ((value.direction.y / magnitude) * value.length),
                        };
                        const auto color = ToImColor(
                            ConfiguredElementColor(settings_, value.element),
                            opacity);
                        drawList->AddLine(
                            start,
                            TransformPoint(endPoint),
                            color,
                            2.0F * settings_.overlay.scale);
                        drawList->AddCircleFilled(
                            start,
                            value.radius * settings_.overlay.scale,
                            color);
                    } else if constexpr (std::is_same_v<Value, TextPrimitive>) {
                        auto position = TransformPoint(value.position);
                        const auto size = value.size * settings_.overlay.scale;
                        const auto dimensions = ImGui::GetFont()->CalcTextSizeA(
                            size,
                            std::numeric_limits<float>::max(),
                            0.0F,
                            value.text.c_str());
                        if (value.anchor == TextAnchor::TopCenter) {
                            position.x -= dimensions.x * 0.5F;
                        } else if (value.anchor == TextAnchor::Center) {
                            position.x -= dimensions.x * 0.5F;
                            position.y -= dimensions.y * 0.5F;
                        }
                        drawList->AddText(
                            ImGui::GetFont(),
                            size,
                            position,
                            ToImColor(value.color, opacity),
                            value.text.c_str());
                    }
                },
                primitive);
        }
    }

    void RecordFrameTiming(
            std::chrono::steady_clock::time_point start,
            std::chrono::steady_clock::time_point end,
            std::uint64_t frameNumber) noexcept {
        const auto work = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count();
        const auto interval = lastPresentedFrame_.time_since_epoch().count() == 0
            ? work
            : std::chrono::duration_cast<std::chrono::microseconds>(
                start - lastPresentedFrame_).count();
        lastPresentedFrame_ = start;

        const auto clampSample = [](std::int64_t value) {
            return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                value,
                0,
                std::numeric_limits<std::uint32_t>::max()));
        };
        workSamples_[frameSampleCount_] = clampSample(work);
        intervalSamples_[frameSampleCount_] = clampSample(interval);
        ++frameSampleCount_;

        const auto firstReport = !firstTelemetryReported_
            && frameSampleCount_ >= 120U;
        const auto batchComplete = frameSampleCount_ >= FrameSampleCapacity;
        if (!firstReport && !batchComplete) return;

        if (firstReport || settings_.diagnostics) {
            EmitFrameTelemetry(frameNumber);
        }
        firstTelemetryReported_ = true;
        frameSampleCount_ = 0;
    }

    void EmitFrameTelemetry(std::uint64_t frameNumber) noexcept {
        if (frameSampleCount_ == 0U) return;
        auto sortedWork = workSamples_;
        auto sortedInterval = intervalSamples_;
        std::sort(
            sortedWork.begin(),
            sortedWork.begin() + static_cast<std::ptrdiff_t>(frameSampleCount_));
        std::sort(
            sortedInterval.begin(),
            sortedInterval.begin() + static_cast<std::ptrdiff_t>(frameSampleCount_));
        const auto percentileIndex = std::min(
            frameSampleCount_ - 1U,
            ((frameSampleCount_ * 95U) + 99U) / 100U - 1U);
        std::uint64_t workTotal{};
        std::uint64_t intervalTotal{};
        for (std::size_t index{}; index < frameSampleCount_; ++index) {
            workTotal += workSamples_[index];
            intervalTotal += intervalSamples_[index];
        }
        const auto workAverage = static_cast<double>(workTotal)
            / static_cast<double>(frameSampleCount_)
            / 1000.0;
        const auto intervalAverage = static_cast<double>(intervalTotal)
            / static_cast<double>(frameSampleCount_)
            / 1000.0;
        const auto workP95 = static_cast<double>(sortedWork[percentileIndex]) / 1000.0;
        const auto intervalP95 = static_cast<double>(sortedInterval[percentileIndex]) / 1000.0;
        char timingLog[384]{};
        (void)std::snprintf(
            timingLog,
            sizeof(timingLog),
            "MapSense WGL timing at frame %llu: work avg %.3f ms/p95 %.3f ms; cadence avg %.3f ms/p95 %.3f ms.",
            static_cast<unsigned long long>(frameNumber),
            workAverage,
            workP95,
            intervalAverage,
            intervalP95);
        Info(timingLog);
    }

    void Cleanup() noexcept {
        DeviceReady.store(false, std::memory_order_release);
        AttachedToGame.store(false, std::memory_order_release);
        ClearInteractionRects();
        (void)SetWindowInteractive(
            false,
            GetForegroundWindow() == overlayWindow_);

        SetOverlayVisible(false);
        const auto overlayWindowValid = overlayWindow_ != nullptr
            && IsWindow(overlayWindow_);
        auto contextCurrent = false;
        if (overlayWindowValid
            && overlayContext_ != nullptr
            && overlayDc_ != nullptr) {
            contextCurrent = wglMakeCurrent(overlayDc_, overlayContext_) != FALSE;
        }
        if (imguiContext_ != nullptr) {
            ImGui::SetCurrentContext(imguiContext_);
            if (imguiOpenGlReady_) {
                if (contextCurrent) {
                    ImGui_ImplOpenGL3_Shutdown();
                } else {
                    Warn(
                        "MapSense: the stale WGL surface was abandoned without "
                        "issuing OpenGL cleanup calls.");
                }
                imguiOpenGlReady_ = false;
            }
            ImGui::DestroyContext(imguiContext_);
            imguiContext_ = nullptr;
            ImGui::SetCurrentContext(nullptr);
        }
        (void)wglMakeCurrent(nullptr, nullptr);
        if (overlayContext_ != nullptr) {
            (void)wglDeleteContext(overlayContext_);
            overlayContext_ = nullptr;
        }
        if (overlayDc_ != nullptr && overlayWindowValid) {
            (void)ReleaseDC(overlayWindow_, overlayDc_);
        }
        overlayDc_ = nullptr;
        DestroyBootstrapContext();
        if (overlayWindow_ != nullptr) {
            OverlayWindowHandle.store(nullptr, std::memory_order_release);
            if (overlayWindowValid) {
                (void)DestroyWindow(overlayWindow_);
            }
            overlayWindow_ = nullptr;
        }
        if (windowClassRegistered_ && module_ != nullptr) {
            (void)UnregisterClassW(OverlayWindowClass, module_);
            windowClassRegistered_ = false;
        }
        gameWindow_ = nullptr;
        geometryPositioned_ = false;
        overlayVisible_ = false;
        preview_.reset();
        choosePixelFormatArb_ = nullptr;
        createContextAttribsArb_ = nullptr;
        getPixelFormatAttribivArb_ = nullptr;
        swapIntervalExt_ = nullptr;
    }

    void Info(const char* message) const noexcept {
        if (infoCallback_ != nullptr) infoCallback_(message);
    }

    void Warn(const char* message) const noexcept {
        if (warningCallback_ != nullptr) warningCallback_(message);
    }

    Config settings_{};
    OverlaySaveCallback saveCallback_{};
    OverlayLogCallback infoCallback_{};
    OverlayLogCallback warningCallback_{};
    HINSTANCE module_{};
    HWND gameWindow_{};
    HWND overlayWindow_{};
    HWND bootstrapWindow_{};
    HDC overlayDc_{};
    HDC bootstrapDc_{};
    HGLRC overlayContext_{};
    HGLRC bootstrapContext_{};
    WglChoosePixelFormatArb choosePixelFormatArb_{};
    WglCreateContextAttribsArb createContextAttribsArb_{};
    WglGetPixelFormatAttribivArb getPixelFormatAttribivArb_{};
    WglSwapIntervalExt swapIntervalExt_{};
    ImGuiContext* imguiContext_{};
    POINT gameOrigin_{};
    LONG gameWidth_{1};
    LONG gameHeight_{1};
    float gameDpiScale_{1.0F};
    bool windowClassRegistered_{};
    bool imguiOpenGlReady_{};
    bool overlayVisible_{};
    bool geometryPositioned_{};
    bool menuExpanded_{};
    bool windowInteractive_{};
    bool firstTelemetryReported_{};
    std::chrono::steady_clock::time_point lastImGuiFrame_{};
    std::chrono::steady_clock::time_point lastPresentedFrame_{};
    std::uint64_t previewSequence_{};
    std::optional<SceneSnapshot> preview_{};
    std::array<ScreenRect, MaxInteractionRects> interactionRects_{};
    std::uint32_t interactionRectCount_{};
    std::uint32_t pressedButtons_{};
    std::array<std::uint32_t, FrameSampleCapacity> workSamples_{};
    std::array<std::uint32_t, FrameSampleCapacity> intervalSamples_{};
    std::size_t frameSampleCount_{};
};

} // namespace

auto StartOverlayWindow(
        const Config& config,
        OverlaySaveCallback saveCallback,
        OverlayLogCallback infoCallback,
        OverlayLogCallback warningCallback) noexcept -> bool {
    std::lock_guard lock(LifecycleLock);
    if (OverlayThread.joinable()) return false;
    StopRequested.store(false, std::memory_order_release);
    ThreadRunning.store(false, std::memory_order_release);
    AttachedToGame.store(false, std::memory_order_release);
    DeviceReady.store(false, std::memory_order_release);
    MenuOpen.store(
        config.menu.startExpanded || config.overlay.startMenuOpen,
        std::memory_order_release);
    PresentedFrames.store(0, std::memory_order_release);
    try {
        OverlayThread = std::thread([
            config,
            saveCallback,
            infoCallback,
            warningCallback] {
            OverlayRenderer renderer(
                config,
                saveCallback,
                infoCallback,
                warningCallback);
            renderer.Run();
        });
        return true;
    } catch (...) {
        return false;
    }
}

void StopOverlayWindow() noexcept {
    std::lock_guard lock(LifecycleLock);
    StopRequested.store(true, std::memory_order_release);
    if (const auto window = OverlayWindowHandle.load(std::memory_order_acquire);
        window != nullptr) {
        (void)PostMessageW(window, WM_NULL, 0, 0);
    }
    if (OverlayThread.joinable()) OverlayThread.join();
    MenuOpen.store(false, std::memory_order_release);
}

void ToggleOverlayMenu() noexcept {
    auto expected = MenuOpen.load(std::memory_order_acquire);
    while (!MenuOpen.compare_exchange_weak(
        expected,
        !expected,
        std::memory_order_acq_rel,
        std::memory_order_acquire)) {}
}

auto GetOverlayRuntimeStatus() noexcept -> OverlayRuntimeStatus {
    return {
        .threadRunning = ThreadRunning.load(std::memory_order_acquire),
        .attachedToGame = AttachedToGame.load(std::memory_order_acquire),
        .deviceReady = DeviceReady.load(std::memory_order_acquire),
        .menuOpen = MenuOpen.load(std::memory_order_acquire),
        .frames = PresentedFrames.load(std::memory_order_relaxed),
    };
}

} // namespace RuffnecKk::MapSense
