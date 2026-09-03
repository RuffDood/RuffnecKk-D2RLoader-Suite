#include <D2RLPlugin/api.h>
#include <D2RLPlugin/diagnostics.h>
#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/item_interactions.h>
#include <D2RLPlugin/localization.h>
#include <D2RLPlugin/shared_events.h>

#include "policy.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace RuffnecKk::MassIdentify {
namespace {

constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::int32_t SharedStashProxyState = 0xBA;
constexpr std::uint64_t HoverLifetimeMilliseconds = 1'500;

constexpr std::uintptr_t SendTwentyOneBytePacketRva = 0x0EC820;
constexpr std::uintptr_t ModernSellAppenderCallRva = 0x2C51A9;
constexpr std::uintptr_t ModernGiveAppenderCallRva = 0x2C5455;
constexpr std::uintptr_t ModernUiStateProbeCallRva = 0x2C55F2;
constexpr std::uintptr_t IsUiStateOpenRva = 0x0CE500;
constexpr std::uintptr_t GetLocalizedStringByKeyRva = 0x5F4B90;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t CheckStateRva = 0x3351B0;
constexpr std::uintptr_t GetUnitIdRva = 0x34A330;
constexpr std::uintptr_t GetUnitInventoryRva = 0x34A360;
constexpr std::uintptr_t GetItemDataRva = 0x34A500;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t CheckItemFlagRva = 0x36E2D0;
constexpr std::uintptr_t SetItemFlagRva = 0x36D8F0;
constexpr std::uintptr_t GetItemCodeRva = 0x36EF50;
constexpr std::uintptr_t GetFirstItemRva = 0x388C10;
constexpr std::uintptr_t GetNextItemRva = 0x38ABA0;
constexpr std::uintptr_t GetParentInventoryRva = 0x38AC50;
constexpr std::uintptr_t GetInventoryOwnerIdRva = 0x388BA0;
constexpr std::uintptr_t GetFirstCorpseRva = 0x388E00;
constexpr std::uintptr_t GetNextCorpseRva = 0x38CD70;
constexpr std::uintptr_t GetCorpseUnitIdRva = 0x2EF880;
constexpr std::uintptr_t IdentifyItemRva = 0x46E8C0;
constexpr std::uintptr_t SynchronizeQuantityRva = 0x46F090;
constexpr std::uintptr_t ServerUnitRva = 0x48FE80;
constexpr std::uintptr_t CainIdentifyCallbackRva = 0x4C6C90;

constexpr std::array<std::uint8_t, 32> SendTwentyOneBytePacketExpected{
    0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x05, 0x9D,
    0xEA, 0x8D, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,
    0x44, 0x24, 0x38, 0x8B, 0x44, 0x24, 0x70, 0x89,
    0x44, 0x24, 0x2D, 0x8B, 0x44, 0x24, 0x78, 0x88,
};
constexpr std::array<std::uint8_t, 5> ModernSellAppenderCallExpected{
    0xE8, 0xE2, 0xF9, 0x32, 0x00,
};
constexpr std::array<std::uint8_t, 5> ModernGiveAppenderCallExpected{
    0xE8, 0x36, 0xF7, 0x32, 0x00,
};
constexpr std::array<std::uint8_t, 5> ModernUiStateProbeCallExpected{
    0xE8, 0x09, 0x8F, 0xE0, 0xFF,
};
constexpr std::array<std::uint8_t, 15> IsUiStateOpenExpected{
    0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8,
    0x95, 0x02, 0x0F, 0xB6, 0x04, 0x08, 0xC3,
};
constexpr std::array<std::uint8_t, 29> GetLocalizedStringByKeyExpected{
    0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x49, 0x8D,
    0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
    0x00, 0x48, 0x8B, 0x05, 0x20, 0x67, 0x3D, 0x02,
    0x48, 0x33, 0xC4, 0x48, 0x89,
};
constexpr std::array<std::uint8_t, 32> CainIdentifyCallbackExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x18, 0xB0, 0xFF, 0xFF, 0xB8, 0xE8, 0x50,
    0x00, 0x00, 0xE8, 0x39, 0xA4, 0xE0, 0x00, 0x48,
    0x2B, 0xE0, 0x48, 0x8B, 0x05, 0x17, 0x46, 0x50,
};
constexpr std::array<std::uint8_t, 32> IdentifyItemExpected{
    0x48, 0x89, 0x6C, 0x24, 0x20, 0x41, 0x54, 0x41,
    0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x49,
    0x8B, 0xE8, 0x45, 0x0F, 0xB6, 0xE1, 0x4C, 0x8B,
    0xF2, 0x4C, 0x8D, 0x0D, 0x30, 0xD7, 0x8A, 0x01,
};
constexpr std::array<std::uint8_t, 32> SynchronizeQuantityExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B,
    0xF8, 0x48, 0x8B, 0xDA, 0x45, 0x33, 0xC0, 0x48,
};
constexpr std::array<std::uint8_t, 16> GetUnitStatExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
};
constexpr std::array<std::uint8_t, 32> CheckStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1, 0xE8, 0x07, 0x68, 0x01,
    0x00, 0x85, 0xC0, 0x74, 0x0E, 0x83, 0xE8, 0x01,
};
constexpr std::array<std::uint8_t, 32> GetUnitIdExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetUnitInventoryExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetItemDataExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x74, 0xC4, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
};
constexpr std::array<std::uint8_t, 32> GetUnitTypeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 16> CheckItemFlagExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48,
};
constexpr std::array<std::uint8_t, 16> SetItemFlagExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
};
constexpr std::array<std::uint8_t, 32> GetItemCodeExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x80, 0x83, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetFirstItemExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x2E, 0x81, 0x39,
    0x04, 0x03, 0x02, 0x01, 0x74, 0x1C, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
};
constexpr std::array<std::uint8_t, 32> GetNextItemExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x84, 0x98, 0xFF, 0xFF, 0xEB, 0x67, 0xE8, 0x0D,
};
constexpr std::array<std::uint8_t, 32> GetParentInventoryExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x6D,
    0x0D, 0xFC, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x19,
    0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
};
constexpr std::array<std::uint8_t, 32> GetInventoryOwnerIdExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1E, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0xB4, 0xC2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x30,
};
constexpr std::array<std::uint8_t, 16> GetFirstCorpseExpected{
    0x48, 0x8B, 0x41, 0x68, 0xC3, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 16> GetNextCorpseExpected{
    0x48, 0x8B, 0x41, 0x10, 0xC3, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 16> GetCorpseUnitIdExpected{
    0x8B, 0x01, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 32> ServerUnitExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48,
    0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x38,
};

struct TooltipLocale {
    std::string_view defenseFingerprint;
    std::string_view massIdentifyText;
};

constexpr std::array TooltipLocales{
    TooltipLocale{"Defense: %d", "Shift + Right Click to Mass ID"},
    TooltipLocale{"防禦：%d", "Shift + 右鍵點擊以批量鑑定"},
    TooltipLocale{"Verteidigung: %d", "Umschalt + Rechtsklick für Massenidentifizierung"},
    TooltipLocale{"Defensa: %d", "Mayús + clic derecho para identificar todo"},
    TooltipLocale{"Défense : %d", "Maj + clic droit pour tout identifier"},
    TooltipLocale{"Difesa: %d", "Maiusc + clic destro per identificare tutto"},
    TooltipLocale{"방어력: %d", "Shift + 오른쪽 클릭으로 모두 감정"},
    TooltipLocale{"Obrona: %d", "Shift + prawy przycisk, aby zidentyfikować wszystko"},
    TooltipLocale{"Defensa: %d", "Mayús + clic derecho para identificar todo"},
    TooltipLocale{"防御力: %d", "Shift + 右クリックですべて鑑定"},
    TooltipLocale{"Defesa: %d", "Shift + clique direito para identificar tudo"},
    TooltipLocale{"Защита: %d", "Shift + ПКМ, чтобы опознать всё"},
    TooltipLocale{"防御: %d", "Shift + 右键点击以批量辨识"},
};

struct GameStringView {
    const char* data{};
    std::size_t size{};
};

using SendTwentyOneBytePacketFn = void(__fastcall*)(
    std::uint8_t, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t, std::uint32_t) noexcept;
using IsUiStateOpenFn = std::int32_t(__fastcall*)(std::int32_t) noexcept;
using GetLocalizedStringByKeyFn = const char*(__fastcall*)(
    const GameStringView*) noexcept;
using TooltipAppenderFn = const char*(__fastcall*)(
    const GameStringView*, void*) noexcept;
using CainIdentifyCallbackFn = std::int32_t(__fastcall*)(
    void*, void*, const std::uint8_t*, std::int32_t) noexcept;
using GetUnitInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetParentInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetItemDataFn = void*(__fastcall*)(void*) noexcept;
using GetItemCodeFn = std::uint32_t(__fastcall*)(void*) noexcept;
using GetUnitIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetServerUnitFn = void*(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using GetFirstItemFn = void*(__fastcall*)(void*) noexcept;
using GetNextItemFn = void*(__fastcall*)(void*) noexcept;
using GetInventoryOwnerIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetFirstCorpseFn = void*(__fastcall*)(void*) noexcept;
using GetNextCorpseFn = void*(__fastcall*)(void*) noexcept;
using GetCorpseUnitIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using CheckItemFlagFn = std::int32_t(__fastcall*)(
    void*, std::uint32_t) noexcept;
using SetItemFlagFn = void(__fastcall*)(
    void*, std::uint32_t, std::int32_t) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::int32_t) noexcept;
using CheckStateFn = std::int32_t(__fastcall*)(
    void*, std::int32_t) noexcept;
using IdentifyItemFn = void(__fastcall*)(
    void*, void*, void*, std::uint8_t) noexcept;
using SynchronizeQuantityFn = void(__fastcall*)(
    void*, void*, void*, std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};

const D2RL::SharedEventServiceV1* SharedEventService{};
const D2RL::DiagnosticsServiceV1* DiagnosticsService{};
const D2RL::ItemInteractionServiceV1* ItemInteractionService{};
const D2RL::ItemServiceV1* ItemService{};
const D2RL::InventoryServiceV1* InventoryService{};
const D2RL::LocalizationServiceV1* LocalizationService{};
D2RL::SharedEvents::ListenerHandle TooltipListenerHandle{};
D2RL::ItemInteractions::ListenerHandle ItemInteractionListenerHandle{};

SendTwentyOneBytePacketFn SendTwentyOneBytePacket{};
CainIdentifyCallbackFn OriginalCainIdentifyCallback{};
IsUiStateOpenFn IsUiStateOpen{};
GetLocalizedStringByKeyFn GetLocalizedStringByKey{};
GetUnitInventoryFn GetUnitInventory{};
GetParentInventoryFn GetParentInventory{};
GetUnitTypeFn GetUnitType{};
GetItemDataFn GetItemData{};
GetItemCodeFn GetItemCode{};
GetUnitIdFn GetUnitId{};
GetServerUnitFn GetServerUnit{};
GetFirstItemFn GetFirstItem{};
GetNextItemFn GetNextItem{};
GetInventoryOwnerIdFn GetInventoryOwnerId{};
GetFirstCorpseFn GetFirstCorpse{};
GetNextCorpseFn GetNextCorpse{};
GetCorpseUnitIdFn GetCorpseUnitId{};
CheckItemFlagFn CheckItemFlag{};
SetItemFlagFn SetItemFlag{};
GetUnitStatFn GetUnitStat{};
CheckStateFn CheckState{};
IdentifyItemFn IdentifyItem{};
SynchronizeQuantityFn SynchronizeQuantity{};

std::atomic<std::uint64_t> RequestsSent{};
std::atomic<std::uint64_t> GesturesObserved{};
std::atomic<std::uint64_t> ItemInteractionsObserved{};
std::atomic<std::uint64_t> ItemInteractionsConsumed{};
std::atomic<std::uint64_t> VendorTradeFallbacks{};
std::atomic<std::uint64_t> UiContextProbesObserved{};
std::atomic<std::uint64_t> RequestsAccepted{};
std::atomic<std::uint64_t> RequestsRejected{};
std::atomic<std::uint64_t> ItemsIdentified{};
std::atomic<std::uint64_t> ChargesConsumed{};
std::atomic<std::uint64_t> DiagnosticMessages{};
std::atomic<std::uint32_t> HoveredFallbackTomeGuid{};
std::atomic<std::uint64_t> HoveredFallbackTomeTick{};
std::atomic_bool FallbackTooltipObserved{};
std::atomic_bool SuppressRightButtonUp{};
std::atomic_bool PluginActive{};
void* TooltipRelayPage{};
bool TooltipCallSitesInstalled{};

std::atomic<HHOOK> GameMessageHook{};
std::mutex GameMessageHookMutex;
HMODULE GameMessageHookModule{};
std::atomic<std::uint32_t> ActiveMessageHookCallbacks{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-mass-identify",
    .name = "MassID",
    .version = "2.1.0",
    .author = "RuffnecKk",
    .description = "Identifies selected item containers from an Identify Tome.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

void ResetCounters() noexcept {
    RequestsSent.store(0, std::memory_order_relaxed);
    GesturesObserved.store(0, std::memory_order_relaxed);
    ItemInteractionsObserved.store(0, std::memory_order_relaxed);
    ItemInteractionsConsumed.store(0, std::memory_order_relaxed);
    VendorTradeFallbacks.store(0, std::memory_order_relaxed);
    UiContextProbesObserved.store(0, std::memory_order_relaxed);
    RequestsAccepted.store(0, std::memory_order_relaxed);
    RequestsRejected.store(0, std::memory_order_relaxed);
    ItemsIdentified.store(0, std::memory_order_relaxed);
    ChargesConsumed.store(0, std::memory_order_relaxed);
    DiagnosticMessages.store(0, std::memory_order_relaxed);
}

auto ShouldLogDiagnostic() noexcept -> bool {
    if (!Settings.diagnosticsEnabled || Context == nullptr) return false;
    const auto ordinal = DiagnosticMessages.fetch_add(
        1, std::memory_order_relaxed) + 1;
    return ordinal <= 8 || ordinal % 100 == 0;
}

void ClearHoverState() noexcept {
    HoveredFallbackTomeGuid.store(0, std::memory_order_release);
    HoveredFallbackTomeTick.store(0, std::memory_order_release);
    FallbackTooltipObserved.store(false, std::memory_order_release);
    SuppressRightButtonUp.store(false, std::memory_order_release);
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "MassID: configuration exceeds 65535 bytes."
            : "MassID: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("MassID: invalid TOML (")
            + error + "); no service, hook, or patch was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

auto QueryRequiredServices() noexcept -> bool {
    constexpr auto SharedEventRegisterFieldEnd = static_cast<std::uint32_t>(
        offsetof(D2RL::SharedEventServiceV1, registerItemTooltipListener)
        + sizeof(D2RL::SharedEvents::RegisterItemTooltipListenerFn));
    constexpr auto ItemInfoFieldEnd = static_cast<std::uint32_t>(
        offsetof(D2RL::ItemServiceV1, getItemInfo)
        + sizeof(D2RL::Items::GetItemInfoFn));
    constexpr auto InventoryCursorFieldEnd = static_cast<std::uint32_t>(
        offsetof(D2RL::InventoryServiceV1, getCursorItem)
        + sizeof(D2RL::Inventory::GetCursorItemFn));
    constexpr auto LocalizationKeyFieldEnd = static_cast<std::uint32_t>(
        offsetof(D2RL::LocalizationServiceV1, getStringByKey)
        + sizeof(D2RL::Localization::GetStringByKeyFn));

    const auto diagnosticsResult = Context->QueryService(
        D2RL::ServiceId::Diagnostics,
        D2RL::DiagnosticsServiceV1Version,
        &DiagnosticsService);
    if (diagnosticsResult == D2RL::ServiceQueryResult::Success) {
        if (!D2RL::HasDiagnosticsServiceV1Field(
                DiagnosticsService, D2RL::DiagnosticsServiceV1RequiredSize)
            || DiagnosticsService->queryHookStatus == nullptr) {
            Context->LogError(
                "MassID: DiagnosticsService v1 returned an invalid contract.");
            return false;
        }
    } else {
        DiagnosticsService = nullptr;
        Context->LogWarn(
            "MassID: DiagnosticsService v1 is unavailable; composable native entries must remain byte-exact vanilla.");
    }

    if (Context->QueryService(
            D2RL::ServiceId::ItemInteraction,
            D2RL::ItemInteractionServiceV1Version,
            &ItemInteractionService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasItemInteractionServiceV1Field(
            ItemInteractionService,
            D2RL::ItemInteractionServiceV1RequiredSize)
        || ItemInteractionService->registerListener == nullptr
        || ItemInteractionService->unregisterListener == nullptr) {
        Context->LogError(
            "MassID: PluginSDK v4 ItemInteractionService v1 is required.");
        return false;
    }

    if (Context->QueryService(
            D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version,
            &SharedEventService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasSharedEventServiceV1Field(
            SharedEventService, SharedEventRegisterFieldEnd)
        || SharedEventService->registerItemTooltipListener == nullptr) {
        Context->LogError(
            "MassID: SharedEventService v1 item-tooltip registration is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Item,
            D2RL::ItemServiceV1Version,
            &ItemService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasItemServiceV1Field(ItemService, ItemInfoFieldEnd)
        || ItemService->getItemInfo == nullptr) {
        Context->LogError("MassID: ItemService v1 inspection is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Inventory,
            D2RL::InventoryServiceV1Version,
            &InventoryService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInventoryServiceV1Field(
            InventoryService, InventoryCursorFieldEnd)
        || InventoryService->getLocalPlayer == nullptr
        || InventoryService->getCursorItem == nullptr) {
        Context->LogError(
            "MassID: InventoryService v1 local-player inspection is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Localization,
            D2RL::LocalizationServiceV1Version,
            &LocalizationService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLocalizationServiceV1Field(
            LocalizationService, LocalizationKeyFieldEnd)
        || LocalizationService->getStringByKey == nullptr) {
        Context->LogError(
            "MassID: LocalizationService v1 key lookup is unavailable.");
        return false;
    }
    return true;
}

template<std::size_t Size>
auto Check(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected,
    const char* label
) noexcept -> bool {
    if (Context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()))) {
        return true;
    }
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "MassID: %s signature mismatch at RVA 0x%llX; no mutation was started.",
        label,
        static_cast<unsigned long long>(rva));
    Context->LogError(message);
    return false;
}

auto IsExecutableAddress(const void* address) noexcept -> bool {
    if (address == nullptr) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(address, &region, sizeof(region)) == 0
        || region.State != MEM_COMMIT) {
        return false;
    }
    const auto protection = region.Protect & 0xFF;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

template<std::size_t Size>
auto ValidateComposableEntry(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected,
    std::string_view expectedOwner,
    const char* label
) noexcept -> bool {
    if (std::memcmp(Base + rva, expected.data(), expected.size()) == 0) {
        return true;
    }
    if (DiagnosticsService == nullptr) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: %s differs and no tracked owner proof is available.",
            label);
        Context->LogError(message);
        return false;
    }

    const D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = rva,
        .expected = expected.data(),
        .expectedSize = static_cast<std::uint32_t>(expected.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    const auto result = DiagnosticsService->queryHookStatus(
        Context, &query, &status);
    if (result != D2RL::Diagnostics::Result::Success
        || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: DiagnosticsService could not validate %s.",
            label);
        Context->LogError(message);
        return false;
    }

    const auto ownerEnd = std::find(
        std::begin(status.ownerPluginId),
        std::end(status.ownerPluginId),
        '\0');
    const std::string_view owner{
        status.ownerPluginId,
        static_cast<std::size_t>(
            ownerEnd - std::begin(status.ownerPluginId))};
    const bool accepted =
        status.state == D2RL::Diagnostics::ModificationState::Tracked
        && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook
        && status.ownerCount == 1
        && owner == expectedOwner
        && IsExecutableAddress(Base + rva);
    if (!accepted) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: %s ownership refused (state=%u; kind=%u; owners=%u; owner=%.*s).",
            label,
            static_cast<unsigned>(status.state),
            static_cast<unsigned>(status.kind),
            status.ownerCount,
            static_cast<int>(owner.size()),
            owner.data());
        Context->LogError(message);
    }
    return accepted;
}

auto ValidateRuntime() noexcept -> bool {
    bool valid = Check(
        SendTwentyOneBytePacketRva,
        SendTwentyOneBytePacketExpected,
        "native 21-byte command sender");
    valid = Check(
        ModernSellAppenderCallRva,
        ModernSellAppenderCallExpected,
        "vendor tooltip action call") && valid;
    valid = Check(
        ModernGiveAppenderCallRva,
        ModernGiveAppenderCallExpected,
        "trade tooltip action call") && valid;
    valid = Check(
        ModernUiStateProbeCallRva,
        ModernUiStateProbeCallExpected,
        "trade UI-state probe call") && valid;
    valid = ValidateComposableEntry(
        IsUiStateOpenRva,
        IsUiStateOpenExpected,
        "ruffneckk-remote-stash",
        "UI_IsStateOpen") && valid;
    valid = ValidateComposableEntry(
        GetLocalizedStringByKeyRva,
        GetLocalizedStringByKeyExpected,
        "eezstreet-plugin-skills",
        "LOCALIZATION_GetStringByKey") && valid;
    valid = Check(
        CainIdentifyCallbackRva,
        CainIdentifyCallbackExpected,
        "server opcode-0x34 callback") && valid;
    valid = Check(
        IdentifyItemRva,
        IdentifyItemExpected,
        "authoritative identify helper") && valid;
    valid = Check(
        SynchronizeQuantityRva,
        SynchronizeQuantityExpected,
        "Tome quantity synchronizer") && valid;
    valid = Check(GetUnitStatRva, GetUnitStatExpected, "unit-stat helper")
        && valid;
    valid = Check(CheckStateRva, CheckStateExpected, "state helper")
        && valid;
    valid = Check(GetUnitIdRva, GetUnitIdExpected, "unit-id helper")
        && valid;
    valid = Check(
        GetUnitInventoryRva,
        GetUnitInventoryExpected,
        "unit-inventory helper") && valid;
    valid = Check(GetItemDataRva, GetItemDataExpected, "item-data helper")
        && valid;
    valid = Check(GetUnitTypeRva, GetUnitTypeExpected, "unit-type helper")
        && valid;
    valid = Check(
        CheckItemFlagRva,
        CheckItemFlagExpected,
        "item-flag reader") && valid;
    valid = Check(SetItemFlagRva, SetItemFlagExpected, "item-flag writer")
        && valid;
    valid = Check(GetItemCodeRva, GetItemCodeExpected, "item-code helper")
        && valid;
    valid = Check(
        GetFirstItemRva,
        GetFirstItemExpected,
        "inventory first-item helper") && valid;
    valid = Check(
        GetNextItemRva,
        GetNextItemExpected,
        "inventory next-item helper") && valid;
    valid = Check(
        GetParentInventoryRva,
        GetParentInventoryExpected,
        "parent-inventory helper") && valid;
    valid = Check(
        GetInventoryOwnerIdRva,
        GetInventoryOwnerIdExpected,
        "inventory-owner helper") && valid;
    valid = Check(
        GetFirstCorpseRva,
        GetFirstCorpseExpected,
        "auxiliary-player list head") && valid;
    valid = Check(
        GetNextCorpseRva,
        GetNextCorpseExpected,
        "auxiliary-player list next") && valid;
    valid = Check(
        GetCorpseUnitIdRva,
        GetCorpseUnitIdExpected,
        "auxiliary-player id helper") && valid;
    valid = Check(
        ServerUnitRva,
        ServerUnitExpected,
        "server-unit resolver") && valid;
    return valid;
}

void BindNativeFunctions() noexcept {
    SendTwentyOneBytePacket = At<SendTwentyOneBytePacketFn>(
        SendTwentyOneBytePacketRva);
    IsUiStateOpen = At<IsUiStateOpenFn>(IsUiStateOpenRva);
    GetLocalizedStringByKey = At<GetLocalizedStringByKeyFn>(
        GetLocalizedStringByKeyRva);
    GetUnitInventory = At<GetUnitInventoryFn>(GetUnitInventoryRva);
    GetParentInventory = At<GetParentInventoryFn>(GetParentInventoryRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetItemData = At<GetItemDataFn>(GetItemDataRva);
    GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
    GetUnitId = At<GetUnitIdFn>(GetUnitIdRva);
    GetServerUnit = At<GetServerUnitFn>(ServerUnitRva);
    GetFirstItem = At<GetFirstItemFn>(GetFirstItemRva);
    GetNextItem = At<GetNextItemFn>(GetNextItemRva);
    GetInventoryOwnerId = At<GetInventoryOwnerIdFn>(
        GetInventoryOwnerIdRva);
    GetFirstCorpse = At<GetFirstCorpseFn>(GetFirstCorpseRva);
    GetNextCorpse = At<GetNextCorpseFn>(GetNextCorpseRva);
    GetCorpseUnitId = At<GetCorpseUnitIdFn>(GetCorpseUnitIdRva);
    CheckItemFlag = At<CheckItemFlagFn>(CheckItemFlagRva);
    SetItemFlag = At<SetItemFlagFn>(SetItemFlagRva);
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    CheckState = At<CheckStateFn>(CheckStateRva);
    IdentifyItem = At<IdentifyItemFn>(IdentifyItemRva);
    SynchronizeQuantity = At<SynchronizeQuantityFn>(SynchronizeQuantityRva);
}

auto InspectItem(
    D2RL::ItemHandle handle,
    D2RL::Items::ItemInfo& info
) noexcept -> bool {
    if (handle == D2RL::InvalidItemHandle || ItemService == nullptr
        || ItemService->getItemInfo == nullptr) {
        return false;
    }
    info = {.structSize = D2RL::Items::ItemInfoSize};
    return ItemService->getItemInfo(Context, handle, &info)
        == D2RL::Items::Result::Success;
}

auto IsSupportedTomeContainer(
    D2RL::Items::ItemContainer container
) noexcept -> bool {
    return container == D2RL::Items::ItemContainer::Inventory
        || container == D2RL::Items::ItemContainer::Cube;
}

auto CursorIsEmpty() noexcept -> bool {
    if (InventoryService == nullptr) return false;
    D2RL::PlayerHandle player{};
    if (InventoryService->getLocalPlayer(Context, &player)
            != D2RL::Inventory::Result::Success
        || player == D2RL::InvalidPlayerHandle) {
        return false;
    }

    D2RL::ItemHandle cursorItem{};
    const auto result = InventoryService->getCursorItem(
        Context, player, &cursorItem);
    return result == D2RL::Inventory::Result::NotFound
        || (result == D2RL::Inventory::Result::Success
            && cursorItem == D2RL::InvalidItemHandle);
}

auto CaptureMassIdentifyRequest(
    D2RL::ItemHandle item,
    const char* path,
    bool shiftDown
) noexcept -> bool {
    GesturesObserved.fetch_add(1, std::memory_order_relaxed);
    D2RL::Items::ItemInfo info{};
    const bool inspected = InspectItem(item, info);
    const bool cursorEmpty = CursorIsEmpty();
    const bool supportedContainer = inspected
        && IsSupportedTomeContainer(info.container);
    if (!inspected || !ShouldCaptureGesture(
            Settings.enabled,
            Settings.rightClickMassIdentify,
            shiftDown,
            true,
            cursorEmpty,
            info.code,
            supportedContainer)) {
        if (ShouldLogDiagnostic()) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "MassID: %s gesture ignored; handle=%llu; shift=%s; directMode=%s; inspected=%s; cursorEmpty=%s; itemCode=0x%08X; supportedContainer=%s.",
                path,
                static_cast<unsigned long long>(item),
                shiftDown ? "true" : "false",
                Settings.rightClickMassIdentify ? "true" : "false",
                inspected ? "true" : "false",
                cursorEmpty ? "true" : "false",
                inspected ? info.code : 0,
                supportedContainer ? "true" : "false");
            Context->LogInfo(message);
        }
        return false;
    }

    SendTwentyOneBytePacket(
        CainIdentifyOpcode,
        info.runtimeId,
        RequestMarker,
        RequestGuard,
        0,
        0);
    RequestsSent.fetch_add(1, std::memory_order_relaxed);
    if (ShouldLogDiagnostic()) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: %s %s captured; Tome runtime id=%u; private opcode 0x34 queued through the native 21-byte sender.",
            path,
            shiftDown ? "Shift-right-click" : "right-click",
            info.runtimeId);
        Context->LogInfo(message);
    }
    return true;
}

auto QueueFallbackMassIdentifyRequest(
    std::uint32_t tomeGuid,
    const char* path,
    bool shiftDown
) noexcept -> bool {
    GesturesObserved.fetch_add(1, std::memory_order_relaxed);
    VendorTradeFallbacks.fetch_add(1, std::memory_order_relaxed);
    if (!ShouldCaptureGesture(
            Settings.enabled,
            Settings.rightClickMassIdentify,
            shiftDown,
            true,
            CursorIsEmpty(),
            IdentifyTomeCode,
            tomeGuid != 0)) {
        return false;
    }
    SendTwentyOneBytePacket(
        CainIdentifyOpcode,
        tomeGuid,
        RequestMarker,
        RequestGuard,
        0,
        0);
    RequestsSent.fetch_add(1, std::memory_order_relaxed);
    if (ShouldLogDiagnostic()) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: %s %s captured; Tome runtime id=%u; private opcode 0x34 queued through the native 21-byte sender.",
            path,
            shiftDown ? "Shift-right-click" : "right-click",
            tomeGuid);
        Context->LogInfo(message);
    }
    return true;
}

auto __cdecl OnItemInteraction(
    const D2RL::PluginContext* context,
    const D2RL::ItemInteractions::ItemInteractionEvent* event,
    void*
) noexcept -> D2RL::ItemInteractions::Decision {
    ItemInteractionsObserved.fetch_add(1, std::memory_order_relaxed);
    if (!PluginActive.load(std::memory_order_acquire)
        || context == nullptr || context != Context || event == nullptr
        || event->structSize
            < D2RL::ItemInteractions::ItemInteractionEventRequiredSize
        || event->action != D2RL::ItemInteractions::Action::Activate
        || event->inputSource
            != D2RL::ItemInteractions::InputSource::KeyboardMouse) {
        return D2RL::ItemInteractions::Decision::Continue;
    }

    const bool shiftDown = (event->modifiers
        & D2RL::ItemInteractions::ModifierBit(
            D2RL::ItemInteractions::Modifier::Shift)) != 0;
    if (!CaptureMassIdentifyRequest(
            event->item,
            "PluginSDK item interaction",
            shiftDown)) {
        return D2RL::ItemInteractions::Decision::Continue;
    }
    ItemInteractionsConsumed.fetch_add(1, std::memory_order_relaxed);
    return D2RL::ItemInteractions::Decision::Consume;
}

void SuppressWindowMessage(MSG* message) noexcept {
    if (message == nullptr) return;
    message->message = WM_NULL;
    message->wParam = 0;
    message->lParam = 0;
}

LRESULT CALLBACK MassIdentifyMessageHook(
    int code,
    WPARAM removeMode,
    LPARAM parameter
) noexcept {
    struct CallbackGuard {
        CallbackGuard() noexcept {
            ActiveMessageHookCallbacks.fetch_add(1, std::memory_order_acq_rel);
        }
        ~CallbackGuard() noexcept {
            ActiveMessageHookCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    } callbackGuard;

    auto* message = code >= 0 && removeMode == PM_REMOVE && parameter != 0
        ? reinterpret_cast<MSG*>(parameter)
        : nullptr;
    if (message != nullptr
        && PluginActive.load(std::memory_order_acquire)
        && Settings.enabled
        && message->message == WM_RBUTTONDOWN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0
            || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (Settings.rightClickMassIdentify || shift) {
            const auto now = GetTickCount64();
            const auto hoveredAt = HoveredFallbackTomeTick.load(
                std::memory_order_acquire);
            const auto tomeGuid = HoveredFallbackTomeGuid.load(
                std::memory_order_acquire);
            const auto age = now >= hoveredAt
                ? now - hoveredAt
                : (std::numeric_limits<std::uint64_t>::max)();
            if (tomeGuid != 0
                && hoveredAt != 0
                && age <= HoverLifetimeMilliseconds
                && QueueFallbackMassIdentifyRequest(
                    tomeGuid,
                    "vendor/trade window fallback",
                    shift)) {
                SuppressRightButtonUp.store(true, std::memory_order_release);
                SuppressWindowMessage(message);
            }
        }
    }
    if (message != nullptr && message->message == WM_RBUTTONUP
        && SuppressRightButtonUp.exchange(
            false, std::memory_order_acq_rel)) {
        SuppressWindowMessage(message);
    }
    return CallNextHookEx(
        GameMessageHook.load(std::memory_order_acquire),
        code,
        removeMode,
        parameter);
}

BOOL CALLBACK FindGameWindowCallback(HWND window, LPARAM state) noexcept {
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId()
        || GetWindow(window, GW_OWNER) != nullptr
        || !IsWindowVisible(window)) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(state) = window;
    return FALSE;
}

auto TryInstallGameMessageHook() noexcept -> bool {
    if (GameMessageHook.load(std::memory_order_acquire) != nullptr) {
        return true;
    }
    const std::lock_guard lock(GameMessageHookMutex);
    if (GameMessageHook.load(std::memory_order_relaxed) != nullptr) {
        return true;
    }

    HWND window{};
    EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&window));
    if (window == nullptr) return false;
    const auto threadId = GetWindowThreadProcessId(window, nullptr);
    if (threadId == 0) return false;

    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&MassIdentifyMessageHook),
            &module)) {
        return false;
    }
    const auto hook = SetWindowsHookExW(
        WH_GETMESSAGE,
        MassIdentifyMessageHook,
        module,
        threadId);
    if (hook == nullptr) {
        FreeLibrary(module);
        return false;
    }
    GameMessageHookModule = module;
    GameMessageHook.store(hook, std::memory_order_release);
    return true;
}

auto CurrentMassIdentifyTooltipText() noexcept -> std::string_view {
    std::array<char, 128> localized{};
    std::uint32_t requiredSize{};
    if (LocalizationService == nullptr
        || LocalizationService->getStringByKey(
            Context,
            "ItemStats1h",
            localized.data(),
            static_cast<std::uint32_t>(localized.size()),
            &requiredSize) != D2RL::Localization::Result::Success) {
        return TooltipLocales.front().massIdentifyText;
    }
    const std::string_view defense(localized.data());
    for (const auto& locale : TooltipLocales) {
        if (locale.defenseFingerprint == defense) {
            return locale.massIdentifyText;
        }
    }
    return TooltipLocales.front().massIdentifyText;
}

void __cdecl ItemTooltipCallback(
    const D2RL::PluginContext*,
    D2RL::SharedEvents::ItemTooltipEvent* event,
    void*
) noexcept {
    if (!PluginActive.load(std::memory_order_acquire)
        || !Settings.enabled
        || event == nullptr
        || event->structSize < D2RL::SharedEvents::ItemTooltipEventRequiredSize
        || event->text == nullptr
        || event->capacity == 0) {
        return;
    }

    TryInstallGameMessageHook();
    D2RL::Items::ItemInfo info{};
    if (!InspectItem(event->item, info) || info.code != IdentifyTomeCode) {
        ClearHoverState();
        return;
    }

    const bool fallbackTooltip = FallbackTooltipObserved.exchange(
        false, std::memory_order_acq_rel);
    if (!fallbackTooltip
        || HoveredFallbackTomeGuid.load(std::memory_order_acquire)
            != info.runtimeId) {
        HoveredFallbackTomeGuid.store(0, std::memory_order_release);
        HoveredFallbackTomeTick.store(0, std::memory_order_release);
    }

    if (!ShouldShowMassIdentifyTooltip(
            Settings.enabled, Settings.rightClickMassIdentify)) {
        return;
    }
    const auto text = CurrentMassIdentifyTooltipText();
    if (text.size() + 1 > event->capacity) return;
    std::memcpy(event->text, text.data(), text.size());
    event->text[text.size()] = '\0';
    event->length = static_cast<std::uint32_t>(text.size());
}

void ObserveVendorTradeTome(void* item) noexcept {
    FallbackTooltipObserved.store(true, std::memory_order_release);
    if (!Settings.enabled || item == nullptr
        || GetUnitType(item) != 4
        || GetItemCode(item) != IdentifyTomeCode
        || !IsSupportedInventoryPage(
            ReadInventoryPageFromItemData(GetItemData(item)))) {
        HoveredFallbackTomeGuid.store(0, std::memory_order_release);
        HoveredFallbackTomeTick.store(0, std::memory_order_release);
        return;
    }
    HoveredFallbackTomeGuid.store(
        static_cast<std::uint32_t>(GetUnitId(item)),
        std::memory_order_release);
    HoveredFallbackTomeTick.store(
        GetTickCount64(), std::memory_order_release);
}

const char* __fastcall HookVendorTradeTooltipAppender(
    const GameStringView* key,
    void* item
) noexcept {
    const auto* original = GetLocalizedStringByKey(key);
    ObserveVendorTradeTome(item);
    return original;
}

std::int32_t __fastcall HookUiContextProbe(
    std::int32_t state,
    void* item
) noexcept {
    const auto result = IsUiStateOpen(state);
    UiContextProbesObserved.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) ObserveVendorTradeTome(item);
    return result;
}

void* AllocateNear(void* hint, std::size_t size) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto base = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1);
    for (std::uintptr_t delta = granularity;
        delta < 0x70000000ULL;
        delta += granularity) {
        if (base > (std::numeric_limits<std::uintptr_t>::max)() - delta) {
            break;
        }
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta),
                size,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

auto WriteTooltipRelay(
    std::uint8_t* destination,
    std::span<const std::uint8_t> itemMove,
    TooltipAppenderFn target
) noexcept -> bool {
    if (destination == nullptr || itemMove.empty()) return false;
    std::memcpy(destination, itemMove.data(), itemMove.size());
    auto* jump = destination + itemMove.size();
    jump[0] = 0xFF;
    jump[1] = 0x25;
    jump[2] = jump[3] = jump[4] = jump[5] = 0x00;
    const auto targetAddress = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(jump + 6, &targetAddress, sizeof(targetAddress));
    return true;
}

auto WriteUiContextProbeRelay(std::uint8_t* destination) noexcept -> bool {
    if (destination == nullptr) return false;
    constexpr std::array<std::uint8_t, 3> ItemMove{
        0x4C, 0x89, 0xE2, // mov rdx, r12
    };
    std::memcpy(destination, ItemMove.data(), ItemMove.size());
    auto* jump = destination + ItemMove.size();
    jump[0] = 0xFF;
    jump[1] = 0x25;
    jump[2] = jump[3] = jump[4] = jump[5] = 0x00;
    const auto targetAddress = reinterpret_cast<std::uint64_t>(
        &HookUiContextProbe);
    std::memcpy(jump + 6, &targetAddress, sizeof(targetAddress));
    return true;
}

auto InstallVendorTradeTooltipCallSites() noexcept -> bool {
    constexpr std::size_t RelayStride = 32;
    constexpr std::size_t RelayBytes = RelayStride * 2;
    constexpr std::array<std::uint8_t, 3> ItemMove{
        0x4C, 0x89, 0xE2, // mov rdx, r12
    };

    TooltipRelayPage = AllocateNear(
        Base + ModernSellAppenderCallRva, RelayBytes);
    if (TooltipRelayPage == nullptr) return false;
    auto* relays = static_cast<std::uint8_t*>(TooltipRelayPage);
    if (!WriteTooltipRelay(
            relays, ItemMove, &HookVendorTradeTooltipAppender)
        || !WriteUiContextProbeRelay(relays + RelayStride)) {
        return false;
    }
    DWORD previousProtection{};
    if (!VirtualProtect(
            relays,
            RelayBytes,
            PAGE_EXECUTE_READ,
            &previousProtection)) {
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relays, RelayBytes);

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(relays);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress) return false;
    const auto relayRva = relayAddress - baseAddress;
    return Context->PatchCallRel32(
            ModernSellAppenderCallRva,
            ModernSellAppenderCallExpected.data(),
            static_cast<std::uint32_t>(
                ModernSellAppenderCallExpected.size()),
            relayRva,
            static_cast<std::uint32_t>(
                ModernSellAppenderCallExpected.size()))
        && Context->PatchCallRel32(
            ModernGiveAppenderCallRva,
            ModernGiveAppenderCallExpected.data(),
            static_cast<std::uint32_t>(
                ModernGiveAppenderCallExpected.size()),
            relayRva,
            static_cast<std::uint32_t>(
                ModernGiveAppenderCallExpected.size()))
        && Context->PatchCallRel32(
            ModernUiStateProbeCallRva,
            ModernUiStateProbeCallExpected.data(),
            static_cast<std::uint32_t>(
                ModernUiStateProbeCallExpected.size()),
            relayRva + RelayStride,
            static_cast<std::uint32_t>(
                ModernUiStateProbeCallExpected.size()));
}

auto GetInventoryPage(void* item) noexcept -> std::uint8_t {
    return ReadInventoryPageFromItemData(GetItemData(item));
}

auto IsOwnedIdentifyTome(
    void* player,
    void* inventory,
    void* tome
) noexcept -> bool {
    return player != nullptr
        && inventory != nullptr
        && tome != nullptr
        && GetUnitType(tome) == 4
        && GetParentInventory(tome) == inventory
        && GetItemCode(tome) == IdentifyTomeCode
        && IsSupportedInventoryPage(GetInventoryPage(tome));
}

auto IdentifyPage(
    void* game,
    void* inventoryActor,
    void* inventory,
    std::uint8_t page,
    std::int32_t budget
) noexcept -> std::int32_t {
    std::int32_t identified{};
    for (void* item = GetFirstItem(inventory);
        item != nullptr && identified < budget;) {
        void* next = GetNextItem(item);
        if (GetUnitType(item) == 4
            && GetParentInventory(item) == inventory
            && GetInventoryPage(item) == page
            && CheckItemFlag(item, IdentifiedItemFlag) == 0) {
            IdentifyItem(game, inventoryActor, item, 1);
            if (CheckItemFlag(item, IdentifiedItemFlag) != 0) {
                ++identified;
            }
        }
        item = next;
    }
    return identified;
}

struct SharedIdentifyResult {
    std::int32_t identified{};
    std::int32_t containers{};
};

auto IdentifySharedStashes(
    void* game,
    void* player,
    void* playerInventory,
    std::int32_t budget
) noexcept -> SharedIdentifyResult {
    SharedIdentifyResult result{};
    if (game == nullptr || player == nullptr || playerInventory == nullptr
        || budget <= 0) {
        return result;
    }

    const auto playerId = GetUnitId(player);
    for (void* record = GetFirstCorpse(playerInventory);
        record != nullptr && result.identified < budget;
        record = GetNextCorpse(record)) {
        const auto proxyId = GetCorpseUnitId(record);
        void* proxy = GetServerUnit(game, 0, proxyId);
        if (proxy == nullptr || proxy == player
            || CheckState(proxy, SharedStashProxyState) == 0) {
            continue;
        }

        void* proxyInventory = GetUnitInventory(proxy);
        if (proxyInventory == nullptr
            || GetInventoryOwnerId(proxyInventory) != playerId) {
            continue;
        }
        ++result.containers;
        // D2R routes shared-stash item updates through the state-0xBA proxy.
        // Passing the main player here creates a client-side personal-stash
        // ghost, so the validated proxy remains the native identify actor.
        result.identified += IdentifyPage(
            game,
            proxy,
            proxyInventory,
            StashPage,
            budget - result.identified);
    }
    return result;
}

std::int32_t __fastcall HookCainIdentifyCallback(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t packetSize
) noexcept {
    if (!IsPrivateRequest(packet, packetSize)) {
        return OriginalCainIdentifyCallback(
            game, player, packet, packetSize);
    }

    void* inventory = player != nullptr
        ? GetUnitInventory(player)
        : nullptr;
    const auto tomeRuntimeId = static_cast<std::int32_t>(ReadU32(packet, 1));
    void* tome = game != nullptr
        ? GetServerUnit(game, 4, tomeRuntimeId)
        : nullptr;
    if (!IsOwnedIdentifyTome(player, inventory, tome)) {
        RequestsRejected.fetch_add(1, std::memory_order_relaxed);
        if (Context != nullptr) {
            char message[160]{};
            std::snprintf(
                message,
                sizeof(message),
                "MassID: rejected request for invalid Tome runtime id %u.",
                static_cast<std::uint32_t>(tomeRuntimeId));
            Context->LogWarn(message);
        }
        return 0;
    }

    // Preserve the public behavior: leave the Tome in place and clear only
    // the targeting flag before the authoritative identify pass.
    SetItemFlag(tome, 0x00000004u, 0);
    const auto quantity = GetUnitStat(
        tome, static_cast<std::int32_t>(QuantityStat), 0);
    const auto budget = IdentificationBudget(
        Settings.freeIdentification, quantity);

    std::int32_t inventoryIdentified{};
    std::int32_t cubeIdentified{};
    std::int32_t personalStashIdentified{};
    SharedIdentifyResult sharedStash{};
    if (budget > 0) {
        inventoryIdentified = IdentifyPage(
            game, player, inventory, InventoryPage, budget);
        if (IncludesTarget(Settings.targets, TargetContainer::Cube)
            && inventoryIdentified < budget) {
            cubeIdentified = IdentifyPage(
                game,
                player,
                inventory,
                CubePage,
                budget - inventoryIdentified);
        }
        if (IncludesTarget(
                Settings.targets, TargetContainer::PersonalStash)
            && inventoryIdentified + cubeIdentified < budget) {
            personalStashIdentified = IdentifyPage(
                game,
                player,
                inventory,
                StashPage,
                budget - inventoryIdentified - cubeIdentified);
        }
        const auto mainInventoryIdentified = inventoryIdentified
            + cubeIdentified
            + personalStashIdentified;
        if (IncludesTarget(Settings.targets, TargetContainer::SharedStash)
            && mainInventoryIdentified < budget) {
            sharedStash = IdentifySharedStashes(
                game,
                player,
                inventory,
                budget - mainInventoryIdentified);
        }
    }

    const auto identified = inventoryIdentified
        + cubeIdentified
        + personalStashIdentified
        + sharedStash.identified;
    if (!Settings.freeIdentification && identified > 0) {
        SynchronizeQuantity(game, player, tome, -identified);
        ChargesConsumed.fetch_add(
            static_cast<std::uint64_t>(identified),
            std::memory_order_relaxed);
    }

    RequestsAccepted.fetch_add(1, std::memory_order_relaxed);
    ItemsIdentified.fetch_add(
        static_cast<std::uint64_t>(identified),
        std::memory_order_relaxed);
    if (ShouldLogDiagnostic()) {
        char message[416]{};
        std::snprintf(
            message,
            sizeof(message),
            "MassID: accepted Tome runtime id %u; quantity=%d; identified=%d (inventory=%d; cube=%d; personalStash=%d; sharedStash=%d; sharedContainers=%d); consumed=%d; freeIdentification=%s.",
            static_cast<std::uint32_t>(tomeRuntimeId),
            quantity,
            identified,
            inventoryIdentified,
            cubeIdentified,
            personalStashIdentified,
            sharedStash.identified,
            sharedStash.containers,
            Settings.freeIdentification ? 0 : identified,
            Settings.freeIdentification ? "true" : "false");
        Context->LogInfo(message);
    }
    return 0;
}

auto RegisterTooltipListener() noexcept -> bool {
    const D2RL::SharedEvents::ItemTooltipListener listener{
        .structSize = D2RL::SharedEvents::ItemTooltipListenerSize,
        .priority = 0,
        .slot = 100,
        .region = D2RL::SharedEvents::ItemTooltipRegion::ActionFooter,
        .position = D2RL::SharedEvents::ItemTooltipPosition::Bottom,
        .anchor = D2RL::SharedEvents::ItemTooltipAnchor::None,
        .fallback = D2RL::SharedEvents::ItemTooltipFallback::Omit,
        .callback = ItemTooltipCallback,
    };
    TooltipListenerHandle = D2RL::SharedEvents::InvalidHandle;
    if (SharedEventService->registerItemTooltipListener(
            Context, &listener, &TooltipListenerHandle)
            != D2RL::SharedEvents::Result::Success
        || TooltipListenerHandle == D2RL::SharedEvents::InvalidHandle) {
        Context->LogError(
            "MassID: shared item-tooltip listener registration failed.");
        return false;
    }
    return true;
}

auto RegisterItemInteractionListener() noexcept -> bool {
    const D2RL::ItemInteractions::ItemInteractionListener listener{
        .structSize = D2RL::ItemInteractions::ItemInteractionListenerSize,
        .flags = 0,
        .priority = 100,
        .reserved = 0,
        .callback = OnItemInteraction,
        .userData = nullptr,
    };
    ItemInteractionListenerHandle = D2RL::ItemInteractions::InvalidHandle;
    if (ItemInteractionService->registerListener(
            Context, &listener, &ItemInteractionListenerHandle)
            != D2RL::ItemInteractions::Result::Success
        || ItemInteractionListenerHandle
            == D2RL::ItemInteractions::InvalidHandle) {
        Context->LogError(
            "MassID: ItemInteractionService listener registration failed.");
        ItemInteractionListenerHandle = D2RL::ItemInteractions::InvalidHandle;
        return false;
    }
    return true;
}

auto InstallChanges() noexcept -> bool {
    if (!Context->InstallInlineHook(
            CainIdentifyCallbackRva,
            CainIdentifyCallbackExpected.data(),
            static_cast<std::uint32_t>(CainIdentifyCallbackExpected.size()),
            HookCainIdentifyCallback,
            &OriginalCainIdentifyCallback)) {
        Context->LogError(
            "MassID: server opcode-0x34 hook installation failed.");
        return false;
    }
    TooltipCallSitesInstalled = InstallVendorTradeTooltipCallSites();
    if (!TooltipCallSitesInstalled) {
        Context->LogError(
            "MassID: vendor/trade tooltip fallback call-sites are unavailable.");
        return false;
    }
    return RegisterTooltipListener()
        && RegisterItemInteractionListener();
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[896]{};
    std::snprintf(
        message,
        sizeof(message),
        "MassID 2.1.0: enabled=%s; freeIdentification=%s; rightClickMassIdentify=%s; includeCube=%s; includePersonalStash=%s; includeSharedStash=%s; diagnostics=%s; itemInteraction=%s; sharedTooltip=%s; vendorTradeFallback=%s; serverTransaction=%s; itemEvents=%llu; sdkConsumed=%llu; fallbackGestures=%llu; uiContextProbes=%llu; gestures=%llu; sent=%llu; accepted=%llu; rejected=%llu; identified=%llu; chargesConsumed=%llu.",
        Settings.enabled ? "true" : "false",
        Settings.freeIdentification ? "true" : "false",
        Settings.rightClickMassIdentify ? "true" : "false",
        Settings.targets.includeCube ? "true" : "false",
        Settings.targets.includePersonalStash ? "true" : "false",
        Settings.targets.includeSharedStash ? "true" : "false",
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        ItemInteractionListenerHandle
                != D2RL::ItemInteractions::InvalidHandle
            ? "registered" : "inactive",
        TooltipListenerHandle != D2RL::SharedEvents::InvalidHandle
            ? "registered" : "inactive",
        GameMessageHook.load(std::memory_order_acquire) != nullptr
            ? "installed" : "pending",
        OriginalCainIdentifyCallback != nullptr ? "installed" : "inactive",
        static_cast<unsigned long long>(
            ItemInteractionsObserved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ItemInteractionsConsumed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            VendorTradeFallbacks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            UiContextProbesObserved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            GesturesObserved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RequestsSent.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RequestsAccepted.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RequestsRejected.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ItemsIdentified.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ChargesConsumed.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ClearNativeBindings() noexcept {
    SynchronizeQuantity = nullptr;
    IdentifyItem = nullptr;
    CheckState = nullptr;
    GetUnitStat = nullptr;
    SetItemFlag = nullptr;
    CheckItemFlag = nullptr;
    GetCorpseUnitId = nullptr;
    GetNextCorpse = nullptr;
    GetFirstCorpse = nullptr;
    GetInventoryOwnerId = nullptr;
    GetNextItem = nullptr;
    GetFirstItem = nullptr;
    GetServerUnit = nullptr;
    GetUnitId = nullptr;
    GetItemCode = nullptr;
    GetItemData = nullptr;
    GetUnitType = nullptr;
    GetParentInventory = nullptr;
    GetUnitInventory = nullptr;
    GetLocalizedStringByKey = nullptr;
    IsUiStateOpen = nullptr;
    OriginalCainIdentifyCallback = nullptr;
    SendTwentyOneBytePacket = nullptr;
}

void UnregisterTooltipListener() noexcept {
    if (Context != nullptr && SharedEventService != nullptr
        && SharedEventService->unregisterItemTooltipListener != nullptr
        && TooltipListenerHandle != D2RL::SharedEvents::InvalidHandle) {
        SharedEventService->unregisterItemTooltipListener(
            Context, TooltipListenerHandle);
    }
    TooltipListenerHandle = D2RL::SharedEvents::InvalidHandle;
}

void UnregisterItemInteractionListener() noexcept {
    if (Context != nullptr && ItemInteractionService != nullptr
        && ItemInteractionService->unregisterListener != nullptr
        && ItemInteractionListenerHandle
            != D2RL::ItemInteractions::InvalidHandle) {
        ItemInteractionService->unregisterListener(
            Context, ItemInteractionListenerHandle);
    }
    ItemInteractionListenerHandle = D2RL::ItemInteractions::InvalidHandle;
}

void RemoveGameMessageHook() noexcept {
    const std::lock_guard lock(GameMessageHookMutex);
    const auto hook = GameMessageHook.load(std::memory_order_acquire);
    if (hook == nullptr) return;
    if (!UnhookWindowsHookEx(hook)) {
        if (Context != nullptr) {
            Context->LogError(
                "MassID: UI message hook removal failed; the inactive DLL remains retained for callback safety.");
        }
        return;
    }

    GameMessageHook.store(nullptr, std::memory_order_release);
    while (ActiveMessageHookCallbacks.load(std::memory_order_acquire) != 0) {
        SwitchToThread();
    }
    if (GameMessageHookModule != nullptr) {
        const auto module = GameMessageHookModule;
        GameMessageHookModule = nullptr;
        FreeLibrary(module);
    }
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
        || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }

    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    Settings = {};
    SharedEventService = nullptr;
    DiagnosticsService = nullptr;
    ItemInteractionService = nullptr;
    ItemService = nullptr;
    InventoryService = nullptr;
    LocalizationService = nullptr;
    TooltipListenerHandle = D2RL::SharedEvents::InvalidHandle;
    ItemInteractionListenerHandle = D2RL::ItemInteractions::InvalidHandle;
    TooltipRelayPage = nullptr;
    TooltipCallSitesInstalled = false;
    PluginActive.store(false, std::memory_order_release);
    ResetCounters();
    ClearHoverState();
    ClearNativeBindings();

    if (Base == nullptr) {
        context->LogError("MassID: D2R executable base is unavailable.");
        return false;
    }
    // The SDK-owned TOML is parsed before any service registration or hook.
    if (!ReadConfiguration()) return false;
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    char buildMessage[192]{};
    std::snprintf(buildMessage, sizeof(buildMessage),
        "MassID: observed D2R build-name=%s; validating the complete native fingerprint.",
        runtimeBuild && runtimeBuild[0] != '\0' ? runtimeBuild : "unknown");
    context->LogInfo(buildMessage);

    if (Settings.enabled) {
        if (!QueryRequiredServices()) return false;
        if (!ValidateRuntime()) {
            context->LogError(
                "MassID: native fingerprint validation failed; no hook installation was attempted.");
            return false;
        }
        BindNativeFunctions();
        if (!InstallChanges()) return false;
        PluginActive.store(true, std::memory_order_release);
        if (!TryInstallGameMessageHook()) {
            context->LogWarn(
                "MassID: UI input capture is pending until an Identify Tome tooltip is rendered.");
        }
    }

    if (!context->RegisterConsoleCommand(
            "mass-id",
            Status,
            "Show MassID settings, SDK services, hooks, and counters.")) {
        context->LogWarn(
            "MassID: status command could not be registered.");
    }

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "MassID 2.1.0 by RuffnecKk loaded: enabled=%s; inventory=always; cube=%s; personal stash=%s; shared stash=%s; freeIdentification=%s; rightClickMassIdentify=%s; diagnostics=%s; input=PluginSDK v4 ItemInteractionService v1 plus vendor/trade fallback; tooltip=%s; native hooks=%s.",
        Settings.enabled ? "true" : "false",
        Settings.targets.includeCube ? "enabled" : "disabled",
        Settings.targets.includePersonalStash ? "enabled" : "disabled",
        Settings.targets.includeSharedStash ? "enabled" : "disabled",
        Settings.freeIdentification ? "true" : "false",
        Settings.rightClickMassIdentify ? "true" : "false",
        Settings.diagnosticsEnabled ? "enabled" : "disabled",
        ShouldShowMassIdentifyTooltip(
                Settings.enabled, Settings.rightClickMassIdentify)
            ? "gray Shift-right-click hint enabled"
            : "MassID hint omitted; native right-click hint retained",
        Settings.enabled
            ? "vendor/trade fallback calls and authoritative opcode-0x34 transaction"
            : "none");
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    PluginActive.store(false, std::memory_order_release);
    UnregisterItemInteractionListener();
    UnregisterTooltipListener();
    RemoveGameMessageHook();
    ClearHoverState();
    ClearNativeBindings();
    TooltipCallSitesInstalled = false;
    TooltipRelayPage = nullptr;
    LocalizationService = nullptr;
    InventoryService = nullptr;
    ItemService = nullptr;
    ItemInteractionService = nullptr;
    DiagnosticsService = nullptr;
    SharedEventService = nullptr;
    Settings = {};
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::MassIdentify
