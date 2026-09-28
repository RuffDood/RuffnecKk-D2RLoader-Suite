#include <D2RLPlugin/api.h>

#include "native_contract.hpp"
#include "compact_layout.hpp"
#include "ui_scale_contract.hpp"
#include "companion_layout.hpp"
#include "companion_native.hpp"
#include "companion_hash.hpp"
#include <D2RLPlugin/resources.h>

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
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
constexpr std::size_t WidgetScaleOffset = 0x80;

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
struct NativeStringView { const char* data; std::size_t length; };
using SetFilenameFn = void(__fastcall*)(void*, const NativeStringView*);
using ImageReadyFn = bool(__fastcall*)(void*);

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
SetFilenameFn SetImageFilename{};
ImageReadyFn IsImageReady{};
std::atomic<std::uint64_t> NormalRequestsSent{};
std::atomic<std::uint64_t> NormalRequestsReceived{};
std::atomic<std::uint64_t> NormalRefreshesArmed{};
std::atomic<std::uint64_t> RejectedNormalRequests{};
std::atomic<std::uint64_t> DynamicPlacements{};
std::atomic<std::uint64_t> PlacementFailures{};
std::atomic_bool PlacementFailureReported{};
std::atomic_bool PlacementSuccessReported{};
std::atomic<std::uint64_t> DiagnosticLogs{};

#if defined(VENDOR_LAYOUT_DIAGNOSTIC)
std::atomic<unsigned> LayoutDiagnosticEvents{};
thread_local bool LayoutDiagnosticActive{};
void LayoutDiagnostic(const char* format, ...) noexcept {
    if (!Context || !LayoutDiagnosticActive) return;
    char message[1024]{};
    const int prefix = std::snprintf(message, sizeof(message), "[VSR-GOLD-216-2] ");
    va_list args;
    va_start(args, format);
    std::vsnprintf(message + prefix, sizeof(message) - static_cast<std::size_t>(prefix), format, args);
    va_end(args);
    Context->LogInfo(message);
}
#else
void LayoutDiagnostic(const char*, ...) noexcept {}
#endif

enum class PacketRoute {
    Invalid,
    Vanilla,
    VerifiedD2RCoreRelay,
};

PacketRoute ActivePacketRoute{PacketRoute::Invalid};

struct GoldPlacement {
    void* widget{};
    RefreshLayoutState layout{};
};

struct RefreshPlacementCache {
    void* panel{};
    void* widget{};
    RefreshLayoutState layout{};
    std::array<GoldPlacement, 3> gold{};
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
    .version = "2.1.6",
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

bool ComputeSha256(
    const std::uint8_t* data,
    std::size_t size,
    NativeContract::Sha256Digest& output
) noexcept {
    if (!data || size == 0 || size > (std::numeric_limits<ULONG>::max)()) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::array<std::uint8_t, 512> hashObject{};
    ULONG objectSize{};
    ULONG returned{};
    bool success{};

    if (BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0) < 0) {
        return false;
    }
    if (BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize),
            &returned,
            0) >= 0
        && returned == sizeof(objectSize)
        && objectSize <= hashObject.size()
        && BCryptCreateHash(
            algorithm,
            &hash,
            hashObject.data(),
            objectSize,
            nullptr,
            0,
            0) >= 0
        && BCryptHashData(
            hash,
            const_cast<PUCHAR>(data),
            static_cast<ULONG>(size),
            0) >= 0
        && BCryptFinishHash(
            hash,
            output.data(),
            static_cast<ULONG>(output.size()),
            0) >= 0) {
        success = true;
    }
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}

// The published DLL and MPQ are one pair. Reject an absent or stale companion
// before touching the game, rather than leaving an empty native background.
bool ValidateCompanion() noexcept {
    const D2RL::ResourceServiceV1* resources{};
    if (Context->QueryService(D2RL::ServiceId::Resource, D2RL::ResourceServiceV1Version,
            &resources) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasResourceServiceV1Field(resources, D2RL::ResourceServiceV1RequiredSize))
        return false;
    const auto* pluginPath = D2RL::GetPluginPath(Context);
    if (!pluginPath || !*pluginPath) return false;
    std::wstring path(pluginPath);
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    path.replace(dot, std::wstring::npos, L".mpq");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool valid = GetFileSizeEx(file, &size) && size.QuadPart > 32
        && size.QuadPart <= 32 * 1024 * 1024;
    HANDLE mapping = valid ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    const auto* bytes = mapping ? static_cast<const std::uint8_t*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    NativeContract::Sha256Digest digest{};
    valid = bytes && ComputeSha256(bytes, static_cast<std::size_t>(size.QuadPart), digest)
        && digest == CompanionSha256;
    if (bytes) UnmapViewOfFile(bytes);
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return valid;
}

bool ValidateD2RCoreProviderAbi(
    HMODULE d2rCore,
    std::size_t d2rCoreImageSize,
    const std::uint8_t* provider,
    NativeContract::D2RCoreProviderProfile profile
) noexcept {
    using namespace NativeContract;

    std::uintptr_t providerRva{};
    std::size_t providerSize{};
    std::uint32_t providerUnwindRva{};
    std::uintptr_t providerFuncInfoRva{};
    const Sha256Digest* expectedHash{};
    const std::array<std::uint8_t, 32>* expectedUnwind{};
    const std::array<std::uint8_t, 40>* expectedFuncInfo{};

    switch (profile) {
    case D2RCoreProviderProfile::D2RLoader12:
        providerRva = D2RCoreProviderRva12;
        providerSize = D2RCoreProviderSize12;
        providerUnwindRva = D2RCoreProviderUnwindRva12;
        providerFuncInfoRva = D2RCoreProviderFuncInfoRva12;
        expectedHash = &D2RCoreProviderHash12;
        expectedUnwind = &D2RCoreProviderUnwind12;
        expectedFuncInfo = &D2RCoreProviderFuncInfo12;
        break;
    case D2RCoreProviderProfile::D2RLoader121:
        providerRva = D2RCoreProviderRva121;
        providerSize = D2RCoreProviderSize121;
        providerUnwindRva = D2RCoreProviderUnwindRva121;
        providerFuncInfoRva = D2RCoreProviderFuncInfoRva121;
        expectedHash = &D2RCoreProviderHash121;
        expectedUnwind = &D2RCoreProviderUnwind121;
        expectedFuncInfo = &D2RCoreProviderFuncInfo121;
        break;
    case D2RCoreProviderProfile::D2RLoader121Release:
        providerRva = D2RCoreProviderRva121Release;
        providerSize = D2RCoreProviderSize121Release;
        providerUnwindRva = D2RCoreProviderUnwindRva121Release;
        providerFuncInfoRva = D2RCoreProviderFuncInfoRva121Release;
        expectedHash = &D2RCoreProviderHash121Release;
        expectedUnwind = &D2RCoreProviderUnwind121Release;
        expectedFuncInfo = &D2RCoreProviderFuncInfo121Release;
        break;
    case D2RCoreProviderProfile::PublicPacketProvider:
        providerRva = D2RCoreProviderRvaPublicPacket;
        providerSize = D2RCoreProviderSizePublicPacket;
        providerUnwindRva = D2RCoreProviderUnwindRvaPublicPacket;
        providerFuncInfoRva = D2RCoreProviderFuncInfoRvaPublicPacket;
        expectedHash = &D2RCoreProviderHashPublicPacket;
        expectedUnwind = &D2RCoreProviderUnwindPublicPacket;
        expectedFuncInfo = &D2RCoreProviderFuncInfoPublicPacket;
        break;
    case D2RCoreProviderProfile::EligibilityCheckedPacketProvider:
        providerRva = D2RCoreProviderRvaEligibilityCheckedPacket;
        providerSize = D2RCoreProviderSizeEligibilityCheckedPacket;
        providerUnwindRva =
            D2RCoreProviderUnwindRvaEligibilityCheckedPacket;
        providerFuncInfoRva =
            D2RCoreProviderFuncInfoRvaEligibilityCheckedPacket;
        expectedHash = &D2RCoreProviderHashEligibilityCheckedPacket;
        expectedUnwind =
            &D2RCoreProviderUnwindEligibilityCheckedPacket;
        expectedFuncInfo =
            &D2RCoreProviderFuncInfoEligibilityCheckedPacket;
        break;
    case D2RCoreProviderProfile::Loader131PacketProvider:
        providerRva = D2RCoreProviderRvaLoader131;
        providerSize = D2RCoreProviderSizeLoader131;
        providerUnwindRva = D2RCoreProviderUnwindRvaLoader131;
        providerFuncInfoRva = D2RCoreProviderFuncInfoRvaLoader131;
        expectedHash = &D2RCoreProviderHashLoader131;
        expectedUnwind = &D2RCoreProviderUnwindLoader131;
        expectedFuncInfo = &D2RCoreProviderFuncInfoLoader131;
        break;
    default:
        return false;
    }

    const auto coreBase = reinterpret_cast<std::uintptr_t>(d2rCore);
    if (!coreBase
        || coreBase > (std::numeric_limits<std::uintptr_t>::max)() - providerRva
        || coreBase > (std::numeric_limits<std::uintptr_t>::max)()
            - providerUnwindRva
        || coreBase > (std::numeric_limits<std::uintptr_t>::max)()
            - providerFuncInfoRva
        || reinterpret_cast<std::uintptr_t>(provider) != coreBase + providerRva
        || !IsWithinImage(provider, providerSize, d2rCore, d2rCoreImageSize)
        || providerRva > (std::numeric_limits<DWORD>::max)() - providerSize) {
        return false;
    }

    NativeContract::Sha256Digest liveHash{};
    if (!ComputeSha256(provider, providerSize, liveHash)
        || liveHash != *expectedHash) {
        return false;
    }

    DWORD64 functionImageBase{};
    const auto* liveFunction = RtlLookupFunctionEntry(
        static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(provider)),
        &functionImageBase,
        nullptr);
    RUNTIME_FUNCTION function{};
    if (!liveFunction
        || functionImageBase != static_cast<DWORD64>(coreBase)
        || !IsReadableRange(liveFunction, sizeof(function))) {
        return false;
    }
    std::memcpy(&function, liveFunction, sizeof(function));
    if (function.BeginAddress != providerRva
        || function.EndAddress != providerRva + providerSize
        || function.UnwindData != providerUnwindRva
        || !IsWithinImage(
            reinterpret_cast<const void*>(coreBase + providerUnwindRva),
            expectedUnwind->size(),
            d2rCore,
            d2rCoreImageSize)
        || !NativeContract::Matches(
            reinterpret_cast<const std::uint8_t*>(
                coreBase + providerUnwindRva),
            *expectedUnwind)
        || !IsWithinImage(
            reinterpret_cast<const void*>(coreBase + providerFuncInfoRva),
            expectedFuncInfo->size(),
            d2rCore,
            d2rCoreImageSize)
        || !NativeContract::Matches(
            reinterpret_cast<const std::uint8_t*>(
                coreBase + providerFuncInfoRva),
            *expectedFuncInfo)) {
        return false;
    }
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
                + NativeContract::D2RCoreForwardingWitness12.size(),
            d2rCore,
            d2rCoreImageSize)) {
        return PacketRoute::Invalid;
    }
    const auto providerProfile = NativeContract::IdentifyD2RCoreProviderProfile(
        provider,
        provider + NativeContract::ProviderForwardingOffset);
    if (providerProfile == NativeContract::D2RCoreProviderProfile::Invalid
        || !ValidateD2RCoreProviderAbi(
            d2rCore,
            d2rCoreImageSize,
            provider,
            providerProfile)) {
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
    const auto companionSurfacesMatch = CompanionNative::Validate(
        [](std::uintptr_t rva, const std::uint8_t* bytes, std::uint32_t size) {
            return Context->CheckExpectedBytes(rva, bytes, size);
        });
    const auto fixedSurfacesMatch = companionSurfacesMatch && Context->CheckExpectedBytes(
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
    if (!fixedSurfacesMatch || !UiScaleContract::Matches(
            [](std::uintptr_t rva, const std::uint8_t* bytes, std::uint32_t size) {
                return Context->CheckExpectedBytes(rva, bytes, size);
            })) return false;
    ActivePacketRoute = ValidateSendNineBytePacketRoute();
    return ActivePacketRoute != PacketRoute::Invalid;
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

bool ReadWidgetGeometry(void* widget, WidgetGeometry& geometry) noexcept {
    if (!ReadWidgetRect(widget, geometry.rect)) return false;
    __try {
        const auto* bytes = static_cast<const std::uint8_t*>(widget);
        // A fill-parent control does not own the local dimensions used here.
        if (bytes[0x52] != 0) return false;
        geometry.scale = *reinterpret_cast<const float*>(bytes + WidgetScaleOffset);
        return UsableScale(geometry.scale);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteWidgetGeometry(void* widget, const WidgetGeometry& geometry) noexcept {
    if (!widget || !UsableScale(geometry.scale)) return false;
    __try {
        auto* bytes = static_cast<std::uint8_t*>(widget);
        auto* rect = reinterpret_cast<WidgetRect*>(bytes + WidgetRectOffset);
        // Keep the original sprite dimensions. Native rendering and hit testing
        // both multiply them by widget+0x80, including the button's child sprite.
        rect->x = geometry.rect.x;
        rect->y = geometry.rect.y;
        *reinterpret_cast<float*>(bytes + WidgetScaleOffset) = geometry.scale;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ImageWidget owns a native string at +0x108 (pointer, length), copied by
// UI_ImageWidget_SetFilename. Reads and calls are covered by companion_native.hpp.
bool ReadImagePath(void* widget, std::array<char, 512>& path) noexcept {
    if (!widget) return false;
    __try {
        const auto* image = static_cast<const std::uint8_t*>(widget);
        const auto* text = *reinterpret_cast<const char* const*>(image + 0x108);
        const auto length = *reinterpret_cast<const std::size_t*>(image + 0x110);
        if (!text || length == 0 || length >= path.size()) return false;
        std::memcpy(path.data(), text, length);
        path[length] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SetImagePath(void* widget, const char* path) noexcept {
    __try {
        const NativeStringView view{path, std::strlen(path)};
        SetImageFilename(widget, &view);
        const bool ready = IsImageReady(static_cast<std::uint8_t*>(widget) + 0x130);
        LayoutDiagnostic("image-set path=%s ready=%d", path, ready);
        return ready;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LayoutDiagnostic("image-set fault path=%s", path);
        return false;
    }
}

bool RestoreCompanionBackgrounds(void* panel) noexcept {
    constexpr std::array names{"background", "background_repair"};
    bool restored = true;
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto* widget = FindNamedWidget(panel, names[i]);
        std::array<char, 512> path{};
        // Restore only our paths, never another mod's controller/custom artwork.
        if (ReadImagePath(widget, path) && SameImagePath(path.data(), CompanionBackgrounds[i]))
            restored = SetImagePath(widget, NativeBackgrounds[i]) && restored;
    }
    return restored;
}

bool ApplyCompanionBackgrounds(void* panel) noexcept {
    constexpr std::array names{"background", "background_repair"};
    std::array<void*, 2> widgets{};
    std::array<bool, 2> alreadyApplied{};
    // Validate both widgets before changing either one. Controller and SD layouts
    // fail this exact desktop geometry/path check and retain the compact fallback.
    for (std::size_t i = 0; i < names.size(); ++i) {
        widgets[i] = FindNamedWidget(panel, names[i]);
        WidgetGeometry geometry{};
        std::array<char, 512> path{};
        if (!ReadWidgetGeometry(widgets[i], geometry)) {
            LayoutDiagnostic("companion rejected: %s geometry unreadable", names[i]); return false;
        }
        if (!IsDesktopBackground(geometry)) {
            LayoutDiagnostic("companion rejected: %s desktop geometry mismatch rect=%d,%d,%d,%d scale=%.6f expected=0,0,1162,1507 scale=1",
                names[i], geometry.rect.x, geometry.rect.y, geometry.rect.width, geometry.rect.height, static_cast<double>(geometry.scale));
            return false;
        }
        if (!ReadImagePath(widgets[i], path)) {
            LayoutDiagnostic("companion rejected: %s filename unreadable", names[i]); return false;
        }
        alreadyApplied[i] = SameImagePath(path.data(), CompanionBackgrounds[i]);
        if (!alreadyApplied[i] && !SameImagePath(path.data(), NativeBackgrounds[i])) {
            LayoutDiagnostic("companion rejected: %s unexpected filename=%s", names[i], path.data()); return false;
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (alreadyApplied[i]) continue;
        if (!SetImagePath(widgets[i], CompanionBackgrounds[i])) {
            LayoutDiagnostic("companion rejected: %s image failed to load", names[i]); return false;
        }
        WidgetGeometry geometry{};
        if (!ReadWidgetGeometry(widgets[i], geometry) || !IsDesktopBackground(geometry)) {
            LayoutDiagnostic("companion rejected: %s post-load geometry mismatch rect=%d,%d,%d,%d scale=%.6f",
                names[i], geometry.rect.x, geometry.rect.y, geometry.rect.width, geometry.rect.height, static_cast<double>(geometry.scale));
            return false;
        }
    }
    return true;
}

// Move the existing native gold widgets rather than drawing a duplicate amount.
// Capture all three before the first write, and retain each original for gambling.
bool SetGoldLayout(void* panel, const WidgetRect* target) noexcept {
    constexpr std::array names{"StashWidget", "gold_icon", "gold_amount"};
    std::array<WidgetGeometry, 3> planned{};
    std::array<bool, 3> write{};
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto& cached = PlacementCache.gold[i];
        if (!target && !cached.layout.hasApplied) continue;
        auto* widget = FindNamedWidget(panel, names[i]);
        WidgetGeometry current{};
        if (!widget || !ReadWidgetGeometry(widget, current)) return false;
        if (cached.widget != widget) cached = {.widget = widget};
        cached.layout.Observe(current);
        planned[i] = cached.layout.original;
        write[i] = target || cached.layout.hasApplied;
    }
    if (target) {
        const auto& anchor = PlacementCache.gold[0].layout.original.rect;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto moved = TranslateGoldWidget(planned[i], anchor, *target);
            if (!moved.valid) return false;
            planned[i] = moved.geometry;
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!write[i]) continue;
        auto& cached = PlacementCache.gold[i];
        if (!WriteWidgetGeometry(cached.widget, planned[i])) return false;
        if (target) cached.layout.Applied(planned[i]);
        else cached.layout.Restored();
    }
    return true;
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

WidgetGeometry DesiredRefreshSize(void* panel, const WidgetGeometry& original) noexcept {
    auto desired = original;
    WidgetGeometry repair{};
    if (ReadWidgetGeometry(FindNamedWidget(panel, "button_repair"), repair)
        && HasUsableSize(repair.rect) && HasUsableSize(original.rect)) {
        const double sx = repair.rect.width * static_cast<double>(repair.scale) / original.rect.width;
        const double sy = repair.rect.height * static_cast<double>(repair.scale) / original.rect.height;
        const auto scale = static_cast<float>(sx < sy ? sx : sy);
        if (UsableScale(scale)) desired.scale = scale;
    }
    return desired;
}

bool ResolveFreeButtonArea(void* panel, const WidgetRect& gold, WidgetRect& area) noexcept {
    WidgetGeometry background{};
    if (!ReadWidgetGeometry(FindNamedWidget(panel, "background"), background)
        || !HasUsableSize(background.rect) || !HasUsableSize(gold)) return false;
    // Coordinates are local to the same vendor parent. Leave an eight-unit gap
    // under gold and a sixteen-unit inset at the outer panel edges.
    const double panelRight = background.rect.x + background.rect.width * static_cast<double>(background.scale) - 16;
    const double panelBottom = background.rect.y + background.rect.height * static_cast<double>(background.scale) - 16;
    const double left = (std::max)(static_cast<double>(gold.x), static_cast<double>(background.rect.x) + 16);
    const double right = (std::min)(static_cast<double>(gold.x) + gold.width, panelRight);
    const double top = (std::max)(static_cast<double>(gold.y) + gold.height + 8, static_cast<double>(background.rect.y) + 16);
    constexpr double low = (std::numeric_limits<std::int32_t>::min)();
    constexpr double high = (std::numeric_limits<std::int32_t>::max)();
    if (left < low || top < low || right > high || panelBottom > high
        || right - left < 8 || panelBottom - top < 8
        || right - left > high || panelBottom - top > high) return false;
    area = {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
        static_cast<std::int32_t>(right - left), static_cast<std::int32_t>(panelBottom - top)};
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

#if defined(VENDOR_LAYOUT_DIAGNOSTIC)
void SnapshotLayout(void* panel, const char* phase) noexcept {
    constexpr std::array names{"background", "background_repair", "button_refresh", "button_repair",
        "button_repair_all", "StashWidget", "gold_icon", "gold_amount", "vendor_refresh_frame",
        "vendor_refresh_slot", "vendor_refresh_gold_anchor"};
    for (const auto* name : names) {
        auto* widget = FindNamedWidget(panel, name);
        WidgetRect rect{};
        WidgetGeometry geometry{};
        const bool rectOk = ReadWidgetRect(widget, rect);
        const bool geometryOk = ReadWidgetGeometry(widget, geometry);
        LayoutDiagnostic("%s widget=%s present=%d rect-ok=%d rect=%d,%d,%d,%d geometry-ok=%d scale=%.6f footprint=%.3fx%.3f",
            phase, name, widget != nullptr, rectOk, rect.x, rect.y, rect.width, rect.height, geometryOk,
            static_cast<double>(geometry.scale), rect.width * static_cast<double>(geometry.scale), rect.height * static_cast<double>(geometry.scale));
        if (std::strcmp(name, "background") == 0 || std::strcmp(name, "background_repair") == 0) {
            std::array<char, 512> path{};
            const bool pathOk = ReadImagePath(widget, path);
            LayoutDiagnostic("%s widget=%s filename-ok=%d filename=%s", phase, name, pathOk, pathOk ? path.data() : "<unavailable>");
        }
    }
}
struct LayoutDiagnosticScope {
    void* panel;
    bool active{};
    unsigned sequence{};
    explicit LayoutDiagnosticScope(void* value) noexcept : panel(value) {
        if (LayoutDiagnosticActive) return;
        sequence = LayoutDiagnosticEvents.fetch_add(1, std::memory_order_relaxed);
        if (sequence >= 12) return;
        active = true; LayoutDiagnosticActive = true;
        LayoutDiagnostic("configure=%u begin after native configuration", sequence + 1);
        SnapshotLayout(panel, "before");
    }
    ~LayoutDiagnosticScope() noexcept {
        if (!active) return;
        SnapshotLayout(panel, "after");
        LayoutDiagnostic("configure=%u end%s", sequence + 1, sequence == 11 ? "; capture limit reached; restart for more" : "");
        LayoutDiagnosticActive = false;
    }
};
#endif

void __fastcall HookConfigureVendorPanel(void* panel) noexcept {
    OriginalConfigureVendorPanel(panel);
    if (!panel) return;
#if defined(VENDOR_LAYOUT_DIAGNOSTIC)
    LayoutDiagnosticScope diagnosticScope(panel);
#endif

    auto* frame = FindNamedWidget(panel, "vendor_refresh_frame");
    SetWidgetState(frame, false);
    auto* refresh = FindNamedWidget(panel, "button_refresh");
    if (!refresh) {
        ReportPlacementFailure("button_refresh was not found");
        return;
    }

    WidgetGeometry current{};
    if (!ReadWidgetGeometry(refresh, current) || !HasUsableSize(current.rect)) {
        SetWidgetState(refresh, false);
        ReportPlacementFailure("button_refresh has no usable geometry");
        return;
    }

    if (PlacementCache.panel != panel || PlacementCache.widget != refresh) {
        PlacementCache = {.panel = panel, .widget = refresh};
    }
    auto& layout = PlacementCache.layout;
    layout.Observe(current);
    LayoutDiagnostic("baseline refresh rect=%d,%d,%d,%d scale=%.6f cached-applied=%d",
        layout.original.rect.x, layout.original.rect.y, layout.original.rect.width, layout.original.rect.height,
        static_cast<double>(layout.original.scale), layout.hasApplied);

    if (IsGambling() != 0) {
        LayoutDiagnostic("route=gambling; restore native background and cached positions");
        if (!RestoreCompanionBackgrounds(panel)) {
            SetWidgetState(refresh, false);
            ReportPlacementFailure("gambling background could not be restored");
            return;
        }
        if (!SetGoldLayout(panel, nullptr)) {
            SetWidgetState(refresh, false);
            ReportPlacementFailure("gambling gold position could not be restored");
            return;
        }
        if (layout.hasApplied) {
            if (!WriteWidgetGeometry(refresh, layout.original)) {
                SetWidgetState(refresh, false);
                ReportPlacementFailure("gambling geometry could not be restored");
                return;
            }
            layout.Restored();
        }
        return;
    }

    const auto desired = DesiredRefreshSize(panel, layout.original);
    WidgetRect slot{};
    WidgetRect goldTarget{};
    CompactPlacement position{};
    const bool legacyPanelLayout = frame
        && ReadWidgetRect(FindNamedWidget(panel, "vendor_refresh_slot"), slot)
        && ReadWidgetRect(FindNamedWidget(panel, "vendor_refresh_gold_anchor"), goldTarget)
        && HasUsableSize(goldTarget)
        && (position = CenterInPanelSlot(slot, desired)).valid;
    // The original revision-3 loose frame used the same high gold anchor.
    // Correct only that known anchor; arbitrary mod-provided anchors stay intact.
    if (legacyPanelLayout && goldTarget.x == 421 && goldTarget.y == 1260
        && goldTarget.width == 313 && goldTarget.height == 58) goldTarget.y += 12;
    const auto companionPosition = CenterInPanelSlot(CompanionSlot, desired);
    const bool companionLayout = !legacyPanelLayout && companionPosition.valid
        && ApplyCompanionBackgrounds(panel);
    if (!companionLayout && !RestoreCompanionBackgrounds(panel)) {
        SetWidgetState(refresh, false);
        ReportPlacementFailure("native background could not be restored");
        return;
    }
    if (companionLayout) {
        position = companionPosition;
        goldTarget = CompanionGold;
    }
    const bool panelLayout = legacyPanelLayout || companionLayout;
    LayoutDiagnostic("route=%s legacy-valid=%d companion-slot-fits=%d companion-applied=%d legacy-slot=%d,%d,%d,%d gold-target=%d,%d,%d,%d",
        legacyPanelLayout ? "legacy-frame" : companionLayout ? "companion-frame" : "measured-fallback",
        legacyPanelLayout, companionPosition.valid, companionLayout,
        slot.x, slot.y, slot.width, slot.height, goldTarget.x, goldTarget.y, goldTarget.width, goldTarget.height);
    if (!SetGoldLayout(panel, panelLayout ? &goldTarget : nullptr)) {
        SetGoldLayout(panel, nullptr);
        RestoreCompanionBackgrounds(panel);
        SetWidgetState(refresh, false);
        ReportPlacementFailure("gold widgets could not follow panel layout");
        return;
    }
    WidgetRect anchor{};
    if (!ResolveGoldAnchor(panel, anchor)) {
        SetGoldLayout(panel, nullptr);
        RestoreCompanionBackgrounds(panel);
        SetWidgetState(refresh, false);
        ReportPlacementFailure("gold anchor was not found");
        return;
    }
    if (!panelLayout) {
        WidgetRect freeArea{};
        if (ResolveFreeButtonArea(panel, anchor, freeArea)) position = FitButtonInArea(freeArea, desired);
        LayoutDiagnostic("fallback free-area=%d,%d,%d,%d desired-scale=%.6f fitted-scale=%.6f valid=%d",
            freeArea.x, freeArea.y, freeArea.width, freeArea.height, static_cast<double>(desired.scale),
            static_cast<double>(position.geometry.scale), position.valid);
    }
    if (!position.valid || !WriteWidgetGeometry(refresh, position.geometry)) {
        SetGoldLayout(panel, nullptr);
        RestoreCompanionBackgrounds(panel);
        SetWidgetState(refresh, false);
        ReportPlacementFailure("computed position was invalid");
        return;
    }

    layout.Applied(position.geometry);
    SetWidgetState(frame, legacyPanelLayout);
    DynamicPlacements.fetch_add(1, std::memory_order_relaxed);
    SetWidgetState(refresh, true);

    if (Context && Settings.diagnosticsEnabled
        && !PlacementSuccessReported.exchange(true, std::memory_order_relaxed)) {
        char message[220]{};
        std::snprintf(
            message,
            sizeof(message),
            "VendorStockRefresh: %s button at %d,%d scale %.3f from gold anchor %d,%d,%d,%d.",
            panelLayout ? "framed" : "measured fallback",
            position.geometry.rect.x,
            position.geometry.rect.y,
            static_cast<double>(position.geometry.scale),
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
        "Vendor Stock Refresh 2.1.6: %s; diagnostics=%s; placed=%llu; "
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
#if defined(VENDOR_LAYOUT_DIAGNOSTIC)
    LayoutDiagnosticEvents.store(0, std::memory_order_relaxed);
    LayoutDiagnosticActive = false;
    context->LogInfo("[VSR-GOLD-216-2] Diagnostic build; automatic first-12 vendor captures; native-size placement with measured fit. Companion validation follows configuration; disabled plugins skip it.");
#endif
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
            "VendorStockRefresh 2.1.6 by RuffnecKk loaded disabled; no hook or service registered.");
        return true;
    }

    const bool companionValid = ValidateCompanion();
#if defined(VENDOR_LAYOUT_DIAGNOSTIC)
    context->LogInfo(companionValid ? "[VSR-GOLD-216-2] Companion hash and resource-service validation PASS."
        : "[VSR-GOLD-216-2] Companion hash/resource-service validation FAIL; see error below.");
#endif
    if (!companionValid) {
        context->LogError("VendorStockRefresh: missing/mismatched companion MPQ or unavailable resource service; install the matching DLL and MPQ together.");
        return false;
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
    SetImageFilename = At<SetFilenameFn>(CompanionNative::SetFilenameRva);
    IsImageReady = At<ImageReadyFn>(CompanionNative::ImageReadyRva);

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
        "VendorStockRefresh 2.1.6 by RuffnecKk active; native button follows the panel slot or runtime gold anchor.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    PlacementCache = {};
    ActiveRefresh = {};
    OriginalConfigureVendorPanel = nullptr;
    GetWidgetRect = nullptr;
    SetImageFilename = nullptr;
    IsImageReady = nullptr;
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
