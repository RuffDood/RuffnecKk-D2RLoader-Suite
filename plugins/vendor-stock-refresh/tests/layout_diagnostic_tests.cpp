// Exercise the actual configure hook and diagnostic scope with synthetic native
// widgets. No game process, native helper call or real memory patch is involved.
#include "../src/plugin.cpp"
#include <vector>
#include <algorithm>
using namespace RuffnecKk::VendorStockRefresh;
namespace {
struct Widget { alignas(16) std::array<std::uint8_t, 384> bytes{}; };
std::array<Widget, 11> widgets{};
constexpr std::array names{"background", "background_repair", "button_refresh", "button_repair", "button_repair_all", "StashWidget", "gold_icon", "gold_amount", "vendor_refresh_frame", "vendor_refresh_slot", "vendor_refresh_gold_anchor"};
std::vector<std::string> messages;
std::array<void*, 11> vtable{};
bool legacy{};
void __fastcall SetFilename(void* widget, const NativeStringView* view) {
    auto* bytes = static_cast<std::uint8_t*>(widget);
    std::memcpy(bytes + 0x108, &view->data, sizeof(view->data));
    std::memcpy(bytes + 0x110, &view->length, sizeof(view->length));
}
bool __fastcall ImageReady(void* reference) {
    auto* bytes = static_cast<std::uint8_t*>(reference) - 0x130;
    const char* path{}; std::memcpy(&path, bytes + 0x108, sizeof(path));
    return SameImagePath(path, NativeBackgrounds[0]) || SameImagePath(path, NativeBackgrounds[1]);
}
void __cdecl Log(const D2RL::PluginContext*, const char* message) noexcept { messages.emplace_back(message); }
void __fastcall State(void*, bool) noexcept {}
void __fastcall Configure(void*) noexcept {}
std::int32_t __fastcall Gambling() noexcept { return 0; }
void* __fastcall Find(void*, const char* name) noexcept {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (std::strcmp(names[i], name) == 0) return i >= 8 && !legacy ? nullptr : widgets[i].bytes.data();
    return nullptr;
}
WidgetRect* __fastcall Rect(void* widget, WidgetRect* out) noexcept {
    std::memcpy(out, static_cast<std::uint8_t*>(widget) + 0x70, sizeof(*out)); return out;
}
void Initialize(bool framed) {
    legacy = framed; widgets = {}; messages.clear(); PlacementCache = {};
    LayoutDiagnosticEvents.store(0); LayoutDiagnosticActive = false;
    vtable[9] = reinterpret_cast<void*>(State); vtable[10] = reinterpret_cast<void*>(State);
    const std::array<WidgetRect, 11> rects{{
        {1,0,1162,1507},{1,0,1162,1507},{877,1277,116,116},{169,1277,116,116},{877,1277,116,116},
        {421,1305,313,58},{427,1304,57,57},{487,1309,249,48},{0,1210,1162,297},CompanionSlot,CompanionGold}};
    for (std::size_t i = 0; i < widgets.size(); ++i) {
        auto* bytes = widgets[i].bytes.data(); auto* vt = vtable.data(); const float scale = 1;
        std::memcpy(bytes, &vt, sizeof(vt));
        std::memcpy(bytes + 0x70, &rects[i], sizeof(WidgetRect));
        std::memcpy(bytes + 0x80, &scale, sizeof(scale));
        if (i < 2) {
            const char* path = NativeBackgrounds[i]; const auto len = std::strlen(path);
            std::memcpy(bytes + 0x108, &path, sizeof(path)); std::memcpy(bytes + 0x110, &len, sizeof(len));
        }
    }
}
bool Contains(const char* text) {
    return std::any_of(messages.begin(), messages.end(), [text](const auto& s) { return s.find(text) != std::string::npos; });
}
}
#define CHECK(e) do { if (!(e)) { std::printf("failed line %d: %s\n", __LINE__, #e); return __LINE__; } } while(false)
int main() {
    D2RL::PluginApi api{}; api.apiSize = D2RL::PluginApiSize; api.logInfo = Log; api.logError = Log;
    D2RL::PluginContext context{}; context.contextSize = D2RL::PluginContextSize; context.api = &api;
    Context = &context; FindWidget = Find; GetWidgetRect = Rect;
    OriginalConfigureVendorPanel = Configure; IsGambling = Gambling; Settings.diagnosticsEnabled = false;
    SetImageFilename = SetFilename; IsImageReady = ImageReady;
    int panel{};
    Initialize(false); HookConfigureVendorPanel(&panel);
    CHECK(Contains("companion rejected: background desktop geometry mismatch"));
    CHECK(Contains("route=measured-fallback"));
    CHECK(Contains("scale=1.000000 footprint=116.000x116.000"));
    CHECK(Contains("before widget=button_repair_all present=1"));
    CHECK(Contains("filename=PANEL/Vendors/VendorShop_BG"));
    for (int i = 1; i < 12; ++i) HookConfigureVendorPanel(&panel);
    CHECK(Contains("configure=12 end; capture limit reached"));
    const auto count = messages.size(); HookConfigureVendorPanel(&panel); CHECK(messages.size() == count);
    // Replay the reported case: native geometry passes, companion load fails,
    // native restoration succeeds. The button must retain the repair size.
    Initialize(false);
    const WidgetRect nativeBackground{0,0,1162,1507};
    for (int i = 0; i < 2; ++i) std::memcpy(widgets[i].bytes.data() + 0x70, &nativeBackground, sizeof(nativeBackground));
    HookConfigureVendorPanel(&panel);
    CHECK(Contains("companion rejected: background image failed to load"));
    CHECK(Contains("image-set path=PANEL/Vendors/VendorShop_BG ready=1"));
    CHECK(Contains("after widget=button_refresh present=1 rect-ok=1 rect=519,1371,116,116 geometry-ok=1 scale=1.000000"));
    // The same framed slot must restore a pre-shrunk control to the repair size.
    Initialize(true);
    const float half = 0.5F; std::memcpy(widgets[2].bytes.data() + 0x80, &half, sizeof(half));
    HookConfigureVendorPanel(&panel);
    CHECK(Contains("route=legacy-frame"));
    CHECK(Contains("after widget=button_refresh present=1 rect-ok=1 rect=520,1352,116,116 geometry-ok=1 scale=1.000000"));
    CHECK(!Contains("companion rejected:"));
    // Move both native gold widgets down 12 units, preserving horizontal
    // position, gap, dimensions and scale. This is not horizontal centering.
    WidgetGeometry coin{}, amount{};
    CHECK(ReadWidgetGeometry(widgets[6].bytes.data(), coin));
    CHECK(ReadWidgetGeometry(widgets[7].bytes.data(), amount));
    CHECK(coin.rect.x == 427 && coin.rect.y == 1271 && coin.rect.width == 57);
    CHECK(amount.rect.x == 487 && amount.rect.y == 1276 && amount.rect.width == 249);
    CHECK(coin.scale == 1 && amount.scale == 1);
    const WidgetRect oldLegacyGold{421,1260,313,58};
    std::memcpy(widgets[10].bytes.data() + 0x70, &oldLegacyGold, sizeof(oldLegacyGold));
    HookConfigureVendorPanel(&panel);
    CHECK(ReadWidgetGeometry(widgets[6].bytes.data(), coin) && coin.rect.y == 1271);
    CHECK(SetGoldLayout(&panel, nullptr));
    CHECK(ReadWidgetGeometry(widgets[6].bytes.data(), coin));
    CHECK(ReadWidgetGeometry(widgets[7].bytes.data(), amount));
    CHECK(coin.rect.x == 427 && coin.rect.y == 1304);
    CHECK(amount.rect.x == 487 && amount.rect.y == 1309 && amount.rect.width == 249);
    std::puts("PASS: automatic captures, fallback reason, dimensions, native repair names, legacy route, 12-event limit.");
    return 0;
}
