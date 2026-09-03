#include "d3d12_renderer.hpp"

// The DX12 swap-chain/command-queue interception and ImGui submission path
// remain a reduced, adapted derivative of D2RHUD-2.4's D3D12Hook.cpp, used,
// modified, and redistributed with locbones' permission obtained 2026-08-16.

#include "floating_damage.hpp"

#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace D3D12 {
namespace {
using Microsoft::WRL::ComPtr;

static_assert(
    IMGUI_VERSION_NUM == RuffnecKk::OverlayHost::ImGuiVersionNumber,
    "Floating Damage and the MapSense overlay host must use the same ImGui ABI.");

struct FrameContext {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> renderTarget;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
    std::uint64_t fenceValue{};
};

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

HMODULE Module{};
std::array<void*, 150> Methods{};
PresentFn OriginalPresent{};
ExecuteCommandListsFn OriginalExecuteCommandLists{};
ResizeBuffersFn OriginalResizeBuffers{};

std::mutex RenderMutex;
std::mutex HookMutex;
bool HooksInstalled{};
bool MinHookInitializedByRenderer{};
bool RendererInitialized{};
bool ImGuiContextCreated{};
bool ImGuiWin32Initialized{};
bool ImGuiDx12Initialized{};
ImGuiContext* AutonomousImGuiContext{};
ImGuiContext* HostedImGuiContext{};
HWND Window{};
DXGI_FORMAT BackBufferFormat{DXGI_FORMAT_R8G8B8A8_UNORM};

// D2RLoader terminates the process without always invoking the plugin unload
// callback. Keep GPU references in intentionally process-lifetime storage so
// C++ static destruction cannot release them after D3D12Core has shut down.
// Explicit plugin unload and swap-chain resize still release them through
// ResetRenderer().
struct RendererStorage {
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent{};
    std::vector<FrameContext> frames;
};

RendererStorage* const ProcessRendererStorage = new RendererStorage{};
auto& CommandQueue = ProcessRendererStorage->commandQueue;
auto& CommandList = ProcessRendererStorage->commandList;
auto& RtvHeap = ProcessRendererStorage->rtvHeap;
auto& SrvHeap = ProcessRendererStorage->srvHeap;
auto& Fence = ProcessRendererStorage->fence;
auto& FenceEvent = ProcessRendererStorage->fenceEvent;
auto& Frames = ProcessRendererStorage->frames;
std::uint64_t NextFenceValue{1};
std::atomic<ID3D12CommandQueue*> CapturedQueue{};
std::atomic<std::uint32_t> ActiveHookCalls{};
std::chrono::steady_clock::time_point LastFrameTime{};
std::atomic<float> DisplayWidth{1920.0f};
std::atomic<float> DisplayHeight{1080.0f};
std::atomic<DiagnosticLogCallback> DiagnosticLogger{};
std::atomic<std::uint64_t> PresentCalls{};
std::atomic<std::uint64_t> DirectQueueCaptures{};
std::atomic<std::uint64_t> RendererInitAttempts{};
std::atomic<std::uint64_t> RendererInitFailures{};
std::atomic<std::uint64_t> RenderedFrames{};
std::atomic<std::uint32_t> LastInitFailureStage{};
std::atomic<std::uint32_t> DiagnosticMessages{};
std::atomic<bool> ExternalOverlaysAvailable{};
constexpr std::size_t MaximumNamedOverlays = 8;

struct NamedOverlayEntry {
    std::array<char, 64> owner{};
    RuffnecKk::FloatingDamageOverlay::OverlayCallback callback{};
};

std::mutex NamedOverlayMutex;
std::array<NamedOverlayEntry, MaximumNamedOverlays> NamedOverlays{};

enum DiagnosticMessage : std::uint32_t {
    PresentInterceptedMessage = 1u << 0,
    DirectQueueCapturedMessage = 1u << 1,
    RendererInitializedMessage = 1u << 2,
    FirstFrameRenderedMessage = 1u << 3,
    RendererInitFailedMessage = 1u << 4,
    KodiaLoadedMessage = 1u << 5,
    KodiaUnavailableMessage = 1u << 6,
    FenceWaitFailedMessage = 1u << 7,
    FenceSignalFailedMessage = 1u << 8,
};

struct HookCallGuard {
    HookCallGuard() noexcept {
        ActiveHookCalls.fetch_add(1, std::memory_order_acq_rel);
    }
    ~HookCallGuard() {
        ActiveHookCalls.fetch_sub(1, std::memory_order_acq_rel);
    }
    HookCallGuard(const HookCallGuard&) = delete;
    auto operator=(const HookCallGuard&) -> HookCallGuard& = delete;
};

void ResetRendererState() noexcept;

void LogDiagnosticOnce(
    std::uint32_t messageBit,
    const char* message) noexcept {
    const std::uint32_t previous = DiagnosticMessages.fetch_or(
        messageBit, std::memory_order_acq_rel);
    if ((previous & messageBit) != 0)
        return;
    if (const auto logger = DiagnosticLogger.load(std::memory_order_acquire))
        logger(message);
}

auto WaitForFenceValueLocked(std::uint64_t value) noexcept -> bool {
    if (value == 0 || !Fence || !FenceEvent) return true;
    if (Fence->GetCompletedValue() >= value) return true;
    if (FAILED(Fence->SetEventOnCompletion(value, FenceEvent))) return false;
    return WaitForSingleObject(FenceEvent, 5'000U) == WAIT_OBJECT_0;
}

auto WaitForGpuIdleLocked() noexcept -> bool {
    if (!CommandQueue || !Fence || !FenceEvent) return true;
    const std::uint64_t value = NextFenceValue++;
    if (FAILED(CommandQueue->Signal(Fence.Get(), value))) return false;
    return WaitForFenceValueLocked(value);
}

bool FailRendererInitialization(
    std::uint32_t stage,
    const char* message) noexcept {
    RendererInitFailures.fetch_add(1, std::memory_order_relaxed);
    LastInitFailureStage.store(stage, std::memory_order_relaxed);
    LogDiagnosticOnce(RendererInitFailedMessage, message);
    ResetRendererState();
    return false;
}

std::array<ImFont*, kFloatingDamageFontCount> FloatingFonts{};
std::filesystem::path OptionalKodiaFontPath;
// Kodia belongs to the active mod and is not distributed in this DLL. Keep
// its bytes alive across ResetRenderer() so an ImGui rebuild after a 4K/2K
// swap-chain resize can add font index 12 again from the same stable buffer.
std::vector<unsigned char> ModFontBytes;
constexpr int SystemFontCount = 12;
constexpr int KodiaFontIndex = 12;
constexpr std::array<const char*, SystemFontCount> SystemFontFiles{
    "segoeui.ttf", "arial.ttf", "calibri.ttf", "georgia.ttf",
    "verdana.ttf", "tahoma.ttf", "trebuc.ttf", "consola.ttf",
    "times.ttf", "cour.ttf", "comic.ttf", "impact.ttf",
};

void ResetRendererState() noexcept {
    if (!WaitForGpuIdleLocked()) {
        LogDiagnosticOnce(
            FenceWaitFailedMessage,
            "FloatingDamage overlay: timed out while waiting for submitted GPU work.");
    }

    auto* const previousContext = ImGui::GetCurrentContext();
    if (AutonomousImGuiContext != nullptr)
        ImGui::SetCurrentContext(AutonomousImGuiContext);
    if (ImGuiDx12Initialized) {
        ImGui_ImplDX12_Shutdown();
        ImGuiDx12Initialized = false;
    }
    if (ImGuiWin32Initialized) {
        ImGui_ImplWin32_Shutdown();
        ImGuiWin32Initialized = false;
    }
    if (ImGuiContextCreated) {
        if (AutonomousImGuiContext != nullptr)
            ImGui::DestroyContext(AutonomousImGuiContext);
        ImGuiContextCreated = false;
    }
    if (previousContext != nullptr
        && previousContext != AutonomousImGuiContext) {
        ImGui::SetCurrentContext(previousContext);
    }
    else {
        ImGui::SetCurrentContext(nullptr);
    }
    AutonomousImGuiContext = nullptr;
    RendererInitialized = false;
    Window = nullptr;
    Frames.clear();
    CommandList.Reset();
    RtvHeap.Reset();
    SrvHeap.Reset();
    Fence.Reset();
    if (FenceEvent != nullptr) {
        CloseHandle(FenceEvent);
        FenceEvent = nullptr;
    }
    CapturedQueue.store(nullptr, std::memory_order_release);
    CommandQueue.Reset();
    FloatingFonts.fill(nullptr);
    // ModFontBytes deliberately survives renderer resets and resolution
    // changes. ImGui receives it again when the font atlas is recreated.
    NextFenceValue = 1;
    LastFrameTime = {};
}

void ResetRenderer() noexcept {
    std::scoped_lock lock(RenderMutex);
    ResetRendererState();
}

bool EnsureModFontBytes() noexcept {
    if (!ModFontBytes.empty()) return true;
    if (OptionalKodiaFontPath.empty()) return false;

    try {
        std::ifstream file(
            OptionalKodiaFontPath,
            std::ios::binary | std::ios::ate);
        const std::streampos end = file ? file.tellg() : std::streampos{};
        if (!file || end <= 0
                || end > static_cast<std::streampos>(
                    (std::numeric_limits<int>::max)())) {
            return false;
        }

        std::vector<unsigned char> loaded(static_cast<std::size_t>(end));
        file.seekg(0, std::ios::beg);
        if (!file.read(
                reinterpret_cast<char*>(loaded.data()),
                static_cast<std::streamsize>(loaded.size()))) {
            return false;
        }
        ModFontBytes = std::move(loaded);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool LoadFonts() noexcept {
    ImGuiIO& io = ImGui::GetIO();
    std::array<char, MAX_PATH> windowsDirectory{};
    const UINT windowsLength = GetWindowsDirectoryA(
        windowsDirectory.data(),
        static_cast<UINT>(windowsDirectory.size()));
    const bool hasWindowsDirectory = windowsLength > 0
        && windowsLength < windowsDirectory.size();
    bool usedFallback{};

    for (int index = 0; index < SystemFontCount; ++index) {
        ImFontConfig config{};
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        const std::string label = "FloatingDamageSystemFont"
            + std::to_string(index);
        strncpy_s(config.Name, label.c_str(), _TRUNCATE);
        const float rasterSize = index == 0 ? 24.0f : 32.0f;
        if (hasWindowsDirectory) {
            const std::string path = std::string(windowsDirectory.data())
                + "\\Fonts\\" + SystemFontFiles[index];
            FloatingFonts[index] = io.Fonts->AddFontFromFileTTF(
                path.c_str(),
                rasterSize,
                &config,
                io.Fonts->GetGlyphRangesDefault());
        }
        if (!FloatingFonts[index]) {
            usedFallback = true;
            config.SizePixels = rasterSize;
            FloatingFonts[index] = io.Fonts->AddFontDefault(&config);
        }
        if (!FloatingFonts[index]) return false;
    }
    if (usedFallback) {
        if (const auto logger = DiagnosticLogger.load(std::memory_order_acquire)) {
            logger("FloatingDamage overlay: one or more Windows fonts were unavailable; ImGui's built-in font was used without embedding third-party font files.");
        }
    }

    FloatingFonts[KodiaFontIndex] = nullptr;
    if (OptionalKodiaFontPath.empty()) return true;
    if (!EnsureModFontBytes()) {
        LogDiagnosticOnce(
            KodiaUnavailableMessage,
            "FloatingDamage: Kodia could not be read from the active mod; font index 12 falls back to index 0.");
        return true;
    }

    ImFontConfig config{};
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    config.FontDataOwnedByAtlas = false;
    strncpy_s(config.Name, "FloatingDamageFont12-Kodia", _TRUNCATE);
    FloatingFonts[KodiaFontIndex] = io.Fonts->AddFontFromMemoryTTF(
        ModFontBytes.data(),
        static_cast<int>(ModFontBytes.size()),
        32.0f,
        &config,
        io.Fonts->GetGlyphRangesDefault());
    if (!FloatingFonts[KodiaFontIndex]) {
        LogDiagnosticOnce(
            KodiaUnavailableMessage,
            "FloatingDamage: Kodia was rejected by the font atlas; font index 12 falls back to index 0.");
        return true;
    }
    LogDiagnosticOnce(
        KodiaLoadedMessage,
        "FloatingDamage: active-mod Kodia loaded as font index 12.");
    return true;
}

bool InitializeRenderer(IDXGISwapChain3* swapChain) noexcept {
    RendererInitAttempts.fetch_add(1, std::memory_order_relaxed);
    ComPtr<ID3D12Device> device;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))))
        return FailRendererInitialization(
            1, "FloatingDamage overlay: renderer initialization failed at swap-chain device lookup.");

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    if (FAILED(swapChain->GetDesc(&swapDesc)) || swapDesc.BufferCount == 0 || !swapDesc.OutputWindow)
        return FailRendererInitialization(
            2, "FloatingDamage overlay: renderer initialization failed at swap-chain description.");
    Window = swapDesc.OutputWindow;
    BackBufferFormat = swapDesc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
        ? DXGI_FORMAT_R8G8B8A8_UNORM
        : swapDesc.BufferDesc.Format;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&SrvHeap))))
        return FailRendererInitialization(
            3, "FloatingDamage overlay: renderer initialization failed at SRV heap creation.");

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = swapDesc.BufferCount;
    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&RtvHeap))))
        return FailRendererInitialization(
            4, "FloatingDamage overlay: renderer initialization failed at RTV heap creation.");

    Frames.clear();
    Frames.resize(swapDesc.BufferCount);
    auto descriptor = RtvHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT index = 0; index < swapDesc.BufferCount; ++index) {
        auto& frame = Frames[index];
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
            return FailRendererInitialization(
                5, "FloatingDamage overlay: renderer initialization failed at command allocator creation.");
        if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&frame.renderTarget))))
            return FailRendererInitialization(
                6, "FloatingDamage overlay: renderer initialization failed at back-buffer lookup.");
        frame.descriptor = descriptor;
        device->CreateRenderTargetView(frame.renderTarget.Get(), nullptr, descriptor);
        descriptor.ptr += descriptorSize;
    }

    if (FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, Frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&CommandList))))
        return FailRendererInitialization(
            7, "FloatingDamage overlay: renderer initialization failed at command-list creation.");
    if (FAILED(CommandList->Close()))
        return FailRendererInitialization(
            8, "FloatingDamage overlay: renderer initialization failed while closing the command list.");

    if (FAILED(device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&Fence)))) {
        return FailRendererInitialization(
            9, "FloatingDamage overlay: renderer initialization failed at fence creation.");
    }
    FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FenceEvent == nullptr) {
        return FailRendererInitialization(
            10, "FloatingDamage overlay: renderer initialization failed at fence-event creation.");
    }

    IMGUI_CHECKVERSION();
    AutonomousImGuiContext = ImGui::CreateContext();
    if (AutonomousImGuiContext == nullptr) {
        return FailRendererInitialization(
            11, "FloatingDamage overlay: renderer initialization failed at ImGui context creation.");
    }
    ImGuiContextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(Window))
        return FailRendererInitialization(
            12, "FloatingDamage overlay: renderer initialization failed at ImGui Win32 startup.");
    ImGuiWin32Initialized = true;
    if (!ImGui_ImplDX12_Init(
            device.Get(), swapDesc.BufferCount, BackBufferFormat, SrvHeap.Get(),
            SrvHeap->GetCPUDescriptorHandleForHeapStart(), SrvHeap->GetGPUDescriptorHandleForHeapStart()))
        return FailRendererInitialization(
            13, "FloatingDamage overlay: renderer initialization failed at ImGui DirectX 12 startup.");
    ImGuiDx12Initialized = true;
    if (!LoadFonts())
        return FailRendererInitialization(
            14, "FloatingDamage overlay: renderer initialization failed while loading fonts.");
    if (!ImGui_ImplDX12_CreateDeviceObjects())
        return FailRendererInitialization(
            15, "FloatingDamage overlay: renderer initialization failed while creating ImGui device objects.");

    LastFrameTime = std::chrono::steady_clock::now();
    RendererInitialized = true;
    LogDiagnosticOnce(
        RendererInitializedMessage,
        "FloatingDamage overlay: ImGui renderer initialized successfully.");
    return true;
}

auto NextFrameDelta() noexcept -> float {
    const auto now = std::chrono::steady_clock::now();
    const float delta = LastFrameTime.time_since_epoch().count() == 0
        ? (1.0f / 60.0f)
        : std::clamp(
            std::chrono::duration<float>(now - LastFrameTime).count(),
            0.0f,
            0.1f);
    LastFrameTime = now;
    return delta;
}

void RenderContentLocked(
        ImGuiIO& io,
        HWND window) noexcept {
    DisplayWidth.store(io.DisplaySize.x, std::memory_order_relaxed);
    DisplayHeight.store(io.DisplaySize.y, std::memory_order_relaxed);
    FloatingDamage::Update(NextFrameDelta());
    FloatingDamage::Render(ImGui::GetBackgroundDrawList(), io.DisplaySize);
    std::array<RuffnecKk::FloatingDamageOverlay::OverlayCallback,
        MaximumNamedOverlays> externalCallbacks{};
    {
        std::scoped_lock registryLock(NamedOverlayMutex);
        for (std::size_t index = 0; index < NamedOverlays.size(); ++index)
            externalCallbacks[index] = NamedOverlays[index].callback;
    }
    for (const auto callback : externalCallbacks) {
        if (callback != nullptr) {
            callback(
                ImGui::GetForegroundDrawList(),
                io.DisplaySize.x,
                io.DisplaySize.y,
                window);
        }
    }
}

void RenderAutonomousFrameLocked(IDXGISwapChain3* swapChain) noexcept {
    if (!CommandQueue) return;
    if (!RendererInitialized && !InitializeRenderer(swapChain)) return;

    const UINT frameIndex = swapChain->GetCurrentBackBufferIndex();
    if (frameIndex >= Frames.size()) return;
    FrameContext& frame = Frames[frameIndex];
    if (!WaitForFenceValueLocked(frame.fenceValue)) {
        LogDiagnosticOnce(
            FenceWaitFailedMessage,
            "FloatingDamage overlay: skipped a frame because its command allocator was still in use.");
        return;
    }

    ImGui::SetCurrentContext(AutonomousImGuiContext);
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    RenderContentLocked(io, Window);
    ImGui::Render();

    if (FAILED(frame.allocator->Reset())) return;
    if (FAILED(CommandList->Reset(frame.allocator.Get(), nullptr))) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.renderTarget.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    CommandList->ResourceBarrier(1, &barrier);
    CommandList->OMSetRenderTargets(1, &frame.descriptor, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[]{SrvHeap.Get()};
    CommandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), CommandList.Get());
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    CommandList->ResourceBarrier(1, &barrier);
    if (FAILED(CommandList->Close())) return;
    ID3D12CommandList* lists[]{CommandList.Get()};
    CommandQueue->ExecuteCommandLists(1, lists);
    const std::uint64_t fenceValue = NextFenceValue++;
    if (FAILED(CommandQueue->Signal(Fence.Get(), fenceValue))) {
        frame.fenceValue = (std::numeric_limits<std::uint64_t>::max)();
        LogDiagnosticOnce(
            FenceSignalFailedMessage,
            "FloatingDamage overlay: failed to fence a submitted ImGui frame.");
    }
    else {
        frame.fenceValue = fenceValue;
    }
    const std::uint64_t rendered = RenderedFrames.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (rendered == 1) {
        LogDiagnosticOnce(
            FirstFrameRenderedMessage,
            "FloatingDamage overlay: submitted the first ImGui frame to the game command queue.");
    }
}

HRESULT STDMETHODCALLTYPE HookPresent(
        IDXGISwapChain3* swapChain,
        UINT syncInterval,
        UINT flags) noexcept {
    [[maybe_unused]] const HookCallGuard hookCall;
    PresentCalls.fetch_add(1, std::memory_order_relaxed);
    LogDiagnosticOnce(
        PresentInterceptedMessage,
        "FloatingDamage overlay: intercepted the first game Present call.");
    const PresentFn original = OriginalPresent;
    if (original == nullptr) return DXGI_ERROR_INVALID_CALL;
    {
        std::scoped_lock lock(RenderMutex);
        RenderAutonomousFrameLocked(swapChain);
    }
    return original(swapChain, syncInterval, flags);
}

void STDMETHODCALLTYPE HookExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT count,
    ID3D12CommandList* const* lists
) noexcept {
    [[maybe_unused]] const HookCallGuard hookCall;
    const ExecuteCommandListsFn original = OriginalExecuteCommandLists;
    if (queue
        && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT
        && CapturedQueue.load(std::memory_order_acquire) == nullptr) {
        std::scoped_lock lock(RenderMutex);
        if (!CommandQueue) {
            CommandQueue = queue;
            CapturedQueue.store(queue, std::memory_order_release);
            DirectQueueCaptures.fetch_add(1, std::memory_order_relaxed);
            LogDiagnosticOnce(
                DirectQueueCapturedMessage,
                "FloatingDamage overlay: captured the game DirectX 12 command queue.");
        }
    }
    if (original != nullptr) original(queue, count, lists);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(
    IDXGISwapChain3* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags
) noexcept {
    [[maybe_unused]] const HookCallGuard hookCall;
    const ResizeBuffersFn original = OriginalResizeBuffers;
    ResetRenderer();
    return original != nullptr
        ? original(swapChain, bufferCount, width, height, format, flags)
        : DXGI_ERROR_INVALID_CALL;
}

bool BuildMethodTable() noexcept {
    const wchar_t* className = L"RuffnecKkFloatingDamageProbe";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = Module;
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    HWND probeWindow = CreateWindowExW(
        0, className, L"RuffnecKk Floating Damage Probe", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, Module, nullptr);
    if (!probeWindow) return false;

    // D2R ships its own D3D12Core runtime. Importing D3D12CreateDevice in this
    // plugin makes Windows initialize the system D3D12 loader while the game
    // runtime is already active, which can reject the DLL during LoadLibrary.
    // Resolve the entry point from the process' live D3D12 module instead.
    using D3D12CreateDeviceFn = HRESULT(WINAPI*)(
        IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    HMODULE d3d12Module = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12Module)
        d3d12Module = LoadLibraryW(L"d3d12.dll");
    const auto createDevice = d3d12Module
        ? reinterpret_cast<D3D12CreateDeviceFn>(
            GetProcAddress(d3d12Module, "D3D12CreateDevice"))
        : nullptr;
    if (!createDevice) {
        DestroyWindow(probeWindow);
        UnregisterClassW(className, Module);
        return false;
    }

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<IDXGISwapChain> swapChain;
    bool success = false;

    do {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) break;
        if (FAILED(createDevice(
                nullptr,
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device)))) break;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) break;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) break;
        if (FAILED(device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)))) break;

        DXGI_SWAP_CHAIN_DESC swapDesc{};
        swapDesc.BufferDesc.Width = 100;
        swapDesc.BufferDesc.Height = 100;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.OutputWindow = probeWindow;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(queue.Get(), &swapDesc, &swapChain))) break;

        std::memcpy(Methods.data(), *reinterpret_cast<void***>(device.Get()), 44 * sizeof(void*));
        std::memcpy(Methods.data() + 44, *reinterpret_cast<void***>(queue.Get()), 19 * sizeof(void*));
        std::memcpy(Methods.data() + 63, *reinterpret_cast<void***>(allocator.Get()), 9 * sizeof(void*));
        std::memcpy(Methods.data() + 72, *reinterpret_cast<void***>(commandList.Get()), 60 * sizeof(void*));
        std::memcpy(Methods.data() + 132, *reinterpret_cast<void***>(swapChain.Get()), 18 * sizeof(void*));
        success = true;
    } while (false);

    DestroyWindow(probeWindow);
    UnregisterClassW(className, Module);
    return success;
}

bool CreateHook(std::size_t methodIndex, void* target, void** original) noexcept {
    void* address = Methods[methodIndex];
    if (!address) return false;
    const MH_STATUS created = MH_CreateHook(address, target, original);
    if (created != MH_OK) return false;
    if (MH_EnableHook(address) == MH_OK) return true;
    MH_RemoveHook(address);
    if (original) *original = nullptr;
    return false;
}
} // namespace

void SetDllModule(HMODULE module) noexcept {
    Module = module;
}

void SetOptionalKodiaFontPath(const wchar_t* path) noexcept {
    std::scoped_lock lock(RenderMutex);
    try {
        const std::filesystem::path requested = path
            ? std::filesystem::path(path)
            : std::filesystem::path{};
        if (requested == OptionalKodiaFontPath) return;
        OptionalKodiaFontPath = requested;
        // Callers update the asset only before initialization or after
        // ResetRenderer(). Never invalidate memory owned by a live atlas.
        if (!RendererInitialized)
            ModFontBytes.clear();
    }
    catch (...) {
        OptionalKodiaFontPath.clear();
        if (!RendererInitialized)
            ModFontBytes.clear();
    }
}

void SetDiagnosticLogCallback(DiagnosticLogCallback callback) noexcept {
    DiagnosticLogger.store(callback, std::memory_order_release);
}

void SetExternalOverlayAvailability(bool available) noexcept {
    ExternalOverlaysAvailable.store(available, std::memory_order_release);
    if (!available) ClearNamedExternalOverlays();
}

bool RegisterNamedExternalOverlay(
    const char* owner,
    RuffnecKk::FloatingDamageOverlay::OverlayCallback callback) noexcept {
    if (!owner || owner[0] == '\0') return false;
    if (callback
        && !ExternalOverlaysAvailable.load(std::memory_order_acquire)) {
        return false;
    }

    // Present owns RenderMutex while invoking callbacks. Taking it here makes
    // unregister a synchronization point so a callback DLL may safely unload.
    std::scoped_lock renderLock(RenderMutex);
    std::scoped_lock registryLock(NamedOverlayMutex);

    NamedOverlayEntry* empty{};
    for (auto& entry : NamedOverlays) {
        if (entry.callback && std::strcmp(entry.owner.data(), owner) == 0) {
            if (callback) entry.callback = callback;
            else entry = {};
            return true;
        }
        if (!entry.callback && !empty) empty = &entry;
    }
    if (!callback) return true;
    if (!empty) return false;

    strncpy_s(empty->owner.data(), empty->owner.size(), owner, _TRUNCATE);
    empty->callback = callback;
    return true;
}

void ClearNamedExternalOverlays() noexcept {
    std::scoped_lock renderLock(RenderMutex);
    std::scoped_lock registryLock(NamedOverlayMutex);
    NamedOverlays = {};
}

namespace {
ImU32 OverlayColor(
    float red,
    float green,
    float blue,
    float alpha) noexcept {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        std::clamp(red, 0.0f, 1.0f),
        std::clamp(green, 0.0f, 1.0f),
        std::clamp(blue, 0.0f, 1.0f),
        std::clamp(alpha, 0.0f, 1.0f)));
}
} // namespace

void OverlayAddRect(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha,
    float thickness) noexcept {
    if (!drawList || right <= left || bottom <= top) return;
    static_cast<ImDrawList*>(drawList)->AddRect(
        ImVec2(left, top), ImVec2(right, bottom),
        OverlayColor(red, green, blue, alpha),
        0.0f, 0, std::max(thickness, 1.0f));
}

void OverlayAddRectFilled(
    void* drawList,
    float left,
    float top,
    float right,
    float bottom,
    float red,
    float green,
    float blue,
    float alpha) noexcept {
    if (!drawList || right <= left || bottom <= top) return;
    static_cast<ImDrawList*>(drawList)->AddRectFilled(
        ImVec2(left, top), ImVec2(right, bottom),
        OverlayColor(red, green, blue, alpha));
}

bool InstallHooks() noexcept {
    std::scoped_lock hookLock(HookMutex);
    if (HooksInstalled) return true;
    if (!Module || !BuildMethodTable()) return false;
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (initialized == MH_OK) MinHookInitializedByRenderer = true;
    const auto failInstall = []() noexcept {
        OriginalResizeBuffers = nullptr;
        OriginalPresent = nullptr;
        OriginalExecuteCommandLists = nullptr;
        Methods = {};
        if (MinHookInitializedByRenderer) {
            MH_Uninitialize();
            MinHookInitializedByRenderer = false;
        }
        return false;
    };
    if (!CreateHook(54, reinterpret_cast<void*>(HookExecuteCommandLists), reinterpret_cast<void**>(&OriginalExecuteCommandLists))) {
        return failInstall();
    }
    if (!CreateHook(140, reinterpret_cast<void*>(HookPresent), reinterpret_cast<void**>(&OriginalPresent))) {
        MH_DisableHook(Methods[54]);
        MH_RemoveHook(Methods[54]);
        return failInstall();
    }
    if (!CreateHook(145, reinterpret_cast<void*>(HookResizeBuffers), reinterpret_cast<void**>(&OriginalResizeBuffers))) {
        MH_DisableHook(Methods[140]);
        MH_RemoveHook(Methods[140]);
        MH_DisableHook(Methods[54]);
        MH_RemoveHook(Methods[54]);
        return failInstall();
    }
    HooksInstalled = true;
    return true;
}

void RemoveHooks() noexcept {
    std::scoped_lock hookLock(HookMutex);
    if (HooksInstalled) {
        MH_DisableHook(Methods[145]);
        MH_DisableHook(Methods[140]);
        MH_DisableHook(Methods[54]);
        while (ActiveHookCalls.load(std::memory_order_acquire) != 0)
            Sleep(1U);
    }
    ResetRenderer();
    if (HooksInstalled) {
        MH_RemoveHook(Methods[145]);
        MH_RemoveHook(Methods[140]);
        MH_RemoveHook(Methods[54]);
    }
    HooksInstalled = false;
    OriginalResizeBuffers = nullptr;
    OriginalPresent = nullptr;
    OriginalExecuteCommandLists = nullptr;
    Methods = {};
    if (MinHookInitializedByRenderer) {
        MH_Uninitialize();
        MinHookInitializedByRenderer = false;
    }
}

OverlayDiagnostics GetOverlayDiagnostics() noexcept {
    std::scoped_lock hookLock(HookMutex);
    std::scoped_lock renderLock(RenderMutex);
    return OverlayDiagnostics{
        .presentCalls = PresentCalls.load(std::memory_order_relaxed),
        .directQueueCaptures = DirectQueueCaptures.load(std::memory_order_relaxed),
        .rendererInitAttempts = RendererInitAttempts.load(std::memory_order_relaxed),
        .rendererInitFailures = RendererInitFailures.load(std::memory_order_relaxed),
        .renderedFrames = RenderedFrames.load(std::memory_order_relaxed),
        .lastInitFailureStage = LastInitFailureStage.load(std::memory_order_relaxed),
        .hooksInstalled = HooksInstalled,
        .commandQueueReady = static_cast<bool>(CommandQueue),
        .rendererInitialized = RendererInitialized,
    };
}

ImFont* GetFloatingDamageFont(int index) noexcept {
    if (index < 0 || index >= kFloatingDamageFontCount) return nullptr;
    if (auto* const configured = FloatingFonts[static_cast<std::size_t>(index)])
        return configured;
    if (ImGui::GetCurrentContext() != nullptr) {
        auto* const atlas = ImGui::GetIO().Fonts;
        if (atlas != nullptr && !atlas->Fonts.empty())
            return atlas->Fonts.front();
    }
    return nullptr;
}

void GetDisplaySize(float& width, float& height) noexcept {
    width = DisplayWidth.load(std::memory_order_relaxed);
    height = DisplayHeight.load(std::memory_order_relaxed);
}

bool AttachExternalImGuiContext(
        const RuffnecKk::OverlayHost::FrameContextV2* frame) noexcept {
    if (frame == nullptr
        || frame->structSize < RuffnecKk::OverlayHost::FrameContextV2Size
        || frame->version != RuffnecKk::OverlayHost::ApiVersion2
        || frame->imguiContext == nullptr) {
        return false;
    }

    std::scoped_lock lock(RenderMutex);
    auto* const context = static_cast<ImGuiContext*>(frame->imguiContext);
    if (HostedImGuiContext == context && FloatingFonts[0] != nullptr)
        return true;
    if (AutonomousImGuiContext != nullptr) return false;

    auto* const previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);
    FloatingFonts.fill(nullptr);
    HostedImGuiContext = context;
    const bool loaded = LoadFonts();
    if (!loaded) FloatingFonts.fill(nullptr);
    ImGui::SetCurrentContext(previousContext == context
        ? context
        : previousContext);
    LastFrameTime = {};
    return loaded;
}

void DetachExternalImGuiContext(
        const RuffnecKk::OverlayHost::FrameContextV2* frame) noexcept {
    std::scoped_lock lock(RenderMutex);
    if (HostedImGuiContext == nullptr) return;
    if (frame != nullptr
        && frame->imguiContext != nullptr
        && frame->imguiContext != HostedImGuiContext) {
        return;
    }
    if (ImGui::GetCurrentContext() == HostedImGuiContext)
        ImGui::SetCurrentContext(nullptr);
    HostedImGuiContext = nullptr;
    FloatingFonts.fill(nullptr);
    LastFrameTime = {};
}

void RenderAsExternalClient(
        const RuffnecKk::OverlayHost::FrameContextV2* frame) noexcept {
    if (frame == nullptr
        || frame->structSize < RuffnecKk::OverlayHost::FrameContextV2Size
        || frame->version != RuffnecKk::OverlayHost::ApiVersion2
        || frame->imguiContext == nullptr
        || frame->window == nullptr) {
        return;
    }

    std::scoped_lock lock(RenderMutex);
    auto* const previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(
        static_cast<ImGuiContext*>(frame->imguiContext));
    ImGuiIO& io = ImGui::GetIO();
    DisplayWidth.store(frame->displayWidth, std::memory_order_relaxed);
    DisplayHeight.store(frame->displayHeight, std::memory_order_relaxed);
    RenderContentLocked(io, frame->window);
    ImGui::SetCurrentContext(previousContext);
    const std::uint64_t rendered = RenderedFrames.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (rendered == 1) {
        LogDiagnosticOnce(
            FirstFrameRenderedMessage,
            "FloatingDamage overlay: rendered its first frame through the MapSense host.");
    }
}

} // namespace D3D12
