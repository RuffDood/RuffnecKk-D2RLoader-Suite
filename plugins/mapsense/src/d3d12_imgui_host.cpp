#include "d3d12_imgui_host.hpp"
#include "automap_sprite_package.hpp"

// The D3D12 interception and ImGui submission path is an adapted derivative
// of locbones/D2RHUD-2.4 at b9373f8508282948ceb3e2b56f892d9eba475744,
// used, modified, and redistributed with permission obtained 2026-08-16.

#include <MinHook.h>
#include <CommCtrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

namespace RuffnecKk::MapSense {
namespace {
using Microsoft::WRL::ComPtr;

static_assert(
    IMGUI_VERSION_NUM
        == static_cast<int>(RuffnecKk::OverlayHost::ImGuiVersionNumber),
    "MapSense and overlay clients must use the governed ImGui commit.");

struct FrameContext {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> renderTarget;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
    std::uint64_t fenceValue{};
};

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*,
    IUnknown*,
    HWND,
    const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
    IDXGIOutput*,
    IDXGISwapChain1**);
using CreateSwapChainForCoreWindowFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*,
    IUnknown*,
    IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*,
    IDXGIOutput*,
    IDXGISwapChain1**);
using CreateSwapChainForCompositionFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*,
    IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*,
    IDXGIOutput*,
    IDXGISwapChain1**);
using D3D12CreateDeviceFn = HRESULT(WINAPI*)(
    IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

constexpr std::size_t MethodCount = 150;
constexpr std::size_t PresentMethod = 140;
constexpr std::size_t ResizeBuffersMethod = 145;
constexpr std::size_t FactoryMethodCount = 25;
constexpr std::size_t CreateSwapChainMethod = 10;
constexpr std::size_t CreateSwapChainForHwndMethod = 15;
constexpr std::size_t CreateSwapChainForCoreWindowMethod = 16;
constexpr std::size_t CreateSwapChainForCompositionMethod = 24;
constexpr DWORD FenceWaitMilliseconds = 5'000;
constexpr std::size_t MaximumExternalClients = 8;
constexpr std::size_t MaximumSwapChainQueueBindings = 16;
constexpr UINT MapSenseSrvDescriptorCount = 4U;
constexpr UINT PrimeMhChestSrvDescriptorIndex = 1U;
constexpr UINT PrimeMhSuperChestSrvDescriptorIndex = 2U;
constexpr UINT AutomapSpriteSrvDescriptorIndex = 3U;

[[nodiscard]] auto WaitForFenceValueLocked(
    std::uint64_t value) noexcept -> bool;

// Exact transparent PrimeMH artwork by Joffreybesos, embedded with permission
// obtained by Vincent Barriere on 2026-08-30. Keeping the original compressed
// PNG bytes in the DLL preserves the authorized pixels and avoids fragile
// companion files.
// Source: joffreybesos/PrimeMH@master src/gui/images/chest.png
// SHA-256: BA429FA42223DE03E4B347E0AE5F28CE188C4CBB140687C0A526A180BF869BDC
constexpr std::string_view PrimeMhChestPngBase64 = R"PRIMEPNG(
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
)PRIMEPNG";
// Source: joffreybesos/PrimeMH@master src/gui/images/superchest.png
// SHA-256: D3DC7EE43A74B7BEA491576DC5B7E418D0CB22D38280C773FAAC0DF5D2372D2B
constexpr std::string_view PrimeMhSuperChestPngBase64 = R"PRIMEPNG(
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
Z0dZOhS4GF3o7NuALKNNkV7codWtkKcys7EHX9T06l/dcc/wvO4MWqFAG8pVLhI1VXM5GFVPH5QXNZ9T0WBs/v359Hhnbv7YwZOr
5Gy+hIlUkNFkvV0lt4WWXlSg481l5giI/HqAWuQKQRsv2YDIx/l1LNxmDTe0dBs87VdhLLoHjxxpx5PPTyKbXoSHqUJbR6+dOPrD
bVxJAwGfnk14Cp+rVdIDw6Pzq771g9nf+dbDfIkEux9XBv8YOqunL1pbnR+Izl+Qz0+/Wde16slVcoppvVTJBWsz3H4XfLR1HwXq
6lqNrp4eZqZM0eOSQ4SQ7r0Yc23XYlh/LQ7V2nFkqo7R0WHWRlM0lwadPEsIXXyRUU9EfBNWefIfT4w8f8P4iZFVwxOz78lk8seZ
VNr7AeIrzgW9JEg3hhfv3tQ5523U0l84VZWcJDhm/CpWvV18k5OakqcvqKGoXwxv++uQbQxgfN6L4yfEtyxVyQ2CYH8B8PjojRv1
WrU0ND+5/8+mxvbtnJqZ2HBi/PivFbNz9xGjvCxCq9qfK3pperZMW3qLP+91O7c5rMrRU1XJdSZ4Xq7ka159AXq7Yyz563juwD4W
hYWTq2SmAvM1WuJePv9XfO5NC5OHLp2deO53q9XMoUbD3jL7Yeh6meicgCLU6544wCR2e60w8eFKIVdtrZLLVQMjI/vwqX/4LI6N
TsmXR4Z2P7PbkOXy6DXdxZiru45yOl9luf32Qmbs+tnx53+7lMvcR7Bkz/i80mmBIs4wnUujf6DTLgT7DN3egJLjai4Aqabl+IKe
VHVzX+Xj5XJyoFFb+CarZFMSvlotR5dQQnt7t+QOZi41V2EucbhumF/I5cq/klqc+pmZsb031gpTt3V1dNxZr9fkFzkW+y5N4DxT
860vFpJPJvkVpfycNBaO2WAJyeaUc+knbjZocv/gZHgwGE68l8lfKp9Pvr+ja/N4OBx9vF4vP51enNrLGmvaqFaKzC7rNA3L5wth
Ve86atNRZsILzHU6bf9SKizQj9JjOelwaZ6yASXXCfZSEmbq9EesY7jE1fw8K/Iwo1P8LEMy8H8BBfduNhgw6/4AAAAASUVORK5C
YII=
)PRIMEPNG";

std::array<void*, MethodCount> Methods{};
std::array<void*, FactoryMethodCount> FactoryMethods{};
PresentFn OriginalPresent{};
ResizeBuffersFn OriginalResizeBuffers{};
CreateSwapChainFn OriginalCreateSwapChain{};
CreateSwapChainForHwndFn OriginalCreateSwapChainForHwnd{};
CreateSwapChainForCoreWindowFn OriginalCreateSwapChainForCoreWindow{};
CreateSwapChainForCompositionFn OriginalCreateSwapChainForComposition{};

// A recursive mutex prevents an unexpected synchronous window message (for
// example SetCapture/ReleaseCapture) from deadlocking the Present thread while
// still serializing ImGui backend access with the D2R window subclass.
std::recursive_mutex HostMutex;
D3D12ImGuiHostCallbacks Callbacks{};
bool Configured{};
bool HooksInstalled{};
bool FactoryHooksInstalled{};
bool MinHookReady{};
bool MinHookInitializedByHost{};
bool RendererInitialized{};
bool ImGuiContextCreated{};
bool ImGuiWin32Initialized{};
bool ImGuiDx12Initialized{};
bool ContextCallbacksActive{};
bool RendererResetPending{};
bool RendererPoisoned{};
HWND GameWindow{};
IDXGISwapChain3* ActiveSwapChain{};
DXGI_FORMAT BackBufferFormat{DXGI_FORMAT_R8G8B8A8_UNORM};

struct SwapChainQueueBinding {
    ComPtr<IUnknown> swapChainIdentity;
    ComPtr<ID3D12CommandQueue> commandQueue;
};

std::vector<SwapChainQueueBinding> SwapChainQueueBindings{};

// Process-lifetime storage avoids releasing D3D12 references during static
// destruction after D2R has already unloaded D3D12Core. Explicit shutdown and
// ResizeBuffers still release every live reference deterministically.
struct RendererStorage {
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12Resource> primeMhChestTexture;
    ComPtr<ID3D12Resource> primeMhSuperChestTexture;
    std::vector<ComPtr<ID3D12Resource>> primeMhTextureUploads;
    ComPtr<ID3D12Resource> automapSpriteTexture;
    ComPtr<ID3D12Resource> automapSpriteUpload;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent{};
    std::vector<FrameContext> frames;
};

RendererStorage* const ProcessRendererStorage = new RendererStorage{};
auto& CommandQueue = ProcessRendererStorage->commandQueue;
auto& CommandList = ProcessRendererStorage->commandList;
auto& RtvHeap = ProcessRendererStorage->rtvHeap;
auto& SrvHeap = ProcessRendererStorage->srvHeap;
auto& PrimeMhChestTexture = ProcessRendererStorage->primeMhChestTexture;
auto& PrimeMhSuperChestTexture =
    ProcessRendererStorage->primeMhSuperChestTexture;
auto& PrimeMhTextureUploads =
    ProcessRendererStorage->primeMhTextureUploads;
auto& AutomapSpriteTexture =
    ProcessRendererStorage->automapSpriteTexture;
auto& AutomapSpriteUpload =
    ProcessRendererStorage->automapSpriteUpload;
auto& Fence = ProcessRendererStorage->fence;
auto& FenceEvent = ProcessRendererStorage->fenceEvent;
auto& Frames = ProcessRendererStorage->frames;
std::uint64_t NextFenceValue{1};
std::chrono::steady_clock::time_point LastFrameTime{};

std::atomic<ID3D12CommandQueue*> CapturedQueue{};
std::atomic<bool> UnboundSwapChainWarningLogged{};
std::atomic<bool> DeviceRemovalWarningLogged{};
std::atomic<bool> MenuOpen{};
std::atomic<bool> AwaitingFirstBounds{};
std::atomic<std::uint32_t> OwnedMouseButtons{};
std::atomic<std::uint32_t> CanceledMouseButtons{};
std::atomic<HWND> PublishedGameWindow{};
std::atomic<HWND> InputSubclassWindow{};
std::atomic<HWND> RequestedSubclassWindow{};
std::atomic<bool> InputSubclassInstalled{};
std::atomic<bool> InputInstallQueued{};
std::atomic<bool> InputQueueWarningLogged{};
std::atomic<std::uint32_t> ActiveHookCalls{};
std::atomic<bool> ClientsAccepting{true};

// BoundsVersion is a small seqlock. Present is the only writer; the window
// subclass may read from another thread without observing a torn rectangle.
std::atomic<std::uint64_t> BoundsVersion{};
std::atomic<bool> BoundsVisible{};
std::atomic<float> BoundsLeft{};
std::atomic<float> BoundsTop{};
std::atomic<float> BoundsRight{};
std::atomic<float> BoundsBottom{};

std::atomic<std::uint64_t> PresentCalls{};
std::atomic<std::uint64_t> ExactQueueBindings{};
std::atomic<std::uint64_t> RendererInitAttempts{};
std::atomic<std::uint64_t> RendererInitFailures{};
std::atomic<std::uint64_t> RenderedFrames{};
std::atomic<std::uint32_t> LastInitFailureStage{};
std::atomic<bool> HooksInstalledPublished{};
std::atomic<bool> RendererInitializedPublished{};
std::atomic<bool> InputSubclassInstalledPublished{};
std::atomic<bool> PrimeMhTexturesReadyPublished{};
std::atomic<bool> AutomapSpriteAtlasReadyPublished{};
std::atomic<bool> ConfiguredPublished{};
std::atomic<D3D12ImGuiLogCallback> InfoLogger{};
std::atomic<D3D12ImGuiLogCallback> WarningLogger{};

constexpr float MapSenseDefaultFontSize = 15.0F;
constexpr std::size_t MaximumMapSenseSystemFontBytes =
    64U * 1'024U * 1'024U;
std::vector<std::uint8_t> MapSenseBaseFontBytes;
std::vector<std::uint8_t> MapSenseJapaneseFontBytes;
std::vector<std::uint8_t> MapSenseKoreanFontBytes;
std::vector<std::uint8_t> MapSenseSimplifiedChineseFontBytes;
std::vector<std::uint8_t> MapSenseTraditionalChineseFontBytes;
std::wstring MapSenseAutomapFontPath;
std::wstring MapSenseAutomapSpritePath;
std::vector<std::uint8_t> MapSenseAutomapFontBytes;
ImFont* MapSenseAutomapFont{};
D3D12ImGuiTextureView PrimeMhChestTextureView{};
D3D12ImGuiTextureView PrimeMhSuperChestTextureView{};
D3D12ImGuiTextureView AutomapSpriteTextureView{};

[[nodiscard]] auto IsRegularFontFile(const char* path) noexcept -> bool {
    if (path == nullptr || path[0] == '\0') return false;
    const auto attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

[[nodiscard]] auto LoadSystemFontFile(
        const char* path,
        std::vector<std::uint8_t>& bytes) noexcept -> bool {
    bytes.clear();
    if (!IsRegularFontFile(path)) return false;
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto length = input.tellg();
        if (length <= 0
            || static_cast<std::uint64_t>(length)
                > MaximumMapSenseSystemFontBytes) {
            return false;
        }
        bytes.resize(static_cast<std::size_t>(length));
        input.seekg(0, std::ios::beg);
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount()
                != static_cast<std::streamsize>(bytes.size())) {
            bytes.clear();
            return false;
        }
        return true;
    } catch (...) {
        bytes.clear();
        return false;
    }
}

[[nodiscard]] auto LoadAutomapFontFile(
        const std::wstring& path,
        std::vector<std::uint8_t>& bytes) noexcept -> bool {
    bytes.clear();
    if (path.empty()) return false;
    try {
        const std::filesystem::path fontPath(path);
        std::error_code error;
        if (!std::filesystem::is_regular_file(fontPath, error) || error)
            return false;
        std::ifstream input(fontPath, std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto length = input.tellg();
        if (length <= 0
            || static_cast<std::uint64_t>(length)
                > MaximumMapSenseSystemFontBytes) {
            return false;
        }
        bytes.resize(static_cast<std::size_t>(length));
        input.seekg(0, std::ios::beg);
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount()
                != static_cast<std::streamsize>(bytes.size())) {
            bytes.clear();
            return false;
        }
        return true;
    } catch (...) {
        bytes.clear();
        return false;
    }
}

// Run before the Present hooks are installed. Renderer initialization may be
// reached from HookPresent, so that path only consumes these immutable bytes
// and never performs filesystem I/O.
void PreloadLocalizedMapSenseFonts() noexcept {
    static_cast<void>(LoadAutomapFontFile(
        MapSenseAutomapFontPath,
        MapSenseAutomapFontBytes));
    static_cast<void>(LoadSystemFontFile(
        "C:\\Windows\\Fonts\\segoeui.ttf",
        MapSenseBaseFontBytes));
    static_cast<void>(LoadSystemFontFile(
        "C:\\Windows\\Fonts\\msgothic.ttc",
        MapSenseJapaneseFontBytes));
    static_cast<void>(LoadSystemFontFile(
        "C:\\Windows\\Fonts\\malgun.ttf",
        MapSenseKoreanFontBytes));
    static_cast<void>(LoadSystemFontFile(
        "C:\\Windows\\Fonts\\msyh.ttc",
        MapSenseSimplifiedChineseFontBytes));
    static_cast<void>(LoadSystemFontFile(
        "C:\\Windows\\Fonts\\msjh.ttc",
        MapSenseTraditionalChineseFontBytes));
}

[[nodiscard]] auto AddSystemFont(
        ImFontAtlas& atlas,
        const std::vector<std::uint8_t>& bytes,
        const ImWchar* ranges,
        bool merge) noexcept -> ImFont* {
    if (bytes.empty()
        || bytes.size()
            > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return nullptr;
    }
    ImFontConfig config{};
    config.MergeMode = merge;
    config.FontDataOwnedByAtlas = false;
    config.PixelSnapH = false;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.RasterizerDensity = 1.25F;
    return atlas.AddFontFromMemoryTTF(
        const_cast<std::uint8_t*>(bytes.data()),
        static_cast<int>(bytes.size()),
        MapSenseDefaultFontSize,
        &config,
        ranges);
}

// MapSense labels are resolved from D2R's local language before the renderer
// starts. The default ImGui bitmap font cannot represent most of those UTF-8
// strings, so build one merged atlas from fonts already installed by Windows.
// No font is redistributed with the plugin. Missing optional language fonts
// degrade independently while the Latin/Cyrillic UI remains available.
[[nodiscard]] auto AddLocalizedMapSenseFont(ImFontAtlas& atlas) noexcept
        -> ImFont* {
    static constexpr ImWchar ExtendedLatinAndPunctuationRanges[]{
        0x0100, 0x024F, // Latin Extended A/B and IPA additions
        0x1E00, 0x1EFF, // Latin Extended Additional
        0x2000, 0x206F, // General punctuation, including typographic quotes
        0x20A0, 0x20CF, // Currency symbols
        0,
    };
    // ImFontConfig retains GlyphRanges until the atlas is built later by the
    // renderer backend. Keep this generated range alive across that deferred
    // build instead of handing ImGui a pointer into a local ImVector.
    static ImVector<ImWchar> baseRanges;
    if (baseRanges.empty()) {
        ImFontGlyphRangesBuilder baseBuilder;
        baseBuilder.AddRanges(atlas.GetGlyphRangesDefault());
        baseBuilder.AddRanges(atlas.GetGlyphRangesGreek());
        baseBuilder.AddRanges(atlas.GetGlyphRangesCyrillic());
        baseBuilder.AddRanges(atlas.GetGlyphRangesVietnamese());
        baseBuilder.AddRanges(ExtendedLatinAndPunctuationRanges);
        baseBuilder.BuildRanges(&baseRanges);
    }

    ImFont* font = AddSystemFont(
        atlas,
        MapSenseBaseFontBytes,
        baseRanges.Data,
        false);
    if (font == nullptr) font = atlas.AddFontDefault();
    if (font == nullptr) return nullptr;

    // Every merge targets the first font. ImGui skips duplicate codepoints and
    // only adds glyphs that the selected system font actually contains.
    static_cast<void>(AddSystemFont(
        atlas,
        MapSenseJapaneseFontBytes,
        atlas.GetGlyphRangesJapanese(),
        true));
    static_cast<void>(AddSystemFont(
        atlas,
        MapSenseKoreanFontBytes,
        atlas.GetGlyphRangesKorean(),
        true));
    static_cast<void>(AddSystemFont(
        atlas,
        MapSenseSimplifiedChineseFontBytes,
        atlas.GetGlyphRangesChineseFull(),
        true));
    static_cast<void>(AddSystemFont(
        atlas,
        MapSenseTraditionalChineseFontBytes,
        atlas.GetGlyphRangesChineseFull(),
        true));
    return font;
}

// D2R ships Exocet in the active mod package. Load it from that player-owned
// path instead of redistributing the font. It remains a separate atlas font:
// the settings UI and any label containing a glyph absent from Exocet keep the
// fully localized Windows fallback above.
[[nodiscard]] auto AddAutomapMapSenseFont(ImFontAtlas& atlas) noexcept
        -> ImFont* {
    if (MapSenseAutomapFontBytes.empty()) return nullptr;
    static constexpr ImWchar WesternAutomapRanges[]{
        0x0020, 0x00FF, // Basic Latin and Latin-1 Supplement
        0x0100, 0x024F, // Latin Extended A/B and IPA additions
        0x0370, 0x052F, // Greek, Coptic, Cyrillic and Cyrillic Supplement
        0x1E00, 0x1EFF, // Latin Extended Additional
        0x2000, 0x206F, // General punctuation
        0x20A0, 0x20CF, // Currency symbols
        0,
    };
    return AddSystemFont(
        atlas,
        MapSenseAutomapFontBytes,
        WesternAutomapRanges,
        false);
}

struct DecodedRgbaImage {
    UINT width{};
    UINT height{};
    std::vector<std::uint8_t> pixels{};
};

class ScopedComInitialization final {
public:
    ScopedComInitialization() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

    ~ScopedComInitialization() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }

    ScopedComInitialization(const ScopedComInitialization&) = delete;
    auto operator=(const ScopedComInitialization&)
        -> ScopedComInitialization& = delete;

    [[nodiscard]] auto ready() const noexcept -> bool {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_{};
};

[[nodiscard]] auto DecodeEmbeddedBase64(
        std::string_view encoded,
        std::vector<std::uint8_t>& decoded) noexcept -> bool {
    decoded.clear();
    if (encoded.empty()
        || encoded.size() > static_cast<std::size_t>(MAXDWORD)) {
        return false;
    }
    DWORD decodedSize{};
    if (CryptStringToBinaryA(
            encoded.data(),
            static_cast<DWORD>(encoded.size()),
            CRYPT_STRING_BASE64,
            nullptr,
            &decodedSize,
            nullptr,
            nullptr) == FALSE
        || decodedSize == 0U
        || decodedSize > 1U * 1'024U * 1'024U) {
        return false;
    }
    try {
        decoded.resize(decodedSize);
    } catch (...) {
        return false;
    }
    if (CryptStringToBinaryA(
            encoded.data(),
            static_cast<DWORD>(encoded.size()),
            CRYPT_STRING_BASE64,
            decoded.data(),
            &decodedSize,
            nullptr,
            nullptr) == FALSE) {
        decoded.clear();
        return false;
    }
    decoded.resize(decodedSize);
    return true;
}

[[nodiscard]] auto DecodePrimeMhPng(
        std::string_view encoded,
        DecodedRgbaImage& image) noexcept -> bool {
    image = {};
    std::vector<std::uint8_t> pngBytes;
    if (!DecodeEmbeddedBase64(encoded, pngBytes)
        || pngBytes.size() > static_cast<std::size_t>(MAXDWORD)) {
        return false;
    }

    ScopedComInitialization com;
    if (!com.ready()) return false;

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))
        || FAILED(factory->CreateStream(&stream))
        || FAILED(stream->InitializeFromMemory(
            pngBytes.data(), static_cast<DWORD>(pngBytes.size())))
        || FAILED(factory->CreateDecoderFromStream(
            stream.Get(),
            nullptr,
            WICDecodeMetadataCacheOnLoad,
            &decoder))
        || FAILED(decoder->GetFrame(0U, &frame))
        || FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        return false;
    }

    UINT width{};
    UINT height{};
    if (FAILED(converter->GetSize(&width, &height))
        || width == 0U
        || height == 0U
        || width > 512U
        || height > 512U
        || width > (std::numeric_limits<UINT>::max)() / 4U) {
        return false;
    }
    const UINT stride = width * 4U;
    if (height > (std::numeric_limits<UINT>::max)() / stride)
        return false;
    const UINT byteCount = stride * height;
    try {
        image.pixels.resize(byteCount);
    } catch (...) {
        return false;
    }
    if (FAILED(converter->CopyPixels(
            nullptr,
            stride,
            byteCount,
            image.pixels.data()))) {
        image = {};
        return false;
    }
    image.width = width;
    image.height = height;
    return true;
}

[[nodiscard]] auto RecordPrimeMhTextureUpload(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        const DecodedRgbaImage& image,
        UINT descriptorIndex,
        ComPtr<ID3D12Resource>& texture,
        ComPtr<ID3D12Resource>& upload,
        D3D12ImGuiTextureView& view) noexcept -> bool {
    texture.Reset();
    upload.Reset();
    view = {};
    if (device == nullptr
        || commandList == nullptr
        || !SrvHeap
        || descriptorIndex >= MapSenseSrvDescriptorCount
        || image.width == 0U
        || image.height == 0U
        || image.pixels.size()
            != static_cast<std::size_t>(image.width) * image.height * 4U) {
        return false;
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1U;
    defaultHeap.VisibleNodeMask = 1U;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = image.width;
    textureDesc.Height = image.height;
    textureDesc.DepthOrArraySize = 1U;
    textureDesc.MipLevels = 1U;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1U;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)))) {
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount{};
    UINT64 rowByteCount{};
    UINT64 uploadByteCount{};
    device->GetCopyableFootprints(
        &textureDesc,
        0U,
        1U,
        0U,
        &footprint,
        &rowCount,
        &rowByteCount,
        &uploadByteCount);
    const UINT sourceRowBytes = image.width * 4U;
    if (uploadByteCount == 0U
        || rowCount != image.height
        || rowByteCount < sourceRowBytes) {
        texture.Reset();
        return false;
    }

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1U;
    uploadHeap.VisibleNodeMask = 1U;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadByteCount;
    uploadDesc.Height = 1U;
    uploadDesc.DepthOrArraySize = 1U;
    uploadDesc.MipLevels = 1U;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1U;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload)))) {
        texture.Reset();
        return false;
    }

    void* mapped{};
    D3D12_RANGE noReadRange{0U, 0U};
    if (FAILED(upload->Map(0U, &noReadRange, &mapped)) || mapped == nullptr) {
        upload.Reset();
        texture.Reset();
        return false;
    }
    auto* const destination = static_cast<std::uint8_t*>(mapped)
        + footprint.Offset;
    for (UINT row = 0U; row < image.height; ++row) {
        std::memcpy(
            destination
                + static_cast<std::size_t>(row)
                    * footprint.Footprint.RowPitch,
            image.pixels.data()
                + static_cast<std::size_t>(row) * sourceRowBytes,
            sourceRowBytes);
    }
    D3D12_RANGE writtenRange{
        static_cast<SIZE_T>(footprint.Offset),
        static_cast<SIZE_T>(footprint.Offset + uploadByteCount),
    };
    upload->Unmap(0U, &writtenRange);

    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = texture.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = 0U;
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = upload.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(
        &destinationLocation,
        0U,
        0U,
        0U,
        &sourceLocation,
        nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1U, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0U;
    srvDesc.Texture2D.MipLevels = 1U;
    auto cpuHandle = SrvHeap->GetCPUDescriptorHandleForHeapStart();
    auto gpuHandle = SrvHeap->GetGPUDescriptorHandleForHeapStart();
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpuHandle.ptr += static_cast<SIZE_T>(descriptorIndex) * descriptorSize;
    gpuHandle.ptr += static_cast<UINT64>(descriptorIndex) * descriptorSize;
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);
    view = D3D12ImGuiTextureView{
        .textureId = gpuHandle.ptr,
        .width = image.width,
        .height = image.height,
    };
    return true;
}

[[nodiscard]] auto InitializePrimeMhChestTexturesLocked(
        ID3D12Device* device) noexcept -> bool {
    PrimeMhChestTextureView = {};
    PrimeMhSuperChestTextureView = {};
    PrimeMhChestTexture.Reset();
    PrimeMhSuperChestTexture.Reset();
    PrimeMhTextureUploads.clear();
    if (device == nullptr
        || !CommandQueue
        || !CommandList
        || Frames.empty()
        || !Frames[0].allocator
        || !Fence
        || !FenceEvent) {
        return false;
    }

    DecodedRgbaImage chestImage;
    DecodedRgbaImage superChestImage;
    if (!DecodePrimeMhPng(PrimeMhChestPngBase64, chestImage)
        || !DecodePrimeMhPng(
            PrimeMhSuperChestPngBase64, superChestImage)
        || chestImage.width != 58U
        || chestImage.height != 50U
        || superChestImage.width != 69U
        || superChestImage.height != 110U) {
        return false;
    }

    if (FAILED(Frames[0].allocator->Reset())
        || FAILED(CommandList->Reset(Frames[0].allocator.Get(), nullptr))) {
        return false;
    }

    ComPtr<ID3D12Resource> chestUpload;
    ComPtr<ID3D12Resource> superChestUpload;
    const bool recorded = RecordPrimeMhTextureUpload(
            device,
            CommandList.Get(),
            chestImage,
            PrimeMhChestSrvDescriptorIndex,
            PrimeMhChestTexture,
            chestUpload,
            PrimeMhChestTextureView)
        && RecordPrimeMhTextureUpload(
            device,
            CommandList.Get(),
            superChestImage,
            PrimeMhSuperChestSrvDescriptorIndex,
            PrimeMhSuperChestTexture,
            superChestUpload,
            PrimeMhSuperChestTextureView);
    if (!recorded) {
        static_cast<void>(CommandList->Close());
        PrimeMhChestTextureView = {};
        PrimeMhSuperChestTextureView = {};
        PrimeMhChestTexture.Reset();
        PrimeMhSuperChestTexture.Reset();
        return false;
    }

    try {
        PrimeMhTextureUploads.push_back(std::move(chestUpload));
        PrimeMhTextureUploads.push_back(std::move(superChestUpload));
    } catch (...) {
        static_cast<void>(CommandList->Close());
        PrimeMhChestTextureView = {};
        PrimeMhSuperChestTextureView = {};
        PrimeMhChestTexture.Reset();
        PrimeMhSuperChestTexture.Reset();
        PrimeMhTextureUploads.clear();
        return false;
    }

    if (FAILED(CommandList->Close())) {
        PrimeMhChestTextureView = {};
        PrimeMhSuperChestTextureView = {};
        PrimeMhChestTexture.Reset();
        PrimeMhSuperChestTexture.Reset();
        PrimeMhTextureUploads.clear();
        return false;
    }
    ID3D12CommandList* const commandLists[]{CommandList.Get()};
    CommandQueue->ExecuteCommandLists(1U, commandLists);
    const std::uint64_t fenceValue = NextFenceValue++;
    if (FAILED(CommandQueue->Signal(Fence.Get(), fenceValue))
        || !WaitForFenceValueLocked(fenceValue)) {
        // The resources and upload buffers stay owned until renderer reset so
        // a late GPU completion can never dereference released upload memory.
        PrimeMhChestTextureView = {};
        PrimeMhSuperChestTextureView = {};
        return false;
    }
    PrimeMhTextureUploads.clear();
    return true;
}

[[nodiscard]] auto LoadAutomapSpriteImage(
        DecodedRgbaImage& image) noexcept -> bool {
    image = {};
    if (MapSenseAutomapSpritePath.empty()) return false;
    try {
        std::ifstream input(
            MapSenseAutomapSpritePath,
            std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto size = input.tellg();
        if (size != static_cast<std::streamoff>(
                AutomapSpritePackageBytes)) {
            return false;
        }
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input || input.peek() != std::char_traits<char>::eof()) {
            return false;
        }
        AutomapSpriteRgbaAtlas atlas;
        if (!ParseAutomapSpritePackage(bytes, atlas)) return false;
        if (atlas.width > (std::numeric_limits<UINT>::max)()
            || atlas.height > (std::numeric_limits<UINT>::max)()) {
            return false;
        }
        image.width = static_cast<UINT>(atlas.width);
        image.height = static_cast<UINT>(atlas.height);
        image.pixels = std::move(atlas.pixels);
        return true;
    } catch (...) {
        image = {};
        return false;
    }
}

[[nodiscard]] auto InitializeAutomapSpriteTextureLocked(
        ID3D12Device* device) noexcept -> bool {
    AutomapSpriteTextureView = {};
    AutomapSpriteTexture.Reset();
    AutomapSpriteUpload.Reset();
    if (device == nullptr
        || !CommandQueue
        || !CommandList
        || Frames.empty()
        || !Frames[0].allocator
        || !Fence
        || !FenceEvent) {
        return false;
    }
    DecodedRgbaImage image;
    if (!LoadAutomapSpriteImage(image)
        || image.width != AutomapSpriteIndexAtlasWidth
        || image.height != AutomapSpriteRgbaAtlasHeight) {
        return false;
    }
    if (FAILED(Frames[0].allocator->Reset())
        || FAILED(CommandList->Reset(Frames[0].allocator.Get(), nullptr))) {
        return false;
    }
    if (!RecordPrimeMhTextureUpload(
            device,
            CommandList.Get(),
            image,
            AutomapSpriteSrvDescriptorIndex,
            AutomapSpriteTexture,
            AutomapSpriteUpload,
            AutomapSpriteTextureView)) {
        static_cast<void>(CommandList->Close());
        AutomapSpriteTextureView = {};
        AutomapSpriteTexture.Reset();
        AutomapSpriteUpload.Reset();
        return false;
    }
    if (FAILED(CommandList->Close())) {
        AutomapSpriteTextureView = {};
        AutomapSpriteTexture.Reset();
        AutomapSpriteUpload.Reset();
        return false;
    }
    ID3D12CommandList* const commandLists[]{CommandList.Get()};
    CommandQueue->ExecuteCommandLists(1U, commandLists);
    const std::uint64_t fenceValue = NextFenceValue++;
    if (FAILED(CommandQueue->Signal(Fence.Get(), fenceValue))
        || !WaitForFenceValueLocked(fenceValue)) {
        // Retain both resources until reset if the GPU completion is late.
        AutomapSpriteTextureView = {};
        return false;
    }
    AutomapSpriteUpload.Reset();
    return true;
}

struct ExternalClientEntry {
    std::array<char, 64> owner{};
    RuffnecKk::OverlayHost::ContextCallbackV2 contextCreated{};
    RuffnecKk::OverlayHost::ContextCallbackV2 contextDestroying{};
    RuffnecKk::OverlayHost::HostStoppedCallbackV2 hostStopped{};
    RuffnecKk::OverlayHost::BeforeFrameCallbackV2 beforeFrame{};
    RuffnecKk::OverlayHost::RenderCallbackV2 render{};
    void* userData{};
};

std::mutex ExternalClientMutex;
std::array<ExternalClientEntry, MaximumExternalClients> ExternalClients{};

int SubclassIdentity{};

class HookCallGuard final {
public:
    HookCallGuard() noexcept {
        ActiveHookCalls.fetch_add(1U, std::memory_order_acq_rel);
    }

    ~HookCallGuard() {
        ActiveHookCalls.fetch_sub(1U, std::memory_order_acq_rel);
    }

    HookCallGuard(const HookCallGuard&) = delete;
    auto operator=(const HookCallGuard&) -> HookCallGuard& = delete;
};

constexpr std::uint32_t LeftButtonBit = 1U << 0U;
constexpr std::uint32_t RightButtonBit = 1U << 1U;
constexpr std::uint32_t MiddleButtonBit = 1U << 2U;
constexpr std::uint32_t XButton1Bit = 1U << 3U;
constexpr std::uint32_t XButton2Bit = 1U << 4U;

void PublishPanelBounds(const D3D12ImGuiPanelBounds& bounds) noexcept {
    BoundsVersion.fetch_add(1, std::memory_order_acq_rel);
    BoundsVisible.store(bounds.visible, std::memory_order_relaxed);
    BoundsLeft.store(bounds.left, std::memory_order_relaxed);
    BoundsTop.store(bounds.top, std::memory_order_relaxed);
    BoundsRight.store(bounds.right, std::memory_order_relaxed);
    BoundsBottom.store(bounds.bottom, std::memory_order_relaxed);
    BoundsVersion.fetch_add(1, std::memory_order_release);
}

auto CopyExternalClients() noexcept
    -> std::array<ExternalClientEntry, MaximumExternalClients> {
    std::scoped_lock lock(ExternalClientMutex);
    return ExternalClients;
}

auto HasExternalFrameClient(
    const std::array<ExternalClientEntry, MaximumExternalClients>& clients)
        noexcept -> bool {
    for (const auto& client : clients) {
        if (client.render != nullptr || client.beforeFrame != nullptr)
            return true;
    }
    return false;
}

auto MakeExternalFrameContextLocked() noexcept
    -> RuffnecKk::OverlayHost::FrameContextV2 {
    RECT clientRect{};
    if (GameWindow != nullptr) GetClientRect(GameWindow, &clientRect);
    return {
        .structSize = RuffnecKk::OverlayHost::FrameContextV2Size,
        .version = RuffnecKk::OverlayHost::ApiVersion2,
        .imguiContext = ImGuiContextCreated
            ? ImGui::GetCurrentContext()
            : nullptr,
        .window = GameWindow,
        .displayWidth = static_cast<float>(clientRect.right - clientRect.left),
        .displayHeight = static_cast<float>(clientRect.bottom - clientRect.top),
    };
}

void NotifyContextCreatedLocked(
    const std::array<ExternalClientEntry, MaximumExternalClients>& clients)
        noexcept {
    const auto frame = MakeExternalFrameContextLocked();
    for (const auto& client : clients) {
        if (client.contextCreated != nullptr)
            client.contextCreated(&frame, client.userData);
    }
    ContextCallbacksActive = true;
}

void NotifyContextDestroyingLocked(
    const std::array<ExternalClientEntry, MaximumExternalClients>& clients)
        noexcept {
    if (!ContextCallbacksActive || !ImGuiContextCreated) return;
    ContextCallbacksActive = false;
    const auto frame = MakeExternalFrameContextLocked();
    for (const auto& client : clients) {
        if (client.contextDestroying != nullptr)
            client.contextDestroying(&frame, client.userData);
    }
}

auto ReadPanelBounds() noexcept -> D3D12ImGuiPanelBounds {
    D3D12ImGuiPanelBounds bounds{};
    for (;;) {
        const std::uint64_t before = BoundsVersion.load(
            std::memory_order_acquire);
        if ((before & 1U) != 0U) continue;
        bounds.visible = BoundsVisible.load(std::memory_order_relaxed);
        bounds.left = BoundsLeft.load(std::memory_order_relaxed);
        bounds.top = BoundsTop.load(std::memory_order_relaxed);
        bounds.right = BoundsRight.load(std::memory_order_relaxed);
        bounds.bottom = BoundsBottom.load(std::memory_order_relaxed);
        const std::uint64_t after = BoundsVersion.load(
            std::memory_order_acquire);
        if (before == after) return bounds;
    }
}

void LogInfo(const char* message) noexcept {
    if (const auto logger = InfoLogger.load(std::memory_order_acquire))
        logger(message);
}

void LogWarning(const char* message) noexcept {
    if (const auto logger = WarningLogger.load(std::memory_order_acquire))
        logger(message);
}

auto SubclassId() noexcept -> UINT_PTR {
    return reinterpret_cast<UINT_PTR>(&SubclassIdentity);
}

auto MousePointFromMessage(
    HWND window,
    UINT message,
    LPARAM lParam) noexcept -> POINT {
    POINT point{};
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        ScreenToClient(window, &point);
        return point;
    }
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        return point;
    }
    if (GetCursorPos(&point)) ScreenToClient(window, &point);
    return point;
}

auto PointInsidePublishedPanel(POINT point) noexcept -> bool {
    if (!MenuOpen.load(std::memory_order_acquire)) return false;
    if (AwaitingFirstBounds.load(std::memory_order_acquire)) return true;
    const auto bounds = ReadPanelBounds();
    return bounds.visible
        && static_cast<float>(point.x) >= bounds.left
        && static_cast<float>(point.x) < bounds.right
        && static_cast<float>(point.y) >= bounds.top
        && static_cast<float>(point.y) < bounds.bottom;
}

auto ButtonBitFromMessage(UINT message, WPARAM wParam) noexcept
    -> std::uint32_t {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
        return LeftButtonBit;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
        return RightButtonBit;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
        return MiddleButtonBit;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
        return GET_XBUTTON_WPARAM(wParam) == XBUTTON1
            ? XButton1Bit
            : XButton2Bit;
    default:
        return 0U;
    }
}

auto IsButtonDownMessage(UINT message) noexcept -> bool {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

auto IsButtonUpMessage(UINT message) noexcept -> bool {
    switch (message) {
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        return true;
    default:
        return false;
    }
}

void FeedImGuiWin32Message(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) noexcept {
    // Defense in depth: the window subclass normally rejects native automap
    // Tab messages before reaching this helper. Keep the backend boundary
    // closed as well so a later input branch cannot accidentally regress it.
    if (!ShouldForwardWin32MessageToImGui(message, wParam)) return;
    std::scoped_lock lock(HostMutex);
    if (!RendererInitialized || !ImGuiContextCreated) return;
    ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
}

auto RemoveInputSubclassMessage() noexcept -> UINT {
    static const UINT message = RegisterWindowMessageW(
        L"RuffnecKk.MapSense.RemoveInputSubclass.v1");
    return message;
}

auto ConsumedButtonResult(UINT message) noexcept -> LRESULT {
    return message == WM_XBUTTONDOWN
            || message == WM_XBUTTONDBLCLK
            || message == WM_XBUTTONUP
        ? TRUE
        : 0;
}

auto CancelOwnedMouseButtons() noexcept -> bool {
    const std::uint32_t owned = OwnedMouseButtons.exchange(
        0U, std::memory_order_acq_rel);
    if (owned == 0U) return false;
    CanceledMouseButtons.fetch_or(owned, std::memory_order_acq_rel);

    std::scoped_lock lock(HostMutex);
    if (ImGuiContextCreated) {
        ImGuiIO& io = ImGui::GetIO();
        if ((owned & LeftButtonBit) != 0U) io.AddMouseButtonEvent(0, false);
        if ((owned & RightButtonBit) != 0U) io.AddMouseButtonEvent(1, false);
        if ((owned & MiddleButtonBit) != 0U) io.AddMouseButtonEvent(2, false);
        if ((owned & XButton1Bit) != 0U) io.AddMouseButtonEvent(3, false);
        if ((owned & XButton2Bit) != 0U) io.AddMouseButtonEvent(4, false);
    }
    return true;
}

auto CALLBACK HostWindowSubclassProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR) noexcept -> LRESULT {
    const UINT removeMessage = RemoveInputSubclassMessage();
    if (removeMessage != 0U
        && message == removeMessage
        && wParam == SubclassId()) {
        const bool releasedOwnedButton = CancelOwnedMouseButtons();
        if (releasedOwnedButton && GetCapture() == window) ReleaseCapture();
        const BOOL removed = RemoveWindowSubclass(
            window, HostWindowSubclassProc, SubclassId());
        if (removed) {
            InputSubclassWindow.store(nullptr, std::memory_order_release);
            InputSubclassInstalled.store(false, std::memory_order_release);
            InputSubclassInstalledPublished.store(
                false, std::memory_order_release);
        }
        return removed ? TRUE : FALSE;
    }

    if (IsInitialOwnedOverlayDismissalMessage(message, wParam, lParam)) {
        // Present owns the same recursive mutex for its complete ImGui frame.
        // Waiting here guarantees the invalidation finishes after any frame
        // already being submitted and before D2R processes Tab or Escape.
        std::scoped_lock lock(HostMutex);
        if (Callbacks.ownedOverlayDismissal != nullptr) {
            Callbacks.ownedOverlayDismissal(Callbacks.userData);
        }
    }

    if (!ShouldForwardWin32MessageToImGui(message, wParam)) {
        // Never consume Tab and never feed it to ImGui. D2R remains the sole
        // owner of its native automap key, including key-up, system-key and
        // translated character messages.
        LogInfo(
            "MapSense diagnostic input: native Tab message bypassed ImGui and was forwarded to D2R.");
        return DefSubclassProc(window, message, wParam, lParam);
    }

    const std::uint32_t ownedBefore = OwnedMouseButtons.load(
        std::memory_order_acquire);
    const bool hasOwnedButton = ownedBefore != 0U;
    const POINT point = MousePointFromMessage(window, message, lParam);
    const bool insidePanel = PointInsidePublishedPanel(point);

    const std::uint32_t buttonBit = ButtonBitFromMessage(message, wParam);
    const std::uint32_t canceledBefore = CanceledMouseButtons.load(
        std::memory_order_acquire);
    if (buttonBit != 0U && (canceledBefore & buttonBit) != 0U) {
        if (IsButtonUpMessage(message)) {
            CanceledMouseButtons.fetch_and(
                ~buttonBit, std::memory_order_acq_rel);
            return ConsumedButtonResult(message);
        }
        // A new down means the canceled press was released while D2R was not
        // delivering messages (for example across GameLeft). Do not discard
        // the first legitimate click of the next session.
        if (IsButtonDownMessage(message)) {
            CanceledMouseButtons.fetch_and(
                ~buttonBit, std::memory_order_acq_rel);
        }
    }

    if (buttonBit != 0U && IsButtonDownMessage(message)) {
        if (insidePanel || hasOwnedButton) {
            OwnedMouseButtons.fetch_or(buttonBit, std::memory_order_acq_rel);
            FeedImGuiWin32Message(window, message, wParam, lParam);
            SetCapture(window);
            return ConsumedButtonResult(message);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    if (buttonBit != 0U && IsButtonUpMessage(message)) {
        if ((ownedBefore & buttonBit) != 0U) {
            FeedImGuiWin32Message(window, message, wParam, lParam);
            const std::uint32_t remaining = OwnedMouseButtons.fetch_and(
                ~buttonBit, std::memory_order_acq_rel) & ~buttonBit;
            if (remaining == 0U && GetCapture() == window) ReleaseCapture();
            return ConsumedButtonResult(message);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    switch (message) {
    case WM_MOUSEMOVE:
        if (MenuOpen.load(std::memory_order_acquire) || hasOwnedButton)
            FeedImGuiWin32Message(window, message, wParam, lParam);
        if (insidePanel || hasOwnedButton) return 0;
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (insidePanel || hasOwnedButton) {
            FeedImGuiWin32Message(window, message, wParam, lParam);
            return 0;
        }
        break;
    case WM_INPUT:
        if (insidePanel || hasOwnedButton) {
            RAWINPUTHEADER header{};
            UINT headerSize = sizeof(header);
            const UINT copied = GetRawInputData(
                reinterpret_cast<HRAWINPUT>(lParam),
                RID_HEADER,
                &header,
                &headerSize,
                sizeof(RAWINPUTHEADER));
            if (copied == sizeof(header)
                && header.dwType == RIM_TYPEMOUSE) {
                // Foreground raw input still needs DefWindowProc cleanup, but
                // bypasses D2R and every later subclass while the panel owns
                // the pointer. Raw keyboard packets remain game-owned.
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }
        break;
    case WM_MOUSELEAVE:
        if (MenuOpen.load(std::memory_order_acquire) || hasOwnedButton)
            FeedImGuiWin32Message(window, message, wParam, lParam);
        break;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != window)
            CancelOwnedMouseButtons();
        FeedImGuiWin32Message(window, message, wParam, lParam);
        break;
    case WM_KILLFOCUS:
        CancelOwnedMouseButtons();
        FeedImGuiWin32Message(window, message, wParam, lParam);
        break;
    default:
        // Keyboard and non-input messages remain D2R-owned. Feeding them to
        // the backend keeps ImGui modifiers/focus correct without consuming
        // the game's message. Native automap Tab messages already returned
        // above and can never reach this backend.
        if (MenuOpen.load(std::memory_order_acquire))
            FeedImGuiWin32Message(window, message, wParam, lParam);
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void InstallInputSubclassOnUiThread(void*) noexcept {
    const HWND window = RequestedSubclassWindow.exchange(
        nullptr, std::memory_order_acq_rel);
    InputInstallQueued.store(false, std::memory_order_release);
    if (window == nullptr || !IsWindow(window)) return;

    DWORD processId{};
    const DWORD ownerThreadId = GetWindowThreadProcessId(window, &processId);
    if (ownerThreadId == 0U
        || ownerThreadId != GetCurrentThreadId()
        || processId != GetCurrentProcessId()) {
        LogWarning(
            "MapSense: refused to subclass D2R from a non-owner UI thread.");
        return;
    }

    std::scoped_lock lock(HostMutex);
    if (!RendererInitialized
        || RendererResetPending
        || GameWindow != window) {
        return;
    }
    if (InputSubclassInstalled.load(std::memory_order_acquire)) {
        if (InputSubclassWindow.load(std::memory_order_acquire) != window) {
            LogWarning(
                "MapSense: refused to replace an input subclass owned by another D2R window.");
        }
        return;
    }
    if (!SetWindowSubclass(
            window, HostWindowSubclassProc, SubclassId(), 0U)) {
        LogWarning("MapSense: D2R input subclass installation failed.");
        return;
    }
    InputSubclassWindow.store(window, std::memory_order_release);
    InputSubclassInstalled.store(true, std::memory_order_release);
    InputSubclassInstalledPublished.store(true, std::memory_order_release);
    AwaitingFirstBounds.store(
        MenuOpen.load(std::memory_order_acquire),
        std::memory_order_release);
    InputQueueWarningLogged.store(false, std::memory_order_release);
}

auto QueueInputSubclassInstallLocked() noexcept -> bool {
    if (GameWindow == nullptr || RendererResetPending) return false;
    if (InputSubclassInstalled.load(std::memory_order_acquire)
        && InputSubclassWindow.load(std::memory_order_acquire)
            == GameWindow) {
        return true;
    }
    if (InputInstallQueued.load(std::memory_order_acquire)) return true;
    if (Callbacks.queueUiTask == nullptr) return false;

    RequestedSubclassWindow.store(GameWindow, std::memory_order_release);
    InputInstallQueued.store(true, std::memory_order_release);
    if (Callbacks.queueUiTask(
            InstallInputSubclassOnUiThread,
            nullptr,
            Callbacks.uiDispatcherUserData)) {
        return true;
    }
    RequestedSubclassWindow.store(nullptr, std::memory_order_release);
    InputInstallQueued.store(false, std::memory_order_release);
    return false;
}

auto RemoveInputSubclassSynchronously() noexcept -> bool {
    RequestedSubclassWindow.store(nullptr, std::memory_order_release);
    InputInstallQueued.store(false, std::memory_order_release);
    const HWND window = InputSubclassWindow.load(std::memory_order_acquire);
    if (!InputSubclassInstalled.load(std::memory_order_acquire)
        || window == nullptr) {
        return true;
    }
    if (!IsWindow(window)) {
        CancelOwnedMouseButtons();
        InputSubclassWindow.store(nullptr, std::memory_order_release);
        InputSubclassInstalled.store(false, std::memory_order_release);
        InputSubclassInstalledPublished.store(
            false, std::memory_order_release);
        return true;
    }

    const UINT message = RemoveInputSubclassMessage();
    DWORD processId{};
    const DWORD ownerThreadId = GetWindowThreadProcessId(window, &processId);
    if (message == 0U
        || ownerThreadId == 0U
        || processId != GetCurrentProcessId()) {
        return false;
    }

    LRESULT result{};
    if (ownerThreadId == GetCurrentThreadId()) {
        result = SendMessageW(window, message, SubclassId(), 0);
    }
    else {
        DWORD_PTR sendResult{};
        if (SendMessageTimeoutW(
                window,
                message,
                SubclassId(),
                0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK,
                FenceWaitMilliseconds,
                &sendResult) == 0) {
            // Unload safety is stricter than responsiveness: once the
            // bounded attempt expires, wait for the owner thread rather than
            // leave a callback pointing into a DLL that may be unloaded.
            result = SendMessageW(window, message, SubclassId(), 0);
        }
        else {
            result = static_cast<LRESULT>(sendResult);
        }
    }
    (void)result;
    if (!InputSubclassInstalled.load(std::memory_order_acquire)) return true;
    if (!IsWindow(window)) {
        CancelOwnedMouseButtons();
        InputSubclassWindow.store(nullptr, std::memory_order_release);
        InputSubclassInstalled.store(false, std::memory_order_release);
        InputSubclassInstalledPublished.store(
            false, std::memory_order_release);
        return true;
    }
    return false;
}

auto PinHostModuleForResidualSubclass() noexcept -> bool {
    HMODULE module{};
    return GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(static_cast<void*>(&SubclassIdentity)),
        &module) != FALSE;
}

auto WaitForFenceValueLocked(std::uint64_t value) noexcept -> bool {
    if (value == 0U || !Fence || !FenceEvent) return true;
    if (Fence->GetCompletedValue() >= value) return true;
    if (FAILED(Fence->SetEventOnCompletion(value, FenceEvent))) return false;
    return WaitForSingleObject(FenceEvent, FenceWaitMilliseconds)
        == WAIT_OBJECT_0;
}

auto WaitForGpuIdleLocked() noexcept -> bool {
    if (!CommandQueue || !Fence || !FenceEvent) return true;
    const std::uint64_t value = NextFenceValue++;
    if (FAILED(CommandQueue->Signal(Fence.Get(), value))) return false;
    return WaitForFenceValueLocked(value);
}

void ResetRendererStateLocked(bool clearInputSubclassState = true) noexcept {
    RendererInitialized = false;
    RendererInitializedPublished.store(false, std::memory_order_release);
    PrimeMhTexturesReadyPublished.store(false, std::memory_order_release);
    AutomapSpriteAtlasReadyPublished.store(
        false, std::memory_order_release);
    const bool gpuIdle = WaitForGpuIdleLocked();
    if (!gpuIdle)
        LogWarning("MapSense: timed out while waiting for submitted GPU work.");

    NotifyContextDestroyingLocked(CopyExternalClients());
    if (ImGuiDx12Initialized) {
        ImGui_ImplDX12_Shutdown();
        ImGuiDx12Initialized = false;
    }
    if (ImGuiWin32Initialized) {
        ImGui_ImplWin32_Shutdown();
        ImGuiWin32Initialized = false;
    }
    if (ImGuiContextCreated) {
        ImGui::DestroyContext();
        ImGuiContextCreated = false;
    }
    MapSenseAutomapFont = nullptr;
    PrimeMhChestTextureView = {};
    PrimeMhSuperChestTextureView = {};
    PrimeMhChestTexture.Reset();
    PrimeMhSuperChestTexture.Reset();
    PrimeMhTextureUploads.clear();
    AutomapSpriteTextureView = {};
    AutomapSpriteTexture.Reset();
    AutomapSpriteUpload.Reset();

    ContextCallbacksActive = false;
    RequestedSubclassWindow.store(nullptr, std::memory_order_release);
    InputInstallQueued.store(false, std::memory_order_release);
    if (clearInputSubclassState) {
        InputSubclassWindow.store(nullptr, std::memory_order_release);
        InputSubclassInstalled.store(false, std::memory_order_release);
        InputSubclassInstalledPublished.store(
            false, std::memory_order_release);
    }
    GameWindow = nullptr;
    PublishedGameWindow.store(nullptr, std::memory_order_release);
    ActiveSwapChain = nullptr;
    Frames.clear();
    CommandList.Reset();
    RtvHeap.Reset();
    SrvHeap.Reset();
    Fence.Reset();
    if (FenceEvent) {
        CloseHandle(FenceEvent);
        FenceEvent = nullptr;
    }
    NextFenceValue = 1U;
    LastFrameTime = {};
    RendererResetPending = false;
    PublishPanelBounds({});
}

auto FailRendererInitialization(
    std::uint32_t stage,
    const char* message) noexcept -> bool {
    RendererInitFailures.fetch_add(1, std::memory_order_relaxed);
    LastInitFailureStage.store(stage, std::memory_order_relaxed);
    LogWarning(message);
    ResetRendererStateLocked();
    return false;
}

auto SameComIdentity(IUnknown* left, IUnknown* right) noexcept -> bool {
    if (left == nullptr || right == nullptr) return false;
    ComPtr<IUnknown> leftIdentity;
    ComPtr<IUnknown> rightIdentity;
    return SUCCEEDED(left->QueryInterface(IID_PPV_ARGS(&leftIdentity)))
        && SUCCEEDED(right->QueryInterface(IID_PPV_ARGS(&rightIdentity)))
        && leftIdentity.Get() == rightIdentity.Get();
}

void RecordExactSwapChainQueue(
    IUnknown* queueCandidate,
    IUnknown* swapChainCandidate) noexcept {
    if (queueCandidate == nullptr || swapChainCandidate == nullptr) return;

    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IUnknown> swapChainIdentity;
    if (FAILED(queueCandidate->QueryInterface(IID_PPV_ARGS(&queue)))
        || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT
        || FAILED(swapChainCandidate->QueryInterface(
            IID_PPV_ARGS(&swapChainIdentity)))) {
        return;
    }

    std::scoped_lock lock(HostMutex);
    for (auto& binding : SwapChainQueueBindings) {
        if (!SameComIdentity(
                binding.swapChainIdentity.Get(), swapChainIdentity.Get())) {
            continue;
        }
        binding.commandQueue = queue;
        return;
    }
    if (SwapChainQueueBindings.size() >= MaximumSwapChainQueueBindings) {
        LogWarning(
            "MapSense: refused a swap-chain command queue because the exact-binding registry is full.");
        return;
    }
    SwapChainQueueBindings.push_back(SwapChainQueueBinding{
        .swapChainIdentity = std::move(swapChainIdentity),
        .commandQueue = std::move(queue),
    });
    ExactQueueBindings.fetch_add(1U, std::memory_order_relaxed);
    LogInfo(
        "MapSense: recorded the exact DirectX 12 command queue supplied at swap-chain creation.");
}

auto SelectExactSwapChainQueueLocked(IDXGISwapChain3* swapChain) noexcept
    -> bool {
    if (swapChain == nullptr || RendererPoisoned) return false;
    if (CommandQueue && ActiveSwapChain == swapChain) return true;

    for (const auto& binding : SwapChainQueueBindings) {
        if (!SameComIdentity(binding.swapChainIdentity.Get(), swapChain))
            continue;
        if (RendererInitialized && CommandQueue
            && !SameComIdentity(CommandQueue.Get(), binding.commandQueue.Get())) {
            RendererPoisoned = true;
            RendererInitializedPublished.store(
                false, std::memory_order_release);
            LogWarning(
                "MapSense: renderer disabled because an initialized swap chain changed command queues.");
            return false;
        }
        CommandQueue = binding.commandQueue;
        CapturedQueue.store(CommandQueue.Get(), std::memory_order_release);
        UnboundSwapChainWarningLogged.store(false, std::memory_order_release);
        return true;
    }

    if (!UnboundSwapChainWarningLogged.exchange(
            true, std::memory_order_acq_rel)) {
        LogWarning(
            "MapSense: GPU rendering is fail-closed because Present has no exact swap-chain command-queue binding.");
    }
    return false;
}

auto STDMETHODCALLTYPE HookCreateSwapChain(
    IDXGIFactory* factory,
    IUnknown* queue,
    DXGI_SWAP_CHAIN_DESC* description,
    IDXGISwapChain** swapChain) noexcept -> HRESULT {
    HookCallGuard hookCall;
    CreateSwapChainFn original{};
    {
        std::scoped_lock lock(HostMutex);
        original = OriginalCreateSwapChain;
    }
    const HRESULT result = original != nullptr
        ? original(factory, queue, description, swapChain)
        : DXGI_ERROR_INVALID_CALL;
    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        RecordExactSwapChainQueue(queue, *swapChain);
    return result;
}

auto STDMETHODCALLTYPE HookCreateSwapChainForHwnd(
    IDXGIFactory2* factory,
    IUnknown* queue,
    HWND window,
    const DXGI_SWAP_CHAIN_DESC1* description,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDescription,
    IDXGIOutput* restrictToOutput,
    IDXGISwapChain1** swapChain) noexcept -> HRESULT {
    HookCallGuard hookCall;
    CreateSwapChainForHwndFn original{};
    {
        std::scoped_lock lock(HostMutex);
        original = OriginalCreateSwapChainForHwnd;
    }
    const HRESULT result = original != nullptr
        ? original(
            factory,
            queue,
            window,
            description,
            fullscreenDescription,
            restrictToOutput,
            swapChain)
        : DXGI_ERROR_INVALID_CALL;
    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        RecordExactSwapChainQueue(queue, *swapChain);
    return result;
}

auto STDMETHODCALLTYPE HookCreateSwapChainForCoreWindow(
    IDXGIFactory2* factory,
    IUnknown* queue,
    IUnknown* window,
    const DXGI_SWAP_CHAIN_DESC1* description,
    IDXGIOutput* restrictToOutput,
    IDXGISwapChain1** swapChain) noexcept -> HRESULT {
    HookCallGuard hookCall;
    CreateSwapChainForCoreWindowFn original{};
    {
        std::scoped_lock lock(HostMutex);
        original = OriginalCreateSwapChainForCoreWindow;
    }
    const HRESULT result = original != nullptr
        ? original(
            factory,
            queue,
            window,
            description,
            restrictToOutput,
            swapChain)
        : DXGI_ERROR_INVALID_CALL;
    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        RecordExactSwapChainQueue(queue, *swapChain);
    return result;
}

auto STDMETHODCALLTYPE HookCreateSwapChainForComposition(
    IDXGIFactory2* factory,
    IUnknown* queue,
    const DXGI_SWAP_CHAIN_DESC1* description,
    IDXGIOutput* restrictToOutput,
    IDXGISwapChain1** swapChain) noexcept -> HRESULT {
    HookCallGuard hookCall;
    CreateSwapChainForCompositionFn original{};
    {
        std::scoped_lock lock(HostMutex);
        original = OriginalCreateSwapChainForComposition;
    }
    const HRESULT result = original != nullptr
        ? original(
            factory,
            queue,
            description,
            restrictToOutput,
            swapChain)
        : DXGI_ERROR_INVALID_CALL;
    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        RecordExactSwapChainQueue(queue, *swapChain);
    return result;
}

auto InitializeRenderer(
    IDXGISwapChain3* swapChain,
    const std::array<ExternalClientEntry, MaximumExternalClients>&
        externalClients) noexcept -> bool {
    RendererInitAttempts.fetch_add(1, std::memory_order_relaxed);
    try {
        ComPtr<ID3D12Device> device;
        if (!swapChain
            || FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device)))) {
            return FailRendererInitialization(
                1,
                "MapSense: renderer initialization failed at swap-chain device lookup.");
        }

        DXGI_SWAP_CHAIN_DESC swapDesc{};
        if (FAILED(swapChain->GetDesc(&swapDesc))
            || swapDesc.BufferCount == 0U
            || !swapDesc.OutputWindow) {
            return FailRendererInitialization(
                2,
                "MapSense: renderer initialization rejected a swap chain without D2R's OutputWindow.");
        }

        ComPtr<ID3D12Device> queueDevice;
        if (!CommandQueue
            || FAILED(CommandQueue->GetDevice(IID_PPV_ARGS(&queueDevice)))
            || !SameComIdentity(queueDevice.Get(), device.Get())) {
            RendererPoisoned = true;
            return FailRendererInitialization(
                3,
                "MapSense: exact swap-chain command queue does not belong to the swap-chain device; GPU rendering is disabled.");
        }

        GameWindow = swapDesc.OutputWindow;
        PublishedGameWindow.store(GameWindow, std::memory_order_release);
        ActiveSwapChain = swapChain;
        BackBufferFormat = swapDesc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
            ? DXGI_FORMAT_R8G8B8A8_UNORM
            : swapDesc.BufferDesc.Format;

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = MapSenseSrvDescriptorCount;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(
                &srvDesc,
                IID_PPV_ARGS(&SrvHeap)))) {
            return FailRendererInitialization(
                4,
                "MapSense: renderer initialization failed at SRV heap creation.");
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = swapDesc.BufferCount;
        if (FAILED(device->CreateDescriptorHeap(
                &rtvDesc,
                IID_PPV_ARGS(&RtvHeap)))) {
            return FailRendererInitialization(
                5,
                "MapSense: renderer initialization failed at RTV heap creation.");
        }

        Frames.clear();
        Frames.resize(swapDesc.BufferCount);
        auto descriptor = RtvHeap->GetCPUDescriptorHandleForHeapStart();
        const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT index = 0U; index < swapDesc.BufferCount; ++index) {
            auto& frame = Frames[index];
            if (FAILED(device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&frame.allocator)))) {
                return FailRendererInitialization(
                    6,
                    "MapSense: renderer initialization failed at command allocator creation.");
            }
            if (FAILED(swapChain->GetBuffer(
                    index,
                    IID_PPV_ARGS(&frame.renderTarget)))) {
                return FailRendererInitialization(
                    7,
                    "MapSense: renderer initialization failed at back-buffer lookup.");
            }
            frame.descriptor = descriptor;
            device->CreateRenderTargetView(
                frame.renderTarget.Get(), nullptr, descriptor);
            descriptor.ptr += descriptorSize;
        }

        if (FAILED(device->CreateCommandList(
                0U,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                Frames[0].allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&CommandList)))) {
            return FailRendererInitialization(
                8,
                "MapSense: renderer initialization failed at command-list creation.");
        }
        if (FAILED(CommandList->Close())) {
            return FailRendererInitialization(
                9,
                "MapSense: renderer initialization failed while closing the command list.");
        }
        if (FAILED(device->CreateFence(
                0U,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&Fence)))) {
            return FailRendererInitialization(
                10,
                "MapSense: renderer initialization failed at fence creation.");
        }
        FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!FenceEvent) {
            return FailRendererInitialization(
                11,
                "MapSense: renderer initialization failed at fence-event creation.");
        }

        IMGUI_CHECKVERSION();
        if (ImGui::CreateContext() == nullptr) {
            return FailRendererInitialization(
                12,
                "MapSense: renderer initialization failed at ImGui context creation.");
        }
        ImGuiContextCreated = true;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        // D2R remains the sole cursor owner. MapSense consumes panel mouse
        // input without asking the Win32 backend to show a second OS cursor.
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImFont* const mapSenseDefaultFont =
            AddLocalizedMapSenseFont(*io.Fonts);
        if (mapSenseDefaultFont == nullptr) {
            return FailRendererInitialization(
                13,
                "MapSense: renderer initialization failed while adding its localized font atlas.");
        }
        io.FontDefault = mapSenseDefaultFont;
        MapSenseAutomapFont = AddAutomapMapSenseFont(*io.Fonts);
        if (!MapSenseAutomapFontPath.empty()
            && MapSenseAutomapFont == nullptr) {
            LogWarning(
                "MapSense: D2R Exocet font could not be loaded; automap labels use the localized system fallback.");
        } else if (MapSenseAutomapFont != nullptr) {
            LogInfo(
                "MapSense: automap labels use D2R's Exocet font from the active mod package.");
        }
        ImGui::StyleColorsDark();
        if (!ImGui_ImplWin32_Init(GameWindow)) {
            return FailRendererInitialization(
                14,
                "MapSense: renderer initialization failed at ImGui Win32 startup.");
        }
        ImGuiWin32Initialized = true;
        // MapSense owns the first/default atlas font. Optional clients attach
        // their fonts before the DX12 backend creates the shared font texture.
        NotifyContextCreatedLocked(externalClients);
        if (!ImGui_ImplDX12_Init(
                device.Get(),
                swapDesc.BufferCount,
                BackBufferFormat,
                SrvHeap.Get(),
                SrvHeap->GetCPUDescriptorHandleForHeapStart(),
                SrvHeap->GetGPUDescriptorHandleForHeapStart())) {
            return FailRendererInitialization(
                15,
                "MapSense: renderer initialization failed at ImGui DirectX 12 startup.");
        }
        ImGuiDx12Initialized = true;
        if (!ImGui_ImplDX12_CreateDeviceObjects()) {
            return FailRendererInitialization(
                16,
                "MapSense: renderer initialization failed while creating ImGui device objects.");
        }
        if (InitializePrimeMhChestTexturesLocked(device.Get())) {
            PrimeMhTexturesReadyPublished.store(
                true, std::memory_order_release);
            LogInfo(
                "MapSense: exact PrimeMH chest and special-chest textures are ready.");
        } else {
            PrimeMhTexturesReadyPublished.store(
                false, std::memory_order_release);
            LogWarning(
                "MapSense: PrimeMH chest textures could not be initialized; chest markers use the procedural fallback.");
        }
        if (InitializeAutomapSpriteTextureLocked(device.Get())) {
            AutomapSpriteAtlasReadyPublished.store(
                true, std::memory_order_release);
            LogInfo(
                "MapSense: exact automap sprite atlas is ready for the generated map underlay.");
        } else {
            AutomapSpriteAtlasReadyPublished.store(
                false, std::memory_order_release);
            LogWarning(
                "MapSense: automap sprite atlas is unavailable or invalid; the generated map underlay remains fail-closed.");
        }

        LastFrameTime = std::chrono::steady_clock::now();
        RendererInitialized = true;
        RendererInitializedPublished.store(true, std::memory_order_release);
        if (!QueueInputSubclassInstallLocked()
            && !InputQueueWarningLogged.exchange(
                true, std::memory_order_acq_rel)) {
            LogWarning(
                "MapSense: input subclass installation could not be queued on D2R's UI thread.");
        }
        LogInfo("MapSense: in-frame D3D12/ImGui host initialized on D2R's window.");
        return true;
    }
    catch (...) {
        return FailRendererInitialization(
            17,
            "MapSense: renderer initialization failed with an unexpected exception.");
    }
}

auto RenderPanelFrame(
    IDXGISwapChain3* swapChain,
    const std::array<ExternalClientEntry, MaximumExternalClients>&
        externalClients) noexcept -> bool {
    const UINT frameIndex = swapChain->GetCurrentBackBufferIndex();
    if (frameIndex >= Frames.size()) return false;
    auto& frame = Frames[frameIndex];
    // Each allocator belongs to one swap-chain back buffer. Do not begin a new
    // recording pass until the GPU has completed the preceding submission for
    // that allocator; skipping this wait can race D2R's presentation path and
    // produce transient visual corruption even when the overlay frame itself
    // is omitted.
    if (!WaitForFenceValueLocked(frame.fenceValue)) {
        RendererPoisoned = true;
        RendererInitializedPublished.store(false, std::memory_order_release);
        LogWarning(
            "MapSense: the current overlay frame did not reach its fence safely.");
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = LastFrameTime.time_since_epoch().count() == 0
        ? (1.0F / 60.0F)
        : std::chrono::duration<float>(now - LastFrameTime).count();
    LastFrameTime = now;

    auto externalFrame = MakeExternalFrameContextLocked();
    for (const auto& client : externalClients) {
        if (client.beforeFrame != nullptr)
            client.beforeFrame(&externalFrame, client.userData);
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaSeconds > 0.0F && deltaSeconds <= 0.1F
        ? deltaSeconds
        : (1.0F / 60.0F);
    ImGui::NewFrame();

    D3D12ImGuiPanelBounds bounds{};
    bool open = MenuOpen.load(std::memory_order_acquire);
    const bool inputReady = InputSubclassInstalled.load(
        std::memory_order_acquire);
    if (open && inputReady && Callbacks.drawPanel)
        bounds = Callbacks.drawPanel(&open, Callbacks.userData);
    if (!open) bounds = {};
    MenuOpen.store(open, std::memory_order_release);
    PublishPanelBounds(bounds);
    AwaitingFirstBounds.store(
        open && !inputReady, std::memory_order_release);

    if (Callbacks.drawOwnedOverlay != nullptr)
        Callbacks.drawOwnedOverlay(Callbacks.userData);

    externalFrame.displayWidth = io.DisplaySize.x;
    externalFrame.displayHeight = io.DisplaySize.y;
    for (const auto& client : externalClients) {
        if (client.render != nullptr)
            client.render(&externalFrame, client.userData);
    }

    ImGui::Render();
    ImDrawData* const drawData = ImGui::GetDrawData();
    if (drawData == nullptr
        || !ShouldSubmitD3D12DrawData(
            drawData->CmdListsCount, drawData->TotalVtxCount)) {
        return true;
    }

    HRESULT result = frame.allocator->Reset();
    if (FAILED(result)) {
        RendererPoisoned = true;
        RendererInitializedPublished.store(false, std::memory_order_release);
        LogWarning(
            "MapSense: GPU rendering disabled after command-allocator reset failed.");
        return false;
    }
    result = CommandList->Reset(frame.allocator.Get(), nullptr);
    if (FAILED(result)) {
        RendererPoisoned = true;
        RendererInitializedPublished.store(false, std::memory_order_release);
        LogWarning(
            "MapSense: GPU rendering disabled after command-list reset failed.");
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.renderTarget.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    CommandList->ResourceBarrier(1U, &barrier);
    CommandList->OMSetRenderTargets(
        1U, &frame.descriptor, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[]{SrvHeap.Get()};
    CommandList->SetDescriptorHeaps(1U, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, CommandList.Get());
    const auto oldState = barrier.Transition.StateBefore;
    barrier.Transition.StateBefore = barrier.Transition.StateAfter;
    barrier.Transition.StateAfter = oldState;
    CommandList->ResourceBarrier(1U, &barrier);
    result = CommandList->Close();
    if (FAILED(result)) {
        RendererPoisoned = true;
        RendererInitializedPublished.store(false, std::memory_order_release);
        LogWarning(
            "MapSense: GPU rendering disabled after command-list close failed.");
        return false;
    }

    ID3D12CommandList* commandLists[]{CommandList.Get()};
    CommandQueue->ExecuteCommandLists(1U, commandLists);
    const std::uint64_t fenceValue = NextFenceValue++;
    result = CommandQueue->Signal(Fence.Get(), fenceValue);
    if (FAILED(result)) {
        RendererPoisoned = true;
        RendererInitializedPublished.store(false, std::memory_order_release);
        LogWarning(
            "MapSense: GPU rendering disabled after command-queue signaling failed.");
        return false;
    }
    frame.fenceValue = fenceValue;
    RenderedFrames.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

auto IsDeviceRemovalResult(HRESULT result) noexcept -> bool {
    return result == DXGI_ERROR_DEVICE_REMOVED
        || result == DXGI_ERROR_DEVICE_HUNG
        || result == DXGI_ERROR_DEVICE_RESET
        || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

void RecordDeviceRemovalLocked(
    IDXGISwapChain3* swapChain,
    HRESULT presentResult) noexcept {
    RendererPoisoned = true;
    RendererResetPending = true;
    RendererInitializedPublished.store(false, std::memory_order_release);
    if (DeviceRemovalWarningLogged.exchange(true, std::memory_order_acq_rel))
        return;

    HRESULT deviceReason = presentResult;
    ComPtr<ID3D12Device> device;
    if (swapChain != nullptr
        && SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&device)))) {
        deviceReason = device->GetDeviceRemovedReason();
    }
    char message[256]{};
    const int written = std::snprintf(
        message,
        sizeof(message),
        "MapSense: Present reported device removal (present=0x%08lX, reason=0x%08lX); all further plugin GPU submissions are blocked.",
        static_cast<unsigned long>(
            static_cast<std::uint32_t>(presentResult)),
        static_cast<unsigned long>(
            static_cast<std::uint32_t>(deviceReason)));
    if (written > 0) LogWarning(message);
}

auto STDMETHODCALLTYPE HookPresent(
    IDXGISwapChain3* swapChain,
    UINT syncInterval,
    UINT flags) noexcept -> HRESULT {
    HookCallGuard hookCall;
    PresentCalls.fetch_add(1U, std::memory_order_relaxed);
    PresentFn original{};
    {
        std::scoped_lock lock(HostMutex);
        original = OriginalPresent;
        if (original != nullptr
            && !RendererPoisoned
            && !RendererResetPending) {
            // Registration and rendering share HostMutex, so this is the one
            // stable client snapshot used for this complete Present pass.
            const auto externalClients = CopyExternalClients();
            const bool hasExternalClient = HasExternalFrameClient(
                externalClients);
            const bool wantsOwnedOverlay
                = Callbacks.wantsOwnedOverlay != nullptr
                && Callbacks.wantsOwnedOverlay(Callbacks.userData);
            const bool wantsFrame = MenuOpen.load(std::memory_order_acquire)
                || OwnedMouseButtons.load(std::memory_order_acquire) != 0U
                || wantsOwnedOverlay
                || hasExternalClient;
            if (wantsFrame
                && (!RendererInitialized
                    || ActiveSwapChain == swapChain)
                && SelectExactSwapChainQueueLocked(swapChain)) {
                if (!RendererInitialized) {
                    InitializeRenderer(swapChain, externalClients);
                }
                if (RendererInitialized) {
                    if (!InputSubclassInstalled.load(
                            std::memory_order_acquire)
                        && !QueueInputSubclassInstallLocked()
                        && !InputQueueWarningLogged.exchange(
                            true, std::memory_order_acq_rel)) {
                        LogWarning(
                            "MapSense: input subclass installation retry could not be queued.");
                    }
                    // Client overlays remain live while the MapSense settings
                    // panel is collapsed.
                    RenderPanelFrame(swapChain, externalClients);
                }
            }
        }
    }
    // Never hold HostMutex across D2R/DXGI code. Shutdown disables hooks and
    // waits for HookCallGuard before removing the trampoline behind this copy.
    const HRESULT result = original != nullptr
        ? original(swapChain, syncInterval, flags)
        : DXGI_ERROR_INVALID_CALL;
    if (IsDeviceRemovalResult(result)) {
        std::scoped_lock lock(HostMutex);
        RecordDeviceRemovalLocked(swapChain, result);
    }
    return result;
}

auto STDMETHODCALLTYPE HookResizeBuffers(
    IDXGISwapChain3* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags) noexcept -> HRESULT {
    HookCallGuard hookCall;
    ResizeBuffersFn original{};
    bool resetRenderer{};
    {
        std::scoped_lock lock(HostMutex);
        original = OriginalResizeBuffers;
        resetRenderer = RendererInitialized && ActiveSwapChain == swapChain;
        if (resetRenderer) {
            RendererResetPending = true;
            RendererInitializedPublished.store(
                false, std::memory_order_release);
        }
    }
    if (resetRenderer && !RemoveInputSubclassSynchronously()) {
        std::scoped_lock lock(HostMutex);
        RendererResetPending = false;
        RendererInitializedPublished.store(
            RendererInitialized, std::memory_order_release);
        LogWarning(
            "MapSense: ResizeBuffers was deferred because the D2R input subclass could not be removed safely.");
        return DXGI_ERROR_INVALID_CALL;
    }
    if (resetRenderer) {
        std::scoped_lock lock(HostMutex);
        if (RendererInitialized && ActiveSwapChain == swapChain)
            ResetRendererStateLocked();
    }
    return original != nullptr
        ? original(
            swapChain, bufferCount, width, height, format, flags)
        : DXGI_ERROR_INVALID_CALL;
}

auto ResolveD3D12CreateDevice() noexcept -> D3D12CreateDeviceFn {
    HMODULE d3d12Module = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12Module) d3d12Module = LoadLibraryW(L"d3d12.dll");
    return d3d12Module
        ? reinterpret_cast<D3D12CreateDeviceFn>(
            GetProcAddress(d3d12Module, "D3D12CreateDevice"))
        : nullptr;
}

auto BuildFactoryMethodTable() noexcept -> bool {
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    std::memcpy(
        FactoryMethods.data(),
        *reinterpret_cast<void***>(factory.Get()),
        FactoryMethodCount * sizeof(void*));
    return true;
}

auto BuildMethodTableWithoutWindow() noexcept -> bool {
    const auto createDevice = ResolveD3D12CreateDevice();
    if (!createDevice) return false;

    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<IDXGISwapChain1> swapChain1;
    ComPtr<IDXGISwapChain3> swapChain3;

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    if (FAILED(createDevice(
            nullptr,
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)))) {
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(
            &queueDesc,
            IID_PPV_ARGS(&queue)))) {
        return false;
    }
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)))) {
        return false;
    }
    if (FAILED(device->CreateCommandList(
            0U,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = 100U;
    swapDesc.Height = 100U;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1U;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2U;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    const HRESULT swapChainResult
        = FactoryHooksInstalled
            && OriginalCreateSwapChainForComposition != nullptr
        ? OriginalCreateSwapChainForComposition(
            factory.Get(), queue.Get(), &swapDesc, nullptr, &swapChain1)
        : factory->CreateSwapChainForComposition(
            queue.Get(), &swapDesc, nullptr, &swapChain1);
    if (FAILED(swapChainResult)) {
        return false;
    }
    if (FAILED(swapChain1.As(&swapChain3))) return false;

    std::memcpy(
        Methods.data(),
        *reinterpret_cast<void***>(device.Get()),
        44U * sizeof(void*));
    std::memcpy(
        Methods.data() + 44U,
        *reinterpret_cast<void***>(queue.Get()),
        19U * sizeof(void*));
    std::memcpy(
        Methods.data() + 63U,
        *reinterpret_cast<void***>(allocator.Get()),
        9U * sizeof(void*));
    std::memcpy(
        Methods.data() + 72U,
        *reinterpret_cast<void***>(commandList.Get()),
        60U * sizeof(void*));
    std::memcpy(
        Methods.data() + 132U,
        *reinterpret_cast<void***>(swapChain3.Get()),
        18U * sizeof(void*));
    return true;
}

auto CreateAndEnableHook(
    std::size_t methodIndex,
    void* detour,
    void** original) noexcept -> bool {
    if (methodIndex >= Methods.size() || !Methods[methodIndex]) return false;
    const MH_STATUS created = MH_CreateHook(
        Methods[methodIndex], detour, original);
    if (created != MH_OK) return false;
    if (MH_EnableHook(Methods[methodIndex]) == MH_OK) return true;
    MH_RemoveHook(Methods[methodIndex]);
    if (original) *original = nullptr;
    return false;
}

auto CreateAndEnableFactoryHook(
    std::size_t methodIndex,
    void* detour,
    void** original) noexcept -> bool {
    if (methodIndex >= FactoryMethods.size()
        || !FactoryMethods[methodIndex]) {
        return false;
    }
    const MH_STATUS created = MH_CreateHook(
        FactoryMethods[methodIndex], detour, original);
    if (created != MH_OK) return false;
    if (MH_EnableHook(FactoryMethods[methodIndex]) == MH_OK) return true;
    MH_RemoveHook(FactoryMethods[methodIndex]);
    if (original) *original = nullptr;
    return false;
}

void RemoveOwnedHook(std::size_t methodIndex) noexcept {
    if (methodIndex >= Methods.size() || !Methods[methodIndex]) return;
    MH_DisableHook(Methods[methodIndex]);
    MH_RemoveHook(Methods[methodIndex]);
}

void RemoveOwnedFactoryHook(std::size_t methodIndex) noexcept {
    if (methodIndex >= FactoryMethods.size()
        || !FactoryMethods[methodIndex]) {
        return;
    }
    MH_DisableHook(FactoryMethods[methodIndex]);
    MH_RemoveHook(FactoryMethods[methodIndex]);
}

void WaitForActiveHookCalls() noexcept {
    while (ActiveHookCalls.load(std::memory_order_acquire) != 0U)
        Sleep(1U);
}

auto AttachClientToLiveContextLocked(
    const ExternalClientEntry& incoming,
    const ExternalClientEntry* replaced) noexcept -> bool {
    if (!ContextCallbacksActive || !ImGuiContextCreated) return true;
    const bool contextChanged = replaced == nullptr
        || replaced->contextCreated != incoming.contextCreated
        || replaced->contextDestroying != incoming.contextDestroying
        || replaced->userData != incoming.userData;
    if (!contextChanged) return true;
    if (!WaitForGpuIdleLocked()) {
        LogWarning(
            "MapSense: a late overlay client could not join while GPU work was active.");
        return false;
    }

    if (ImGuiDx12Initialized) ImGui_ImplDX12_InvalidateDeviceObjects();
    const auto frame = MakeExternalFrameContextLocked();
    if (replaced != nullptr && replaced->contextDestroying != nullptr)
        replaced->contextDestroying(&frame, replaced->userData);
    if (incoming.contextCreated != nullptr)
        incoming.contextCreated(&frame, incoming.userData);

    if (!ImGuiDx12Initialized || ImGui_ImplDX12_CreateDeviceObjects())
        return true;

    // Restore the previous client lifecycle if the shared atlas could not be
    // uploaded. Registration remains transactional from the registry's view.
    if (incoming.contextDestroying != nullptr)
        incoming.contextDestroying(&frame, incoming.userData);
    if (replaced != nullptr && replaced->contextCreated != nullptr)
        replaced->contextCreated(&frame, replaced->userData);
    if (ImGuiDx12Initialized
        && !ImGui_ImplDX12_CreateDeviceObjects()) {
        LogWarning(
            "MapSense: the ImGui device objects could not be restored after a client registration failure.");
    }
    return false;
}
} // namespace

void SetD3D12ImGuiAutomapFontPath(const wchar_t* path) noexcept {
    std::scoped_lock lock(HostMutex);
    if (Configured || HooksInstalled || RendererInitialized) return;
    MapSenseAutomapFontPath.clear();
    if (path == nullptr) return;
    constexpr std::size_t MaximumFontPathCharacters = 32'767U;
    std::size_t length{};
    while (length < MaximumFontPathCharacters && path[length] != L'\0')
        ++length;
    if (length == MaximumFontPathCharacters) return;
    try {
        MapSenseAutomapFontPath.assign(path, length);
    } catch (...) {
        MapSenseAutomapFontPath.clear();
    }
}

void SetD3D12ImGuiAutomapSpritePath(const wchar_t* path) noexcept {
    std::scoped_lock lock(HostMutex);
    if (Configured || HooksInstalled || RendererInitialized) return;
    MapSenseAutomapSpritePath.clear();
    if (path == nullptr) return;
    constexpr std::size_t MaximumPathCharacters = 32'767U;
    std::size_t length{};
    while (length < MaximumPathCharacters && path[length] != L'\0')
        ++length;
    if (length == MaximumPathCharacters) return;
    try {
        MapSenseAutomapSpritePath.assign(path, length);
    } catch (...) {
        MapSenseAutomapSpritePath.clear();
    }
}

auto GetD3D12ImGuiAutomapFont() noexcept -> ImFont* {
    // Called only from callbacks serialized by HostMutex on Present. Reset
    // clears the pointer before destroying the atlas.
    return MapSenseAutomapFont;
}

auto GetD3D12ImGuiAutomapSpriteTexture() noexcept
        -> D3D12ImGuiTextureView {
    std::scoped_lock lock(HostMutex);
    return AutomapSpriteTextureView;
}

auto GetD3D12ImGuiPrimeMhChestTexture(
        bool specialChest) noexcept -> D3D12ImGuiTextureView {
    // The Present callbacks and renderer lifecycle are serialized by the
    // recursive host mutex. Taking it here also keeps this accessor safe for
    // future diagnostics outside the render callback.
    std::scoped_lock lock(HostMutex);
    return specialChest
        ? PrimeMhSuperChestTextureView
        : PrimeMhChestTextureView;
}

auto InitializeD3D12ImGuiHost(
    D3D12ImGuiHostCallbacks callbacks) noexcept -> bool {
    const bool configured = callbacks.drawPanel != nullptr
        && callbacks.ownedOverlayDismissal != nullptr
        && callbacks.queueUiTask != nullptr;
    if (configured) PreloadLocalizedMapSenseFonts();
    {
        std::scoped_lock lock(HostMutex);
        Callbacks = callbacks;
        Configured = configured;
        ClientsAccepting.store(true, std::memory_order_release);
        ConfiguredPublished.store(Configured, std::memory_order_release);
        InfoLogger.store(callbacks.info, std::memory_order_release);
        WarningLogger.store(callbacks.warning, std::memory_order_release);
    }
    if (!configured) return false;
    return TryInstallD3D12ImGuiHooks();
}

auto TryInstallD3D12ImGuiHooks() noexcept -> bool {
    std::scoped_lock lock(HostMutex);
    if (HooksInstalled) return true;
    if (!Configured) return false;

    if (!MinHookReady) {
        const MH_STATUS initialized = MH_Initialize();
        if (initialized != MH_OK
            && initialized != MH_ERROR_ALREADY_INITIALIZED) {
            return false;
        }
        MinHookReady = true;
        MinHookInitializedByHost = initialized == MH_OK;
    }

    const auto rollbackFactoryHooks = []() noexcept -> bool {
        if (OriginalCreateSwapChainForComposition != nullptr)
            RemoveOwnedFactoryHook(CreateSwapChainForCompositionMethod);
        if (OriginalCreateSwapChainForCoreWindow != nullptr)
            RemoveOwnedFactoryHook(CreateSwapChainForCoreWindowMethod);
        if (OriginalCreateSwapChainForHwnd != nullptr)
            RemoveOwnedFactoryHook(CreateSwapChainForHwndMethod);
        if (OriginalCreateSwapChain != nullptr)
            RemoveOwnedFactoryHook(CreateSwapChainMethod);
        OriginalResizeBuffers = nullptr;
        OriginalPresent = nullptr;
        OriginalCreateSwapChainForComposition = nullptr;
        OriginalCreateSwapChainForCoreWindow = nullptr;
        OriginalCreateSwapChainForHwnd = nullptr;
        OriginalCreateSwapChain = nullptr;
        FactoryHooksInstalled = false;
        FactoryMethods = {};
        if (MinHookInitializedByHost) MH_Uninitialize();
        MinHookReady = false;
        MinHookInitializedByHost = false;
        return false;
    };

    if (!FactoryHooksInstalled) {
        FactoryMethods = {};
        if (!BuildFactoryMethodTable()
            || !CreateAndEnableFactoryHook(
                CreateSwapChainMethod,
                reinterpret_cast<void*>(HookCreateSwapChain),
                reinterpret_cast<void**>(&OriginalCreateSwapChain))
            || !CreateAndEnableFactoryHook(
                CreateSwapChainForHwndMethod,
                reinterpret_cast<void*>(HookCreateSwapChainForHwnd),
                reinterpret_cast<void**>(&OriginalCreateSwapChainForHwnd))
            || !CreateAndEnableFactoryHook(
                CreateSwapChainForCoreWindowMethod,
                reinterpret_cast<void*>(HookCreateSwapChainForCoreWindow),
                reinterpret_cast<void**>(
                    &OriginalCreateSwapChainForCoreWindow))
            || !CreateAndEnableFactoryHook(
                CreateSwapChainForCompositionMethod,
                reinterpret_cast<void*>(HookCreateSwapChainForComposition),
                reinterpret_cast<void**>(
                    &OriginalCreateSwapChainForComposition))) {
            return rollbackFactoryHooks();
        }
        FactoryHooksInstalled = true;
        LogInfo(
            "MapSense: early DXGI swap-chain ownership hooks installed before D2R graphics initialization.");
    }

    Methods = {};
    if (!BuildMethodTableWithoutWindow()) {
        LogWarning(
            "MapSense: D3D12 Present method discovery is waiting while early swap-chain ownership hooks remain active.");
        return false;
    }

    const auto rollbackRenderHooks = []() noexcept -> bool {
        if (OriginalResizeBuffers != nullptr)
            RemoveOwnedHook(ResizeBuffersMethod);
        if (OriginalPresent != nullptr) RemoveOwnedHook(PresentMethod);
        OriginalResizeBuffers = nullptr;
        OriginalPresent = nullptr;
        return false;
    };
    if (!CreateAndEnableHook(
            PresentMethod,
            reinterpret_cast<void*>(HookPresent),
            reinterpret_cast<void**>(&OriginalPresent))
        || !CreateAndEnableHook(
            ResizeBuffersMethod,
            reinterpret_cast<void*>(HookResizeBuffers),
            reinterpret_cast<void**>(&OriginalResizeBuffers))) {
        return rollbackRenderHooks();
    }

    HooksInstalled = true;
    HooksInstalledPublished.store(true, std::memory_order_release);
    LogInfo(
        "MapSense: fail-closed D3D12 hooks installed with exact swap-chain command-queue ownership.");
    return true;
}

void ShutdownD3D12ImGuiHost() noexcept {
    {
        std::scoped_lock lock(HostMutex);
        if (HooksInstalled) {
            // Disable first so no new hook entry can race resource teardown.
            MH_DisableHook(Methods[ResizeBuffersMethod]);
            MH_DisableHook(Methods[PresentMethod]);
            HooksInstalledPublished.store(false, std::memory_order_release);
        }
        if (FactoryHooksInstalled) {
            MH_DisableHook(
                FactoryMethods[CreateSwapChainForCompositionMethod]);
            MH_DisableHook(
                FactoryMethods[CreateSwapChainForCoreWindowMethod]);
            MH_DisableHook(FactoryMethods[CreateSwapChainForHwndMethod]);
            MH_DisableHook(FactoryMethods[CreateSwapChainMethod]);
        }
        RendererResetPending = true;
        MenuOpen.store(false, std::memory_order_release);
        AwaitingFirstBounds.store(false, std::memory_order_release);
        PublishPanelBounds({});
    }

    WaitForActiveHookCalls();
    const bool inputSubclassRemoved = RemoveInputSubclassSynchronously();
    if (!inputSubclassRemoved) {
        if (!PinHostModuleForResidualSubclass()) {
            LogWarning(
                "MapSense: waiting for the D2R input subclass to acknowledge removal before unload.");
            while (!RemoveInputSubclassSynchronously()) Sleep(1U);
        }
        else {
            LogWarning(
                "MapSense: D2R retained the input subclass; the host module was pinned for process-lifetime safety.");
        }
    }

    {
        std::scoped_lock lock(HostMutex);
        ResetRendererStateLocked(
            inputSubclassRemoved
                || !InputSubclassInstalled.load(std::memory_order_acquire));
        CommandQueue.Reset();
        CapturedQueue.store(nullptr, std::memory_order_release);
        SwapChainQueueBindings.clear();
        if (HooksInstalled) {
            MH_RemoveHook(Methods[ResizeBuffersMethod]);
            MH_RemoveHook(Methods[PresentMethod]);
        }
        if (FactoryHooksInstalled) {
            MH_RemoveHook(
                FactoryMethods[CreateSwapChainForCompositionMethod]);
            MH_RemoveHook(
                FactoryMethods[CreateSwapChainForCoreWindowMethod]);
            MH_RemoveHook(FactoryMethods[CreateSwapChainForHwndMethod]);
            MH_RemoveHook(FactoryMethods[CreateSwapChainMethod]);
        }
        if (MinHookInitializedByHost) MH_Uninitialize();
        MinHookReady = false;
        MinHookInitializedByHost = false;
        HooksInstalled = false;
        FactoryHooksInstalled = false;
        HooksInstalledPublished.store(false, std::memory_order_release);
        OriginalResizeBuffers = nullptr;
        OriginalPresent = nullptr;
        OriginalCreateSwapChainForComposition = nullptr;
        OriginalCreateSwapChainForCoreWindow = nullptr;
        OriginalCreateSwapChainForHwnd = nullptr;
        OriginalCreateSwapChain = nullptr;
        Methods = {};
        FactoryMethods = {};
        RendererPoisoned = false;
        UnboundSwapChainWarningLogged.store(false, std::memory_order_release);
        DeviceRemovalWarningLogged.store(false, std::memory_order_release);
        MenuOpen.store(false, std::memory_order_release);
        AwaitingFirstBounds.store(false, std::memory_order_release);
        PublishPanelBounds({});
        Callbacks = {};
        Configured = false;
        ConfiguredPublished.store(false, std::memory_order_release);
        InfoLogger.store(nullptr, std::memory_order_release);
        WarningLogger.store(nullptr, std::memory_order_release);
    }
}

void ShutdownD3D12ImGuiHostAndNotifyClients() noexcept {
    ClientsAccepting.store(false, std::memory_order_release);
    ShutdownD3D12ImGuiHost();

    std::array<ExternalClientEntry, MaximumExternalClients> clients{};
    {
        std::scoped_lock lock(ExternalClientMutex);
        clients = ExternalClients;
        ExternalClients = {};
    }
    // Renderer resources and all MapSense hooks are already gone. A client
    // may now safely install its autonomous host from this callback.
    for (const auto& client : clients) {
        if (client.hostStopped != nullptr)
            client.hostStopped(client.userData);
    }
}

void ToggleD3D12ImGuiMenu() noexcept {
    std::scoped_lock lock(HostMutex);
    bool wasOpen = MenuOpen.load(std::memory_order_acquire);
    while (!MenuOpen.compare_exchange_weak(
        wasOpen,
        !wasOpen,
        std::memory_order_acq_rel,
        std::memory_order_acquire)) {
    }
    const bool isOpen = !wasOpen;
    AwaitingFirstBounds.store(isOpen, std::memory_order_release);
    if (!isOpen) PublishPanelBounds({});
}

void SetD3D12ImGuiMenuOpen(bool open) noexcept {
    // RenderPanelFrame reads, lets the panel mutate, and republishes this state
    // while holding HostMutex. Serialize lifecycle changes with that complete
    // transition so a late Present cannot undo GameLeft or LocalPlayerReady.
    std::scoped_lock lock(HostMutex);
    const bool changed = MenuOpen.exchange(open, std::memory_order_acq_rel)
        != open;
    if (changed && open)
        AwaitingFirstBounds.store(true, std::memory_order_release);
    if (!open) {
        const bool releasedOwnedButton = CancelOwnedMouseButtons();
        const HWND window = PublishedGameWindow.load(std::memory_order_acquire);
        if (releasedOwnedButton
            && window != nullptr
            && GetCapture() == window) {
            ReleaseCapture();
        }
        AwaitingFirstBounds.store(false, std::memory_order_release);
        PublishPanelBounds({});
    }
}

auto IsD3D12ImGuiMenuOpen() noexcept -> bool {
    return MenuOpen.load(std::memory_order_acquire);
}

auto GetD3D12ImGuiHostStatus() noexcept -> D3D12ImGuiHostStatus {
    return D3D12ImGuiHostStatus{
        .presentCalls = PresentCalls.load(std::memory_order_relaxed),
        .directQueueCaptures = ExactQueueBindings.load(
            std::memory_order_relaxed),
        .rendererInitAttempts = RendererInitAttempts.load(
            std::memory_order_relaxed),
        .rendererInitFailures = RendererInitFailures.load(
            std::memory_order_relaxed),
        .renderedFrames = RenderedFrames.load(std::memory_order_relaxed),
        .lastInitFailureStage = LastInitFailureStage.load(
            std::memory_order_relaxed),
        .configured = ConfiguredPublished.load(std::memory_order_acquire),
        .hooksInstalled = HooksInstalledPublished.load(
            std::memory_order_acquire),
        .commandQueueReady = CapturedQueue.load(
            std::memory_order_acquire) != nullptr,
        .rendererInitialized = RendererInitializedPublished.load(
            std::memory_order_acquire),
        .inputSubclassInstalled = InputSubclassInstalledPublished.load(
            std::memory_order_acquire),
        .primeMhChestTexturesReady = PrimeMhTexturesReadyPublished.load(
            std::memory_order_acquire),
        .automapSpriteAtlasReady = AutomapSpriteAtlasReadyPublished.load(
            std::memory_order_acquire),
        .menuOpen = MenuOpen.load(std::memory_order_acquire),
        .gameWindow = PublishedGameWindow.load(std::memory_order_acquire),
    };
}

auto RegisterD3D12ImGuiClientV2(
        const RuffnecKk::OverlayHost::ClientV2* client) noexcept -> bool {
    if (client == nullptr
        || client->structSize < RuffnecKk::OverlayHost::ClientV2Size
        || client->version != RuffnecKk::OverlayHost::ApiVersion2
        || client->imguiAbiFingerprint
            != RuffnecKk::OverlayHost::ImGuiAbiFingerprint
        || client->owner == nullptr
        || client->owner[0] == '\0'
        || strnlen_s(client->owner, 64U) >= 64U
        || (client->contextCreated == nullptr
            && client->contextDestroying == nullptr
            && client->hostStopped == nullptr
            && client->beforeFrame == nullptr
            && client->render == nullptr)
        || !ClientsAccepting.load(std::memory_order_acquire)) {
        return false;
    }

    std::scoped_lock hostLock(HostMutex);
    if (!ClientsAccepting.load(std::memory_order_acquire)) return false;

    ExternalClientEntry incoming{};
    strncpy_s(
        incoming.owner.data(),
        incoming.owner.size(),
        client->owner,
        _TRUNCATE);
    incoming.contextCreated = client->contextCreated;
    incoming.contextDestroying = client->contextDestroying;
    incoming.hostStopped = client->hostStopped;
    incoming.beforeFrame = client->beforeFrame;
    incoming.render = client->render;
    incoming.userData = client->userData;

    std::size_t selected = MaximumExternalClients;
    ExternalClientEntry replaced{};
    bool replacing{};
    {
        std::scoped_lock registryLock(ExternalClientMutex);
        for (std::size_t index = 0U;
             index < ExternalClients.size();
             ++index) {
            const auto& entry = ExternalClients[index];
            if (entry.owner[0] != '\0'
                && std::strcmp(entry.owner.data(), client->owner) == 0) {
                selected = index;
                replaced = entry;
                replacing = true;
                break;
            }
            if (entry.owner[0] == '\0'
                && selected == MaximumExternalClients) {
                selected = index;
            }
        }
    }
    if (selected == MaximumExternalClients) return false;
    if (!AttachClientToLiveContextLocked(
            incoming, replacing ? &replaced : nullptr)) {
        return false;
    }
    {
        std::scoped_lock registryLock(ExternalClientMutex);
        ExternalClients[selected] = incoming;
    }
    return true;
}

auto UnregisterD3D12ImGuiClientV2(const char* owner) noexcept -> bool {
    if (owner == nullptr || owner[0] == '\0') return false;
    std::scoped_lock hostLock(HostMutex);
    std::size_t selected = MaximumExternalClients;
    ExternalClientEntry removed{};
    {
        std::scoped_lock registryLock(ExternalClientMutex);
        for (std::size_t index = 0U;
             index < ExternalClients.size();
             ++index) {
            const auto& entry = ExternalClients[index];
            if (entry.owner[0] != '\0'
                && std::strcmp(entry.owner.data(), owner) == 0) {
                selected = index;
                removed = entry;
                break;
            }
        }
    }
    if (selected == MaximumExternalClients) return true;

    if (ContextCallbacksActive && ImGuiContextCreated) {
        if (!WaitForGpuIdleLocked()) {
            LogWarning(
                "MapSense: GPU idle wait failed while unregistering an overlay client.");
        }
        const auto frame = MakeExternalFrameContextLocked();
        if (removed.contextDestroying != nullptr)
            removed.contextDestroying(&frame, removed.userData);
    }
    {
        std::scoped_lock registryLock(ExternalClientMutex);
        ExternalClients[selected] = {};
    }
    return true;
}

void ClearD3D12ImGuiClientsV2() noexcept {
    std::scoped_lock hostLock(HostMutex);
    const auto clients = CopyExternalClients();
    if (ContextCallbacksActive && ImGuiContextCreated) {
        if (!WaitForGpuIdleLocked()) {
            LogWarning(
                "MapSense: GPU idle wait failed while clearing overlay clients.");
        }
        const auto frame = MakeExternalFrameContextLocked();
        for (const auto& client : clients) {
            if (client.contextDestroying != nullptr)
                client.contextDestroying(&frame, client.userData);
        }
    }
    std::scoped_lock registryLock(ExternalClientMutex);
    ExternalClients = {};
}

} // namespace RuffnecKk::MapSense
