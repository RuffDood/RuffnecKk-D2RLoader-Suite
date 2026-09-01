#include <D2RLPlugin/api.h>

#include "native_contract.hpp"
#include "policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace RuffnecKk::VendorStockRefresh {
namespace {

constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uint64_t MaximumDiagnosticLogs = 12;
constexpr std::uintptr_t SendVendorRefreshRva = 0x10F520;
constexpr std::uintptr_t IsGamblingRva = 0x10CAC0;
constexpr std::uintptr_t SendNineBytePacketRva = 0x0EC730;
constexpr std::uintptr_t DownstreamQueueRva = 0x0EE360;
constexpr std::uintptr_t CurrentNpcGuidRva = 0x2A4875C;
constexpr std::uintptr_t EntityActionRva = 0x4B0470;
constexpr std::uintptr_t ConfigureVendorInteractionRva = 0x502F60;
constexpr std::uintptr_t GetVendorChainEntryRva = 0x502B70;
constexpr std::uintptr_t ConfigureVendorPanelRva = 0x2411E0;
constexpr std::uintptr_t FindWidgetRva = 0x856220;
constexpr std::uintptr_t GetWidgetRectRva = 0x8562A0;

constexpr std::size_t EntityActionPacketSize = 9;
constexpr std::size_t EntityActionOffset = 1;
constexpr std::size_t VendorEntryFilledOffset = 0x34;
constexpr std::size_t VendorEntryRefreshPendingOffset = 0x35;
constexpr std::size_t WidgetRectOffset = 0x70;

constexpr std::array<std::uint8_t, 19> SendVendorRefreshExpected{
    0x44, 0x8B, 0x05, 0x35, 0x92, 0x93, 0x02, 0xBA,
    0x02, 0x00, 0x00, 0x00, 0xB1, 0x38, 0xE9, 0xFD,
    0xD1, 0xFD, 0xFF
};
constexpr std::array<std::uint8_t, 32> ConfigureVendorInteractionExpected{
    0x40, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x48, 0x89, 0x6C, 0x24, 0x48, 0x41, 0x0F,
    0xB6, 0xF1, 0x4C, 0x89, 0x74, 0x24, 0x50, 0x49,
    0x8B, 0xE8, 0x4C, 0x8B, 0xF1, 0x4C, 0x8B, 0xFA
};
constexpr std::array<std::uint8_t, 21> EntityActionExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81,
    0xEC, 0xC8, 0x00, 0x00, 0x00
};
constexpr std::array<std::uint8_t, 7> IsGamblingExpected{
    0x8B, 0x05, 0x42, 0xBD, 0x93, 0x02, 0xC3
};
constexpr std::array<std::uint8_t, 24> GetVendorChainEntryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xC2, 0x49, 0x8B, 0xF8,
    0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x15, 0xD6, 0x6D
};
constexpr std::array<std::uint8_t, 36> ConfigureVendorPanelExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
    0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFD, 0xFF, 0xFF,
    0x48, 0x81, 0xEC, 0x20, 0x03, 0x00, 0x00, 0x48,
    0x8B, 0x05, 0xC2, 0xA0
};
constexpr std::array<std::uint8_t, 32> FindWidgetExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0x41, 0x60, 0x48, 0x8D, 0x3C, 0xC3, 0x48, 0x3B
};
constexpr std::array<std::uint8_t, 32> GetWidgetRectExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79,
    0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48,
    0x8B, 0x49, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20,
    0xE8, 0xE3, 0xFF, 0xFF, 0xFF, 0x33, 0xC0, 0x48
};

using SendVendorRefreshFn = void(__fastcall*)() noexcept;
using IsGamblingFn = std::int32_t(__fastcall*)() noexcept;
using SendNineBytePacketFn = void(__fastcall*)(
    std::uint8_t opcode,
    std::uint32_t action,
    std::uint32_t npcGuid
) noexcept;
using ConfigureVendorInteractionFn = void(__fastcall*)(
    void* game,
    void* npc,
    void* player,
    std::uint8_t mode
) noexcept;
using EntityActionFn = std::int32_t(__fastcall*)(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t packetSize
) noexcept;
using GetVendorChainEntryFn = void*(__fastcall*)(
    void* game,
    void* npc,
    std::int32_t* indexOut
) noexcept;
using ConfigureVendorPanelFn = void(__fastcall*)(void* panel) noexcept;
using FindWidgetFn = void*(__fastcall*)(void* panel, const char* name) noexcept;
using GetWidgetRectFn = WidgetRect*(__fastcall*)(
    void* widget,
    WidgetRect* rectOut
) noexcept;
using SetWidgetBoolFn = void(__fastcall*)(void* widget, bool value) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
SendVendorRefreshFn OriginalSendVendorRefresh{};
ConfigureVendorInteractionFn OriginalConfigureVendorInteraction{};
EntityActionFn OriginalEntityAction{};
IsGamblingFn IsGambling{};
SendNineBytePacketFn SendNineBytePacket{};
GetVendorChainEntryFn GetVendorChainEntry{};
ConfigureVendorPanelFn OriginalConfigureVendorPanel{};
FindWidgetFn FindWidget{};
GetWidgetRectFn GetWidgetRect{};
std::atomic<std::uint64_t> NormalRequestsSent{};
std::atomic<std::uint64_t> NormalRequestsReceived{};
std::atomic<std::uint64_t> NormalRefreshesArmed{};
std::atomic<std::uint64_t> RejectedNormalRequests{};
std::atomic<std::uint64_t> DynamicPlacements{};
std::atomic<std::uint64_t> PlacementFailures{};
std::atomic_bool PlacementFailureReported{};
std::atomic_bool PlacementSuccessReported{};
std::atomic<std::uint64_t> DiagnosticLogs{};

enum class PacketRoute {
    Invalid,
    Vanilla,
    VerifiedD2RCoreRelay,
};

PacketRoute ActivePacketRoute{PacketRoute::Invalid};

struct RefreshPlacementCache {
    void* panel{};
    void* widget{};
    WidgetRect original{};
    WidgetRect applied{};
    bool hasOriginal{};
    bool hasApplied{};
};

RefreshPlacementCache PlacementCache{};

struct RefreshScope {
    bool active{};
    bool armed{};
    void* player{};
};

thread_local RefreshScope ActiveRefresh{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-vendor-stock-refresh",
    .name = "Vendor Stock Refresh",
    .version = "2.0.0",
    .author = "RuffnecKk",
    .description = "Refreshes a vendor's stock with one click.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "VendorStockRefresh: configuration exceeds 65535 bytes."
            : "VendorStockRefresh: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("VendorStockRefresh: invalid TOML (")
            + error + "); no hook was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

auto TakeDiagnosticLogSlot() noexcept -> bool {
    return Settings.diagnosticsEnabled
        && DiagnosticLogs.fetch_add(1, std::memory_order_relaxed)
            < MaximumDiagnosticLogs;
}

bool IsReadableRange(const void* address, std::size_t size) noexcept {
    if (!address || size == 0) return false;
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    if (cursor > std::numeric_limits<std::uintptr_t>::max() - size) return false;
    const auto end = cursor + size;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory))
            != sizeof(memory)) {
            return false;
        }
        if (memory.State != MEM_COMMIT
            || (memory.Protect & PAGE_GUARD) != 0
            || (memory.Protect & PAGE_NOACCESS) != 0) {
            return false;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (regionStart > std::numeric_limits<std::uintptr_t>::max() - memory.RegionSize) {
            return false;
        }
        const auto regionEnd = regionStart + memory.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}

std::size_t ModuleImageSize(HMODULE module) noexcept {
    if (!module || !IsReadableRange(module, sizeof(IMAGE_DOS_HEADER))) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto ntAddress = NativeContract::AddSignedDisplacement(base, dos->e_lfanew);
    if (!ntAddress
        || !IsReadableRange(reinterpret_cast<const void*>(*ntAddress),
            sizeof(IMAGE_NT_HEADERS64))) {
        return 0;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(*ntAddress);
    if (nt->Signature != IMAGE_NT_SIGNATURE
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return 0;
    }
    return nt->OptionalHeader.SizeOfImage;
}

bool IsWithinImage(
    const void* address,
    std::size_t size,
    HMODULE module,
    std::size_t imageSize
) noexcept {
    if (!address || !module || size == 0 || imageSize < size) return false;
    const auto imageStart = reinterpret_cast<std::uintptr_t>(module);
    if (imageStart > std::numeric_limits<std::uintptr_t>::max() - imageSize) return false;
    const auto imageEnd = imageStart + imageSize;
    const auto rangeStart = reinterpret_cast<std::uintptr_t>(address);
    if (rangeStart < imageStart || rangeStart > imageEnd - size) return false;
    return IsReadableRange(address, size);
}

bool ReadInt32(const std::uint8_t* address, std::int32_t& value) noexcept {
    if (!IsReadableRange(address, sizeof(value))) return false;
    std::memcpy(&value, address, sizeof(value));
    return true;
}

bool ReadPointer(const void* address, std::uintptr_t& value) noexcept {
    if (!IsReadableRange(address, sizeof(value))) return false;
    std::memcpy(&value, address, sizeof(value));
    return true;
}

PacketRoute ValidateSendNineBytePacketRoute() noexcept {
    const auto mainModule = reinterpret_cast<HMODULE>(Base);
    const auto mainImageSize = ModuleImageSize(mainModule);
    auto* builder = Base + SendNineBytePacketRva;
    if (!IsWithinImage(
            builder,
            NativeContract::VanillaBuilder.size(),
            mainModule,
            mainImageSize)) {
        return PacketRoute::Invalid;
    }
    if (NativeContract::Matches(builder, NativeContract::VanillaBuilder)) {
        return PacketRoute::Vanilla;
    }
    if (!NativeContract::MatchesRelayBuilder(builder)) return PacketRoute::Invalid;

    std::int32_t builderDisplacement{};
    if (!ReadInt32(
            builder + NativeContract::BuilderCallDisplacementOffset,
            builderDisplacement)) {
        return PacketRoute::Invalid;
    }
    const auto relayAddress = NativeContract::ResolveRelativeTarget(
        reinterpret_cast<std::uintptr_t>(builder + NativeContract::BuilderCallOffset),
        5,
        builderDisplacement);
    if (!relayAddress) return PacketRoute::Invalid;
    const auto* relay = reinterpret_cast<const std::uint8_t*>(*relayAddress);
    if (!IsWithinImage(
            relay,
            6,
            mainModule,
            mainImageSize)
        || !NativeContract::Matches(relay, NativeContract::RelayStubOpcode)) {
        return PacketRoute::Invalid;
    }

    std::int32_t relayDisplacement{};
    if (!ReadInt32(relay + 2, relayDisplacement)) return PacketRoute::Invalid;
    const auto relaySlotAddress = NativeContract::ResolveRelativeTarget(
        reinterpret_cast<std::uintptr_t>(relay),
        6,
        relayDisplacement);
    if (!relaySlotAddress
        || !IsWithinImage(
            reinterpret_cast<const void*>(*relaySlotAddress),
            sizeof(std::uintptr_t),
            mainModule,
            mainImageSize)) {
        return PacketRoute::Invalid;
    }
    std::uintptr_t providerAddress{};
    if (!ReadPointer(reinterpret_cast<const void*>(*relaySlotAddress), providerAddress)) {
        return PacketRoute::Invalid;
    }

    const auto d2rCore = GetModuleHandleW(L"D2RCore.dll");
    const auto d2rCoreImageSize = ModuleImageSize(d2rCore);
    const auto* provider = reinterpret_cast<const std::uint8_t*>(providerAddress);
    if (!IsWithinImage(
            provider,
            NativeContract::ProviderForwardingOffset
                + NativeContract::D2RCoreForwardingWitness.size(),
            d2rCore,
            d2rCoreImageSize)
        || !NativeContract::Matches(provider, NativeContract::D2RCoreProviderEntry)
        || !NativeContract::Matches(
            provider + NativeContract::ProviderForwardingOffset,
            NativeContract::D2RCoreForwardingWitness)) {
        return PacketRoute::Invalid;
    }

    const auto* forwardingCall = provider
        + NativeContract::ProviderForwardingOffset
        + NativeContract::ProviderForwardingCallOffset;
    if (forwardingCall[0] != 0xFF || forwardingCall[1] != 0x15) {
        return PacketRoute::Invalid;
    }
    std::int32_t forwardingDisplacement{};
    if (!ReadInt32(forwardingCall + 2, forwardingDisplacement)) {
        return PacketRoute::Invalid;
    }
    const auto forwardingSlotAddress = NativeContract::ResolveRelativeTarget(
        reinterpret_cast<std::uintptr_t>(forwardingCall),
        6,
        forwardingDisplacement);
    if (!forwardingSlotAddress
        || !IsWithinImage(
            reinterpret_cast<const void*>(*forwardingSlotAddress),
            sizeof(std::uintptr_t),
            d2rCore,
            d2rCoreImageSize)) {
        return PacketRoute::Invalid;
    }
    std::uintptr_t downstreamAddress{};
    if (!ReadPointer(
            reinterpret_cast<const void*>(*forwardingSlotAddress),
            downstreamAddress)
        || downstreamAddress != reinterpret_cast<std::uintptr_t>(Base + DownstreamQueueRva)
        || !IsWithinImage(
            reinterpret_cast<const void*>(downstreamAddress),
            NativeContract::DownstreamQueueEntry.size(),
            mainModule,
            mainImageSize)
        || !NativeContract::Matches(
            reinterpret_cast<const std::uint8_t*>(downstreamAddress),
            NativeContract::DownstreamQueueEntry)) {
        return PacketRoute::Invalid;
    }
    return PacketRoute::VerifiedD2RCoreRelay;
}

bool ValidateRuntime() noexcept {
    ActivePacketRoute = PacketRoute::Invalid;
    const auto fixedSurfacesMatch = Context->CheckExpectedBytes(
            SendVendorRefreshRva,
            SendVendorRefreshExpected.data(),
            static_cast<std::uint32_t>(SendVendorRefreshExpected.size()))
        && Context->CheckExpectedBytes(
            EntityActionRva,
            EntityActionExpected.data(),
            static_cast<std::uint32_t>(EntityActionExpected.size()))
        && Context->CheckExpectedBytes(
            ConfigureVendorInteractionRva,
            ConfigureVendorInteractionExpected.data(),
            static_cast<std::uint32_t>(ConfigureVendorInteractionExpected.size()))
        && Context->CheckExpectedBytes(
            IsGamblingRva,
            IsGamblingExpected.data(),
            static_cast<std::uint32_t>(IsGamblingExpected.size()))
        && Context->CheckExpectedBytes(
            GetVendorChainEntryRva,
            GetVendorChainEntryExpected.data(),
            static_cast<std::uint32_t>(GetVendorChainEntryExpected.size()))
        && Context->CheckExpectedBytes(
            ConfigureVendorPanelRva,
            ConfigureVendorPanelExpected.data(),
            static_cast<std::uint32_t>(ConfigureVendorPanelExpected.size()))
        && Context->CheckExpectedBytes(
            FindWidgetRva,
            FindWidgetExpected.data(),
            static_cast<std::uint32_t>(FindWidgetExpected.size()))
        && Context->CheckExpectedBytes(
            GetWidgetRectRva,
            GetWidgetRectExpected.data(),
            static_cast<std::uint32_t>(GetWidgetRectExpected.size()));
    if (!fixedSurfacesMatch) return false;
    ActivePacketRoute = ValidateSendNineBytePacketRoute();
    return ActivePacketRoute != PacketRoute::Invalid;
}

bool SameRect(const WidgetRect& first, const WidgetRect& second) noexcept {
    return first.x == second.x
        && first.y == second.y
        && first.width == second.width
        && first.height == second.height;
}

void* FindNamedWidget(void* panel, const char* name) noexcept {
    if (!panel || !name) return nullptr;
    __try {
        return FindWidget(panel, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadWidgetRect(void* widget, WidgetRect& rect) noexcept {
    if (!widget) return false;
    __try {
        WidgetRect current{};
        if (GetWidgetRect(widget, &current) != &current) return false;
        rect = current;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteWidgetPosition(
    void* widget,
    std::int32_t x,
    std::int32_t y
) noexcept {
    if (!widget) return false;
    __try {
        auto* rect = reinterpret_cast<WidgetRect*>(
            static_cast<std::uint8_t*>(widget) + WidgetRectOffset
        );
        rect->x = x;
        rect->y = y;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ResolveGoldAnchor(void* panel, WidgetRect& anchor) noexcept {
    WidgetRect stash{};
    if (ReadWidgetRect(FindNamedWidget(panel, "StashWidget"), stash)
        && HasUsableSize(stash)) {
        anchor = stash;
        return true;
    }

    WidgetRect icon{};
    WidgetRect amount{};
    ReadWidgetRect(FindNamedWidget(panel, "gold_icon"), icon);
    ReadWidgetRect(FindNamedWidget(panel, "gold_amount"), amount);
    const auto combined = UnionRect(icon, amount);
    if (!HasUsableSize(combined)) return false;
    anchor = combined;
    return true;
}

void ReportPlacementFailure(const char* reason) noexcept {
    PlacementFailures.fetch_add(1, std::memory_order_relaxed);
    if (!Context || PlacementFailureReported.exchange(true, std::memory_order_relaxed)) return;
    const auto message = std::string("VendorStockRefresh: dynamic placement failed (")
        + reason + "); normal refresh stays hidden.";
    Context->LogError(message.c_str());
}

void SetWidgetState(void* widget, bool value) noexcept {
    if (!widget) return;
    __try {
        auto** vtable = *reinterpret_cast<void***>(widget);
        auto setEnabled = reinterpret_cast<SetWidgetBoolFn>(vtable[9]);
        auto setVisible = reinterpret_cast<SetWidgetBoolFn>(vtable[10]);
        setEnabled(widget, value);
        setVisible(widget, value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (Context) {
            Context->LogError("VendorStockRefresh: refresh widget state failed.");
        }
    }
}

void __fastcall HookConfigureVendorPanel(void* panel) noexcept {
    OriginalConfigureVendorPanel(panel);
    if (!panel) return;

    auto* refresh = FindNamedWidget(panel, "button_refresh");
    if (!refresh) {
        ReportPlacementFailure("button_refresh was not found");
        return;
    }

    WidgetRect current{};
    if (!ReadWidgetRect(refresh, current) || !HasUsableSize(current)) {
        ReportPlacementFailure("button_refresh has no usable rectangle");
        return;
    }

    if (PlacementCache.panel != panel
        || PlacementCache.widget != refresh
        || !PlacementCache.hasOriginal
        || (!PlacementCache.hasApplied
            && !SameRect(current, PlacementCache.original))
        || (PlacementCache.hasApplied
            && !SameRect(current, PlacementCache.applied))) {
        PlacementCache = {
            .panel = panel,
            .widget = refresh,
            .original = current,
            .hasOriginal = true,
        };
    }

    if (IsGambling() != 0) {
        if (PlacementCache.hasApplied) {
            if (!WriteWidgetPosition(
                    refresh,
                    PlacementCache.original.x,
                    PlacementCache.original.y
                )) {
                ReportPlacementFailure("gambling position could not be restored");
            }
            PlacementCache.hasApplied = false;
        }
        return;
    }

    WidgetRect anchor{};
    if (!ResolveGoldAnchor(panel, anchor)) {
        SetWidgetState(refresh, false);
        ReportPlacementFailure("gold anchor was not found");
        return;
    }
    const auto position = CenterBelow(anchor, current);
    if (!position.valid || !WriteWidgetPosition(refresh, position.x, position.y)) {
        SetWidgetState(refresh, false);
        ReportPlacementFailure("computed position was invalid");
        return;
    }

    PlacementCache.applied = current;
    PlacementCache.applied.x = position.x;
    PlacementCache.applied.y = position.y;
    PlacementCache.hasApplied = true;
    DynamicPlacements.fetch_add(1, std::memory_order_relaxed);
    SetWidgetState(refresh, true);

    if (Context && Settings.diagnosticsEnabled
        && !PlacementSuccessReported.exchange(true, std::memory_order_relaxed)) {
        char message[220]{};
        std::snprintf(
            message,
            sizeof(message),
            "VendorStockRefresh: dynamic button placed at %d,%d from gold anchor %d,%d,%d,%d.",
            position.x,
            position.y,
            anchor.x,
            anchor.y,
            anchor.width,
            anchor.height
        );
        Context->LogInfo(message);
    }
}

void __fastcall HookSendVendorRefresh() noexcept {
    const auto isGambling = IsGambling() != 0;
    const auto action = RefreshActionForPanel(isGambling);
    const auto npcGuid = *At<const std::uint32_t*>(CurrentNpcGuidRva);
    if (!isGambling) {
        const auto sent = NormalRequestsSent.fetch_add(1, std::memory_order_relaxed) + 1;
        if (Context && TakeDiagnosticLogSlot()) {
            char message[128]{};
            std::snprintf(
                message,
                sizeof(message),
                "VendorStockRefresh: client normal refresh sent (sent=%llu).",
                static_cast<unsigned long long>(sent)
            );
            Context->LogInfo(message);
        }
    }
    SendNineBytePacket(0x38, action, npcGuid);
}

std::int32_t __fastcall HookEntityAction(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t packetSize
) noexcept {
    if (!packet || packetSize != EntityActionPacketSize) {
        return OriginalEntityAction(game, player, packet, packetSize);
    }

    std::uint32_t action{};
    std::memcpy(&action, packet + EntityActionOffset, sizeof(action));
    if (action != NormalRefreshAction) {
        return OriginalEntityAction(game, player, packet, packetSize);
    }

    NormalRequestsReceived.fetch_add(1, std::memory_order_relaxed);
    std::array<std::uint8_t, EntityActionPacketSize> vanillaPacket{};
    std::memcpy(vanillaPacket.data(), packet, vanillaPacket.size());
    std::memcpy(
        vanillaPacket.data() + EntityActionOffset,
        &VanillaNormalVendorAction,
        sizeof(VanillaNormalVendorAction)
    );

    const auto previousScope = ActiveRefresh;
    ActiveRefresh = {.active = true, .armed = false, .player = player};
    const auto result = OriginalEntityAction(
        game,
        player,
        vanillaPacket.data(),
        static_cast<std::int32_t>(vanillaPacket.size())
    );
    const auto armed = ActiveRefresh.armed;
    ActiveRefresh = previousScope;

    if (!armed) {
        RejectedNormalRequests.fetch_add(1, std::memory_order_relaxed);
    }
    if (Context && TakeDiagnosticLogSlot()) {
        char message[220]{};
        std::snprintf(
            message,
            sizeof(message),
            "VendorStockRefresh: normal refresh %s (received=%llu, armed=%llu, rejected=%llu).",
            armed ? "armed" : "rejected",
            static_cast<unsigned long long>(NormalRequestsReceived.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(NormalRefreshesArmed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(RejectedNormalRequests.load(std::memory_order_relaxed))
        );
        Context->LogInfo(message);
    }
    return result;
}

void __fastcall HookConfigureVendorInteraction(
    void* game,
    void* npc,
    void* player,
    std::uint8_t requestedMode
) noexcept {
    if (ActiveRefresh.active
        && ActiveRefresh.player == player
        && requestedMode == NormalVendorMode) {
        __try {
            std::int32_t vendorIndex{-1};
            auto* vendorEntry = static_cast<std::uint8_t*>(
                GetVendorChainEntry(game, npc, &vendorIndex)
            );
            const auto inventoryFilled = vendorEntry
                && vendorEntry[VendorEntryFilledOffset] != 0;

            if (ShouldArmNormalRefresh(
                    true,
                    requestedMode,
                    vendorEntry != nullptr && vendorIndex >= 0,
                    inventoryFilled
                )) {
                vendorEntry[VendorEntryRefreshPendingOffset] = 1;
                if (!ActiveRefresh.armed) {
                    NormalRefreshesArmed.fetch_add(1, std::memory_order_relaxed);
                    ActiveRefresh.armed = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ActiveRefresh.armed = false;
        }
    }

    OriginalConfigureVendorInteraction(game, npc, player, requestedMode);
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[420]{};
    std::snprintf(
        message,
        sizeof(message),
        "Vendor Stock Refresh 2.0.0: %s; diagnostics=%s; placed=%llu; "
        "placementFailures=%llu; sent=%llu; received=%llu; armed=%llu; rejected=%llu.",
        Settings.enabled ? "active" : "disabled",
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        static_cast<unsigned long long>(DynamicPlacements.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PlacementFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NormalRequestsSent.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NormalRequestsReceived.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NormalRefreshesArmed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RejectedNormalRequests.load(std::memory_order_relaxed))
    );
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}
} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    if (!D2RL::HasContext(context)
        || context->apiVersion < D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = nullptr;
    Settings = {};
    NormalRequestsSent.store(0, std::memory_order_relaxed);
    NormalRequestsReceived.store(0, std::memory_order_relaxed);
    NormalRefreshesArmed.store(0, std::memory_order_relaxed);
    RejectedNormalRequests.store(0, std::memory_order_relaxed);
    DynamicPlacements.store(0, std::memory_order_relaxed);
    PlacementFailures.store(0, std::memory_order_relaxed);
    PlacementFailureReported.store(false, std::memory_order_relaxed);
    PlacementSuccessReported.store(false, std::memory_order_relaxed);
    DiagnosticLogs.store(0, std::memory_order_relaxed);
    PlacementCache = {};
    ActivePacketRoute = PacketRoute::Invalid;

    if (!ReadConfiguration()) return false;
    if (!Settings.enabled) {
        context->LogInfo(
            "VendorStockRefresh 2.0.0 by RuffnecKk loaded disabled; no hook or service registered.");
        return true;
    }

    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

    if (!Base) {
        context->LogError("VendorStockRefresh: D2R executable base is unavailable.");
        return false;
    }
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    char buildMessage[192]{};
    std::snprintf(buildMessage, sizeof(buildMessage),
        "VendorStockRefresh: observed D2R build-name=%s; validating the complete native fingerprint.",
        runtimeBuild && runtimeBuild[0] != '\0' ? runtimeBuild : "unknown");
    context->LogInfo(buildMessage);

    if (!ValidateRuntime()) {
        context->LogError(
            "VendorStockRefresh: native hook or helper fingerprint mismatch; plugin refused.");
        return false;
    }
    context->LogInfo(ActivePacketRoute == PacketRoute::Vanilla
        ? "VendorStockRefresh: verified the vanilla nine-byte packet route."
        : "VendorStockRefresh: verified the D2RCore packet relay and downstream queue ABI.");

    IsGambling = At<IsGamblingFn>(IsGamblingRva);
    SendNineBytePacket = At<SendNineBytePacketFn>(SendNineBytePacketRva);
    GetVendorChainEntry = At<GetVendorChainEntryFn>(GetVendorChainEntryRva);
    FindWidget = At<FindWidgetFn>(FindWidgetRva);
    GetWidgetRect = At<GetWidgetRectFn>(GetWidgetRectRva);

    if (!context->InstallInlineHook(
                ConfigureVendorPanelRva,
                ConfigureVendorPanelExpected.data(),
                static_cast<std::uint32_t>(ConfigureVendorPanelExpected.size()),
                HookConfigureVendorPanel,
                &OriginalConfigureVendorPanel
            )) {
            context->LogError(
                "VendorStockRefresh: vendor-panel configuration hook failed.");
        return false;
    }
    if (!context->InstallInlineHook(
                ConfigureVendorInteractionRva,
                ConfigureVendorInteractionExpected.data(),
                static_cast<std::uint32_t>(ConfigureVendorInteractionExpected.size()),
                HookConfigureVendorInteraction,
                &OriginalConfigureVendorInteraction
            )) {
            context->LogError(
                "VendorStockRefresh: server vendor-session hook failed.");
        return false;
    }
    if (!context->InstallInlineHook(
                EntityActionRva,
                EntityActionExpected.data(),
                static_cast<std::uint32_t>(EntityActionExpected.size()),
                HookEntityAction,
                &OriginalEntityAction
            )) {
            context->LogError(
                "VendorStockRefresh: server entity-action hook failed.");
        return false;
    }
    if (!context->InstallInlineHook(
                SendVendorRefreshRva,
                SendVendorRefreshExpected.data(),
                static_cast<std::uint32_t>(SendVendorRefreshExpected.size()),
                HookSendVendorRefresh,
                &OriginalSendVendorRefresh
            )) {
            context->LogError(
                "VendorStockRefresh: client refresh-sender hook failed.");
        return false;
    }

    if (!context->RegisterConsoleCommand(
            "vendor-stock-refresh",
            Status,
            "Show vendor stock refresh status and counters."
        )) {
        context->LogWarn(
            "VendorStockRefresh: status command could not be registered.");
    }

    context->LogInfo(
        "VendorStockRefresh 2.0.0 by RuffnecKk active; native button uses the runtime gold anchor.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    PlacementCache = {};
    ActiveRefresh = {};
    OriginalConfigureVendorPanel = nullptr;
    GetWidgetRect = nullptr;
    FindWidget = nullptr;
    GetVendorChainEntry = nullptr;
    SendNineBytePacket = nullptr;
    IsGambling = nullptr;
    OriginalEntityAction = nullptr;
    OriginalConfigureVendorInteraction = nullptr;
    OriginalSendVendorRefresh = nullptr;
    DiagnosticLogs.store(0, std::memory_order_relaxed);
    Settings = {};
    ActivePacketRoute = PacketRoute::Invalid;
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::VendorStockRefresh
