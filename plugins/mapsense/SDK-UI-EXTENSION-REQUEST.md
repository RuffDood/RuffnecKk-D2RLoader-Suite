# PluginSDK request: typed widget values

- **Audience:** Dimentino / D2RLoader maintainers
- **Requestor:** RuffnecKk
- **Reference implementation:** RuffnecKk MapSense 0.3.0 native-settings prototype
- **SDK baseline evaluated:** PluginSDK API v3, commit `4933e2c42cb2592958cd0df3b6dc5003102252d1`

## Executive request

Could `WidgetService` expose the primary value of supported native widgets as a small, versioned, typed API?

MapSense only needs:

- get and set a `bool`, `int32`, `float`, UTF-8 text, or RGBA color;
- receive a typed value-changed callback containing the originating widget handle and the change source;
- keep all calls owner-scoped and UI-thread-only, like the existing widget operations.

This is deliberately **not** a request for a new UI framework, renderer, configuration system, input hook, or access to native widget pointers. A generic tagged value plus four additional `WidgetService` operations would be enough.

## What PluginSDK v3 already solves well

The public v3 services already provide the important ownership and lifecycle foundation:

- `ResourceServiceV1` registers an embedded D2R layout resource.
- `PanelServiceV1` registers, opens, closes, and toggles an owner-scoped panel.
- `WidgetServiceV1` finds panels and descendant widgets by stable path handles, reads a widget rectangle, changes visibility or enabled state, and dispatches a UI action.
- `SharedEventServiceV1` lets a plugin receive the existing `target` / `command` / `text` UI-message shape.
- service queries are explicitly versioned and can return `UnsupportedVersion` or `Unavailable`.
- widget calls run on the D2RLoader UI thread and do not expose native pointers to plugins.

MapSense 0.3.0 uses only these public services. Its native panel does not own a Win32 window, intercept D2R input, or hook a graphics API.

The missing primitive is widget **value state**. `WidgetServiceV1` cannot currently read or write the checked state of a toggle, the numeric value of a slider or selector, editable text, or a color. A raw UI message tells us which command string fired, but it does not carry an authoritative typed value or the widget handle that owns it.

## What the current MapSense prototype must do instead

The 0.3.0 prototype uses `ButtonWidget` and `TextBoxWidget` variants and selects the visible variant through `setWidgetVisible`. The plugin owns a separate draft state and interprets messages such as “previous”, “next”, and “toggle”.

That keeps the panel native and safe, but it forces finite choices:

- opacity is reduced to `25%`, `50%`, `75%`, `90%`, or `100%`;
- monster marker size is reduced to `6`, `8`, `10`, `14`, or `18` pixels;
- immunity colors are reduced to three complete palettes;
- booleans require paired visual states;
- every additional option requires more layout widgets, visibility bookkeeping, and string commands.

This is acceptable for proving panel registration, input ownership, Apply/Default/Discard behavior, and TOML persistence. It is not a maintainable final editor for per-monster colors, immunity colors, map scale, line thickness, projectile styles, and similar settings.

## Smallest useful extension

The smallest surface we can see is a `WidgetServiceV2` that keeps the V1 prefix and appends four operations:

1. `getWidgetValue`
2. `setWidgetValue`
3. `registerWidgetValueChangedListener`
4. `unregisterWidgetValueChangedListener`

A single tagged value avoids ten separate getter/setter function pointers while preserving concrete types.

The following is an illustrative ABI shape, not a requirement on naming or exact layout:

```cpp
namespace D2RL::Widgets {

enum class ValueType : uint32_t {
    Boolean   = 1,
    Int32     = 2,
    Float32   = 3,
    Utf8Text  = 4,
    RgbaFloat = 5,
};

enum class ValueChangeSource : uint32_t {
    Unknown       = 0,
    UserInput     = 1,
    PluginSet     = 2,
    NativeBinding = 3,
};

struct Utf8View {
    const char* data;
    uint32_t    length;
    uint32_t    reserved;
};

struct RgbaFloat {
    float red;
    float green;
    float blue;
    float alpha;
};

union ValuePayload {
    uint32_t  booleanValue; // 0 or 1; avoids bool ABI ambiguity
    int32_t   int32Value;
    float     float32Value;
    Utf8View  utf8Text;
    RgbaFloat rgbaFloat;
};

struct WidgetValue {
    uint32_t     structSize;
    ValueType    type;
    uint32_t     flags;
    uint32_t     reserved;
    ValuePayload payload;
};

struct WidgetValueChangedEvent {
    uint32_t          structSize;
    uint32_t          flags;
    WidgetHandle      widget;
    ValueChangeSource source;
    uint32_t          reserved;
    WidgetValue       value;
};

using ValueListenerHandle = uint64_t;

using WidgetValueChangedCallback = void(__cdecl*)(
    const PluginContext* context,
    const WidgetValueChangedEvent* event,
    void* userData) noexcept;

struct WidgetValueChangedListener {
    uint32_t                   structSize;
    uint32_t                   flags;
    int32_t                    priority;
    uint32_t                   reserved;
    WidgetHandle               widgetFilter;
    WidgetValueChangedCallback callback;
    void*                      userData;
};

using GetWidgetValueFn = Result(__cdecl*)(
    const PluginContext* context,
    WidgetHandle handle,
    WidgetValue* value) noexcept;

using SetWidgetValueFn = Result(__cdecl*)(
    const PluginContext* context,
    WidgetHandle handle,
    const WidgetValue* value,
    uint32_t flags) noexcept;

using RegisterWidgetValueChangedListenerFn = Result(__cdecl*)(
    const PluginContext* context,
    const WidgetValueChangedListener* listener,
    ValueListenerHandle* handle) noexcept;

using UnregisterWidgetValueChangedListenerFn = Result(__cdecl*)(
    const PluginContext* context,
    ValueListenerHandle handle) noexcept;

} // namespace D2RL::Widgets
```

`WidgetServiceV2` could repeat the V1 function-pointer prefix and append those four pointers. A plugin would query `ServiceId::Widget` with version `2`; a Loader that only supports V1 would continue returning V1 unchanged.

Keeping the changed listener in `WidgetServiceV2`, instead of also creating a new `SharedEventService` version, makes this request additive to only one service. If D2RLoader architecture strongly prefers all callbacks in `SharedEventService`, the same event contract there would also work.

### Versioned C ABI discipline

The ABI-facing definitions should follow the SDK's current C-compatible pattern:

- fixed-width integer and floating-point fields only;
- `uint32_t` tags instead of C++ RTTI or `std::variant`;
- `structSize` on every structure supplied across the boundary;
- `serviceSize` and an exact `serviceVersion` on `WidgetServiceV2`;
- `__cdecl` `noexcept` function pointers;
- no STL containers, owning strings, virtual methods, or allocator ownership across the boundary;
- standard-layout, trivially-copyable structures with compile-time size and offset assertions.

The namespaced C++ declarations above are only the header presentation used by PluginSDK. The binary contract remains a C-style table of function pointers and POD payloads.

## Proposed semantics

### One primary value, not field reflection

Each supported editor widget exposes one primary value:

- toggle or checkbox: `Boolean`;
- integer selector or integer slider: `Int32`;
- continuous slider: `Float32`;
- editable or dynamically writable text control: `Utf8Text`;
- color-valued control: `RgbaFloat`.

The API does not need to expose arbitrary JSON fields, widget internals, animation state, textures, navigation links, or native addresses.

### Type safety

- `getWidgetValue` returns the widget's actual primary type and value.
- `setWidgetValue` rejects a mismatched tag with `InvalidArgument` or `Unsupported`.
- unsupported widget classes return `Unsupported` without changing the widget.
- non-finite floats and colors, invalid UTF-8, and boolean values other than `0` or `1` are rejected.
- the Loader applies the same range, step, clamping, and validation rules used by native user interaction.

### Text lifetime

For `Utf8Text`, `setWidgetValue` copies the supplied bytes before returning. A text view returned by `getWidgetValue`, or delivered in a callback, may be borrowed. Its documented lifetime can be limited to the current call or callback; plugins will copy it if they need to retain it.

### Changed event

The callback should contain:

- the stable `WidgetHandle` that changed;
- the complete new typed value;
- a small source enum that distinguishes user interaction, plugin assignment, and a native binding update.

Mouse, keyboard, and controller do not need separate sources for this use case. One `UserInput` source is sufficient.

An invalid `widgetFilter` can mean “all value changes for widgets owned by this plugin”. A concrete handle can restrict the listener to one widget.

To prevent feedback loops, either of these contracts is sufficient:

- programmatic sets emit `PluginSet`, and `setWidgetValue` accepts a `SuppressChangedEvent` flag; or
- programmatic sets never emit a changed event, and the source enum is reserved for user/native changes.

The first form is more flexible, but MapSense does not depend on receiving its own sets.

### Threading and ownership

- every get, set, registration, unregistration, and callback occurs on the UI thread;
- handles remain stable owner-scoped paths rather than native pointers;
- a plugin cannot mutate or subscribe to another owner's private widgets;
- listeners are automatically removed when their owner unloads;
- callbacks never cross the ABI with C++ exceptions.

These rules match the safety model already established by the v3 services.

## MapSense use cases unlocked by this primitive

The extension would let MapSense replace its variant workaround incrementally:

| MapSense setting | Preferred value |
|---|---|
| Enable map additions and monster categories | `Boolean` |
| Opacity, map scale, marker size, halo thickness, line width | `Float32` |
| Shape/style selection and hotkey code | `Int32` |
| User-facing labels or editable key text | `Utf8Text` |
| Monster, immunity, path, and projectile colors | `RgbaFloat` |

Reveal Level, Reveal Act, Reveal All, Off, Apply, Default, and Discard are actions rather than settings. Existing button messages already handle them correctly and do not require a new API.

MapSense will continue to own:

- its draft/applied/default state model;
- TOML parsing, validation, and persistence through the existing plugin configuration API;
- all map data, reveal logic, rendering policy, and hotkey actions;
- fallback behavior when the richer widget service is unavailable.

## Explicitly out of scope

This request does **not** require:

- an ImGui integration or any other immediate-mode GUI;
- a second overlay window, swap-chain hook, OpenGL hook, or render callback;
- cursor capture, hit-testing, Windows message hooks, or input suppression APIs;
- a schema-driven automatic settings generator;
- SDK-owned TOML/JSON persistence;
- access to D2R native widget pointers or arbitrary memory;
- cross-plugin widget mutation;
- a new color-picker implementation if D2RLoader does not already own a suitable native control.

Typed RGBA transport is still useful even if the first implementation only supports a color-valued widget that already exists. A new visual color picker can remain a separate Loader decision.

## Compatibility and failure behavior

MapSense would treat V2 as an optional enhancement:

1. Query `ServiceId::Widget` version `2`.
2. Validate `serviceVersion`, `serviceSize`, and every function pointer before use.
3. Use typed controls when V2 is available.
4. Fall back to the current discrete V1 panel when V2 is unavailable.
5. Keep reveal actions and the rest of the plugin operational even if the settings panel cannot initialize.

No PluginSDK v1/v2 service contract needs to change. Existing API v3 plugins querying `WidgetServiceV1` should remain binary- and source-compatible.

## Acceptance criteria

The extension would be sufficient for MapSense when all of the following are true:

1. An API v3 plugin can query `WidgetServiceV2` without affecting V1 callers.
2. The plugin can find an owner-scoped supported widget and round-trip each supported primary type: boolean, `int32`, `float`, UTF-8 text, and RGBA float color.
3. Setting a value updates the visible native control on the UI thread and applies that control's native validation rules.
4. A user change by mouse, keyboard, or controller produces one typed callback with the changed widget handle, the new value, and `UserInput` as its source.
5. Programmatic notification behavior is documented and cannot force a callback loop.
6. Unsupported widget/type combinations fail with a normal `Widgets::Result` and do not corrupt or partially update state.
7. Stale handles, closed panels, inactive owners, and owner mismatches retain the existing fail-safe result behavior.
8. Listener teardown is automatic on plugin unload and explicit unregistration is safe from the callback or UI thread.
9. Borrowed UTF-8 lifetime is documented and no allocator ownership crosses the ABI.
10. The existing PluginSDK V1 widget and UI-panel samples compile and behave unchanged.
11. A small SDK sample demonstrates one toggle and one numeric control being hydrated by a plugin, changed by the user, and reported through the typed callback.

## Why this is the right boundary

The native MapSense prototype has already proved that Resource, Panel, Widget, and SharedEvent services can host a stable D2R-styled settings surface without a separate renderer or window. The observed limitation is narrow: plugins can address widgets, but cannot exchange their values.

Adding typed primary values at the existing widget-handle boundary preserves D2RLoader ownership of native UI details, preserves plugin isolation, and lets plugins keep responsibility for their own configuration and behavior. It solves the measured gap without turning PluginSDK into a general UI framework.
