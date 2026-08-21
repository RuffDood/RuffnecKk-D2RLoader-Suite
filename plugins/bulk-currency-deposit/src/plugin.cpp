#define NOMINMAX
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/diagnostics.h>
#include <D2RLPlugin/input.h>
#include <D2RLPlugin/panels.h>
#include <D2RLPlugin/resources.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/threads.h>

#include "bulk_currency_deposit_policy.hpp"
#include "resource_ids.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
using namespace ruffneckk::bulk_currency_deposit;

constexpr wchar_t ConfigFileName[] = L"ruffneckk-bulk-currency-deposit.toml";
constexpr std::uint64_t RequestLifetimeMs = 500;
constexpr std::uint32_t DispatchTimerPeriodMs = 25;
constexpr std::uint32_t CallbackRundownTimeoutMs = 3000;
constexpr std::size_t MaximumInventoryTraversal = 2048;
constexpr std::int32_t StashInterfaceState = 0x18;
constexpr std::size_t WidgetVisibleOffset = 0x51;

constexpr char ButtonLayoutVirtualPath[] =
    "data/global/ui/layouts/bulk-currency-deposit/inventory-button.json";
constexpr char ButtonMoldVirtualPath[] =
    "data/hd/global/ui/d2rloader/bulk-currency-deposit/button-mold.sprite";
constexpr char ButtonMoldLowendVirtualPath[] =
    "data/hd/global/ui/d2rloader/bulk-currency-deposit/button-mold.lowend.sprite";
constexpr char DepositButtonVirtualPath[] =
    "data/hd/global/ui/d2rloader/bulk-currency-deposit/deposit-button.sprite";
constexpr char DepositButtonLowendVirtualPath[] =
    "data/hd/global/ui/d2rloader/bulk-currency-deposit/deposit-button.lowend.sprite";
constexpr char ButtonChildLocalId[] = "inventory-button";

constexpr char DefaultConfig[] = R"toml(# Bulk Currency Deposit
# Auto transfers stackable currency items through the active mod's native Advanced Stash routing.

[deposit]
# Master switch. false disables the Controls action, button, resources and deposit logic.
enabled = true

# Adds an optional button to the Inventory panel. Disabled by default
# so the plugin does not add a control to layouts unless the player requests it.
inventory_button_enabled = false

# Delay between native transfers. Keep the default unless troubleshooting.
# Values from 50 to 1000 milliseconds are accepted; higher values only make
# the batch slower. The tested player-friendly default is 100 milliseconds.
item_delay_ms = 100

# Empty means every item accepted by the native Advanced Stash registry.
# A non-empty list narrows the candidates to these one-to-four-character item
# codes. Examples: ["r01", "r33"] for El and Zod, or ["gpv", "gpy"] for
# perfect amethyst and perfect topaz. Codes are case-sensitive.
include_item_codes = []

# Exclusions are applied after the optional include list. Example:
# exclude_item_codes = ["r33", "gpv"]
exclude_item_codes = []

[button]
# Inventory-layout coordinates for the optional button. The default places it
# directly below Dimentio's standard Charm Inventory button. Adjust these two
# values if another mod or plugin already uses this location.
x = 3
y = 813
)toml";

constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t IsUiStateOpenRva = 0x0CE500;
constexpr std::uintptr_t CanDepositToAdvancedStashRva = 0x15A0B0;
constexpr std::uintptr_t TransferItemToInventoryPageRva = 0x15F8B0;
constexpr std::uintptr_t FinishInventoryInteractionRva = 0x1A0780;
constexpr std::uintptr_t IsItemInteractionBlockedRva = 0x1C7360;
constexpr std::uintptr_t GetUnitIdRva = 0x34A330;
constexpr std::uintptr_t GetUnitInventoryRva = 0x34A360;
constexpr std::uintptr_t GetItemDataRva = 0x34A500;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t GetItemCodeRva = 0x36EF50;
constexpr std::uintptr_t GetFirstItemRva = 0x388C10;
constexpr std::uintptr_t GetNextItemRva = 0x38ABA0;
constexpr std::uintptr_t GetParentInventoryRva = 0x38AC50;
constexpr std::uintptr_t GetAdvancedStashDestinationRva = 0x46DA50;
constexpr std::uintptr_t FindTopLevelPanelRva = 0x846170;

constexpr std::array<std::uint8_t, 32> GetLocalDataContextExpected{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x8B, 0x05, 0x76, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 32> GetLocalPlayerExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
    0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C,
    0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19,
};
constexpr std::array<std::uint8_t, 15> IsUiStateOpenExpected{
    0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8,
    0x95, 0x02, 0x0F, 0xB6, 0x04, 0x08, 0xC3,
};
constexpr std::array<std::uint8_t, 32> CanDepositToAdvancedStashExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0xB9, 0x18, 0x00, 0x00, 0x00, 0xE8, 0x3D,
    0x44, 0xF7, 0xFF, 0x84, 0xC0, 0x74, 0x3E, 0xE8,
    0xB4, 0x12, 0xF3, 0xFF, 0x0F, 0xB6, 0xC8, 0xBA,
};
constexpr std::array<std::uint8_t, 32> TransferItemToInventoryPageExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x08, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
    0xF8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xF5,
};
constexpr std::array<std::uint8_t, 32> FinishInventoryInteractionExpected{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC,
    0x00, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x2D,
    0xAB, 0x82, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,
};
constexpr std::array<std::uint8_t, 32> IsItemInteractionBlockedExpected{
    0x48, 0x83, 0xEC, 0x28, 0xBA, 0x02, 0x00, 0x00,
    0x00, 0xE8, 0xA2, 0xE4, 0x12, 0x00, 0x48, 0x85,
    0xC0, 0x75, 0x29, 0xE8, 0x58, 0x3F, 0xEC, 0xFF,
    0x8B, 0xC8, 0xE8, 0x01, 0x31, 0xED, 0xFF, 0x48,
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
constexpr std::array<std::uint8_t, 32> GetAdvancedStashDestinationExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x85,
    0xC9, 0x75, 0x1B, 0x88, 0x4C, 0x24, 0x30, 0x48,
    0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xC7, 0xF3, 0xFF,
    0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0,
};
constexpr std::array<std::uint8_t, 22> FindTopLevelPanelExpected{
    0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xF6, 0x9F,
    0xBF, 0x02, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0xDD,
    0x95, 0x05, 0x00, 0x33, 0xC0, 0xC3,
};

using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t) noexcept;
using IsUiStateOpenFn = std::int32_t(__fastcall*)(std::int32_t) noexcept;
using CanDepositToAdvancedStashFn = bool(__fastcall*)(void*) noexcept;
using TransferItemToInventoryPageFn = bool(__fastcall*)(
    void*, void*, std::uint8_t, std::uint8_t, bool, void*) noexcept;
using FinishInventoryInteractionFn = void(__fastcall*)(
    std::int32_t, void*, std::int32_t, std::int32_t, bool) noexcept;
using IsItemInteractionBlockedFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetItemDataFn = void*(__fastcall*)(void*) noexcept;
using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetItemCodeFn = std::uint32_t(__fastcall*)(void*) noexcept;
using GetFirstItemFn = void*(__fastcall*)(void*) noexcept;
using GetNextItemFn = void*(__fastcall*)(void*) noexcept;
using GetParentInventoryFn = void*(__fastcall*)(void*) noexcept;
using GetAdvancedStashDestinationFn = void*(__fastcall*)(void*) noexcept;
using FindTopLevelPanelFn = void*(__fastcall*)(const char*) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};

GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
IsUiStateOpenFn IsUiStateOpen{};
CanDepositToAdvancedStashFn CanDepositToAdvancedStash{};
TransferItemToInventoryPageFn TransferItemToInventoryPage{};
FinishInventoryInteractionFn FinishInventoryInteraction{};
IsItemInteractionBlockedFn IsItemInteractionBlocked{};
GetUnitIdFn GetUnitId{};
GetUnitInventoryFn GetUnitInventory{};
GetItemDataFn GetItemData{};
GetUnitTypeFn GetUnitType{};
GetItemCodeFn GetItemCode{};
GetFirstItemFn GetFirstItem{};
GetNextItemFn GetNextItem{};
GetParentInventoryFn GetParentInventory{};
GetAdvancedStashDestinationFn GetAdvancedStashDestination{};
FindTopLevelPanelFn FindTopLevelPanel{};

const D2RL::SharedEventServiceV1* SharedEventService{};
const D2RL::PanelServiceV1* PanelService{};
const D2RL::ResourceServiceV1* ResourceService{};
const D2RL::InputServiceV1* InputService{};
const D2RL::ThreadServiceV1* ThreadService{};
const D2RL::DiagnosticsServiceV1* DiagnosticsService{};
std::atomic<D2RL::Input::ActionHandle> DepositAction{
    D2RL::Input::InvalidHandle};
D2RL::SharedEvents::ListenerHandle ButtonMessageListener{
    D2RL::SharedEvents::InvalidHandle};
D2RL::Panels::ChildLayoutHandle ButtonChildLayout{
    D2RL::Panels::InvalidChildLayoutHandle};
D2RL::Resources::RegistrationHandle ButtonLayoutResource{
    D2RL::Resources::InvalidHandle};
D2RL::Resources::RegistrationHandle ButtonMoldResource{
    D2RL::Resources::InvalidHandle};
D2RL::Resources::RegistrationHandle ButtonMoldLowendResource{
    D2RL::Resources::InvalidHandle};
D2RL::Resources::RegistrationHandle DepositButtonResource{
    D2RL::Resources::InvalidHandle};
D2RL::Resources::RegistrationHandle DepositButtonLowendResource{
    D2RL::Resources::InvalidHandle};

HANDLE InputThread{};
HANDLE InputStopEvent{};
std::atomic_bool InputThreadReady{};
std::atomic_bool InputThreadFailed{};
std::atomic_bool InputStopping{};
std::atomic_bool UiDispatchReady{};
BindingCaptureSet CapturedInputBindings{};
CallbackRundownState CallbackRundown{};
std::atomic_bool InitialUiWorkPending{};
std::atomic<std::uint64_t> RequestedAt{};

struct Candidate {
    std::uint32_t guid{};
    std::uint32_t itemCode{};
};

std::deque<Candidate> PendingItems;
std::atomic<std::uint64_t> PendingItemCount{};
std::atomic_bool BatchActive{};
std::atomic_bool CancelRequested{};
std::atomic_bool StepUiWorkPending{};
std::atomic<std::uint64_t> NextStepAt{};
std::uint64_t BatchSequence{};
std::uint64_t BatchInitialCount{};
std::uint64_t BatchTransferred{};
std::uint64_t BatchFailed{};
std::uint64_t BatchSkipped{};

std::atomic<std::uint64_t> InputRequests{};
std::atomic<std::uint64_t> CoalescedRequests{};
std::atomic<std::uint64_t> RefusedRequests{};
std::atomic<std::uint64_t> StaleRequests{};
std::atomic<std::uint64_t> NoCandidateRequests{};
std::atomic<std::uint64_t> BatchesStarted{};
std::atomic<std::uint64_t> BatchesCompleted{};
std::atomic<std::uint64_t> BatchesCancelled{};
std::atomic<std::uint64_t> ItemsQueued{};
std::atomic<std::uint64_t> ItemsTransferred{};
std::atomic<std::uint64_t> ItemsFailed{};
std::atomic<std::uint64_t> ItemsSkipped{};
std::atomic<std::uint64_t> InputFailures{};
std::atomic<std::uint64_t> ButtonRequests{};

class CallbackGuard {
public:
    CallbackGuard() noexcept
        : admitted_(CallbackRundown.Enter()) {}

    ~CallbackGuard() noexcept {
        CallbackRundown.Leave();
    }

    bool CanProcess() const noexcept {
        return admitted_ && CallbackRundown.CanProcess();
    }

private:
    bool admitted_{};
};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "bulk-currency-deposit",
    .name = "Bulk Currency Deposit",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description = "Auto transfers all your stackable currency items into their respective stash slots.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

template<std::size_t Size>
bool Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept {
    return Base
        && std::memcmp(Base + rva, expected.data(), expected.size()) == 0;
}

bool IsExecutableAddress(const void* address) noexcept {
    if (!address) return false;
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

bool QueryDiagnosticsService() noexcept {
    const auto result = Context->QueryService(
        D2RL::ServiceId::Diagnostics,
        D2RL::DiagnosticsServiceV1Version,
        &DiagnosticsService);
    if (result != D2RL::ServiceQueryResult::Success) {
        DiagnosticsService = nullptr;
        Context->LogWarn(
            "BulkCurrencyDeposit: DiagnosticsService v1 is unavailable; composable UI-state validation requires the strict vanilla signature.");
        return true;
    }
    if (!D2RL::HasDiagnosticsServiceV1Field(
            DiagnosticsService,
            D2RL::DiagnosticsServiceV1RequiredSize)
            || DiagnosticsService->queryHookStatus == nullptr) {
        Context->LogError(
            "BulkCurrencyDeposit: DiagnosticsService v1 returned an invalid contract.");
        DiagnosticsService = nullptr;
        return false;
    }
    return true;
}

bool ValidateUiStateEntry() noexcept {
    if (!DiagnosticsService) {
        if (AcceptUiStateEntry(
                UiStateEntryStatus::Unchanged,
                Matches(IsUiStateOpenRva, IsUiStateOpenExpected),
                false,
                0,
                {})) {
            return true;
        }
        Context->LogError(
            "BulkCurrencyDeposit: UI_IsStateOpen differs from its vanilla signature and no DiagnosticsService owner proof is available.");
        return false;
    }

    const D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .flags = 0,
        .rva = IsUiStateOpenRva,
        .expected = IsUiStateOpenExpected.data(),
        .expectedSize = static_cast<std::uint32_t>(
            IsUiStateOpenExpected.size()),
        .reserved = 0,
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    const auto result = DiagnosticsService->queryHookStatus(
        Context, &query, &status);
    if (result != D2RL::Diagnostics::Result::Success
            || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize) {
        Context->LogError(
            "BulkCurrencyDeposit: DiagnosticsService could not validate UI_IsStateOpen.");
        return false;
    }
    if (status.state == D2RL::Diagnostics::ModificationState::Unchanged) {
        if (AcceptUiStateEntry(
                UiStateEntryStatus::Unchanged,
                Matches(IsUiStateOpenRva, IsUiStateOpenExpected),
                false,
                status.ownerCount,
                {})) {
            return true;
        }
        Context->LogError(
            "BulkCurrencyDeposit: Diagnostics reported UI_IsStateOpen unchanged but its vanilla signature does not match.");
        return false;
    }

    const auto ownerLength = std::find(
        std::begin(status.ownerPluginId),
        std::end(status.ownerPluginId),
        '\0') - std::begin(status.ownerPluginId);
    const std::string_view owner{
        status.ownerPluginId,
        static_cast<std::size_t>(ownerLength)};
    const auto policyStatus =
        status.state == D2RL::Diagnostics::ModificationState::Tracked
            && status.kind
                == D2RL::Diagnostics::ModificationKind::InlineHook
        ? UiStateEntryStatus::TrackedInlineHook
        : UiStateEntryStatus::Other;
    if (AcceptUiStateEntry(
            policyStatus,
            false,
            IsExecutableAddress(Base + IsUiStateOpenRva),
            status.ownerCount,
            owner)) {
        return true;
    }

    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "BulkCurrencyDeposit: UI_IsStateOpen ownership refused (state=%u, kind=%u, owners=%u, owner=%.*s).",
        static_cast<unsigned>(status.state),
        static_cast<unsigned>(status.kind),
        status.ownerCount,
        static_cast<int>(owner.size()),
        owner.data());
    Context->LogError(message);
    return false;
}

bool ValidateRuntime() noexcept {
    bool valid = Base != nullptr;
    const auto check = [&valid](
            std::uintptr_t rva,
            const auto& expected,
            const char* label) noexcept {
        if (Matches(rva, expected)) return;
        valid = false;
        if (Context) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "BulkCurrencyDeposit: signature mismatch for %s at RVA 0x%llX.",
                label,
                static_cast<unsigned long long>(rva));
            Context->LogError(message);
        }
    };

    check(GetLocalDataContextRva, GetLocalDataContextExpected,
        "CLIENT_GetLocalDataContext");
    check(GetLocalPlayerRva, GetLocalPlayerExpected,
        "CLIENT_GetLocalPlayer");
    check(CanDepositToAdvancedStashRva, CanDepositToAdvancedStashExpected,
        "UI_CanDepositToAdvancedStash");
    check(TransferItemToInventoryPageRva,
        TransferItemToInventoryPageExpected,
        "CLIENT_TransferItemToInventoryPage");
    check(FinishInventoryInteractionRva, FinishInventoryInteractionExpected,
        "UI_FinishInventoryInteraction");
    check(IsItemInteractionBlockedRva, IsItemInteractionBlockedExpected,
        "UI_IsItemInteractionBlocked");
    check(GetUnitIdRva, GetUnitIdExpected, "UNITS_GetUnitId");
    check(GetUnitInventoryRva, GetUnitInventoryExpected,
        "UNITS_GetInventory");
    check(GetItemDataRva, GetItemDataExpected, "UNITS_GetItemData");
    check(GetUnitTypeRva, GetUnitTypeExpected, "UNITS_GetUnitType");
    check(GetItemCodeRva, GetItemCodeExpected, "ITEMS_GetItemCode");
    check(GetFirstItemRva, GetFirstItemExpected,
        "INVENTORY_GetFirstItem");
    check(GetNextItemRva, GetNextItemExpected,
        "INVENTORY_GetNextItem");
    check(GetParentInventoryRva, GetParentInventoryExpected,
        "INVENTORY_GetParentInventory");
    check(GetAdvancedStashDestinationRva,
        GetAdvancedStashDestinationExpected,
        "CLIENT_GetAdvancedStashDestination");
    check(FindTopLevelPanelRva, FindTopLevelPanelExpected,
        "UI_FindTopLevelPanel");

    if (!ValidateUiStateEntry()) valid = false;
    return valid;
}

std::vector<std::filesystem::path> ConfigCandidates() {
    std::filesystem::path activeModConfigDirectory;
    std::filesystem::path scopeConfigDirectory;
    if (Context && Context->activeMod && Context->activeMod[0] != '\0'
            && Context->modSupportDirectory
            && Context->modSupportDirectory[0] != L'\0') {
        activeModConfigDirectory =
            std::filesystem::path(Context->modSupportDirectory) / L"config";
    }
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        scopeConfigDirectory =
            std::filesystem::path(Context->pluginConfigPath).parent_path();
    }
    std::error_code currentPathError;
    const auto currentPath = std::filesystem::current_path(currentPathError);
    const auto globalConfigDirectory = currentPathError
        ? std::filesystem::path{}
        : currentPath / L"d2rloader" / L"config";
    return BuildConfigCandidates(
        activeModConfigDirectory,
        scopeConfigDirectory,
        globalConfigDirectory,
        ConfigFileName);
}

bool LoadConfig() noexcept {
    Settings = {};
    LoadedConfigPath = "embedded defaults";
    const auto candidates = ConfigCandidates();
    for (const auto& path : candidates) {
        std::error_code statusError;
        if (!std::filesystem::is_regular_file(path, statusError)) continue;
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("configuration file cannot be opened");
            }
            const std::string text{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            Config parsed{};
            std::string error;
            if (!ParseToml(text, parsed, error)) {
                throw std::invalid_argument(error);
            }
            Settings = std::move(parsed);
            LoadedConfigPath = path.string();
            return true;
        } catch (const std::exception& exception) {
            if (Context) {
                const auto message = std::string("BulkCurrencyDeposit: invalid ")
                    + path.string() + " (" + exception.what() + ").";
                Context->LogError(message.c_str());
            }
            return false;
        }
    }

    std::filesystem::path materializedPath;
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        materializedPath = std::filesystem::path(
            Context->pluginConfigPath).parent_path() / ConfigFileName;
    } else if (!candidates.empty()) {
        materializedPath = candidates.front();
    }
    if (!materializedPath.empty()) {
        try {
            std::error_code directoryError;
            std::filesystem::create_directories(
                materializedPath.parent_path(), directoryError);
            if (!directoryError) {
                const auto handle = CreateFileW(
                    materializedPath.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
                if (handle != INVALID_HANDLE_VALUE) {
                    DWORD written{};
                    const auto writeOk = WriteFile(
                        handle,
                        DefaultConfig,
                        static_cast<DWORD>(sizeof(DefaultConfig) - 1),
                        &written,
                        nullptr) != FALSE
                        && written == sizeof(DefaultConfig) - 1;
                    CloseHandle(handle);
                    if (!writeOk) {
                        (void)DeleteFileW(materializedPath.c_str());
                    }
                    if (writeOk) {
                        LoadedConfigPath = materializedPath.string();
                        if (Context) {
                            const auto message = std::string(
                                "BulkCurrencyDeposit: created default configuration at ")
                                + LoadedConfigPath + ".";
                            Context->LogInfo(message.c_str());
                        }
                        return true;
                    }
                }
            }
        } catch (...) {
        }
    }
    if (Context) {
        Context->LogWarn(
            "BulkCurrencyDeposit: no TOML was found or created; embedded defaults are active.");
    }
    return true;
}

bool QueryInputService() noexcept {
    if (Context->QueryService(
            D2RL::ServiceId::Input,
            D2RL::InputServiceV1Version,
            &InputService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInputServiceV1Field(
            InputService, D2RL::InputServiceV1RequiredSize)
        || InputService->registerAction == nullptr
        || InputService->unregisterAction == nullptr) {
        Context->LogError(
            "BulkCurrencyDeposit: D2RLoader InputService v1 is unavailable.");
        InputService = nullptr;
        return false;
    }
    return true;
}

bool QueryThreadService() noexcept {
    if (Context->QueryService(
            D2RL::ServiceId::Thread,
            D2RL::ThreadServiceV1Version,
            &ThreadService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(
            ThreadService, D2RL::ThreadServiceV1RequiredSize)
        || ThreadService->runOnUiThread == nullptr) {
        Context->LogError(
            "BulkCurrencyDeposit: D2RLoader ThreadService v1 is unavailable.");
        ThreadService = nullptr;
        return false;
    }
    return true;
}

bool QueryButtonServices() noexcept {
    if (!Settings.inventoryButtonEnabled) return true;
    if (Context->QueryService(
            D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version,
            &SharedEventService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasSharedEventServiceV1Field(
            SharedEventService, D2RL::SharedEventServiceV1RequiredSize)
        || SharedEventService->registerUiMessageListener == nullptr
        || SharedEventService->unregisterUiMessageListener == nullptr) {
        Context->LogError(
            "BulkCurrencyDeposit: D2RLoader SharedEvent service v1 is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Panel,
            D2RL::PanelServiceV1Version,
            &PanelService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasPanelServiceV1Field(
            PanelService, D2RL::PanelServiceV1RequiredSize)
        || PanelService->registerChildLayout == nullptr
        || PanelService->unregisterChildLayout == nullptr) {
        Context->LogError(
            "BulkCurrencyDeposit: D2RLoader Panel service v1 is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Resource,
            D2RL::ResourceServiceV1Version,
            &ResourceService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasResourceServiceV1Field(
            ResourceService, D2RL::ResourceServiceV1RequiredSize)
        || ResourceService->registerResource == nullptr
        || ResourceService->unregisterResource == nullptr) {
        Context->LogError(
            "BulkCurrencyDeposit: D2RLoader Resource service v1 is unavailable.");
        return false;
    }
    return true;
}

bool LoadEmbeddedResource(
        std::uint16_t resourceId,
        std::vector<std::uint8_t>& bytes) noexcept {
    const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
    const auto resource = FindResourceW(
        module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!resource) return false;
    const auto size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* data = loaded
        ? static_cast<const std::uint8_t*>(LockResource(loaded))
        : nullptr;
    if (!data || size == 0) return false;
    try {
        bytes.assign(data, data + size);
        return true;
    } catch (...) {
        return false;
    }
}

bool RegisterResource(
        const char* path,
        const void* bytes,
        std::uint64_t byteCount,
        D2RL::Resources::RegistrationHandle& handle) noexcept {
    const D2RL::Resources::ResourceRegistration registration{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags = 0,
        .path = path,
        .bytes = bytes,
        .byteCount = byteCount,
    };
    const auto result = ResourceService->registerResource(
        Context, &registration, &handle);
    return result == D2RL::Resources::Result::Success
        && handle != D2RL::Resources::InvalidHandle;
}

bool UnregisterOwnedButton() noexcept {
    if (PanelService && Context
            && ButtonChildLayout != D2RL::Panels::InvalidChildLayoutHandle) {
        const auto result = PanelService->unregisterChildLayout(
            Context, ButtonChildLayout);
        if (result != D2RL::Panels::Result::Success
                && result != D2RL::Panels::Result::NotFound
                && result != D2RL::Panels::Result::StaleHandle) {
            Context->LogError(
                "BulkCurrencyDeposit: Inventory child layout removal failed; SDK owner cleanup will complete during unload.");
            return false;
        }
    }
    ButtonChildLayout = D2RL::Panels::InvalidChildLayoutHandle;

    const auto unregisterResource = [](auto& handle) noexcept -> bool {
        if (ResourceService && Context
                && handle != D2RL::Resources::InvalidHandle) {
            const auto result = ResourceService->unregisterResource(
                Context, handle);
            if (result != D2RL::Resources::Result::Success
                    && result != D2RL::Resources::Result::NotFound
                    && result != D2RL::Resources::Result::StaleHandle) {
                Context->LogError(
                    "BulkCurrencyDeposit: Inventory button resource removal failed; SDK owner cleanup will complete during unload.");
                return false;
            }
        }
        handle = D2RL::Resources::InvalidHandle;
        return true;
    };
    return unregisterResource(ButtonLayoutResource)
        && unregisterResource(DepositButtonLowendResource)
        && unregisterResource(DepositButtonResource)
        && unregisterResource(ButtonMoldLowendResource)
        && unregisterResource(ButtonMoldResource);
}

bool RegisterOwnedButton() noexcept {
    std::vector<std::uint8_t> mold;
    std::vector<std::uint8_t> moldLowend;
    std::vector<std::uint8_t> button;
    std::vector<std::uint8_t> buttonLowend;
    if (!LoadEmbeddedResource(
            BULK_CURRENCY_DEPOSIT_BUTTON_MOLD_RESOURCE_ID, mold)
        || !LoadEmbeddedResource(
            BULK_CURRENCY_DEPOSIT_BUTTON_MOLD_LOWEND_RESOURCE_ID,
            moldLowend)
        || !LoadEmbeddedResource(
            BULK_CURRENCY_DEPOSIT_BUTTON_RESOURCE_ID, button)
        || !LoadEmbeddedResource(
            BULK_CURRENCY_DEPOSIT_BUTTON_LOWEND_RESOURCE_ID,
            buttonLowend)) {
        Context->LogError(
            "BulkCurrencyDeposit: embedded Inventory button sprites are unavailable.");
        return false;
    }
    const auto layout = BuildButtonLayoutJson(Settings.button);
    if (!RegisterResource(
            ButtonMoldVirtualPath, mold.data(), mold.size(), ButtonMoldResource)
        || !RegisterResource(
            ButtonMoldLowendVirtualPath,
            moldLowend.data(),
            moldLowend.size(),
            ButtonMoldLowendResource)
        || !RegisterResource(
            DepositButtonVirtualPath,
            button.data(),
            button.size(),
            DepositButtonResource)
        || !RegisterResource(
            DepositButtonLowendVirtualPath,
            buttonLowend.data(),
            buttonLowend.size(),
            DepositButtonLowendResource)
        || !RegisterResource(
            ButtonLayoutVirtualPath,
            layout.data(),
            layout.size(),
            ButtonLayoutResource)) {
        Context->LogError(
            "BulkCurrencyDeposit: plugin-owned Inventory button resource registration failed.");
        (void)UnregisterOwnedButton();
        return false;
    }

    const D2RL::Panels::ChildLayoutRegistration registration{
        .structSize = D2RL::Panels::ChildLayoutRegistrationSize,
        .flags = D2RL::Panels::ChildLayoutFlags::KeyboardMouseOnly,
        .stockPanel = D2RL::Panels::StockPanel::PlayerInventory,
        .reserved = 0,
        .localId = ButtonChildLocalId,
    };
    const auto result = PanelService->registerChildLayout(
        Context, &registration, &ButtonChildLayout);
    if (result != D2RL::Panels::Result::Success
            || ButtonChildLayout == D2RL::Panels::InvalidChildLayoutHandle) {
        Context->LogError(
            "BulkCurrencyDeposit: Inventory child layout registration failed.");
        (void)UnregisterOwnedButton();
        return false;
    }
    return true;
}

void ResetCountersAndBatch() noexcept {
    PendingItems.clear();
    PendingItemCount.store(0, std::memory_order_relaxed);
    BatchActive.store(false, std::memory_order_relaxed);
    CancelRequested.store(false, std::memory_order_relaxed);
    StepUiWorkPending.store(false, std::memory_order_relaxed);
    InitialUiWorkPending.store(false, std::memory_order_relaxed);
    NextStepAt.store(0, std::memory_order_relaxed);
    BatchSequence = 0;
    BatchInitialCount = 0;
    BatchTransferred = 0;
    BatchFailed = 0;
    BatchSkipped = 0;
    InputRequests.store(0, std::memory_order_relaxed);
    CoalescedRequests.store(0, std::memory_order_relaxed);
    RefusedRequests.store(0, std::memory_order_relaxed);
    StaleRequests.store(0, std::memory_order_relaxed);
    NoCandidateRequests.store(0, std::memory_order_relaxed);
    BatchesStarted.store(0, std::memory_order_relaxed);
    BatchesCompleted.store(0, std::memory_order_relaxed);
    BatchesCancelled.store(0, std::memory_order_relaxed);
    ItemsQueued.store(0, std::memory_order_relaxed);
    ItemsTransferred.store(0, std::memory_order_relaxed);
    ItemsFailed.store(0, std::memory_order_relaxed);
    ItemsSkipped.store(0, std::memory_order_relaxed);
    InputFailures.store(0, std::memory_order_relaxed);
    ButtonRequests.store(0, std::memory_order_relaxed);
}

bool WidgetIsVisible(void* widget) noexcept {
    if (!widget) return false;
    __try {
        return *(static_cast<std::uint8_t*>(widget) + WidgetVisibleOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool KnownInputIsBlocked() noexcept {
    constexpr const char* blockers[]{
        "ChatPanel",
        "TextInputModal",
        "DropGoldModal",
        "ConfirmationModal",
        "ButtonBindingModal",
        "KeyBindingDefaultsModal",
        "AddFriendModal",
        "LootFilterRenameProfileModal",
        "LootFilterExportProfileModal",
        "LootFilterDeleteProfileModal",
        "LootFilterNewProfileModal",
        "LootFilterImportProfileModal",
        "LootFilterRenameRuleModal",
        "LootFilterCopyRuleModal",
        "LootFilterDeleteRuleModal",
    };
    __try {
        for (const auto* name : blockers) {
            if (WidgetIsVisible(FindTopLevelPanel(name))) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    return false;
}

bool TryReadCandidate(
        void* item,
        void* inventory,
        Candidate& candidate,
        bool requireNativeEligibility) noexcept {
    if (!item || !inventory) return false;
    __try {
        if (GetUnitType(item) != static_cast<std::int32_t>(ItemUnitType)
                || IsItemInteractionBlocked(item) != 0
                || GetParentInventory(item) != inventory
                || ReadInventoryPageFromItemData(GetItemData(item))
                    != MainInventoryPage) {
            return false;
        }
        const auto itemCode = GetItemCode(item);
        if (!MatchesItemCodeFilter(Settings, itemCode)
                || (requireNativeEligibility
                    && !CanDepositToAdvancedStash(item))) {
            return false;
        }
        const auto guid = GetUnitId(item);
        if (guid < 0) return false;
        candidate.guid = static_cast<std::uint32_t>(guid);
        candidate.itemCode = itemCode;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* FindCurrentItem(
        void* inventory,
        const Candidate& expected) noexcept {
    if (!inventory) return nullptr;
    __try {
        auto* item = GetFirstItem(inventory);
        std::size_t traversed{};
        while (item && traversed++ < MaximumInventoryTraversal) {
            Candidate current{};
            if (TryReadCandidate(item, inventory, current, true)
                    && current.guid == expected.guid
                    && current.itemCode == expected.itemCode) {
                return item;
            }
            item = GetNextItem(item);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

bool BuildBatch(void* inventory) noexcept {
    PendingItems.clear();
    PendingItemCount.store(0, std::memory_order_release);
    if (!inventory) return false;
    __try {
        auto* item = GetFirstItem(inventory);
        std::size_t traversed{};
        while (item && traversed++ < MaximumInventoryTraversal) {
            Candidate candidate{};
            if (TryReadCandidate(item, inventory, candidate, true)) {
                PendingItems.push_back(candidate);
                PendingItemCount.fetch_add(1, std::memory_order_relaxed);
            }
            item = GetNextItem(item);
        }
        if (item != nullptr) {
            PendingItems.clear();
            PendingItemCount.store(0, std::memory_order_release);
            if (Context) {
                Context->LogError(
                    "BulkCurrencyDeposit: inventory traversal exceeded its safety bound; request refused.");
            }
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        PendingItems.clear();
        PendingItemCount.store(0, std::memory_order_release);
        return false;
    }
    return true;
}

bool TryNativeTransfer(
        void* item,
        void* destination,
        bool& transferred) noexcept {
    std::array<std::uint8_t, 16> placement{};
    __try {
        transferred = TransferItemToInventoryPage(
            item,
            destination,
            AdvancedStashPage,
            MainInventoryPage,
            true,
            placement.data());
        FinishInventoryInteraction(3, nullptr, 0, 0, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        transferred = false;
        return false;
    }
}

void ResetBatchState() noexcept {
    PendingItems.clear();
    PendingItemCount.store(0, std::memory_order_release);
    NextStepAt.store(0, std::memory_order_release);
    StepUiWorkPending.store(false, std::memory_order_release);
    CancelRequested.store(false, std::memory_order_release);
    BatchActive.store(false, std::memory_order_release);
}

void LogBatchSummary(const char* state, const char* reason) noexcept {
    if (!Context) return;
    char message[448]{};
    std::snprintf(
        message,
        sizeof(message),
        "BulkCurrencyDeposit: batch %llu %s; queued=%llu; transferred=%llu; failed=%llu; skipped=%llu; remaining=%llu; reason=%s.",
        static_cast<unsigned long long>(BatchSequence),
        state,
        static_cast<unsigned long long>(BatchInitialCount),
        static_cast<unsigned long long>(BatchTransferred),
        static_cast<unsigned long long>(BatchFailed),
        static_cast<unsigned long long>(BatchSkipped),
        static_cast<unsigned long long>(PendingItems.size()),
        reason ? reason : "none");
    Context->LogInfo(message);
}

void CompleteBatch() noexcept {
    BatchesCompleted.fetch_add(1, std::memory_order_relaxed);
    LogBatchSummary("completed", "queue exhausted");
    ResetBatchState();
}

void CancelBatch(const char* reason) noexcept {
    BatchesCancelled.fetch_add(1, std::memory_order_relaxed);
    LogBatchSummary("cancelled", reason);
    ResetBatchState();
}

void ScheduleNextStep() noexcept {
    NextStepAt.store(
        GetTickCount64() + Settings.itemDelayMs,
        std::memory_order_release);
}

void ProcessNextItem() noexcept {
    StepUiWorkPending.store(false, std::memory_order_release);
    if (!BatchActive.load(std::memory_order_acquire)) return;
    if (CancelRequested.exchange(false, std::memory_order_acq_rel)) {
        CancelBatch("UI-thread handoff failure");
        return;
    }
    if (!IsUiStateOpen(StashInterfaceState)) {
        CancelBatch("stash closed");
        return;
    }

    auto* player = GetLocalPlayer(GetLocalDataContext());
    auto* inventory = player ? GetUnitInventory(player) : nullptr;
    if (!player || !inventory) {
        CancelBatch("local player inventory unavailable");
        return;
    }

    while (!PendingItems.empty()) {
        const auto candidate = PendingItems.front();
        PendingItems.pop_front();
        PendingItemCount.fetch_sub(1, std::memory_order_relaxed);
        auto* item = FindCurrentItem(inventory, candidate);
        if (!item) {
            ++BatchSkipped;
            ItemsSkipped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        void* destination{};
        __try {
            destination = GetAdvancedStashDestination(player);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            destination = nullptr;
        }
        if (!destination) {
            CancelBatch("Advanced Stash destination unavailable");
            return;
        }

        bool transferred{};
        const auto callCompleted = TryNativeTransfer(
            item, destination, transferred);
        if (callCompleted && transferred) {
            ++BatchTransferred;
            ItemsTransferred.fetch_add(1, std::memory_order_relaxed);
        } else {
            ++BatchFailed;
            ItemsFailed.fetch_add(1, std::memory_order_relaxed);
            if (!callCompleted) {
                CancelBatch("native transfer raised an exception");
                return;
            }
        }

        if (PendingItems.empty()) CompleteBatch();
        else ScheduleNextStep();
        return;
    }
    CompleteBatch();
}

void ProcessDepositRequest(std::uint64_t requestedAt) noexcept {
    if (requestedAt == 0) return;
    const auto now = GetTickCount64();
    if (!IsFreshRequest(now, requestedAt, RequestLifetimeMs)) {
        StaleRequests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (BatchActive.load(std::memory_order_acquire)
            || KnownInputIsBlocked()) {
        RefusedRequests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!IsUiStateOpen(StashInterfaceState)) {
        RefusedRequests.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto* player = GetLocalPlayer(GetLocalDataContext());
    auto* inventory = player ? GetUnitInventory(player) : nullptr;
    if (!player || !inventory || !BuildBatch(inventory)) {
        RefusedRequests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (PendingItems.empty()) {
        NoCandidateRequests.fetch_add(1, std::memory_order_relaxed);
        if (Context) {
            Context->LogInfo(
                "BulkCurrencyDeposit: no natively eligible inventory item was found.");
        }
        return;
    }

    ++BatchSequence;
    BatchInitialCount = PendingItems.size();
    BatchTransferred = 0;
    BatchFailed = 0;
    BatchSkipped = 0;
    ItemsQueued.fetch_add(BatchInitialCount, std::memory_order_relaxed);
    BatchesStarted.fetch_add(1, std::memory_order_relaxed);
    BatchActive.store(true, std::memory_order_release);

    if (Context) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "BulkCurrencyDeposit: batch %llu started with %llu native candidate(s).",
            static_cast<unsigned long long>(BatchSequence),
            static_cast<unsigned long long>(BatchInitialCount));
        Context->LogInfo(message);
    }
    ProcessNextItem();
}

void ProcessInitialRequest() noexcept {
    ProcessDepositRequest(RequestedAt.exchange(0, std::memory_order_acq_rel));
}

bool QueueDepositRequest(bool controlsSource) noexcept {
    if (InputStopping.load(std::memory_order_acquire)
            || !UiDispatchReady.load(std::memory_order_acquire)
            || BatchActive.load(std::memory_order_acquire)) {
        RefusedRequests.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const auto now = GetTickCount64();
    std::uint64_t expected{};
    if (!RequestedAt.compare_exchange_strong(
            expected,
            now,
            std::memory_order_acq_rel)) {
        CoalescedRequests.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (controlsSource) {
        InputRequests.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

auto __cdecl OnDepositUiMessage(
        const D2RL::PluginContext*,
        const D2RL::SharedEvents::UiMessageEvent* event,
        void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
    CallbackGuard callbackGuard;
    if (!event
            || event->structSize
                < D2RL::SharedEvents::UiMessageEventRequiredSize
            || !event->target
            || !event->command
            || !event->text
            || !IsDepositUiMessage(
                event->target, event->command, event->text)) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    if (!callbackGuard.CanProcess()) {
        return D2RL::SharedEvents::UiMessageAction::Consume;
    }
    ButtonRequests.fetch_add(1, std::memory_order_relaxed);
    (void)QueueDepositRequest(false);
    // The exact plugin-owned message is private and must never reach the
    // normal PanelManager OpenPanel handler.
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

bool UnregisterButtonListener() noexcept {
    if (Context && SharedEventService
            && ButtonMessageListener != D2RL::SharedEvents::InvalidHandle) {
        const auto result = SharedEventService->unregisterUiMessageListener(
            Context, ButtonMessageListener);
        if (result != D2RL::SharedEvents::Result::Success
                && result != D2RL::SharedEvents::Result::NotFound) {
            Context->LogError(
                "BulkCurrencyDeposit: SDK button listener removal failed; SDK owner cleanup will complete during unload.");
            return false;
        }
    }
    ButtonMessageListener = D2RL::SharedEvents::InvalidHandle;
    return true;
}

bool RegisterButtonListener() noexcept {
    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags = 0,
        .priority = 10'000,
        .reserved = 0,
        .callback = OnDepositUiMessage,
        .userData = nullptr,
    };
    const auto result = SharedEventService->registerUiMessageListener(
        Context, &listener, &ButtonMessageListener);
    if (result != D2RL::SharedEvents::Result::Success
            || ButtonMessageListener == D2RL::SharedEvents::InvalidHandle) {
        ButtonMessageListener = D2RL::SharedEvents::InvalidHandle;
        Context->LogError(
            "BulkCurrencyDeposit: SDK button message listener registration failed.");
        return false;
    }
    return true;
}

// Governed D2R 3.3 evidence places the native Ctrl-click chain on the client
// UI path: 0x2AAAA0 -> CLIENT_TransferItemToInventoryPage (0x15F8B0) ->
// UI_FinishInventoryInteraction (0x1A0780). The transfer helper computes the
// placement and emits packet 0x54; it does not perform the authoritative server
// mutation described by ThreadService's generic native-item guidance.
void __cdecl ProcessInitialRequestOnUiThread(
        const D2RL::PluginContext*,
        void*) noexcept {
    CallbackGuard callbackGuard;
    InitialUiWorkPending.store(false, std::memory_order_release);
    if (!callbackGuard.CanProcess()) {
        RequestedAt.store(0, std::memory_order_release);
        return;
    }
    ProcessInitialRequest();
}

void __cdecl ProcessNextItemOnUiThread(
        const D2RL::PluginContext*,
        void*) noexcept {
    CallbackGuard callbackGuard;
    if (!callbackGuard.CanProcess()) {
        StepUiWorkPending.store(false, std::memory_order_release);
        return;
    }
    ProcessNextItem();
}

D2RL::Input::ActionResult __cdecl OnControlsAction(
        const D2RL::PluginContext*,
        const D2RL::Input::ActionEvent* event,
        void*) noexcept {
    CallbackGuard callbackGuard;
    if (!callbackGuard.CanProcess()) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (!D2RL::Input::HasActionEventField(
            event, D2RL::Input::ActionEventRequiredSize)
            || event->action
                != DepositAction.load(std::memory_order_acquire)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    const auto binding = PackActionBinding(
        static_cast<std::uint32_t>(event->binding.key),
        static_cast<std::uint32_t>(event->binding.modifier));
    if (binding == 0) return D2RL::Input::ActionResult::Ignored;
    if (event->kind == D2RL::Input::ActionEventKind::Released) {
        return CapturedInputBindings.Release(binding)
            ? D2RL::Input::ActionResult::Handled
            : D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }

    if (CapturedInputBindings.Contains(binding)) {
        return D2RL::Input::ActionResult::Handled;
    }
    if (!CapturedInputBindings.Capture(binding)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    const auto accepted = QueueDepositRequest(true);
    if (!accepted) (void)CapturedInputBindings.Release(binding);
    return accepted
        ? D2RL::Input::ActionResult::Handled
        : D2RL::Input::ActionResult::Ignored;
}

bool RegisterControlsAction() noexcept {
    const D2RL::Input::ActionRegistration registration{
        .structSize = D2RL::Input::ActionRegistrationSize,
        .flags = 0,
        .logicalId = "bulk-currency-deposit",
        .displayName = "Bulk Currency Deposit",
        .category = "RuffnecKk Suite",
        .defaultPrimary = {
            D2RL::Input::Key::D,
            D2RL::Input::Modifier::Shift,
        },
        .defaultSecondary = {
            D2RL::Input::Key::None,
            D2RL::Input::Modifier::None,
        },
        .callback = OnControlsAction,
        .userData = nullptr,
    };
    D2RL::Input::ActionHandle action{D2RL::Input::InvalidHandle};
    const auto result = InputService->registerAction(
        Context, &registration, &action);
    if (result == D2RL::Input::Result::Success
            && action != D2RL::Input::InvalidHandle) {
        DepositAction.store(action, std::memory_order_release);
        return true;
    }
    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "BulkCurrencyDeposit: Controls action registration failed with result %u.",
        static_cast<unsigned>(result));
    Context->LogError(message);
    DepositAction.store(D2RL::Input::InvalidHandle, std::memory_order_release);
    return false;
}

void UnregisterControlsAction() noexcept {
    const auto action = DepositAction.exchange(
        D2RL::Input::InvalidHandle,
        std::memory_order_acq_rel);
    if (InputService && Context
            && action != D2RL::Input::InvalidHandle) {
        (void)InputService->unregisterAction(Context, action);
    }
    CapturedInputBindings.Reset();
}

void DispatchPendingControlsRequest() noexcept {
    if (RequestedAt.load(std::memory_order_acquire) == 0
            || InitialUiWorkPending.exchange(
                true, std::memory_order_acq_rel)) {
        return;
    }
    if (!ThreadService
            || ThreadService->runOnUiThread(
                Context,
                ProcessInitialRequestOnUiThread,
                nullptr) != D2RL::Threads::Result::Success) {
        InitialUiWorkPending.store(false, std::memory_order_release);
        RequestedAt.store(0, std::memory_order_release);
        InputFailures.fetch_add(1, std::memory_order_relaxed);
    }
}

void DispatchDueBatchStep() noexcept {
    if (!BatchActive.load(std::memory_order_acquire)) return;
    const auto due = NextStepAt.load(std::memory_order_acquire);
    if (due == 0 || GetTickCount64() < due) return;
    if (StepUiWorkPending.exchange(true, std::memory_order_acq_rel)) return;
    if (!ThreadService
            || ThreadService->runOnUiThread(
                Context,
                ProcessNextItemOnUiThread,
                nullptr) != D2RL::Threads::Result::Success) {
        StepUiWorkPending.store(false, std::memory_order_release);
        CancelRequested.store(true, std::memory_order_release);
        InputFailures.fetch_add(1, std::memory_order_relaxed);
    }
}

DWORD WINAPI InputThreadProc(void* parameter) noexcept {
    const auto module = static_cast<HMODULE>(parameter);
    UiDispatchReady.store(true, std::memory_order_release);
    InputThreadReady.store(true, std::memory_order_release);
    for (;;) {
        const auto wait = WaitForSingleObject(
            InputStopEvent,
            DispatchTimerPeriodMs);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_TIMEOUT) {
            InputThreadFailed.store(true, std::memory_order_release);
            break;
        }
        DispatchPendingControlsRequest();
        DispatchDueBatchStep();
    }
    UiDispatchReady.store(false, std::memory_order_release);
    FreeLibraryAndExitThread(module, 0);
}

bool StartInput() noexcept {
    InputThreadReady.store(false, std::memory_order_relaxed);
    InputThreadFailed.store(false, std::memory_order_relaxed);
    InputStopping.store(false, std::memory_order_release);
    UiDispatchReady.store(false, std::memory_order_relaxed);
    CapturedInputBindings.Reset();
    InitialUiWorkPending.store(false, std::memory_order_relaxed);
    RequestedAt.store(0, std::memory_order_relaxed);
    InputStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!InputStopEvent) return false;

    HMODULE workerModule{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&InputThreadProc),
            &workerModule)) {
        CloseHandle(InputStopEvent);
        InputStopEvent = nullptr;
        return false;
    }
    InputThread = CreateThread(
        nullptr,
        0,
        InputThreadProc,
        workerModule,
        0,
        nullptr);
    if (!InputThread) {
        FreeLibrary(workerModule);
        CloseHandle(InputStopEvent);
        InputStopEvent = nullptr;
        return false;
    }
    for (unsigned attempt = 0;
            attempt < 200
                && !InputThreadReady.load(std::memory_order_acquire);
            ++attempt) {
        Sleep(10);
    }
    return InputThreadReady.load(std::memory_order_acquire)
        && !InputThreadFailed.load(std::memory_order_acquire);
}

bool StopInput() noexcept {
    InputStopping.store(true, std::memory_order_release);
    CancelRequested.store(true, std::memory_order_release);
    if (InputStopEvent) SetEvent(InputStopEvent);
    if (InputThread) {
        const auto wait = WaitForSingleObject(InputThread, 3000);
        if (wait != WAIT_OBJECT_0) {
            if (Context) {
                Context->LogError(
                    "BulkCurrencyDeposit: UI dispatch worker did not stop; its module reference is retained for safety.");
            }
            return false;
        }
        CloseHandle(InputThread);
    }
    InputThread = nullptr;
    if (InputStopEvent) CloseHandle(InputStopEvent);
    InputStopEvent = nullptr;
    UiDispatchReady.store(false, std::memory_order_release);
    return true;
}

HMODULE AcquireTeardownModuleReference() noexcept {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&__ImageBase),
            &module)) {
        if (Context) {
            Context->LogError(
                "BulkCurrencyDeposit: teardown module reference could not be retained.");
        }
        return nullptr;
    }
    return module;
}

bool WaitForCallbackRundown() noexcept {
    const auto deadline = GetTickCount64() + CallbackRundownTimeoutMs;
    while (CallbackRundown.ActiveCount() != 0) {
        if (GetTickCount64() >= deadline) {
            if (Context) {
                Context->LogError(
                    "BulkCurrencyDeposit: SDK callback rundown timed out; module reference and plugin state are retained for safety.");
            }
            return false;
        }
        SwitchToThread();
        Sleep(1);
    }
    return true;
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    CallbackGuard callbackGuard;
    if (!callbackGuard.CanProcess()) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[1024]{};
    std::snprintf(
        message,
        sizeof(message),
        "Bulk Currency Deposit 1.0.0: enabled=%s; Controls=%s; defaultBinding=SHIFT+D; UI=%s; button=%s; buttonPosition=%d,%d; delay=%ums; include=%llu; exclude=%llu; batch=%s; pending=%llu; requests=%llu; buttonRequests=%llu; coalesced=%llu; refused=%llu; stale=%llu; empty=%llu; started=%llu; completed=%llu; cancelled=%llu; queued=%llu; transferred=%llu; failed=%llu; skipped=%llu; dispatchFailures=%llu; TOML=%s.",
        Settings.enabled ? "true" : "false",
        DepositAction.load(std::memory_order_acquire)
                != D2RL::Input::InvalidHandle
            ? "registered"
            : "not-registered",
        !Settings.enabled
            ? "disabled"
            : (UiDispatchReady.load(std::memory_order_acquire)
                ? "ready"
                : "pending"),
        Settings.inventoryButtonEnabled ? "enabled" : "disabled",
        Settings.button.x,
        Settings.button.y,
        Settings.itemDelayMs,
        static_cast<unsigned long long>(Settings.includeItemCodes.size()),
        static_cast<unsigned long long>(Settings.excludeItemCodes.size()),
        BatchActive.load(std::memory_order_acquire) ? "active" : "idle",
        static_cast<unsigned long long>(PendingItemCount.load()),
        static_cast<unsigned long long>(InputRequests.load()),
        static_cast<unsigned long long>(ButtonRequests.load()),
        static_cast<unsigned long long>(CoalescedRequests.load()),
        static_cast<unsigned long long>(RefusedRequests.load()),
        static_cast<unsigned long long>(StaleRequests.load()),
        static_cast<unsigned long long>(NoCandidateRequests.load()),
        static_cast<unsigned long long>(BatchesStarted.load()),
        static_cast<unsigned long long>(BatchesCompleted.load()),
        static_cast<unsigned long long>(BatchesCancelled.load()),
        static_cast<unsigned long long>(ItemsQueued.load()),
        static_cast<unsigned long long>(ItemsTransferred.load()),
        static_cast<unsigned long long>(ItemsFailed.load()),
        static_cast<unsigned long long>(ItemsSkipped.load()),
        static_cast<unsigned long long>(InputFailures.load()),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    InputStopping.store(false, std::memory_order_release);
    CallbackRundown.Reset();
    ResetCountersAndBatch();

    if (!LoadConfig()) return false;
    if (!Settings.enabled) {
        if (!context->RegisterConsoleCommand(
                "bulk-currency-deposit",
                Status,
                "Show Bulk Currency Deposit status and counters.")) {
            context->LogWarn(
                "BulkCurrencyDeposit: optional status command was not registered.");
        }
        context->LogInfo(
            "Bulk Currency Deposit 1.0.0 by RuffnecKk loaded disabled; no Controls action, SDK listeners or resources installed.");
        return true;
    }

    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (!runtimeBuild
            || (std::strcmp(runtimeBuild, "92777") != 0
                && std::strcmp(runtimeBuild, "93847") != 0)) {
        context->LogError(
            "BulkCurrencyDeposit: only D2R builds 92777 and 93847 are supported.");
        return false;
    }
    if (!QueryDiagnosticsService()) return false;
    if (!ValidateRuntime()) {
        context->LogError(
            "BulkCurrencyDeposit: D2R 3.3.93847 runtime validation failed; plugin refused.");
        return false;
    }

    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    IsUiStateOpen = At<IsUiStateOpenFn>(IsUiStateOpenRva);
    CanDepositToAdvancedStash = At<CanDepositToAdvancedStashFn>(
        CanDepositToAdvancedStashRva);
    TransferItemToInventoryPage = At<TransferItemToInventoryPageFn>(
        TransferItemToInventoryPageRva);
    FinishInventoryInteraction = At<FinishInventoryInteractionFn>(
        FinishInventoryInteractionRva);
    IsItemInteractionBlocked = At<IsItemInteractionBlockedFn>(
        IsItemInteractionBlockedRva);
    GetUnitId = At<GetUnitIdFn>(GetUnitIdRva);
    GetUnitInventory = At<GetUnitInventoryFn>(GetUnitInventoryRva);
    GetItemData = At<GetItemDataFn>(GetItemDataRva);
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
    GetFirstItem = At<GetFirstItemFn>(GetFirstItemRva);
    GetNextItem = At<GetNextItemFn>(GetNextItemRva);
    GetParentInventory = At<GetParentInventoryFn>(GetParentInventoryRva);
    GetAdvancedStashDestination = At<GetAdvancedStashDestinationFn>(
        GetAdvancedStashDestinationRva);
    FindTopLevelPanel = At<FindTopLevelPanelFn>(FindTopLevelPanelRva);

    if (!QueryInputService()
            || !QueryThreadService()
            || !QueryButtonServices()) {
        return false;
    }
    if (!StartInput()) {
        (void)StopInput();
        context->LogError(
            "BulkCurrencyDeposit: bounded UI-thread handoff failed.");
        return false;
    }
    if (!RegisterControlsAction()) {
        InputStopping.store(true, std::memory_order_release);
        CallbackRundown.Stop();
        (void)StopInput();
        return false;
    }
    if (Settings.inventoryButtonEnabled
            && (!RegisterButtonListener() || !RegisterOwnedButton())) {
        const auto teardownModule = AcquireTeardownModuleReference();
        InputStopping.store(true, std::memory_order_release);
        CallbackRundown.Stop();
        CancelRequested.store(true, std::memory_order_release);
        UnregisterControlsAction();
        (void)UnregisterOwnedButton();
        (void)UnregisterButtonListener();
        if (!StopInput() || !WaitForCallbackRundown()) return false;
        if (teardownModule) FreeLibrary(teardownModule);
        return false;
    }

    if (!context->RegisterConsoleCommand(
            "bulk-currency-deposit",
            Status,
            "Show Bulk Currency Deposit status and counters.")) {
        context->LogWarn(
            "BulkCurrencyDeposit: optional status command was not registered.");
    }

    char message[640]{};
    std::snprintf(
        message,
        sizeof(message),
        "Bulk Currency Deposit 1.0.0 by RuffnecKk active for D2R %s; Controls action=Bulk Currency Deposit (default SHIFT+D); button=%s at %d,%d; delay=%ums; routing=native Advanced Stash registry; installation=%s; TOML=%s.",
        runtimeBuild,
        Settings.inventoryButtonEnabled ? "enabled" : "disabled",
        Settings.button.x,
        Settings.button.y,
        Settings.itemDelayMs,
        context->loadScope == D2RL::LoadScope::Mod ? "mod-local" : "global",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    const auto teardownModule = AcquireTeardownModuleReference();
    InputStopping.store(true, std::memory_order_release);
    CallbackRundown.Stop();
    CancelRequested.store(true, std::memory_order_release);
    UnregisterControlsAction();
    (void)UnregisterOwnedButton();
    (void)UnregisterButtonListener();
    if (Settings.enabled && !StopInput()) return;
    if (!WaitForCallbackRundown()) return;
    ResetBatchState();
    RequestedAt.store(0, std::memory_order_release);
    GetLocalDataContext = nullptr;
    GetLocalPlayer = nullptr;
    IsUiStateOpen = nullptr;
    CanDepositToAdvancedStash = nullptr;
    TransferItemToInventoryPage = nullptr;
    FinishInventoryInteraction = nullptr;
    IsItemInteractionBlocked = nullptr;
    GetUnitId = nullptr;
    GetUnitInventory = nullptr;
    GetItemData = nullptr;
    GetUnitType = nullptr;
    GetItemCode = nullptr;
    GetFirstItem = nullptr;
    GetNextItem = nullptr;
    GetParentInventory = nullptr;
    GetAdvancedStashDestination = nullptr;
    FindTopLevelPanel = nullptr;
    SharedEventService = nullptr;
    PanelService = nullptr;
    ResourceService = nullptr;
    InputService = nullptr;
    ThreadService = nullptr;
    DiagnosticsService = nullptr;
    Settings = {};
    Base = nullptr;
    Context = nullptr;
    if (teardownModule) FreeLibrary(teardownModule);
}
