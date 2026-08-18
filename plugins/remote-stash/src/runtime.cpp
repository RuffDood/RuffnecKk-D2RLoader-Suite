#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/input.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/panels.h>
#include <D2RLPlugin/resources.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/threads.h>

#include "policy.hpp"
#include "resource_ids.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
using ruffneckk::remote_stash::BuildButtonLayoutJson;
using ruffneckk::remote_stash::ButtonAnchor;
using ruffneckk::remote_stash::ButtonAnchorName;
using ruffneckk::remote_stash::ButtonConfig;
using ruffneckk::remote_stash::ButtonFramesFit;
using ruffneckk::remote_stash::ButtonPlacement;
using ruffneckk::remote_stash::ButtonPlacementName;
using ruffneckk::remote_stash::ExactModifiersMatch;
using ruffneckk::remote_stash::HasUsableSize;
using ruffneckk::remote_stash::HotkeyMode;
using ruffneckk::remote_stash::HotkeyModeName;
using ruffneckk::remote_stash::HotkeyConfig;
using ruffneckk::remote_stash::HotkeyDispatch;
using ruffneckk::remote_stash::IsMouseHotkey;
using ruffneckk::remote_stash::IsRemoteStashUiMessage;
using ruffneckk::remote_stash::IsSdkInputCompatible;
using ruffneckk::remote_stash::InspectSpA1Sprite;
using ruffneckk::remote_stash::PairedCloseOrigin;
using ruffneckk::remote_stash::PairedInterface;
using ruffneckk::remote_stash::ParseConfig;
using ruffneckk::remote_stash::PlaceAtAnchor;
using ruffneckk::remote_stash::PlaceDesktopFooterLeft;
using ruffneckk::remote_stash::ResolvePairedClosePlan;
using ruffneckk::remote_stash::ResolveCompanionInventoryClose;
using ruffneckk::remote_stash::ResolveRemoteOpenRollbackPlan;
using ruffneckk::remote_stash::ResolveRemoteTogglePlan;
using ruffneckk::remote_stash::ResolveRemoteStashTransitionFlag;
using ruffneckk::remote_stash::ShouldSuppressHotkeyMouseReset;
using ruffneckk::remote_stash::ShouldKeepInventoryOpenAfterRemoteOpen;
using ruffneckk::remote_stash::ShouldRestoreIndependentInventory;
using ruffneckk::remote_stash::SdkModifierValue;
using ruffneckk::remote_stash::ToggleSource;
using ruffneckk::remote_stash::UnionRect;
using ruffneckk::remote_stash::WidgetRect;

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uint64_t MaximumCustomSpriteBytes = 64ULL * 1024ULL * 1024ULL;

#define REMOTE_SITE(value) value

constexpr std::uintptr_t QueueOutgoingPacketRva = 0xEE2A0;
constexpr std::uintptr_t SharedGoldDepositRva = 0x14F5330;
constexpr std::uintptr_t SharedGoldDepositCallRva = 0x14F5895;
constexpr std::uintptr_t RemoveItemHandlerRva = 0x4AA100;
constexpr std::uintptr_t InsertItemHandlerRva = 0x4BFF30;
constexpr std::uintptr_t SharedDepositHandlerRva = 0x4C5570;
constexpr std::uintptr_t SharedWithdrawalHandlerRva = 0x4C6480;
constexpr std::uintptr_t ValidateItemPacketStateRva = 0x474700;
constexpr std::uintptr_t GoldButtonHandlerRva = 0x4BA580;
constexpr std::uintptr_t GoldRangeGateRva = 0x4BA617;
constexpr std::uintptr_t GoldRangeBypassRva = 0x4BA6A1;
constexpr std::uintptr_t SendServerUiRva = 0x480650;
constexpr std::uintptr_t GetClientFromPlayerRva = 0x48FDE0;
constexpr std::uintptr_t RemoveServerUnitRva = 0x43EC10;
constexpr std::uintptr_t GetLocalDataContextRva = 0x8B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x9A480;
constexpr std::uintptr_t IsRoomInTownRva = 0x2F0750;
constexpr std::uintptr_t TransferItemToInventoryPageRva = 0x15F8B0;
constexpr std::uintptr_t GetUiStateRva = 0xCE500;
constexpr std::uintptr_t OpenInterfaceStateRva = 0xCD7C0;
constexpr std::uintptr_t CloseInterfaceStateRva = 0xC7D30;
constexpr std::uintptr_t MovementUiCloseRva = 0xC8240;
constexpr std::uintptr_t MovementUiCloseCallRva = 0x102590;
constexpr std::uintptr_t StashInterfaceTransitionRva = 0x11FB80;
constexpr std::uintptr_t StashInterfaceTransitionCallRva = 0xC1F01;
constexpr std::uintptr_t ResetMouseInputStateRva = 0x8D510;
constexpr std::uintptr_t ResetMouseInputStateWithFinalizeRva = 0x8D540;
constexpr std::uintptr_t MouseInputState0Rva = 0x2A23454;
constexpr std::uintptr_t MouseInputState1Rva = 0x2A23478;
constexpr std::uintptr_t MouseInputState2Rva = 0x2A236EA;
constexpr std::uintptr_t MouseInputState3Rva = 0x2A23480;
constexpr std::uintptr_t MouseInputState4Rva = 0x2A23484;
constexpr std::uintptr_t MouseInputState5Rva = 0x2A236EB;
constexpr std::uintptr_t MarkUiDirtyRva = 0x843FC0;
constexpr std::uintptr_t FindTopLevelPanelRva = 0x846170;
constexpr std::uintptr_t ConfigurePlayerInventoryRva = 0x22BA70;
constexpr std::uintptr_t FindChildWidgetRva = 0x856220;
constexpr std::uintptr_t GetWidgetRectRva = 0x8562A0;

// These two return addresses are the only client stash lifecycle checks that
// may observe the synthetic remote session as a town-backed stash session.
constexpr std::uintptr_t ClientStashTownCheckReturnRva = 0x259137;
constexpr std::uintptr_t ClientStashCleanupTownCheckReturnRva = 0x25A122;
constexpr std::uintptr_t QuickMoveItemInitTownCheckReturnRva = 0xFEE3B;
constexpr std::uintptr_t QuickMoveItemPlacementTownCheckReturnRva = 0xFF1DC;
constexpr std::uintptr_t QuickMoveStashUiStateReturnRva = 0x15F982;
constexpr std::uintptr_t QuickMoveToStashItemPacketStateReturnRva = 0x473F80;
constexpr std::uintptr_t QuickMoveFromStashItemPacketStateReturnRva = 0x474257;
constexpr std::uintptr_t ServerUiCloseStashReturnRva = 0x1F0AD8;
constexpr std::uintptr_t StashPanelCloseButtonReturnRva = 0x23C150;
constexpr std::uintptr_t GeneralUiTeardownCloseStashCallRva = 0x22A9F9;
constexpr std::uintptr_t GeneralUiTeardownStashReturnRva = 0x22A9FE;
constexpr std::size_t WidgetVisibleOffset = 0x51;
constexpr std::size_t WidgetRectOffset = 0x70;
constexpr std::int32_t StashInterfaceState = 0x18;
constexpr std::int32_t InventoryInterfaceState = 1;
constexpr std::uint64_t HotkeyOpenTransitionWindowMs = 2000;
constexpr std::uint64_t CompanionInventoryCloseWindowMs = 2000;

constexpr char ButtonLayoutVirtualPath[] =
    "data/global/ui/layouts/ruffneckk-remote-stash/inventory-button.json";
constexpr char ButtonSpriteVirtualPath[] =
    "data/hd/global/ui/d2rloader/ruffneckk-remote-stash/inventory-button.sprite";
constexpr char ButtonLowendSpriteVirtualPath[] =
    "data/hd/global/ui/d2rloader/ruffneckk-remote-stash/inventory-button.lowend.sprite";
constexpr char ButtonChildLocalId[] = "inventory-button";
constexpr char ButtonWidgetName[] =
    "ruffneckk-remote-stash/inventory-button";
constexpr std::array<const char*, 2> LegacyButtonNames{
    "remote_stash",
    "ruffneckk_remote_stash_button",
};

constexpr std::array<std::uint8_t, 32> ConfigurePlayerInventoryExpected{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55,
    0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D,
    0x6C, 0x24, 0x90, 0x48, 0x81, 0xEC, 0x70, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0x05, 0x37, 0xF8, 0x79,
};
constexpr std::array<std::uint8_t, 32> FindChildWidgetExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0x41, 0x60, 0x48, 0x8D, 0x3C, 0xC3, 0x48, 0x3B,
};
constexpr std::array<std::uint8_t, 32> GetWidgetRectExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79,
    0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48,
    0x8B, 0x49, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20,
    0xE8, 0xE3, 0xFF, 0xFF, 0xFF, 0x33, 0xC0, 0x48,
};

constexpr std::array<std::uint8_t, 32> InsertItemHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x8D, 0x6C, 0x24, 0xF0, 0x48, 0x81, 0xEC, 0x10,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x7E, 0xB3,
    0x50, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> RemoveItemHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x8D, 0x6C, 0x24, 0xF0, 0x48, 0x81, 0xEC, 0x10,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xAE, 0x11,
    0x52, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> SharedDepositHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48,
    0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xF0,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x3E, 0x5D,
    0x50, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> SharedWithdrawalHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48,
    0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xF0,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x2E, 0x4E,
    0x50, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 32> ValidateItemPacketStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xD9, 0x41, 0x0F, 0xB6, 0xF0, 0x48, 0x8B,
    0x09, 0x48, 0x8B, 0xFA, 0xE8, 0xAF, 0x72, 0xED
};
constexpr std::array<std::uint8_t, 32> GoldButtonHandlerExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x57, 0x48,
    0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0x00,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x2E, 0x0D,
    0x51, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45
};
constexpr std::array<std::uint8_t, 15> GoldRangeGateExpected{
    0x44, 0x39, 0x7D, 0xBF, 0x0F, 0x86, 0x39, 0x0E,
    0x00, 0x00, 0x48, 0x85, 0xFF, 0x75, 0x7B
};
constexpr std::array<std::uint8_t, 16> SendServerUiExpected{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0xC6, 0x44,
    0x24, 0x50, 0x77, 0x48, 0x8B, 0xF9, 0x88, 0x54
};
constexpr std::array<std::uint8_t, 16> GetClientFromPlayerExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x85, 0xC9, 0x74, 0x1E, 0xE8, 0xDD
};
constexpr std::array<std::uint8_t, 16> RemoveServerUnitExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA
};
constexpr std::array<std::uint8_t, 16> GetLocalDataContextExpected{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC
};
constexpr std::array<std::uint8_t, 16> GetLocalPlayerExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85
};
constexpr std::array<std::uint8_t, 16> IsRoomInTownExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3
};
constexpr std::array<std::uint8_t, 16> TransferItemToInventoryPageExpected{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC
};
constexpr std::array<std::uint8_t, 15> GetUiStateExpected{
    0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8,
    0x95, 0x02, 0x0F, 0xB6, 0x04, 0x08, 0xC3
};
constexpr std::array<std::uint8_t, 68> OpenInterfaceStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41,
    0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x40, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
    0xC0, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xE5,
    0xDA, 0x8F, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,
    0x85, 0xB0, 0x02, 0x00, 0x00, 0x48, 0x63, 0xD9,
    0x4C, 0x8D, 0x25, 0x09, 0x28, 0xF3, 0xFF, 0x44,
    0x0F, 0xB6, 0xF2, 0x45, 0x0F, 0xB6, 0xBC, 0x1C,
    0xA0, 0xAD, 0xA2, 0x02,
};
constexpr std::array<std::uint8_t, 64> CloseInterfaceStateExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFD,
    0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x20, 0x03, 0x00,
    0x00, 0x48, 0x8B, 0x05, 0x70, 0x35, 0x90, 0x02,
    0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0x10, 0x02,
    0x00, 0x00, 0x48, 0x63, 0xD9, 0x4C, 0x8D, 0x25,
    0x94, 0x82, 0xF3, 0xFF, 0x0F, 0xB6, 0xF2, 0x46,
};
constexpr std::array<std::uint8_t, 20> StashInterfaceTransitionExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x0F, 0xB6, 0xFA, 0x8B, 0xD9, 0xE8,
    0xAC, 0x75, 0x72, 0x00,
};
constexpr std::array<std::uint8_t, 32> ResetMouseInputStateExpected{
    0x33, 0xC0, 0x89, 0x05, 0x3C, 0x5F, 0x99, 0x02,
    0x89, 0x05, 0x5A, 0x5F, 0x99, 0x02, 0x88, 0x05,
    0xC6, 0x61, 0x99, 0x02, 0x89, 0x05, 0x56, 0x5F,
    0x99, 0x02, 0x89, 0x05, 0x54, 0x5F, 0x99, 0x02,
};
constexpr std::array<std::uint8_t, 32> ResetMouseInputStateWithFinalizeExpected{
    0x8B, 0x0D, 0xBE, 0x61, 0x99, 0x02, 0x33, 0xC0,
    0x89, 0x05, 0x06, 0x5F, 0x99, 0x02, 0x89, 0x05,
    0x24, 0x5F, 0x99, 0x02, 0x88, 0x05, 0x90, 0x61,
    0x99, 0x02, 0x89, 0x05, 0x20, 0x5F, 0x99, 0x02,
};
constexpr std::array<std::uint8_t, 20> MarkUiDirtyExpected{
    0x48, 0x8B, 0x05, 0xA9, 0xC1, 0xBF, 0x02, 0x48,
    0x85, 0xC0, 0x74, 0x07, 0xC6, 0x80, 0xB8, 0x00,
    0x00, 0x00, 0x01, 0xC3,
};
constexpr std::array<std::uint8_t, 22> FindTopLevelPanelExpected{
    0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xF6, 0x9F,
    0xBF, 0x02, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0xDD,
    0x95, 0x05, 0x00, 0x33, 0xC0, 0xC3
};
constexpr std::array<std::uint8_t, 5> GeneralUiTeardownCloseStashCallExpected{
    0xE8, 0x32, 0xD3, 0xE9, 0xFF,
};

struct RelativeCallSite {
    const char* manifestId{};
    std::uintptr_t rva{};
    std::array<std::uint8_t, 5> expected{};
};

constexpr std::array<RelativeCallSite, 1> StashInterfaceTransitionCallSites{{
    {REMOTE_SITE("misc.remoteStash.stashTransitionCall"),
        StashInterfaceTransitionCallRva, {0xE8, 0x7A, 0xDC, 0x05, 0x00}},
}};
constexpr std::array<RelativeCallSite, 1> MovementUiCloseCallSites{{
    {REMOTE_SITE("misc.remoteStash.movementUiCloseCall"),
        MovementUiCloseCallRva, {0xE8, 0xAB, 0x5C, 0xFC, 0xFF}},
}};
constexpr std::array<RelativeCallSite, 1> SharedGoldDepositCallSites{{
    {REMOTE_SITE("misc.remoteStash.sharedGoldDepositCall"),
        SharedGoldDepositCallRva, {0xE8, 0x96, 0xFA, 0xFF, 0xFF}},
}};
constexpr std::array<RelativeCallSite, 4> IsRoomInTownCallSites{{
    {REMOTE_SITE("misc.remoteStash.townCheck.clientLifecycle"),
        0x259132, {0xE8, 0x19, 0x76, 0x09, 0x00}},
    {REMOTE_SITE("misc.remoteStash.townCheck.clientCleanup"),
        0x25A11D, {0xE8, 0x2E, 0x66, 0x09, 0x00}},
    {REMOTE_SITE("misc.remoteStash.townCheck.quickMoveInit"),
        0x0FEE36, {0xE8, 0x15, 0x19, 0x1F, 0x00}},
    {REMOTE_SITE("misc.remoteStash.townCheck.quickMovePlacement"),
        0x0FF1D7, {0xE8, 0x74, 0x15, 0x1F, 0x00}},
}};
constexpr std::array<RelativeCallSite, 8> TransferItemToInventoryPageCallSites{{
    {REMOTE_SITE("misc.remoteStash.transferItem.15E382"),
        0x15E382, {0xE8, 0x29, 0x15, 0x00, 0x00}},
    {REMOTE_SITE("misc.remoteStash.transferItem.1F6674"),
        0x1F6674, {0xE8, 0x37, 0x92, 0xF6, 0xFF}},
    {REMOTE_SITE("misc.remoteStash.transferItem.2A9D68"),
        0x2A9D68, {0xE8, 0x43, 0x5B, 0xEB, 0xFF}},
    {REMOTE_SITE("misc.remoteStash.transferItem.2AAB0D"),
        0x2AAB0D, {0xE8, 0x9E, 0x4D, 0xEB, 0xFF}},
    {REMOTE_SITE("misc.remoteStash.transferItem.2ABB09"),
        0x2ABB09, {0xE8, 0xA2, 0x3D, 0xEB, 0xFF}},
    {REMOTE_SITE("misc.remoteStash.transferItem.2C6055"),
        0x2C6055, {0xE8, 0x56, 0x98, 0xE9, 0xFF}},
    {REMOTE_SITE("misc.remoteStash.transferItem.2C6265"),
        0x2C6265, {0xE8, 0x46, 0x96, 0xE9, 0xFF}},
    {REMOTE_SITE("misc.remoteStash.transferItem.2C7479"),
        0x2C7479, {0xE8, 0x32, 0x84, 0xE9, 0xFF}},
}};

using QueueOutgoingPacketFn = void(__fastcall*)(const std::uint8_t* packet, std::int32_t size) noexcept;
using SharedGoldDepositFn = std::int32_t(__fastcall*)(
    void* panel,
    std::int32_t amount
) noexcept;
using ServerPacketHandlerFn = std::int32_t(__fastcall*)(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept;
using ValidateItemPacketStateFn = bool(__fastcall*)(
    void* transaction,
    const void* packetState,
    bool bypassStashProximity
) noexcept;
using SendServerUiFn = void(__fastcall*)(void* client, std::uint8_t action) noexcept;
using GetClientFromPlayerFn = void*(__fastcall*)(void* player) noexcept;
using RemoveServerUnitFn = void(__fastcall*)(void* game, void* unit) noexcept;
using GetLocalDataContextFn = std::int32_t(__fastcall*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t context) noexcept;
using IsRoomInTownFn = std::int32_t(__fastcall*)(void* room) noexcept;
using TransferItemToInventoryPageFn = bool(__fastcall*)(
    void* item,
    void* destinationUnit,
    std::uint8_t inventoryPage,
    std::uint8_t destinationKind,
    bool transferMode,
    void* placementOut
) noexcept;
using GetUiStateFn = std::uint8_t(__fastcall*)(std::int32_t state) noexcept;
using OpenInterfaceStateFn = bool(__fastcall*)(
    std::int32_t state,
    bool secondary
) noexcept;
using CloseInterfaceStateFn = void(__fastcall*)(
    std::int32_t state,
    bool secondary
) noexcept;
using MovementUiCloseFn = void(__fastcall*)(
    std::int32_t closeMode,
    std::int32_t secondary
) noexcept;
using StashInterfaceTransitionFn = void(__fastcall*)(
    std::int32_t mode,
    bool transitionFlag
) noexcept;
using ResetMouseInputStateFn = void(__fastcall*)() noexcept;
using MarkUiDirtyFn = void(__fastcall*)() noexcept;
using FindTopLevelPanelFn = void*(__fastcall*)(const char* name) noexcept;
using ConfigurePlayerInventoryFn = void(__fastcall*)(void* panel) noexcept;
using FindChildWidgetFn = void*(__fastcall*)(
    void* panel,
    const char* name
) noexcept;
using GetWidgetRectFn = WidgetRect*(__fastcall*)(
    void* widget,
    WidgetRect* rectOut
) noexcept;
using SetWidgetBoolFn = void(__fastcall*)(void* widget, bool value) noexcept;
const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
QueueOutgoingPacketFn QueueOutgoingPacket{};
SharedGoldDepositFn OriginalSharedGoldDeposit{};
ServerPacketHandlerFn OriginalRemoveItemHandler{};
ServerPacketHandlerFn OriginalInsertItemHandler{};
ServerPacketHandlerFn OriginalSharedDepositHandler{};
ServerPacketHandlerFn OriginalSharedWithdrawalHandler{};
ValidateItemPacketStateFn OriginalValidateItemPacketState{};
ServerPacketHandlerFn OriginalGoldButtonHandler{};
SendServerUiFn SendServerUi{};
GetClientFromPlayerFn GetClientFromPlayer{};
RemoveServerUnitFn OriginalRemoveServerUnit{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
IsRoomInTownFn OriginalIsRoomInTown{};
TransferItemToInventoryPageFn OriginalTransferItemToInventoryPage{};
GetUiStateFn OriginalGetUiState{};
OpenInterfaceStateFn OriginalOpenInterfaceState{};
CloseInterfaceStateFn OriginalCloseInterfaceState{};
MovementUiCloseFn OriginalMovementUiClose{};
StashInterfaceTransitionFn OriginalStashInterfaceTransition{};
ResetMouseInputStateFn OriginalResetMouseInputState{};
ResetMouseInputStateFn OriginalResetMouseInputStateWithFinalize{};
MarkUiDirtyFn MarkUiDirty{};
FindTopLevelPanelFn FindTopLevelPanel{};
ConfigurePlayerInventoryFn OriginalConfigurePlayerInventory{};
FindChildWidgetFn FindChildWidget{};
GetWidgetRectFn GetWidgetRect{};

const D2RL::InputServiceV1* InputService{};
const D2RL::ThreadServiceV1* ThreadService{};
const D2RL::LifecycleServiceV1* LifecycleService{};
const D2RL::SharedEventServiceV1* SharedEventService{};
const D2RL::PanelServiceV1* PanelService{};
const D2RL::ResourceServiceV1* ResourceService{};
D2RL::Input::ActionHandle InputAction{D2RL::Input::InvalidHandle};
D2RL::SharedEvents::ListenerHandle ButtonMessageListener{
    D2RL::SharedEvents::InvalidHandle};
D2RL::Lifecycle::ListenerHandle GameJoinedListener{
    D2RL::Lifecycle::InvalidHandle};
D2RL::Lifecycle::ListenerHandle GameLeftListener{
    D2RL::Lifecycle::InvalidHandle};
D2RL::Panels::ChildLayoutHandle ButtonChildLayout{
    D2RL::Panels::InvalidChildLayoutHandle};
D2RL::Resources::RegistrationHandle ButtonLayoutResource{
    D2RL::Resources::InvalidHandle};
D2RL::Resources::RegistrationHandle ButtonSpriteResource{
    D2RL::Resources::InvalidHandle};
D2RL::Resources::RegistrationHandle ButtonLowendSpriteResource{
    D2RL::Resources::InvalidHandle};
bool UsesSdkInput{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-remote-stash",
    .name = "Remote Stash",
    .version = "2.0.0",
    .author = "RuffnecKk",
    .description =
        "Opens the native personal and shared stash from anywhere.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

HotkeyConfig HotkeySettings{};
ButtonConfig EffectiveButtonSettings{};
std::string LoadedConfigPath{"built-in disabled defaults"};
std::string ButtonSpriteSource{"embedded RuffnecKk chest"};
HANDLE InputThread{};
DWORD InputThreadId{};
std::atomic_bool InputThreadReady{};
std::atomic_bool InputThreadFailed{};
std::atomic_bool InputStopping{};
std::atomic_bool UiDispatchReady{};
std::atomic_bool HotkeyPressed{};
std::atomic_bool HotkeyCaptured{};
std::atomic<std::uint32_t> ReplayVirtualKey{};
std::atomic_bool HotkeyRequestPending{};
std::atomic<std::uint64_t> HotkeyAcceptedRequests{};
std::atomic<std::uint64_t> HotkeyCoalescedRequests{};
std::atomic<std::uint64_t> HotkeyDispatchedRequests{};
std::atomic<std::uint64_t> HotkeyRefusedRequests{};
std::atomic<std::uint64_t> HotkeyFailedRequests{};
std::atomic<std::uint64_t> HotkeyOpenTransitionDeadline{};
std::atomic<std::uint64_t> CompanionInventoryCloseDeadline{};
std::atomic<std::uint64_t> HotkeyOpenTransitionTickets{};
std::atomic<std::uint64_t> HotkeyOpenTransitionApplications{};
std::atomic<std::uint64_t> HotkeyOpenTransitionExpirations{};
std::atomic<std::uint64_t> HotkeyMouseResetSuppressions{};
std::atomic<std::uint64_t> HotkeyMouseStateRestorations{};
std::atomic<std::uint64_t> PairedInterfaceCloses{};
std::atomic<std::uint64_t> SdkButtonActivations{};
std::atomic<std::uint64_t> SdkButtonFailures{};
std::atomic<std::uint64_t> ButtonPlacements{};
std::atomic<std::uint64_t> ButtonPlacementFailures{};
std::atomic<std::uint64_t> LegacyButtonsNeutralized{};
std::atomic<std::uint64_t> OpenRequests{};
std::atomic<std::uint64_t> CloseRequests{};
std::atomic<std::uint64_t> ServerSessionsOpened{};
std::atomic<std::uint64_t> ServerSessionsClosed{};
std::atomic<std::uint64_t> ServerSessionsPruned{};
std::atomic<std::uint64_t> RemoteItemOperations{};
std::atomic<std::uint64_t> RemoteItemFailures{};
std::atomic<std::uint64_t> MaxRemoteItemOperationMs{};
std::atomic<std::uint64_t> RemoteStashProximityBypasses{};
std::atomic<std::uint64_t> RemoteTownBypasses{};
std::atomic<std::uint64_t> RemoteSharedTransferOperations{};
std::atomic<std::uint64_t> RemoteSharedTransferFailures{};
std::atomic<std::uint64_t> RemoteGoldTransactions{};
std::atomic<std::uint64_t> RemoteGoldFailures{};
std::atomic<std::uint64_t> RemoteQuickMoveUiBypasses{};
std::atomic<std::uint64_t> RemoteMovementCloseSuppressions{};
std::atomic<std::uint64_t> RemoteGeneralUiCloses{};
std::atomic<std::uint64_t> RemoteServerUiCloses{};
std::atomic<std::uint64_t> RemoteQuickMoveWithdrawalDeadline{};
std::atomic_bool FirstHotkeyDispatchReported{};
std::atomic_bool FirstButtonActivationReported{};
std::atomic_bool FirstButtonPlacementReported{};
std::atomic_bool FirstButtonPlacementFailureReported{};
std::atomic_bool FirstLegacyButtonReported{};
std::atomic_bool FirstServerSessionReported{};

struct RemoteSession {
    void* game{};
};

std::mutex RemoteSessionsMutex;
std::unordered_map<void*, RemoteSession> RemoteSessions;
std::atomic_bool RemoteClientSessionActive{};
std::atomic_bool RemoteClientUiObservedOpen{};
std::atomic_bool RemoteClientInventoryCoupled{};
std::atomic_bool RemoteClientInventoryWasOpenBeforeOpen{};
thread_local bool RemoteItemScope{};
thread_local bool RemoteGoldScope{};
thread_local bool RemoteHotkeyOpenTransitionScope{};
thread_local bool RemoteMovementUiCloseScope{};
void* GoldRangeStub{};
void* GoldRangeTrampoline{};
void* CallSiteRelayPage{};

constexpr std::array<std::uint8_t, 17> RemoteOpenRequest{
    0x18,
    0x52, 0x53, 0x54, 0x41,
    0x53, 0x48, 0x52, 0x55,
    0x46, 0x46, 0x4E, 0x45,
    0x43, 0x4B, 0x4B, 0x21,
};
constexpr std::array<std::uint8_t, 17> RemoteCloseRequest{
    0x18,
    0x52, 0x53, 0x54, 0x41,
    0x53, 0x48, 0x52, 0x55,
    0x46, 0x46, 0x4E, 0x45,
    0x43, 0x4B, 0x4B, 0x22,
};
constexpr std::uint8_t OpenStashUiAction = 0x10;

template<class T>
T At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<T>(Base + rva);
}

void LogDiagnosticOnce(
    std::atomic_bool& reported,
    const char* message
) noexcept {
    if (!HotkeySettings.diagnostics || !Context || !message
        || reported.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    Context->LogInfo(message);
}

void TryMarkUiDirty() noexcept {
    if (!MarkUiDirty) return;
    __try {
        MarkUiDirty();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

template<class Function>
bool InstallNamedInlineHook(
    const D2RL::PluginContext* context,
    const char* site,
    std::uintptr_t rva,
    const void* expected,
    std::uint32_t expectedSize,
    Function target,
    Function* original
) noexcept {
    (void)site;
    return context->InstallInlineHook(
        rva, expected, expectedSize, target, original);
}

template<std::size_t Size>
bool Matches(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected
) noexcept {
    return Context && Context->CheckExpectedBytes(
        rva, expected.data(), static_cast<std::uint32_t>(Size));
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

template<std::size_t Count>
bool MatchesAll(const std::array<RelativeCallSite, Count>& sites) noexcept {
    for (const auto& site : sites) {
        if (!Context->CheckExpectedBytes(
                site.rva,
                site.expected.data(),
                static_cast<std::uint32_t>(site.expected.size()))) {
            return false;
        }
    }
    return true;
}

bool LoadConfig() noexcept {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "RemoteStash: configuration exceeds 65535 bytes."
            : "RemoteStash: configuration could not be read.");
        return false;
    }

    HotkeyConfig parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("RemoteStash: invalid TOML (")
            + error + "); no service, listener, or hook was registered.";
        Context->LogError(message.c_str());
        return false;
    }
    HotkeySettings = parsed;
    LoadedConfigPath = "config/ruffneckk-remote-stash.toml";
    return true;
}

bool LoadEmbeddedResource(
    std::uint16_t resourceId,
    std::vector<std::uint8_t>& bytes
) noexcept {
    const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
    const auto resource = FindResourceW(
        module,
        MAKEINTRESOURCEW(resourceId),
        RT_RCDATA
    );
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

std::filesystem::path ResolveConfiguredSpritePath(
    std::string_view configured
) {
    std::filesystem::path path{std::string(configured)};
    if (path.is_absolute()) return path.lexically_normal();
    if (Context && Context->pluginConfigPath) {
        return (std::filesystem::path(Context->pluginConfigPath).parent_path()
            / path).lexically_normal();
    }
    if (Context && Context->pluginDirectory) {
        return (std::filesystem::path(Context->pluginDirectory) / path)
            .lexically_normal();
    }
    return path.lexically_normal();
}

bool ReadSpriteFile(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& reason
) noexcept {
    try {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            reason = "the configured file does not exist";
            return false;
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > MaximumCustomSpriteBytes) {
            reason = "the configured file is empty or larger than 64 MiB";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            reason = "the configured file could not be opened";
            return false;
        }
        bytes.resize(static_cast<std::size_t>(size));
        if (!file.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            bytes.clear();
            reason = "the configured file could not be read completely";
            return false;
        }
        return true;
    } catch (...) {
        bytes.clear();
        reason = "the configured path could not be resolved";
        return false;
    }
}

void ResetEffectiveVisualToDefault() noexcept {
    const auto placement = HotkeySettings.button.placement;
    const auto anchor = HotkeySettings.button.anchor;
    const auto offsetX = HotkeySettings.button.offsetX;
    const auto offsetY = HotkeySettings.button.offsetY;
    EffectiveButtonSettings = {};
    EffectiveButtonSettings.placement = placement;
    EffectiveButtonSettings.anchor = anchor;
    EffectiveButtonSettings.offsetX = offsetX;
    EffectiveButtonSettings.offsetY = offsetY;
}

bool LoadButtonSpriteBytes(
    std::vector<std::uint8_t>& sprite,
    std::vector<std::uint8_t>& lowend
) noexcept {
    EffectiveButtonSettings = HotkeySettings.button;
    ButtonSpriteSource = "embedded RuffnecKk chest";

    std::vector<std::uint8_t> embeddedSprite;
    std::vector<std::uint8_t> embeddedLowend;
    if (!LoadEmbeddedResource(
            REMOTE_STASH_DEFAULT_SPRITE_RESOURCE_ID,
            embeddedSprite)
        || !LoadEmbeddedResource(
            REMOTE_STASH_DEFAULT_LOWEND_SPRITE_RESOURCE_ID,
            embeddedLowend)) {
        Context->LogError(
            "RemoteStash: embedded RuffnecKk button sprites are unavailable."
        );
        return false;
    }

    ruffneckk::remote_stash::SpriteMetadata embeddedMetadata{};
    ruffneckk::remote_stash::SpriteMetadata embeddedLowendMetadata{};
    if (!InspectSpA1Sprite(
            embeddedSprite.data(), embeddedSprite.size(), embeddedMetadata)
        || !InspectSpA1Sprite(
            embeddedLowend.data(),
            embeddedLowend.size(),
            embeddedLowendMetadata)) {
        Context->LogError(
            "RemoteStash: embedded RuffnecKk button sprite validation failed."
        );
        return false;
    }

    if (HotkeySettings.button.spriteFile.empty()) {
        if (!ButtonFramesFit(
                EffectiveButtonSettings, embeddedMetadata.frameCount)
            || !ButtonFramesFit(
                EffectiveButtonSettings, embeddedLowendMetadata.frameCount)) {
            ResetEffectiveVisualToDefault();
            Context->LogWarn(
                "RemoteStash: configured button frames exceed the embedded sprite; default dimensions and frames are used."
            );
        }
        sprite = std::move(embeddedSprite);
        lowend = std::move(embeddedLowend);
        return true;
    }

    std::string reason;
    std::vector<std::uint8_t> customSprite;
    std::vector<std::uint8_t> customLowend;
    ruffneckk::remote_stash::SpriteMetadata customMetadata{};
    ruffneckk::remote_stash::SpriteMetadata customLowendMetadata{};
    bool valid{};
    try {
        valid = ReadSpriteFile(
            ResolveConfiguredSpritePath(HotkeySettings.button.spriteFile),
            customSprite,
            reason
        ) && InspectSpA1Sprite(
            customSprite.data(), customSprite.size(), customMetadata
        ) && ButtonFramesFit(
            EffectiveButtonSettings, customMetadata.frameCount);
        if (valid && !HotkeySettings.button.lowendSpriteFile.empty()) {
            valid = ReadSpriteFile(
                ResolveConfiguredSpritePath(
                    HotkeySettings.button.lowendSpriteFile),
                customLowend,
                reason
            ) && InspectSpA1Sprite(
                customLowend.data(),
                customLowend.size(),
                customLowendMetadata
            ) && ButtonFramesFit(
                EffectiveButtonSettings, customLowendMetadata.frameCount);
        } else if (valid) {
            customLowend = customSprite;
            customLowendMetadata = customMetadata;
        }
    } catch (...) {
        valid = false;
        reason = "the configured path could not be resolved";
    }

    if (!valid) {
        if (reason.empty()) {
            reason = "the sprite header or configured frame indexes are invalid";
        }
        ResetEffectiveVisualToDefault();
        sprite = std::move(embeddedSprite);
        lowend = std::move(embeddedLowend);
        ButtonSpriteSource = "embedded fallback";
        const auto message = std::string(
            "RemoteStash: custom button sprite rejected (")
            + reason + "); the embedded RuffnecKk chest is used.";
        Context->LogWarn(message.c_str());
        return true;
    }

    sprite = std::move(customSprite);
    lowend = std::move(customLowend);
    ButtonSpriteSource = "custom file";
    return true;
}

bool RegisterResource(
    const char* path,
    const void* bytes,
    std::uint64_t byteCount,
    D2RL::Resources::RegistrationHandle& handle
) noexcept {
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

void UnregisterOwnedButton() noexcept {
    if (PanelService && Context
        && ButtonChildLayout != D2RL::Panels::InvalidChildLayoutHandle) {
        (void)PanelService->unregisterChildLayout(
            Context, ButtonChildLayout);
    }
    ButtonChildLayout = D2RL::Panels::InvalidChildLayoutHandle;

    const auto unregisterResource = [](auto& handle) noexcept {
        if (ResourceService && Context
            && handle != D2RL::Resources::InvalidHandle) {
            (void)ResourceService->unregisterResource(Context, handle);
        }
        handle = D2RL::Resources::InvalidHandle;
    };
    unregisterResource(ButtonLayoutResource);
    unregisterResource(ButtonLowendSpriteResource);
    unregisterResource(ButtonSpriteResource);
}

bool RegisterOwnedButton() noexcept {
    std::vector<std::uint8_t> sprite;
    std::vector<std::uint8_t> lowend;
    if (!LoadButtonSpriteBytes(sprite, lowend)) return false;
    const auto layout = BuildButtonLayoutJson(EffectiveButtonSettings);

    if (!RegisterResource(
            ButtonSpriteVirtualPath,
            sprite.data(),
            sprite.size(),
            ButtonSpriteResource)
        || !RegisterResource(
            ButtonLowendSpriteVirtualPath,
            lowend.data(),
            lowend.size(),
            ButtonLowendSpriteResource)
        || !RegisterResource(
            ButtonLayoutVirtualPath,
            layout.data(),
            layout.size(),
            ButtonLayoutResource)) {
        Context->LogError(
            "RemoteStash: plugin-owned button resource registration failed."
        );
        UnregisterOwnedButton();
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
            "RemoteStash: plugin-owned Inventory child layout registration failed."
        );
        UnregisterOwnedButton();
        return false;
    }
    return true;
}

bool ValidateRuntime() noexcept {
    return
        Matches(
            ConfigurePlayerInventoryRva,
            ConfigurePlayerInventoryExpected
        )
        && Matches(FindChildWidgetRva, FindChildWidgetExpected)
        && Matches(GetWidgetRectRva, GetWidgetRectExpected)
        // These are composable live entries: call the current executable entry
        // rather than requiring a vanilla prologue owned by another plugin.
        && IsExecutableAddress(Base + QueueOutgoingPacketRva)
        && IsExecutableAddress(Base + MovementUiCloseRva)
        && MatchesAll(SharedGoldDepositCallSites)
        && IsExecutableAddress(Base + SharedGoldDepositRva)
        && Matches(RemoveItemHandlerRva, RemoveItemHandlerExpected)
        && Matches(InsertItemHandlerRva, InsertItemHandlerExpected)
        && Matches(SharedDepositHandlerRva, SharedDepositHandlerExpected)
        && Matches(SharedWithdrawalHandlerRva, SharedWithdrawalHandlerExpected)
        && Matches(ValidateItemPacketStateRva, ValidateItemPacketStateExpected)
        && Matches(GoldButtonHandlerRva, GoldButtonHandlerExpected)
        && Matches(GoldRangeGateRva, GoldRangeGateExpected)
        && Matches(SendServerUiRva, SendServerUiExpected)
        && Matches(GetClientFromPlayerRva, GetClientFromPlayerExpected)
        && Matches(RemoveServerUnitRva, RemoveServerUnitExpected)
        && Matches(GetLocalDataContextRva, GetLocalDataContextExpected)
        && Matches(GetLocalPlayerRva, GetLocalPlayerExpected)
        && Matches(IsRoomInTownRva, IsRoomInTownExpected)
        && Matches(
            TransferItemToInventoryPageRva,
            TransferItemToInventoryPageExpected
        )
        && Matches(GetUiStateRva, GetUiStateExpected)
        && Matches(OpenInterfaceStateRva, OpenInterfaceStateExpected)
        && Matches(CloseInterfaceStateRva, CloseInterfaceStateExpected)
        && Matches(
            StashInterfaceTransitionRva,
            StashInterfaceTransitionExpected
        )
        && Matches(
            ResetMouseInputStateRva,
            ResetMouseInputStateExpected
        )
        && Matches(
            ResetMouseInputStateWithFinalizeRva,
            ResetMouseInputStateWithFinalizeExpected
        )
        && Matches(MarkUiDirtyRva, MarkUiDirtyExpected)
        && Matches(
            GeneralUiTeardownCloseStashCallRva,
            GeneralUiTeardownCloseStashCallExpected
        )
        && MatchesAll(StashInterfaceTransitionCallSites)
        && MatchesAll(MovementUiCloseCallSites)
        && MatchesAll(IsRoomInTownCallSites)
        && MatchesAll(TransferItemToInventoryPageCallSites);
}

bool ValidateHotkeyRuntime() noexcept {
    return Matches(FindTopLevelPanelRva, FindTopLevelPanelExpected);
}

bool IsRemoteControlRequest(
    const std::uint8_t* packet,
    std::int32_t size,
    const std::array<std::uint8_t, 17>& request
) noexcept {
    return packet
        && size == static_cast<std::int32_t>(request.size())
        && std::memcmp(packet, request.data(), request.size()) == 0;
}

bool IsRemoteOpenRequest(
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return IsRemoteControlRequest(packet, size, RemoteOpenRequest);
}

bool IsRemoteCloseRequest(
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return IsRemoteControlRequest(packet, size, RemoteCloseRequest);
}

bool HasRemoteSession(void* game, void* player) noexcept {
    if (!game || !player) return false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        const auto found = RemoteSessions.find(player);
        return found != RemoteSessions.end() && found->second.game == game;
    } catch (...) {
        return false;
    }
}

bool CloseRemoteSession(void* game, void* player) noexcept {
    if (!game || !player) return false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        const auto found = RemoteSessions.find(player);
        if (found != RemoteSessions.end() && found->second.game == game) {
            RemoteSessions.erase(found);
            ServerSessionsClosed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    } catch (...) {
    }
    return false;
}

void UpdateMaximum(
    std::atomic<std::uint64_t>& maximum,
    std::uint64_t candidate
) noexcept {
    auto current = maximum.load(std::memory_order_relaxed);
    while (candidate > current
        && !maximum.compare_exchange_weak(
            current,
            candidate,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        )) {
    }
}

void RecordRemoteItemOperation(
    std::int32_t result,
    std::uint64_t elapsedMs
) noexcept {
    RemoteItemOperations.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        RemoteItemFailures.fetch_add(1, std::memory_order_relaxed);
    }
    UpdateMaximum(MaxRemoteItemOperationMs, elapsedMs);
}

void DeactivateRemoteClientSession(bool notifyServer) noexcept {
    const auto wasActive = RemoteClientSessionActive.exchange(
        false,
        std::memory_order_acq_rel
    );
    RemoteClientUiObservedOpen.store(false, std::memory_order_release);
    RemoteClientInventoryCoupled.store(false, std::memory_order_release);
    RemoteClientInventoryWasOpenBeforeOpen.store(false, std::memory_order_release);
    RemoteQuickMoveWithdrawalDeadline.store(0, std::memory_order_release);
    HotkeyOpenTransitionDeadline.store(0, std::memory_order_release);
    CompanionInventoryCloseDeadline.store(0, std::memory_order_release);
    if (!notifyServer || !wasActive || !QueueOutgoingPacket) return;

    __try {
        QueueOutgoingPacket(
            RemoteCloseRequest.data(),
            static_cast<std::int32_t>(RemoteCloseRequest.size())
        );
        CloseRequests.fetch_add(1, std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void ClearLifecycleState() noexcept {
    DeactivateRemoteClientSession(false);
    HotkeyRequestPending.store(false, std::memory_order_release);
    HotkeyCaptured.store(false, std::memory_order_release);
    HotkeyPressed.store(false, std::memory_order_release);
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.clear();
    } catch (...) {
    }
}

void __cdecl OnGameplayLifecycle(
    const D2RL::PluginContext*,
    const D2RL::Lifecycle::GameplayEvent* event,
    void*
) noexcept {
    if (!D2RL::Lifecycle::HasGameplayEventField(
            event, D2RL::Lifecycle::GameplayEventRequiredSize)) {
        return;
    }
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined
        || event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
        ClearLifecycleState();
    }
}

bool QuerySdkServices() noexcept {
    if (Context->QueryService(
            D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version,
            &LifecycleService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceV1Field(
            LifecycleService, D2RL::LifecycleServiceV1RequiredSize)) {
        Context->LogError(
            "RemoteStash: D2RLoader Lifecycle service v1 is unavailable.");
        return false;
    }
    if (HotkeySettings.inventoryButtonEnabled) {
        if (Context->QueryService(
            D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version,
            &SharedEventService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasSharedEventServiceV1Field(
            SharedEventService, D2RL::SharedEventServiceV1RequiredSize)
        || SharedEventService->registerUiMessageListener == nullptr
        || SharedEventService->unregisterUiMessageListener == nullptr) {
        Context->LogError(
            "RemoteStash: D2RLoader SharedEvent service v1 is unavailable.");
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
                "RemoteStash: D2RLoader Panel service is unavailable.");
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
                "RemoteStash: D2RLoader Resource service is unavailable.");
            return false;
        }
    }
    if (!HotkeySettings.hotkeyEnabled) return true;

    if (Context->QueryService(
            D2RL::ServiceId::Thread,
            D2RL::ThreadServiceV1Version,
            &ThreadService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(
            ThreadService, D2RL::ThreadServiceV1RequiredSize)) {
        Context->LogError(
            "RemoteStash: D2RLoader Thread service v1 is unavailable.");
        return false;
    }

    UsesSdkInput = IsSdkInputCompatible(HotkeySettings.hotkey);
    if (!UsesSdkInput) return true;
    if (Context->QueryService(
            D2RL::ServiceId::Input,
            D2RL::InputServiceV1Version,
            &InputService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInputServiceV1Field(
            InputService, D2RL::InputServiceV1RequiredSize)) {
        Context->LogError(
            "RemoteStash: D2RLoader Input service v1 is unavailable.");
        return false;
    }
    return true;
}

bool RegisterLifecycleListeners() noexcept {
    D2RL::Lifecycle::GameplayEventListener listener{
        .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
        .flags = 0,
        .kind = D2RL::Lifecycle::GameplayEventKind::GameJoined,
        .reserved = 0,
        .callback = OnGameplayLifecycle,
        .userData = nullptr,
    };
    auto result = LifecycleService->registerGameplayEventListener(
        Context, &listener, &GameJoinedListener);
    if (result != D2RL::Lifecycle::Result::Success
        || GameJoinedListener == D2RL::Lifecycle::InvalidHandle) {
        Context->LogError(
            "RemoteStash: GameJoined listener registration failed.");
        return false;
    }

    listener.kind = D2RL::Lifecycle::GameplayEventKind::GameLeft;
    result = LifecycleService->registerGameplayEventListener(
        Context, &listener, &GameLeftListener);
    if (result == D2RL::Lifecycle::Result::Success
        && GameLeftListener != D2RL::Lifecycle::InvalidHandle) {
        return true;
    }
    (void)LifecycleService->unregisterGameplayEventListener(
        Context, GameJoinedListener);
    GameJoinedListener = D2RL::Lifecycle::InvalidHandle;
    Context->LogError("RemoteStash: GameLeft listener registration failed.");
    return false;
}

std::size_t RemoteSessionCount() noexcept {
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        return RemoteSessions.size();
    } catch (...) {
        return 0;
    }
}

bool TrySendStashUi(void* player, std::uint8_t action) noexcept {
    if (!player || !GetClientFromPlayer || !SendServerUi) return false;
    __try {
        auto* client = GetClientFromPlayer(player);
        if (!client) return false;
        SendServerUi(client, action);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool OpenRemoteSession(void* game, void* player) noexcept {
    if (!game || !player) return false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.insert_or_assign(player, RemoteSession{game});
    } catch (...) {
        return false;
    }

    if (!TrySendStashUi(player, OpenStashUiAction)) {
        CloseRemoteSession(game, player);
        return false;
    }

    ServerSessionsOpened.fetch_add(1, std::memory_order_relaxed);
    LogDiagnosticOnce(
        FirstServerSessionReported,
        "RemoteStash diagnostics: first authoritative server stash session opened.");
    return true;
}

std::int32_t __fastcall HookInsertItemHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    if (IsRemoteOpenRequest(packet, size)) {
        if (OpenRemoteSession(game, player)) return 0;
        if (Context) {
            Context->LogError("RemoteStash: server could not open the remote session.");
        }
        return 1;
    }
    if (IsRemoteCloseRequest(packet, size)) {
        CloseRemoteSession(game, player);
        return 0;
    }

    const auto remote = HasRemoteSession(game, player);
    const auto previousScope = RemoteItemScope;
    RemoteItemScope = remote;
    const auto started = GetTickCount64();
    const auto result = OriginalInsertItemHandler(game, player, packet, size);
    const auto elapsed = GetTickCount64() - started;
    RemoteItemScope = previousScope;
    if (remote) {
        RecordRemoteItemOperation(result, elapsed);
    }
    return result;
}

std::int32_t __fastcall HookRemoveItemHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    const auto remote = HasRemoteSession(game, player);
    const auto previousScope = RemoteItemScope;
    RemoteItemScope = remote;
    const auto started = GetTickCount64();
    const auto result = OriginalRemoveItemHandler(game, player, packet, size);
    const auto elapsed = GetTickCount64() - started;
    RemoteItemScope = previousScope;
    if (remote) {
        RecordRemoteItemOperation(result, elapsed);
    }
    return result;
}

std::int32_t ForwardSharedTransfer(
    ServerPacketHandlerFn original,
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    const auto remote = HasRemoteSession(game, player);
    const auto previousScope = RemoteItemScope;
    RemoteItemScope = remote;
    const auto started = GetTickCount64();
    const auto result = original(game, player, packet, size);
    const auto elapsed = GetTickCount64() - started;
    RemoteItemScope = previousScope;
    if (!remote) return result;

    UpdateMaximum(MaxRemoteItemOperationMs, elapsed);
    RemoteSharedTransferOperations.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        RemoteSharedTransferFailures.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

std::int32_t __fastcall HookSharedDepositHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return ForwardSharedTransfer(
        OriginalSharedDepositHandler,
        game,
        player,
        packet,
        size
    );
}

std::int32_t __fastcall HookSharedWithdrawalHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    return ForwardSharedTransfer(
        OriginalSharedWithdrawalHandler,
        game,
        player,
        packet,
        size
    );
}

bool __fastcall HookValidateItemPacketState(
    void* transaction,
    const void* packetState,
    bool bypassStashProximity
) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    bool remoteStashState{};
    std::int32_t page{-1};
    if (packetState) {
        __try {
            std::memcpy(
                &page,
                static_cast<const std::uint8_t*>(packetState) + 4,
                sizeof(page)
            );
            remoteStashState = page == 4
                && (RemoteItemScope
                    || (RemoteClientSessionActive.load(std::memory_order_acquire)
                        && (returnAddress == reinterpret_cast<std::uintptr_t>(
                                Base + QuickMoveToStashItemPacketStateReturnRva
                            )
                            || returnAddress == reinterpret_cast<std::uintptr_t>(
                                Base + QuickMoveFromStashItemPacketStateReturnRva
                            ))));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page = -1;
            remoteStashState = false;
        }
    }

    if (remoteStashState && !bypassStashProximity) {
        RemoteStashProximityBypasses.fetch_add(1, std::memory_order_relaxed);
        bypassStashProximity = true;
    }

    return OriginalValidateItemPacketState(
        transaction,
        packetState,
        bypassStashProximity
    );
}

bool IsRemoteClientStashTownCallsite(std::uintptr_t returnAddress) noexcept {
    if (!RemoteClientSessionActive.load(std::memory_order_acquire)) return false;
    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + ClientStashTownCheckReturnRva
        )
        || returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + ClientStashCleanupTownCheckReturnRva
        )) {
        return true;
    }

    const auto quickMoveWithdrawalActive = GetTickCount64()
        <= RemoteQuickMoveWithdrawalDeadline.load(std::memory_order_acquire);
    return quickMoveWithdrawalActive
        && (returnAddress == reinterpret_cast<std::uintptr_t>(
                Base + QuickMoveItemInitTownCheckReturnRva
            )
            || returnAddress == reinterpret_cast<std::uintptr_t>(
                Base + QuickMoveItemPlacementTownCheckReturnRva
            ));
}

std::int32_t __fastcall HookIsRoomInTown(void* room) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (IsRemoteClientStashTownCallsite(returnAddress)) {
        RemoteTownBypasses.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    return OriginalIsRoomInTown(room);
}

struct UnitProbe {
    std::uint32_t type{0xFFFFFFFF};
    std::uint32_t classId{0xFFFFFFFF};
    std::uint32_t unitId{0xFFFFFFFF};
};

UnitProbe ProbeUnit(const void* unit) noexcept {
    UnitProbe probe{};
    if (!unit) return probe;
    __try {
        const auto* bytes = static_cast<const std::uint8_t*>(unit);
        std::memcpy(&probe.type, bytes, sizeof(probe.type));
        std::memcpy(&probe.classId, bytes + 4, sizeof(probe.classId));
        std::memcpy(&probe.unitId, bytes + 8, sizeof(probe.unitId));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return UnitProbe{};
    }
    return probe;
}

bool IsLocalPlayer(void* player) noexcept {
    if (!player || !GetLocalDataContext || !GetLocalPlayer) return false;
    const auto playerProbe = ProbeUnit(player);
    if (playerProbe.type != 0 || playerProbe.unitId == 0xFFFFFFFF) return false;

    void* localPlayer{};
    __try {
        localPlayer = GetLocalPlayer(GetLocalDataContext());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    const auto localProbe = ProbeUnit(localPlayer);
    return localProbe.type == 0 && localProbe.unitId == playerProbe.unitId;
}

bool LocalPlayerIsAvailable() noexcept {
    if (!GetLocalDataContext || !GetLocalPlayer) return false;
    void* player{};
    __try {
        player = GetLocalPlayer(GetLocalDataContext());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return ProbeUnit(player).type == 0;
}

void __fastcall HookRemoveServerUnit(void* game, void* unit) noexcept {
    if (ProbeUnit(unit).type != 0) {
        OriginalRemoveServerUnit(game, unit);
        return;
    }
    const auto session = HasRemoteSession(game, unit);
    const auto localPlayer = session && IsLocalPlayer(unit);
    if (session && CloseRemoteSession(game, unit)) {
        ServerSessionsPruned.fetch_add(1, std::memory_order_relaxed);
        if (localPlayer) {
            DeactivateRemoteClientSession(false);
        }
    }
    OriginalRemoveServerUnit(game, unit);
}

bool __fastcall HookTransferItemToInventoryPage(
    void* item,
    void* destinationUnit,
    std::uint8_t inventoryPage,
    std::uint8_t destinationKind,
    bool transferMode,
    void* placementOut
) noexcept {
    const auto remoteWithdrawal =
        RemoteClientSessionActive.load(std::memory_order_acquire)
        && inventoryPage == 0
        && destinationKind == 4;
    if (remoteWithdrawal) {
        RemoteQuickMoveWithdrawalDeadline.store(
            GetTickCount64() + 1000,
            std::memory_order_release
        );
    }
    const auto result = OriginalTransferItemToInventoryPage(
        item,
        destinationUnit,
        inventoryPage,
        destinationKind,
        transferMode,
        placementOut
    );
    return result;
}

std::uint8_t __fastcall HookGetUiState(std::int32_t state) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto result = OriginalGetUiState(state);
    if (state != 0x16
        || !RemoteClientSessionActive.load(std::memory_order_acquire)) {
        return result;
    }

    if (result != 0) {
        RemoteClientUiObservedOpen.store(true, std::memory_order_release);
    } else {
        const auto observedOpen = RemoteClientUiObservedOpen.load(
            std::memory_order_acquire
        );
        if (observedOpen) {
            DeactivateRemoteClientSession(true);
        }
    }

    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + QuickMoveStashUiStateReturnRva
        )) {
        RemoteQuickMoveUiBypasses.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

void ProcessCompanionInventoryCloseAfterOpen(std::int32_t state) noexcept {
    const auto now = GetTickCount64();
    const auto deadline = CompanionInventoryCloseDeadline.load(
        std::memory_order_acquire
    );
    const auto decision = ResolveCompanionInventoryClose(
        deadline,
        state == StashInterfaceState,
        now
    );
    if (decision == ruffneckk::remote_stash::
            CompanionInventoryCloseDecision::Wait) {
        return;
    }

    auto expected = deadline;
    if (!CompanionInventoryCloseDeadline.compare_exchange_strong(
            expected,
            0,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        return;
    }
    if (decision == ruffneckk::remote_stash::
            CompanionInventoryCloseDecision::Expire) {
        return;
    }

    __try {
        if (OriginalGetUiState(StashInterfaceState) != 0
            && OriginalGetUiState(InventoryInterfaceState) != 0) {
            OriginalCloseInterfaceState(InventoryInterfaceState, false);
            TryMarkUiDirty();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CompanionInventoryCloseDeadline.store(0, std::memory_order_release);
        TryMarkUiDirty();
    }
}

bool __fastcall HookOpenInterfaceState(
    std::int32_t state,
    bool secondary
) noexcept {
    bool scopedHotkeyTransition = false;
    if (state == StashInterfaceState) {
        const auto deadline = HotkeyOpenTransitionDeadline.exchange(
            0,
            std::memory_order_acq_rel
        );
        if (deadline != 0) {
            if (GetTickCount64() <= deadline) {
                scopedHotkeyTransition = true;
            } else {
                HotkeyOpenTransitionExpirations.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }
        }
    }

    const auto previousTransitionScope = RemoteHotkeyOpenTransitionScope;
    RemoteHotkeyOpenTransitionScope =
        previousTransitionScope || scopedHotkeyTransition;
    const auto originalResult = OriginalOpenInterfaceState(state, secondary);
    auto result = originalResult;

    // The native stash open may also open Inventory. The session records whether
    // Inventory belongs to this toggle: the SDK button never couples it, while
    // the hotkey follows hotkey_mode.
    if (state == StashInterfaceState
        && RemoteClientSessionActive.load(std::memory_order_acquire)) {
        const auto inventoryIsCoupled = RemoteClientInventoryCoupled.load(
            std::memory_order_acquire
        );
        const auto inventoryWasOpenBeforeOpen =
            RemoteClientInventoryWasOpenBeforeOpen.load(
                std::memory_order_acquire
            );
        __try {
            auto stashIsOpen = OriginalGetUiState(StashInterfaceState) != 0;
            auto inventoryIsOpen = OriginalGetUiState(InventoryInterfaceState) != 0;
            const auto inventoryMustRemainOpen =
                ShouldKeepInventoryOpenAfterRemoteOpen(
                    inventoryIsCoupled,
                    inventoryWasOpenBeforeOpen
                );

            // The authoritative 0x77 response can ask D2R to open the stash a
            // second time after the hotkey already opened the pair locally.
            // The native open then returns false even though both interfaces
            // are correctly visible. Treat that idempotent state as success.
            if (stashIsOpen && inventoryMustRemainOpen && !inventoryIsOpen) {
                (void)OriginalOpenInterfaceState(
                    InventoryInterfaceState,
                    false
                );
                inventoryIsOpen =
                    OriginalGetUiState(InventoryInterfaceState) != 0;
            }
            stashIsOpen = OriginalGetUiState(StashInterfaceState) != 0;
            result = stashIsOpen && (!inventoryMustRemainOpen || inventoryIsOpen);
            if (result) {
                ProcessCompanionInventoryCloseAfterOpen(state);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            result = false;
        }
        if (!result) {
            DeactivateRemoteClientSession(true);
            __try {
                if (OriginalGetUiState(StashInterfaceState) != 0) {
                    OriginalCloseInterfaceState(StashInterfaceState, false);
                }
                const auto inventoryIsOpen =
                    OriginalGetUiState(InventoryInterfaceState) != 0;
                if (inventoryWasOpenBeforeOpen && !inventoryIsOpen) {
                    (void)OriginalOpenInterfaceState(
                        InventoryInterfaceState,
                        false
                    );
                } else if (!inventoryWasOpenBeforeOpen && inventoryIsOpen) {
                    OriginalCloseInterfaceState(InventoryInterfaceState, false);
                }
                TryMarkUiDirty();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TryMarkUiDirty();
            }
            if (Context) {
                Context->LogError(
                    "RemoteStash: native remote stash open failed."
                );
            }
        }
    }
    RemoteHotkeyOpenTransitionScope = previousTransitionScope;
    return result;
}

void __fastcall HookStashInterfaceTransition(
    std::int32_t mode,
    bool transitionFlag
) noexcept {
    const auto resolvedFlag = ResolveRemoteStashTransitionFlag(
        RemoteHotkeyOpenTransitionScope && mode == 2,
        transitionFlag
    );
    if (resolvedFlag != transitionFlag) {
        HotkeyOpenTransitionApplications.fetch_add(1, std::memory_order_relaxed);
    }
    OriginalStashInterfaceTransition(mode, resolvedFlag);
}

void __fastcall HookResetMouseInputState() noexcept {
    if (ShouldSuppressHotkeyMouseReset(RemoteHotkeyOpenTransitionScope)) {
        HotkeyMouseResetSuppressions.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    OriginalResetMouseInputState();
}

struct MouseInputStateSnapshot {
    std::uint32_t value0{};
    std::uint32_t value1{};
    std::uint8_t value2{};
    std::uint32_t value3{};
    std::uint32_t value4{};
    std::uint8_t value5{};
};

MouseInputStateSnapshot CaptureMouseInputState() noexcept {
    MouseInputStateSnapshot snapshot{};
    std::memcpy(&snapshot.value0, Base + MouseInputState0Rva, sizeof(snapshot.value0));
    std::memcpy(&snapshot.value1, Base + MouseInputState1Rva, sizeof(snapshot.value1));
    std::memcpy(&snapshot.value2, Base + MouseInputState2Rva, sizeof(snapshot.value2));
    std::memcpy(&snapshot.value3, Base + MouseInputState3Rva, sizeof(snapshot.value3));
    std::memcpy(&snapshot.value4, Base + MouseInputState4Rva, sizeof(snapshot.value4));
    std::memcpy(&snapshot.value5, Base + MouseInputState5Rva, sizeof(snapshot.value5));
    return snapshot;
}

void RestoreMouseInputState(const MouseInputStateSnapshot& snapshot) noexcept {
    std::memcpy(Base + MouseInputState0Rva, &snapshot.value0, sizeof(snapshot.value0));
    std::memcpy(Base + MouseInputState1Rva, &snapshot.value1, sizeof(snapshot.value1));
    std::memcpy(Base + MouseInputState2Rva, &snapshot.value2, sizeof(snapshot.value2));
    std::memcpy(Base + MouseInputState3Rva, &snapshot.value3, sizeof(snapshot.value3));
    std::memcpy(Base + MouseInputState4Rva, &snapshot.value4, sizeof(snapshot.value4));
    std::memcpy(Base + MouseInputState5Rva, &snapshot.value5, sizeof(snapshot.value5));
}

void __fastcall HookResetMouseInputStateWithFinalize() noexcept {
    if (!ShouldSuppressHotkeyMouseReset(RemoteHotkeyOpenTransitionScope)) {
        OriginalResetMouseInputStateWithFinalize();
        return;
    }

    const auto snapshot = CaptureMouseInputState();
    OriginalResetMouseInputStateWithFinalize();
    RestoreMouseInputState(snapshot);
    HotkeyMouseStateRestorations.fetch_add(1, std::memory_order_relaxed);
}

PairedInterface ClassifyPairedInterface(std::int32_t state) noexcept {
    if (state == StashInterfaceState) return PairedInterface::Stash;
    if (state == InventoryInterfaceState) return PairedInterface::Inventory;
    return PairedInterface::Other;
}

PairedCloseOrigin ClassifyPairedCloseOrigin(
    std::int32_t state,
    std::uintptr_t returnAddress,
    bool escapeIsDown
) noexcept {
    if (RemoteMovementUiCloseScope) return PairedCloseOrigin::Movement;
    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + ServerUiCloseStashReturnRva
        )) {
        return PairedCloseOrigin::Server;
    }
    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + StashPanelCloseButtonReturnRva
        )) {
        return PairedCloseOrigin::StashButton;
    }
    if (returnAddress == reinterpret_cast<std::uintptr_t>(
            Base + GeneralUiTeardownStashReturnRva
        )) {
        return PairedCloseOrigin::GeneralTeardown;
    }
    if (escapeIsDown) return PairedCloseOrigin::Escape;
    if (state == InventoryInterfaceState) {
        return PairedCloseOrigin::Inventory;
    }
    return PairedCloseOrigin::Other;
}

void __fastcall HookMovementUiClose(
    std::int32_t closeMode,
    std::int32_t secondary
) noexcept {
    const auto previousScope = RemoteMovementUiCloseScope;
    RemoteMovementUiCloseScope = true;
    OriginalMovementUiClose(closeMode, secondary);
    RemoteMovementUiCloseScope = previousScope;
}

void __fastcall HookCloseInterfaceState(
    std::int32_t state,
    bool secondary
) noexcept {
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto escapeIsDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const auto interfaceToClose = ClassifyPairedInterface(state);
    const auto remoteSessionIsActive = RemoteClientSessionActive.load(
        std::memory_order_acquire
    );
    const auto inventoryIsCoupled = RemoteClientInventoryCoupled.load(
        std::memory_order_acquire
    );
    const auto origin = ClassifyPairedCloseOrigin(
        state,
        returnAddress,
        escapeIsDown
    );
    bool stashInterfaceIsOpen{};
    __try {
        stashInterfaceIsOpen = OriginalGetUiState(StashInterfaceState) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        stashInterfaceIsOpen = false;
    }
    const auto plan = ResolvePairedClosePlan(
        remoteSessionIsActive,
        inventoryIsCoupled,
        stashInterfaceIsOpen,
        interfaceToClose,
        origin
    );
    if (plan.suppress) {
        RemoteMovementCloseSuppressions.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!plan.deactivate) {
        OriginalCloseInterfaceState(state, secondary);
        if (remoteSessionIsActive
            && !inventoryIsCoupled
            && interfaceToClose == PairedInterface::Inventory) {
            TryMarkUiDirty();
        }
        return;
    }

    // Deactivate before changing either native state. The atomic transition in
    // DeactivateRemoteClientSession guarantees at most one client close request
    // even when D2R immediately asks to close the other paired state too.
    __try {
        const auto inventoryWasOpen =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        DeactivateRemoteClientSession(plan.notifyServer);
        if (plan.closeStash
            && OriginalGetUiState(StashInterfaceState) != 0) {
            OriginalCloseInterfaceState(StashInterfaceState, false);
        }
        if (plan.closeInventory
            && OriginalGetUiState(InventoryInterfaceState) != 0) {
            OriginalCloseInterfaceState(InventoryInterfaceState, false);
        }
        const auto inventoryIsOpen =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        if (plan.preserveInventory
            && ShouldRestoreIndependentInventory(
                inventoryWasOpen,
                inventoryIsOpen
            )) {
            if (!OriginalOpenInterfaceState(InventoryInterfaceState, false)
                || OriginalGetUiState(InventoryInterfaceState) == 0) {
                if (Context) {
                    Context->LogError(
                        "RemoteStash: independent Inventory restoration failed during native close."
                    );
                }
            }
        }
        TryMarkUiDirty();
        PairedInterfaceCloses.fetch_add(1, std::memory_order_relaxed);
        if (origin == PairedCloseOrigin::GeneralTeardown) {
            RemoteGeneralUiCloses.fetch_add(1, std::memory_order_relaxed);
        } else if (origin == PairedCloseOrigin::Server) {
            RemoteServerUiCloses.fetch_add(1, std::memory_order_relaxed);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TryMarkUiDirty();
        if (Context) {
            Context->LogError("RemoteStash: paired interface close failed.");
        }
    }
}

bool ShouldBypassGoldRange() noexcept {
    return RemoteGoldScope;
}

std::int32_t __fastcall HookGoldButtonHandler(
    void* game,
    void* player,
    const std::uint8_t* packet,
    std::int32_t size
) noexcept {
    // D2R 3.2 routes stash-gold operations through the modern 0x27 handler.
    // Its final dword is a signed gold delta, not the legacy 18/19/20 action
    // value that older implementations assumed. Scope the handler itself; the
    // injected bypass still applies only at its stash-object proximity gate.
    const auto remote = packet
        && size == 17
        && packet[0] == 0x27
        && HasRemoteSession(game, player);
    const auto previousScope = RemoteGoldScope;
    RemoteGoldScope = remote;
    const auto result = OriginalGoldButtonHandler(game, player, packet, size);
    RemoteGoldScope = previousScope;

    if (!remote) return result;
    RemoteGoldTransactions.fetch_add(1, std::memory_order_relaxed);
    if (result != 0) {
        RemoteGoldFailures.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

bool CreateGoldRangeStubImmediate() noexcept {
    constexpr std::size_t StubSize = 50;
    constexpr std::size_t HelperAddressOffset = 6;
    constexpr std::size_t BypassAddressOffset = 26;
    constexpr std::size_t TrampolineAddressOffset = 39;

    GoldRangeStub = VirtualAlloc(
        nullptr,
        StubSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE
    );
    if (!GoldRangeStub) return false;

    std::array<std::uint8_t, StubSize> stub{
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xD0,
        0x48, 0x83, 0xC4, 0x20,
        0x84, 0xC0,
        0x74, 0x0D,
        0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
        0x41, 0xFF, 0xE3,
        0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
        0x41, 0xFF, 0xE3,
    };
    const auto helper = reinterpret_cast<std::uintptr_t>(&ShouldBypassGoldRange);
    const auto bypass = reinterpret_cast<std::uintptr_t>(Base + GoldRangeBypassRva);
    std::memcpy(stub.data() + HelperAddressOffset, &helper, sizeof(helper));
    std::memcpy(stub.data() + BypassAddressOffset, &bypass, sizeof(bypass));
    std::memcpy(GoldRangeStub, stub.data(), stub.size());

    if (!Context->InstallInlineHook(
            GoldRangeGateRva,
            GoldRangeGateExpected.data(),
            static_cast<std::uint32_t>(GoldRangeGateExpected.size()),
            GoldRangeStub,
            &GoldRangeTrampoline)
        || !GoldRangeTrampoline) {
        VirtualFree(GoldRangeStub, 0, MEM_RELEASE);
        GoldRangeStub = nullptr;
        GoldRangeTrampoline = nullptr;
        return false;
    }

    const auto trampoline = reinterpret_cast<std::uintptr_t>(GoldRangeTrampoline);
    std::memcpy(
        static_cast<std::uint8_t*>(GoldRangeStub) + TrampolineAddressOffset,
        &trampoline,
        sizeof(trampoline)
    );
    FlushInstructionCache(GetCurrentProcess(), GoldRangeStub, StubSize);

    DWORD previousProtection{};
    if (!VirtualProtect(GoldRangeStub, StubSize, PAGE_EXECUTE_READ, &previousProtection)) {
        return false;
    }
    return true;
}

bool CreateGoldRangeStub() noexcept {
    return CreateGoldRangeStubImmediate();
}

std::int32_t __fastcall HookSharedGoldDeposit(
    void* panel,
    std::int32_t amount
) noexcept {
    auto resolvedAmount = amount;
    if (panel && RemoteClientSessionActive.load(std::memory_order_acquire)) {
        __try {
            auto* modal = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(panel) + 0x510
            );
            if (modal && modal[0x14E8] != 0) {
                std::int32_t requestedAmount{};
                std::memcpy(
                    &requestedAmount,
                    modal + 0x14E4,
                    sizeof(requestedAmount)
                );
                if (requestedAmount > 0) {
                    resolvedAmount = requestedAmount;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return OriginalSharedGoldDeposit
        ? OriginalSharedGoldDeposit(panel, resolvedAmount)
        : 2;
}

bool TryReadTopLevelPanelVisibility(const char* name, bool& visible) noexcept {
    visible = false;
    if (!FindTopLevelPanel || !name) return false;
    __try {
        auto* widget = FindTopLevelPanel(name);
        if (widget) {
            visible = *(static_cast<std::uint8_t*>(widget) + WidgetVisibleOffset) != 0;
        }
        return true;
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
    for (const auto* name : blockers) {
        bool visible{};
        if (!TryReadTopLevelPanelVisibility(name, visible) || visible) return true;
    }
    return false;
}

bool TryQueueRemoteOpenRequest(
    const ruffneckk::remote_stash::RemoteTogglePlan& plan,
    bool inventoryIsOpen,
    bool smoothHotkeyTransition = false
) noexcept {
    HotkeyOpenTransitionDeadline.store(0, std::memory_order_release);
    CompanionInventoryCloseDeadline.store(0, std::memory_order_release);
    if (!QueueOutgoingPacket || !LocalPlayerIsAvailable()) return false;
    __try {
        const auto now = GetTickCount64();
        if (plan.closeCompanionInventoryAfterOpen) {
            CompanionInventoryCloseDeadline.store(
                now + CompanionInventoryCloseWindowMs,
                std::memory_order_release
            );
        }
        if (smoothHotkeyTransition) {
            HotkeyOpenTransitionDeadline.store(
                now + HotkeyOpenTransitionWindowMs,
                std::memory_order_release
            );
            HotkeyOpenTransitionTickets.fetch_add(1, std::memory_order_relaxed);
        }
        RemoteClientUiObservedOpen.store(false, std::memory_order_release);
        RemoteClientInventoryCoupled.store(
            plan.coupleInventory,
            std::memory_order_release
        );
        RemoteClientInventoryWasOpenBeforeOpen.store(
            inventoryIsOpen,
            std::memory_order_release
        );
        RemoteClientSessionActive.store(true, std::memory_order_release);
        QueueOutgoingPacket(
            RemoteOpenRequest.data(),
            static_cast<std::int32_t>(RemoteOpenRequest.size())
        );
        OpenRequests.fetch_add(1, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DeactivateRemoteClientSession(false);
        if (Context) {
            Context->LogError("RemoteStash: server open request could not be queued.");
        }
        return false;
    }
}

bool TryCloseRemoteInterfaces(
    bool notifyServer,
    bool closeInventory,
    bool preserveInventory
) noexcept {
    if (!OriginalGetUiState || !OriginalCloseInterfaceState || !MarkUiDirty) {
        return false;
    }
    if (preserveInventory && !OriginalOpenInterfaceState) return false;
    __try {
        const auto inventoryWasOpen =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        DeactivateRemoteClientSession(notifyServer);
        if (OriginalGetUiState(StashInterfaceState) != 0) {
            OriginalCloseInterfaceState(StashInterfaceState, false);
        }
        auto inventoryIsOpen =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        if (closeInventory && inventoryIsOpen) {
            OriginalCloseInterfaceState(InventoryInterfaceState, false);
            inventoryIsOpen = false;
        } else if (preserveInventory
            && ShouldRestoreIndependentInventory(
                inventoryWasOpen,
                inventoryIsOpen
            )) {
            if (!OriginalOpenInterfaceState(InventoryInterfaceState, false)
                || OriginalGetUiState(InventoryInterfaceState) == 0) {
                TryMarkUiDirty();
                if (Context) {
                    Context->LogError(
                        "RemoteStash: independent Inventory could not be restored."
                    );
                }
                return false;
            }
        }
        TryMarkUiDirty();
        PairedInterfaceCloses.fetch_add(1, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TryMarkUiDirty();
        if (Context) {
            Context->LogError("RemoteStash: native toggle close failed.");
        }
        return false;
    }
}

bool TryRollbackRemoteOpen(bool inventoryWasOpenBeforeOpen) noexcept {
    if (!OriginalGetUiState || !OriginalOpenInterfaceState
        || !OriginalCloseInterfaceState || !MarkUiDirty) {
        return false;
    }
    __try {
        DeactivateRemoteClientSession(true);
        if (OriginalGetUiState(StashInterfaceState) != 0) {
            OriginalCloseInterfaceState(StashInterfaceState, false);
        }
        const auto inventoryIsOpenNow =
            OriginalGetUiState(InventoryInterfaceState) != 0;
        const auto rollback = ResolveRemoteOpenRollbackPlan(
            inventoryWasOpenBeforeOpen,
            inventoryIsOpenNow
        );
        if (rollback.closeInventory) {
            OriginalCloseInterfaceState(InventoryInterfaceState, false);
        } else if (rollback.openInventory
            && !OriginalOpenInterfaceState(InventoryInterfaceState, false)) {
            TryMarkUiDirty();
            return false;
        }
        TryMarkUiDirty();
        return OriginalGetUiState(InventoryInterfaceState)
            == static_cast<std::int32_t>(inventoryWasOpenBeforeOpen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        __try {
            TryMarkUiDirty();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }
}

bool TryOpenStashUiFromHotkey() noexcept {
    if (!OriginalGetUiState || !OriginalOpenInterfaceState) return false;
    __try {
        const auto inventoryIsCoupled = RemoteClientInventoryCoupled.load(
            std::memory_order_acquire
        );
        if (OriginalGetUiState(StashInterfaceState) != 0
            && (!inventoryIsCoupled
                || OriginalGetUiState(InventoryInterfaceState) != 0)) {
            return true;
        }

        // Open synchronously on D2R's UI thread, like the native panel
        // hotkeys. The authoritative remote request is queued first; when its
        // 0x77/0x10 response arrives, the native client handler sees the
        // already-visible stash panel and leaves it untouched.
        if (!HookOpenInterfaceState(StashInterfaceState, false)
            || OriginalGetUiState(StashInterfaceState) == 0) {
            if (Context) {
                Context->LogError(
                    "RemoteStash: immediate hotkey UI open failed."
                );
            }
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (Context) {
            Context->LogError(
                "RemoteStash: immediate hotkey UI open raised an exception."
            );
        }
        return false;
    }
}

enum class RemoteToggleExecution : std::uint8_t {
    Refused,
    Dispatched,
    Failed,
};

RemoteToggleExecution TryExecuteRemoteToggle(
    ToggleSource source,
    bool knownInputIsBlocked
) noexcept {
    bool inventoryIsOpen{};
    if (OriginalGetUiState) {
        __try {
            inventoryIsOpen =
                OriginalGetUiState(InventoryInterfaceState) != 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            inventoryIsOpen = false;
        }
    }
    const auto plan = ResolveRemoteTogglePlan(
        source,
        HotkeySettings.mode,
        RemoteClientSessionActive.load(std::memory_order_acquire),
        knownInputIsBlocked,
        inventoryIsOpen
    );
    if (plan.dispatch == HotkeyDispatch::Refuse) {
        return RemoteToggleExecution::Refused;
    }

    if (plan.dispatch == HotkeyDispatch::Close) {
        if (!TryCloseRemoteInterfaces(
                true,
                plan.closeInventoryAfterClose,
                plan.preserveInventoryAfterClose
            )) {
            return RemoteToggleExecution::Failed;
        }
        return RemoteToggleExecution::Dispatched;
    }

    const auto fromHotkey = source == ToggleSource::Hotkey;
    if (!TryQueueRemoteOpenRequest(plan, inventoryIsOpen, fromHotkey)) {
        return RemoteToggleExecution::Failed;
    }
    if (fromHotkey && !TryOpenStashUiFromHotkey()) {
        if (!TryRollbackRemoteOpen(inventoryIsOpen) && Context) {
            Context->LogError(
                "RemoteStash: failed open could not restore the prior Inventory state."
            );
        }
        return RemoteToggleExecution::Failed;
    }
    return RemoteToggleExecution::Dispatched;
}

void* FindNamedWidget(void* panel, const char* name) noexcept {
    if (!panel || !name || !FindChildWidget) return nullptr;
    __try {
        return FindChildWidget(panel, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadWidgetRect(void* widget, WidgetRect& rect) noexcept {
    if (!widget || !GetWidgetRect) return false;
    __try {
        WidgetRect current{};
        if (GetWidgetRect(widget, &current) != &current) return false;
        rect = current;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteWidgetRect(void* widget, const WidgetRect& value) noexcept {
    if (!widget || !HasUsableSize(value)) return false;
    __try {
        auto* rect = reinterpret_cast<WidgetRect*>(
            static_cast<std::uint8_t*>(widget) + WidgetRectOffset
        );
        *rect = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
            Context->LogError("RemoteStash: button state update failed.");
        }
    }
}

void NeutralizeLegacyButtons(void* panel) noexcept {
    for (const auto* name : LegacyButtonNames) {
        auto* legacy = FindNamedWidget(panel, name);
        if (!legacy) continue;
        SetWidgetState(legacy, false);
        LegacyButtonsNeutralized.fetch_add(1, std::memory_order_relaxed);
        if (Context && !FirstLegacyButtonReported.exchange(
                true, std::memory_order_relaxed)) {
            Context->LogWarn(
                "RemoteStash: an obsolete manually merged Remote Stash button was hidden; remove the old Inventory-layout snippet during migration."
            );
        }
    }
}

bool IsSafeAutomaticPlacement(
    const WidgetRect& panel,
    const WidgetRect& candidate,
    const WidgetRect& grid,
    const WidgetRect& footer
) noexcept {
    return ruffneckk::remote_stash::Contains(panel, candidate)
        && (!HasUsableSize(grid)
            || !ruffneckk::remote_stash::Intersects(candidate, grid))
        && (!HasUsableSize(footer)
            || !ruffneckk::remote_stash::Intersects(candidate, footer));
}

bool AddButtonOffsets(WidgetRect& rect) noexcept {
    const auto x = static_cast<std::int64_t>(rect.x)
        + EffectiveButtonSettings.offsetX;
    const auto y = static_cast<std::int64_t>(rect.y)
        + EffectiveButtonSettings.offsetY;
    if (x < std::numeric_limits<std::int32_t>::min()
        || x > std::numeric_limits<std::int32_t>::max()
        || y < std::numeric_limits<std::int32_t>::min()
        || y > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    rect.x = static_cast<std::int32_t>(x);
    rect.y = static_cast<std::int32_t>(y);
    return true;
}

ruffneckk::remote_stash::PlacementResult ResolveButtonPlacement(
    const WidgetRect& panel,
    const WidgetRect& grid,
    const WidgetRect& goldButton,
    const WidgetRect& goldAmount
) noexcept {
    const WidgetRect button{
        .width = EffectiveButtonSettings.width,
        .height = EffectiveButtonSettings.height,
    };
    if (EffectiveButtonSettings.placement == ButtonPlacement::Custom) {
        return PlaceAtAnchor(
            panel,
            button,
            EffectiveButtonSettings.anchor,
            EffectiveButtonSettings.offsetX,
            EffectiveButtonSettings.offsetY,
            false
        );
    }

    const auto footer = UnionRect(goldButton, goldAmount);
    auto preferred = PlaceDesktopFooterLeft(
        panel, grid, goldButton, goldAmount, button);
    if (preferred.valid && AddButtonOffsets(preferred.rect)
        && IsSafeAutomaticPlacement(panel, preferred.rect, grid, footer)) {
        return preferred;
    }

    constexpr std::int32_t Margin = 16;
    const std::array<ButtonAnchor, 5> anchors{
        EffectiveButtonSettings.anchor,
        ButtonAnchor::BottomLeft,
        ButtonAnchor::BottomRight,
        ButtonAnchor::TopLeft,
        ButtonAnchor::TopRight,
    };
    for (const auto anchor : anchors) {
        const auto horizontalMargin = anchor == ButtonAnchor::TopRight
                || anchor == ButtonAnchor::BottomRight
            ? -Margin : Margin;
        const auto verticalMargin = anchor == ButtonAnchor::BottomLeft
                || anchor == ButtonAnchor::BottomRight
            ? -Margin : Margin;
        auto candidate = PlaceAtAnchor(
            panel,
            button,
            anchor,
            EffectiveButtonSettings.offsetX + horizontalMargin,
            EffectiveButtonSettings.offsetY + verticalMargin,
            true
        );
        if (candidate.valid
            && IsSafeAutomaticPlacement(panel, candidate.rect, grid, footer)) {
            return candidate;
        }
    }

    // A radically customized Inventory may expose no safe named anchors.
    // Keep the control usable in a contained corner; players can switch to
    // custom placement when that fallback overlaps their own layout.
    return PlaceAtAnchor(
        panel,
        button,
        EffectiveButtonSettings.anchor,
        EffectiveButtonSettings.offsetX,
        EffectiveButtonSettings.offsetY,
        true
    );
}

void ReportButtonPlacementFailure(const char* reason) noexcept {
    ButtonPlacementFailures.fetch_add(1, std::memory_order_relaxed);
    if (!Context || FirstButtonPlacementFailureReported.exchange(
            true, std::memory_order_relaxed)) {
        return;
    }
    char message[260]{};
    std::snprintf(
        message,
        sizeof(message),
        "RemoteStash: plugin-owned button placement failed (%s); the button stays hidden.",
        reason
    );
    Context->LogWarn(message);
}

void __fastcall HookConfigurePlayerInventory(void* panel) noexcept {
    OriginalConfigurePlayerInventory(panel);
    if (!panel) return;

    NeutralizeLegacyButtons(panel);
    if (!HotkeySettings.inventoryButtonEnabled) return;

    auto* button = FindNamedWidget(panel, ButtonWidgetName);
    if (!button) {
        // Expected for controller layouts because the child registration is
        // explicitly keyboard/mouse-only.
        return;
    }
    SetWidgetState(button, false);

    WidgetRect panelRect{};
    WidgetRect gridRect{};
    WidgetRect goldButtonRect{};
    WidgetRect goldAmountRect{};
    const auto panelOk = ReadWidgetRect(panel, panelRect);
    (void)ReadWidgetRect(FindNamedWidget(panel, "grid"), gridRect);
    (void)ReadWidgetRect(
        FindNamedWidget(panel, "gold_button"), goldButtonRect);
    (void)ReadWidgetRect(
        FindNamedWidget(panel, "gold_amount"), goldAmountRect);
    if (!panelOk || !HasUsableSize(panelRect)) {
        ReportButtonPlacementFailure("Inventory panel geometry is unavailable");
        return;
    }

    panelRect.x = 0;
    panelRect.y = 0;
    const auto placement = ResolveButtonPlacement(
        panelRect, gridRect, goldButtonRect, goldAmountRect);
    if (!placement.valid || !WriteWidgetRect(button, placement.rect)) {
        ReportButtonPlacementFailure("the configured rectangle is unusable");
        return;
    }

    SetWidgetState(button, true);
    ButtonPlacements.fetch_add(1, std::memory_order_relaxed);
    if (Context && !FirstButtonPlacementReported.exchange(
            true, std::memory_order_relaxed)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "RemoteStash: plugin-owned button placed at %d,%d with size %dx%d (%s, anchor=%s, sprite=%s).",
            placement.rect.x,
            placement.rect.y,
            placement.rect.width,
            placement.rect.height,
            ButtonPlacementName(EffectiveButtonSettings.placement),
            ButtonAnchorName(EffectiveButtonSettings.anchor),
            ButtonSpriteSource.c_str()
        );
        Context->LogInfo(message);
    }
}

auto __cdecl OnRemoteStashUiMessage(
    const D2RL::PluginContext*,
    const D2RL::SharedEvents::UiMessageEvent* event,
    void*
) noexcept -> D2RL::SharedEvents::UiMessageAction {
    if (event == nullptr
        || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
        || event->target == nullptr
        || event->command == nullptr
        || event->text == nullptr
        || !IsRemoteStashUiMessage(
            event->target,
            event->command,
            event->text
        )) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    SdkButtonActivations.fetch_add(1, std::memory_order_relaxed);
    LogDiagnosticOnce(
        FirstButtonActivationReported,
        "RemoteStash diagnostics: first SDK Inventory button activation received.");
    if (TryExecuteRemoteToggle(ToggleSource::Button, false)
            == RemoteToggleExecution::Failed) {
        SdkButtonFailures.fetch_add(1, std::memory_order_relaxed);
        if (Context) {
            Context->LogError("RemoteStash: SDK inventory button toggle failed.");
        }
    }
    // The exact plugin-owned message is never forwarded to PanelManager. This
    // remains fail-closed even when the requested toggle cannot be completed.
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

void UnregisterSdkButtonListener() noexcept {
    if (Context && SharedEventService
        && ButtonMessageListener != D2RL::SharedEvents::InvalidHandle) {
        (void)SharedEventService->unregisterUiMessageListener(
            Context,
            ButtonMessageListener
        );
    }
    ButtonMessageListener = D2RL::SharedEvents::InvalidHandle;
}

bool RegisterSdkButtonListener() noexcept {
    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags = 0,
        .priority = 10'000,
        .reserved = 0,
        .callback = OnRemoteStashUiMessage,
        .userData = nullptr,
    };
    const auto listenerResult = SharedEventService->registerUiMessageListener(
        Context,
        &listener,
        &ButtonMessageListener
    );
    if (listenerResult != D2RL::SharedEvents::Result::Success
        || ButtonMessageListener == D2RL::SharedEvents::InvalidHandle) {
        ButtonMessageListener = D2RL::SharedEvents::InvalidHandle;
        Context->LogError(
            "RemoteStash: SDK button message listener registration failed."
        );
        return false;
    }
    return true;
}

bool CurrentProcessOwnsForegroundWindow() noexcept {
    const auto foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId{};
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

bool ModifierDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

void ReplayRefusedHotkey() noexcept {
    std::array<INPUT, 2> input{};
    if (!IsMouseHotkey(HotkeySettings.hotkey)) {
        const auto replayKey = UsesSdkInput
            ? ReplayVirtualKey.load(std::memory_order_acquire)
            : HotkeySettings.hotkey.virtualKey;
        if (replayKey == 0) return;
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = static_cast<WORD>(replayKey);
        input[1] = input[0];
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    } else {
        input[0].type = INPUT_MOUSE;
        input[1].type = INPUT_MOUSE;
        switch (HotkeySettings.hotkey.virtualKey) {
        case VK_MBUTTON:
            input[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            break;
        case VK_XBUTTON1:
        case VK_XBUTTON2:
            input[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
            input[1].mi.dwFlags = MOUSEEVENTF_XUP;
            input[0].mi.mouseData = HotkeySettings.hotkey.virtualKey == VK_XBUTTON1
                ? XBUTTON1 : XBUTTON2;
            input[1].mi.mouseData = input[0].mi.mouseData;
            break;
        default:
            return;
        }
    }
    // The low-level hook explicitly ignores injected input, so a request that
    // the UI thread refuses is replayed once to preserve D2R's normal action.
    if (SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT))
            != input.size() && Context) {
        Context->LogWarn(
            "RemoteStash: a refused hotkey could not be replayed to the game.");
    }
}

void ProcessQueuedHotkeyRequest() noexcept {
    if (!HotkeyRequestPending.exchange(false, std::memory_order_acq_rel)) return;
    const auto result = TryExecuteRemoteToggle(
        ToggleSource::Hotkey,
        KnownInputIsBlocked()
    );
    if (result == RemoteToggleExecution::Refused) {
        HotkeyRefusedRequests.fetch_add(1, std::memory_order_relaxed);
        ReplayRefusedHotkey();
        return;
    }
    if (result == RemoteToggleExecution::Dispatched) {
        HotkeyDispatchedRequests.fetch_add(1, std::memory_order_relaxed);
        LogDiagnosticOnce(
            FirstHotkeyDispatchReported,
            "RemoteStash diagnostics: first hotkey request dispatched on the game UI thread.");
        return;
    }
    HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
    ReplayRefusedHotkey();
}

void __cdecl ProcessQueuedHotkeyRequestOnUiThread(
    const D2RL::PluginContext*,
    void*
) noexcept {
    ProcessQueuedHotkeyRequest();
}

bool QueueHotkeyRequestCore() noexcept {
    bool noRequest{};
    if (!HotkeyRequestPending.compare_exchange_strong(
            noRequest,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        HotkeyCoalescedRequests.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (!ThreadService) {
        HotkeyRequestPending.store(false, std::memory_order_release);
        HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (ThreadService->runOnUiThread(
            Context,
            ProcessQueuedHotkeyRequestOnUiThread,
            nullptr) != D2RL::Threads::Result::Success) {
        HotkeyRequestPending.store(false, std::memory_order_release);
        HotkeyFailedRequests.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    HotkeyAcceptedRequests.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool QueueHotkeyRequest() noexcept {
    if (!CurrentProcessOwnsForegroundWindow()) return false;
    if (!ExactModifiersMatch(
            HotkeySettings.hotkey,
            ModifierDown(VK_CONTROL),
            ModifierDown(VK_SHIFT),
            ModifierDown(VK_MENU))) {
        return false;
    }
    return QueueHotkeyRequestCore();
}

D2RL::Input::ActionResult __cdecl OnSdkInputAction(
    const D2RL::PluginContext*,
    const D2RL::Input::ActionEvent* event,
    void*
) noexcept {
    if (!D2RL::Input::HasActionEventField(
            event, D2RL::Input::ActionEventRequiredSize)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind == D2RL::Input::ActionEventKind::Released) {
        return HotkeyCaptured.exchange(false, std::memory_order_acq_rel)
            ? D2RL::Input::ActionResult::Handled
            : D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }

    ReplayVirtualKey.store(
        static_cast<std::uint32_t>(event->binding.key),
        std::memory_order_release);
    const auto accepted = QueueHotkeyRequestCore();
    HotkeyCaptured.store(accepted, std::memory_order_release);
    return accepted
        ? D2RL::Input::ActionResult::Handled
        : D2RL::Input::ActionResult::Ignored;
}

bool RegisterSdkInput() noexcept {
    const D2RL::Input::ActionRegistration registration{
        .structSize = D2RL::Input::ActionRegistrationSize,
        .flags = 0,
        .logicalId = "toggle-remote-stash",
        .displayName = "Toggle Remote Stash",
        .category = "RuffnecKk Suite",
        .defaultPrimary = {
            static_cast<D2RL::Input::Key>(HotkeySettings.hotkey.virtualKey),
            static_cast<D2RL::Input::Modifier>(
                SdkModifierValue(HotkeySettings.hotkey)),
        },
        .defaultSecondary = {
            D2RL::Input::Key::None,
            D2RL::Input::Modifier::None,
        },
        .callback = OnSdkInputAction,
        .userData = nullptr,
    };
    const auto result = InputService->registerAction(
        Context, &registration, &InputAction);
    if (result == D2RL::Input::Result::Success
        && InputAction != D2RL::Input::InvalidHandle) {
        return true;
    }
    char message[180]{};
    std::snprintf(
        message,
        sizeof(message),
        "RemoteStash: Input v1 action registration failed with result %u.",
        static_cast<unsigned>(result));
    Context->LogError(message);
    InputAction = D2RL::Input::InvalidHandle;
    return false;
}

bool HandleInputTransition(bool isDown, bool isUp, bool injected) noexcept {
    if (InputStopping.load(std::memory_order_acquire)) return false;
    if (isUp) {
        HotkeyPressed.store(false, std::memory_order_release);
        const auto captured = HotkeyCaptured.exchange(false, std::memory_order_acq_rel);
        return captured
            && CurrentProcessOwnsForegroundWindow();
    }
    if (!isDown || injected || !CurrentProcessOwnsForegroundWindow()) return false;
    const auto modifiersMatch = ExactModifiersMatch(
        HotkeySettings.hotkey,
        ModifierDown(VK_CONTROL),
        ModifierDown(VK_SHIFT),
        ModifierDown(VK_MENU)
    );
    if (!modifiersMatch) return false;

    const auto firstDown = !HotkeyPressed.exchange(true, std::memory_order_acq_rel);
    if (!firstDown) {
        return HotkeyCaptured.load(std::memory_order_acquire);
    }
    // Capture only a request that was actually handed to the game UI thread.
    // Focus/modifier mismatches and unavailable handoff paths remain native.
    const auto accepted = QueueHotkeyRequest();
    HotkeyCaptured.store(accepted, std::memory_order_release);
    return accepted;
}

LRESULT CALLBACK KeyboardHook(
    int code,
    WPARAM message,
    LPARAM parameter
) noexcept {
    if (code != HC_ACTION || !parameter) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    const auto* input = reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
    if (IsMouseHotkey(HotkeySettings.hotkey)
        || input->vkCode != HotkeySettings.hotkey.virtualKey) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }

    const auto isDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const auto isUp = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!isDown && !isUp) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    if (HandleInputTransition(
            isDown,
            isUp,
            (input->flags & LLKHF_INJECTED) != 0
        )) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

LRESULT CALLBACK MouseHook(
    int code,
    WPARAM message,
    LPARAM parameter
) noexcept {
    if (code != HC_ACTION || !parameter || !IsMouseHotkey(HotkeySettings.hotkey)) {
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    const auto* input = reinterpret_cast<const MSLLHOOKSTRUCT*>(parameter);
    bool matches{};
    bool isDown{};
    bool isUp{};
    switch (HotkeySettings.hotkey.virtualKey) {
    case VK_MBUTTON:
        matches = message == WM_MBUTTONDOWN || message == WM_MBUTTONUP;
        isDown = message == WM_MBUTTONDOWN;
        isUp = message == WM_MBUTTONUP;
        break;
    case VK_XBUTTON1:
    case VK_XBUTTON2: {
        const auto expected = HotkeySettings.hotkey.virtualKey == VK_XBUTTON1
            ? XBUTTON1
            : XBUTTON2;
        matches = (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
            && HIWORD(input->mouseData) == expected;
        isDown = message == WM_XBUTTONDOWN;
        isUp = message == WM_XBUTTONUP;
        break;
    }
    default:
        break;
    }
    if (!matches) return CallNextHookEx(nullptr, code, message, parameter);
    if (HandleInputTransition(
            isDown,
            isUp,
            (input->flags & LLMHF_INJECTED) != 0
        )) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, parameter);
}

DWORD WINAPI InputThreadProc(void* parameter) noexcept {
    const auto module = static_cast<HMODULE>(parameter);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    const auto inputHook = SetWindowsHookExW(
        IsMouseHotkey(HotkeySettings.hotkey) ? WH_MOUSE_LL : WH_KEYBOARD_LL,
        IsMouseHotkey(HotkeySettings.hotkey) ? MouseHook : KeyboardHook,
        module,
        0
    );
    if (!inputHook) {
        if (inputHook) UnhookWindowsHookEx(inputHook);
        InputThreadFailed.store(true, std::memory_order_release);
        InputThreadReady.store(true, std::memory_order_release);
        FreeLibraryAndExitThread(module, 1);
    }

    InputThreadReady.store(true, std::memory_order_release);
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnhookWindowsHookEx(inputHook);
    FreeLibraryAndExitThread(module, 0);
}

bool StartInput() noexcept {
    InputThreadReady.store(false, std::memory_order_relaxed);
    InputThreadFailed.store(false, std::memory_order_relaxed);
    InputStopping.store(false, std::memory_order_release);
    HMODULE workerModule{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&InputThreadProc),
            &workerModule)) {
        return false;
    }
    InputThread = CreateThread(
        nullptr,
        0,
        InputThreadProc,
        workerModule,
        0,
        &InputThreadId
    );
    if (!InputThread) {
        FreeLibrary(workerModule);
        return false;
    }
    for (unsigned attempt = 0;
         attempt < 200 && !InputThreadReady.load(std::memory_order_acquire);
         ++attempt) {
        Sleep(10);
    }
    return InputThreadReady.load(std::memory_order_acquire)
        && !InputThreadFailed.load(std::memory_order_acquire);
}

bool StopInput() noexcept {
    InputStopping.store(true, std::memory_order_release);
    if (InputThreadId != 0) PostThreadMessageW(InputThreadId, WM_QUIT, 0, 0);
    if (InputThread) {
        const auto wait = WaitForSingleObject(InputThread, 3000);
        if (wait != WAIT_OBJECT_0) {
            if (Context) {
                Context->LogError(
                    "RemoteStash: hotkey input worker did not stop; its module reference is retained for safety."
                );
            }
            return false;
        }
        CloseHandle(InputThread);
    }
    InputThread = nullptr;
    InputThreadId = 0;
    return true;
}

void CleanupFailedLoadSdkState() noexcept {
    UnregisterSdkButtonListener();
    UnregisterOwnedButton();
    UiDispatchReady.store(false, std::memory_order_release);
    if (!UsesSdkInput) {
        (void)StopInput();
    }
    if (InputService && Context
        && InputAction != D2RL::Input::InvalidHandle) {
        (void)InputService->unregisterAction(Context, InputAction);
    }
    InputAction = D2RL::Input::InvalidHandle;
    if (LifecycleService && Context
        && GameJoinedListener != D2RL::Lifecycle::InvalidHandle) {
        (void)LifecycleService->unregisterGameplayEventListener(
            Context,
            GameJoinedListener
        );
    }
    GameJoinedListener = D2RL::Lifecycle::InvalidHandle;
    if (LifecycleService && Context
        && GameLeftListener != D2RL::Lifecycle::InvalidHandle) {
        (void)LifecycleService->unregisterGameplayEventListener(
            Context,
            GameLeftListener
        );
    }
    GameLeftListener = D2RL::Lifecycle::InvalidHandle;
}

void* AllocateCallSiteRelayPageNear(void* hint) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity
    );
    const auto base = reinterpret_cast<std::uintptr_t>(hint) & ~(granularity - 1);

    for (std::uintptr_t delta = granularity;
         delta < 0x70000000ULL;
         delta += granularity) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta),
                systemInfo.dwPageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE
            )) {
            return memory;
        }
    }
    return nullptr;
}

bool WriteAbsoluteJumpRelay(
    std::uint8_t* destination,
    const void* target
) noexcept {
    if (!destination || !target) return false;
    constexpr std::size_t RelaySize = 14;
    std::array<std::uint8_t, RelaySize> relay{
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const auto targetAddress = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(relay.data() + 6, &targetAddress, sizeof(targetAddress));
    std::memcpy(destination, relay.data(), relay.size());
    return true;
}

template<std::size_t Count>
bool PatchCallSites(
    const std::array<RelativeCallSite, Count>& sites,
    std::uintptr_t relayRva
) noexcept {
    for (const auto& site : sites) {
        if (!Context->PatchCallRel32(
                site.rva,
                site.expected.data(),
                static_cast<std::uint32_t>(site.expected.size()),
                relayRva,
                static_cast<std::uint32_t>(site.expected.size())
            )) {
            return false;
        }
    }
    return true;
}

bool InstallComposableCallSiteRedirects() noexcept {
    constexpr std::size_t RelayStride = 16;
    constexpr std::size_t RelayCount = 5;
    constexpr std::size_t RelayBytes = RelayStride * RelayCount;

    CallSiteRelayPage = AllocateCallSiteRelayPageNear(
        Base + IsRoomInTownCallSites.front().rva
    );
    if (!CallSiteRelayPage) return false;

    auto* relays = static_cast<std::uint8_t*>(CallSiteRelayPage);
    if (!WriteAbsoluteJumpRelay(
            relays,
            reinterpret_cast<const void*>(&HookIsRoomInTown)
        )
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride,
            reinterpret_cast<const void*>(&HookTransferItemToInventoryPage)
        )
        || (HotkeySettings.hotkeyEnabled && !WriteAbsoluteJumpRelay(
            relays + RelayStride * 2,
            reinterpret_cast<const void*>(&HookStashInterfaceTransition)
        ))
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride * 3,
            reinterpret_cast<const void*>(&HookMovementUiClose)
        )
        || !WriteAbsoluteJumpRelay(
            relays + RelayStride * 4,
            reinterpret_cast<const void*>(&HookSharedGoldDeposit)
        )) {
        VirtualFree(CallSiteRelayPage, 0, MEM_RELEASE);
        CallSiteRelayPage = nullptr;
        return false;
    }

    DWORD previousProtection{};
    if (!VirtualProtect(
            CallSiteRelayPage,
            RelayBytes,
            PAGE_EXECUTE_READ,
            &previousProtection
        )) {
        VirtualFree(CallSiteRelayPage, 0, MEM_RELEASE);
        CallSiteRelayPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), CallSiteRelayPage, RelayBytes);

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(CallSiteRelayPage);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress) return false;
    const auto relayRva = relayAddress - baseAddress;
    const auto coreRedirectsInstalled =
        PatchCallSites(IsRoomInTownCallSites, relayRva)
        && PatchCallSites(
            TransferItemToInventoryPageCallSites,
            relayRva + RelayStride
        )
        && PatchCallSites(
            MovementUiCloseCallSites,
            relayRva + RelayStride * 3
        )
        && PatchCallSites(
            SharedGoldDepositCallSites,
            relayRva + RelayStride * 4
        );
    if (!coreRedirectsInstalled) return false;
    return !HotkeySettings.hotkeyEnabled
        || PatchCallSites(
            StashInterfaceTransitionCallSites,
            relayRva + RelayStride * 2
        );
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[2400]{};
    std::snprintf(
        message,
        sizeof(message),
        "Remote Stash 2.0.0: enabled=%s; diagnostics=%s; hotkeyEnabled=%s; "
        "hotkey=%s; hotkeyMode=%s; hotkeyInput=%s; hotkeyUiDispatch=%s; "
        "inventoryButtonEnabled=%s; buttonListener=%s; buttonChild=%s; "
        "buttonResources=%s; buttonPlacement=%s; buttonAnchor=%s; "
        "buttonOffset=%d,%d; buttonSize=%dx%d; buttonFrames=%u/%u/%u/%u; "
        "buttonSprite=%s; buttonPlacements=%llu; buttonPlacementFailures=%llu; "
        "legacyButtonsNeutralized=%llu; config=%s; hotkeyAccepted=%llu; "
        "hotkeyCoalesced=%llu; hotkeyDispatched=%llu; hotkeyRefused=%llu; "
        "hotkeyFailed=%llu; sdkButtonActivations=%llu; sdkButtonFailures=%llu; "
        "hotkeyTransitionTickets=%llu; hotkeyTransitionApplications=%llu; "
        "hotkeyTransitionExpirations=%llu; hotkeyMouseResetSuppressions=%llu; "
        "pairedInterfaceCloses=%llu; movementCloseSuppressions=%llu; "
        "generalUiCloses=%llu; serverUiCloses=%llu; "
        "clientOpenRequests=%llu; clientCloseRequests=%llu; sessionsOpened=%llu; "
        "sessionsClosed=%llu; sessionsPruned=%llu; activeSessions=%llu; "
        "remoteItemOps=%llu; remoteItemFailures=%llu; maxRemoteItemMs=%llu; "
        "remoteGoldOps=%llu; "
        "remoteGoldFailures=%llu; stashProximityBypasses=%llu; "
        "clientTownBypasses=%llu; sharedTransferOps=%llu; "
        "sharedTransferFailures=%llu; quickMoveUiBypasses=%llu.",
        HotkeySettings.enabled ? "true" : "false",
        HotkeySettings.diagnostics ? "true" : "false",
        HotkeySettings.hotkeyEnabled ? "true" : "false",
        HotkeySettings.hotkeyText.c_str(),
        HotkeyModeName(HotkeySettings.mode),
        !HotkeySettings.hotkeyEnabled ? "disabled"
            : (UsesSdkInput ? "SDK Input v1" : "native compatibility fallback"),
        UiDispatchReady.load(std::memory_order_acquire) ? "ready" : "disabled",
        HotkeySettings.inventoryButtonEnabled ? "true" : "false",
        ButtonMessageListener != D2RL::SharedEvents::InvalidHandle
            ? "registered" : "disabled",
        ButtonChildLayout != D2RL::Panels::InvalidChildLayoutHandle
            ? "registered" : "disabled",
        ButtonLayoutResource != D2RL::Resources::InvalidHandle
                && ButtonSpriteResource != D2RL::Resources::InvalidHandle
                && ButtonLowendSpriteResource != D2RL::Resources::InvalidHandle
            ? "registered" : "disabled",
        ButtonPlacementName(EffectiveButtonSettings.placement),
        ButtonAnchorName(EffectiveButtonSettings.anchor),
        EffectiveButtonSettings.offsetX,
        EffectiveButtonSettings.offsetY,
        EffectiveButtonSettings.width,
        EffectiveButtonSettings.height,
        EffectiveButtonSettings.normalFrame,
        EffectiveButtonSettings.pressedFrame,
        EffectiveButtonSettings.disabledFrame,
        EffectiveButtonSettings.hoveredFrame,
        ButtonSpriteSource.c_str(),
        static_cast<unsigned long long>(
            ButtonPlacements.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ButtonPlacementFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            LegacyButtonsNeutralized.load(std::memory_order_relaxed)),
        LoadedConfigPath.c_str(),
        static_cast<unsigned long long>(
            HotkeyAcceptedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyCoalescedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyDispatchedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyRefusedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyFailedRequests.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            SdkButtonActivations.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            SdkButtonFailures.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyOpenTransitionTickets.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyOpenTransitionApplications.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyOpenTransitionExpirations.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyMouseResetSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            PairedInterfaceCloses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteMovementCloseSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteGeneralUiCloses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteServerUiCloses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(OpenRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CloseRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ServerSessionsOpened.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ServerSessionsClosed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ServerSessionsPruned.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteSessionCount()),
        static_cast<unsigned long long>(RemoteItemOperations.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteItemFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(MaxRemoteItemOperationMs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteGoldTransactions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RemoteGoldFailures.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RemoteStashProximityBypasses.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(RemoteTownBypasses.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RemoteSharedTransferOperations.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteSharedTransferFailures.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            RemoteQuickMoveUiBypasses.load(std::memory_order_relaxed)
        )
    );
    command->plugin->WriteConsoleMessage(message);
    char inputDiagnostic[144]{};
    std::snprintf(
        inputDiagnostic,
        sizeof(inputDiagnostic),
        "RemoteStash input: hotkeyMouseResetSuppressions=%llu; "
        "hotkeyMouseStateRestorations=%llu",
        static_cast<unsigned long long>(
            HotkeyMouseResetSuppressions.load(std::memory_order_relaxed)
        ),
        static_cast<unsigned long long>(
            HotkeyMouseStateRestorations.load(std::memory_order_relaxed)
        )
    );
    command->plugin->WriteConsoleMessage(inputDiagnostic);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand(
            "remote-stash",
            Status,
            "Show Remote Stash status and counters."
        )) {
        Context->LogWarn("RemoteStash: status command could not be registered.");
    }
}
} // namespace

namespace RuffnecKk::RemoteStash {

bool Load(
    const D2RL::PluginContext* context
) noexcept {
    if (!D2RL::HasContext(context)
        || context->apiVersion < D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    OpenRequests.store(0, std::memory_order_relaxed);
    CloseRequests.store(0, std::memory_order_relaxed);
    ServerSessionsOpened.store(0, std::memory_order_relaxed);
    ServerSessionsClosed.store(0, std::memory_order_relaxed);
    ServerSessionsPruned.store(0, std::memory_order_relaxed);
    RemoteItemOperations.store(0, std::memory_order_relaxed);
    RemoteItemFailures.store(0, std::memory_order_relaxed);
    MaxRemoteItemOperationMs.store(0, std::memory_order_relaxed);
    RemoteStashProximityBypasses.store(0, std::memory_order_relaxed);
    RemoteTownBypasses.store(0, std::memory_order_relaxed);
    RemoteSharedTransferOperations.store(0, std::memory_order_relaxed);
    RemoteSharedTransferFailures.store(0, std::memory_order_relaxed);
    RemoteGoldTransactions.store(0, std::memory_order_relaxed);
    RemoteGoldFailures.store(0, std::memory_order_relaxed);
    RemoteQuickMoveUiBypasses.store(0, std::memory_order_relaxed);
    HotkeySettings = {};
    EffectiveButtonSettings = {};
    LoadedConfigPath = "config/ruffneckk-remote-stash.toml";
    ButtonSpriteSource = "embedded RuffnecKk chest";
    InputService = nullptr;
    ThreadService = nullptr;
    LifecycleService = nullptr;
    SharedEventService = nullptr;
    PanelService = nullptr;
    ResourceService = nullptr;
    InputAction = D2RL::Input::InvalidHandle;
    ButtonMessageListener = D2RL::SharedEvents::InvalidHandle;
    GameJoinedListener = D2RL::Lifecycle::InvalidHandle;
    GameLeftListener = D2RL::Lifecycle::InvalidHandle;
    ButtonChildLayout = D2RL::Panels::InvalidChildLayoutHandle;
    ButtonLayoutResource = D2RL::Resources::InvalidHandle;
    ButtonSpriteResource = D2RL::Resources::InvalidHandle;
    ButtonLowendSpriteResource = D2RL::Resources::InvalidHandle;
    UsesSdkInput = false;
    InputThread = nullptr;
    InputThreadId = 0;
    InputThreadReady.store(false, std::memory_order_relaxed);
    InputThreadFailed.store(false, std::memory_order_relaxed);
    InputStopping.store(false, std::memory_order_relaxed);
    UiDispatchReady.store(false, std::memory_order_relaxed);
    HotkeyPressed.store(false, std::memory_order_relaxed);
    HotkeyCaptured.store(false, std::memory_order_relaxed);
    ReplayVirtualKey.store(0, std::memory_order_relaxed);
    HotkeyRequestPending.store(false, std::memory_order_relaxed);
    HotkeyAcceptedRequests.store(0, std::memory_order_relaxed);
    HotkeyCoalescedRequests.store(0, std::memory_order_relaxed);
    HotkeyDispatchedRequests.store(0, std::memory_order_relaxed);
    HotkeyRefusedRequests.store(0, std::memory_order_relaxed);
    HotkeyFailedRequests.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionDeadline.store(0, std::memory_order_relaxed);
    CompanionInventoryCloseDeadline.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionTickets.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionApplications.store(0, std::memory_order_relaxed);
    HotkeyOpenTransitionExpirations.store(0, std::memory_order_relaxed);
    HotkeyMouseResetSuppressions.store(0, std::memory_order_relaxed);
    HotkeyMouseStateRestorations.store(0, std::memory_order_relaxed);
    PairedInterfaceCloses.store(0, std::memory_order_relaxed);
    SdkButtonActivations.store(0, std::memory_order_relaxed);
    SdkButtonFailures.store(0, std::memory_order_relaxed);
    ButtonPlacements.store(0, std::memory_order_relaxed);
    ButtonPlacementFailures.store(0, std::memory_order_relaxed);
    LegacyButtonsNeutralized.store(0, std::memory_order_relaxed);
    FindTopLevelPanel = nullptr;
    OriginalConfigurePlayerInventory = nullptr;
    FindChildWidget = nullptr;
    GetWidgetRect = nullptr;
    OriginalOpenInterfaceState = nullptr;
    OriginalCloseInterfaceState = nullptr;
    OriginalMovementUiClose = nullptr;
    OriginalStashInterfaceTransition = nullptr;
    OriginalResetMouseInputState = nullptr;
    OriginalResetMouseInputStateWithFinalize = nullptr;
    MarkUiDirty = nullptr;
    RemoteClientSessionActive.store(false, std::memory_order_relaxed);
    RemoteClientUiObservedOpen.store(false, std::memory_order_relaxed);
    RemoteClientInventoryCoupled.store(false, std::memory_order_relaxed);
    RemoteClientInventoryWasOpenBeforeOpen.store(false, std::memory_order_relaxed);
    RemoteQuickMoveWithdrawalDeadline.store(0, std::memory_order_relaxed);
    RemoteMovementCloseSuppressions.store(0, std::memory_order_relaxed);
    RemoteGeneralUiCloses.store(0, std::memory_order_relaxed);
    RemoteServerUiCloses.store(0, std::memory_order_relaxed);
    FirstHotkeyDispatchReported.store(false, std::memory_order_relaxed);
    FirstButtonActivationReported.store(false, std::memory_order_relaxed);
    FirstButtonPlacementReported.store(false, std::memory_order_relaxed);
    FirstButtonPlacementFailureReported.store(false, std::memory_order_relaxed);
    FirstLegacyButtonReported.store(false, std::memory_order_relaxed);
    FirstServerSessionReported.store(false, std::memory_order_relaxed);
    RemoteItemScope = false;
    RemoteGoldScope = false;
    RemoteHotkeyOpenTransitionScope = false;
    RemoteMovementUiCloseScope = false;
    GoldRangeStub = nullptr;
    GoldRangeTrampoline = nullptr;
    CallSiteRelayPage = nullptr;
    OriginalSharedGoldDeposit = nullptr;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.clear();
    } catch (...) {
        return false;
    }

    if (!Base) {
        context->LogError("RemoteStash: D2R executable base is unavailable.");
        return false;
    }
    if (!LoadConfig()) return false;
    EffectiveButtonSettings = HotkeySettings.button;
    if (!HotkeySettings.enabled) {
        RegisterStatusCommand();
        context->LogInfo(
            "Remote Stash 2.0.0 by RuffnecKk disabled; no hook, input action, listener, resource, or child layout was registered.");
        return true;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError("RemoteStash: only D2R build 92777 is supported.");
        return false;
    }
    if (!QuerySdkServices() || !ValidateRuntime()) {
        context->LogError("RemoteStash: 92777 native signature mismatch; plugin refused.");
        return false;
    }
    if (HotkeySettings.hotkeyEnabled && !ValidateHotkeyRuntime()) {
        context->LogError(
            "RemoteStash: 92777 hotkey UI/input signature mismatch; plugin refused."
        );
        return false;
    }
    if (!RegisterLifecycleListeners()) return false;
    if (HotkeySettings.hotkeyEnabled && UsesSdkInput && !RegisterSdkInput()) {
        return false;
    }
    UiDispatchReady.store(
        HotkeySettings.hotkeyEnabled && ThreadService != nullptr,
        std::memory_order_release);

    OriginalOpenInterfaceState = At<OpenInterfaceStateFn>(OpenInterfaceStateRva);
    MarkUiDirty = At<MarkUiDirtyFn>(MarkUiDirtyRva);
    FindChildWidget = At<FindChildWidgetFn>(FindChildWidgetRva);
    GetWidgetRect = At<GetWidgetRectFn>(GetWidgetRectRva);
    if (HotkeySettings.hotkeyEnabled) {
        FindTopLevelPanel = At<FindTopLevelPanelFn>(FindTopLevelPanelRva);
        OriginalStashInterfaceTransition = At<StashInterfaceTransitionFn>(
            StashInterfaceTransitionRva
        );
    }
    SendServerUi = At<SendServerUiFn>(SendServerUiRva);
    GetClientFromPlayer = At<GetClientFromPlayerFn>(GetClientFromPlayerRva);
    GetLocalDataContext = At<GetLocalDataContextFn>(GetLocalDataContextRva);
    GetLocalPlayer = At<GetLocalPlayerFn>(GetLocalPlayerRva);
    QueueOutgoingPacket = At<QueueOutgoingPacketFn>(QueueOutgoingPacketRva);
    OriginalSharedGoldDeposit = At<SharedGoldDepositFn>(
        SharedGoldDepositRva
    );
    OriginalIsRoomInTown = At<IsRoomInTownFn>(IsRoomInTownRva);
    OriginalTransferItemToInventoryPage = At<TransferItemToInventoryPageFn>(
        TransferItemToInventoryPageRva
    );
    OriginalMovementUiClose = At<MovementUiCloseFn>(MovementUiCloseRva);
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.configurePlayerInventory"),
            ConfigurePlayerInventoryRva,
            ConfigurePlayerInventoryExpected.data(),
            static_cast<std::uint32_t>(
                ConfigurePlayerInventoryExpected.size()),
            HookConfigurePlayerInventory,
            &OriginalConfigurePlayerInventory
        )) {
        context->LogError(
            "RemoteStash: Inventory button placement hook failed.");
        return false;
    }
    if (!InstallComposableCallSiteRedirects()) {
        context->LogError(
            "RemoteStash: composable town-state, quick-move, movement, or shared-gold "
            "call-site redirect failed."
        );
        return false;
    }
    if (!CreateGoldRangeStub()) {
        context->LogError("RemoteStash: scoped stash-range hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.getUiState"),
            GetUiStateRva,
            GetUiStateExpected.data(),
            static_cast<std::uint32_t>(GetUiStateExpected.size()),
            HookGetUiState,
            &OriginalGetUiState
        )) {
        context->LogError("RemoteStash: scoped quick-move UI-state hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.closeInterfaceState"),
            CloseInterfaceStateRva,
            CloseInterfaceStateExpected.data(),
            static_cast<std::uint32_t>(CloseInterfaceStateExpected.size()),
            HookCloseInterfaceState,
            &OriginalCloseInterfaceState
        )) {
        context->LogError("RemoteStash: persistent remote UI hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.openInterfaceState"),
            OpenInterfaceStateRva,
            OpenInterfaceStateExpected.data(),
            static_cast<std::uint32_t>(OpenInterfaceStateExpected.size()),
            HookOpenInterfaceState,
            &OriginalOpenInterfaceState
        )) {
        context->LogError("RemoteStash: paired interface open hook failed.");
        return false;
    }
    if (HotkeySettings.hotkeyEnabled
        && !InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.resetMouseInputFinalize"),
            ResetMouseInputStateWithFinalizeRva,
            ResetMouseInputStateWithFinalizeExpected.data(),
            static_cast<std::uint32_t>(
                ResetMouseInputStateWithFinalizeExpected.size()
            ),
            HookResetMouseInputStateWithFinalize,
            &OriginalResetMouseInputStateWithFinalize
        )) {
        context->LogError(
            "RemoteStash: scoped held-click state restoration hook failed."
        );
        return false;
    }
    if (HotkeySettings.hotkeyEnabled
        && !InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.resetMouseInput"),
            ResetMouseInputStateRva,
            ResetMouseInputStateExpected.data(),
            static_cast<std::uint32_t>(ResetMouseInputStateExpected.size()),
            HookResetMouseInputState,
            &OriginalResetMouseInputState
        )) {
        context->LogError("RemoteStash: scoped held-click preservation hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.goldButtonHandler"),
            GoldButtonHandlerRva,
            GoldButtonHandlerExpected.data(),
            static_cast<std::uint32_t>(GoldButtonHandlerExpected.size()),
            HookGoldButtonHandler,
            &OriginalGoldButtonHandler
        )) {
        context->LogError("RemoteStash: stash gold-button hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.removeServerUnit"),
            RemoveServerUnitRva,
            RemoveServerUnitExpected.data(),
            static_cast<std::uint32_t>(RemoveServerUnitExpected.size()),
            HookRemoveServerUnit,
            &OriginalRemoveServerUnit
        )) {
        context->LogError("RemoteStash: player-session lifecycle hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.removeItemHandler"),
            RemoveItemHandlerRva,
            RemoveItemHandlerExpected.data(),
            static_cast<std::uint32_t>(RemoveItemHandlerExpected.size()),
            HookRemoveItemHandler,
            &OriginalRemoveItemHandler
        )) {
        context->LogError("RemoteStash: stash item-removal hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.validateItemPacketState"),
            ValidateItemPacketStateRva,
            ValidateItemPacketStateExpected.data(),
            static_cast<std::uint32_t>(ValidateItemPacketStateExpected.size()),
            HookValidateItemPacketState,
            &OriginalValidateItemPacketState
        )) {
        context->LogError("RemoteStash: scoped stash-proximity validator hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.sharedDepositHandler"),
            SharedDepositHandlerRva,
            SharedDepositHandlerExpected.data(),
            static_cast<std::uint32_t>(SharedDepositHandlerExpected.size()),
            HookSharedDepositHandler,
            &OriginalSharedDepositHandler
        )) {
        context->LogError("RemoteStash: shared-deposit handler hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.sharedWithdrawalHandler"),
            SharedWithdrawalHandlerRva,
            SharedWithdrawalHandlerExpected.data(),
            static_cast<std::uint32_t>(SharedWithdrawalHandlerExpected.size()),
            HookSharedWithdrawalHandler,
            &OriginalSharedWithdrawalHandler
        )) {
        context->LogError("RemoteStash: shared-withdrawal handler hook failed.");
        return false;
    }
    if (!InstallNamedInlineHook(
            context,
            REMOTE_SITE("misc.remoteStash.insertItemHandler"),
            InsertItemHandlerRva,
            InsertItemHandlerExpected.data(),
            static_cast<std::uint32_t>(InsertItemHandlerExpected.size()),
            HookInsertItemHandler,
            &OriginalInsertItemHandler
        )) {
        context->LogError("RemoteStash: authoritative open-request hook failed.");
        return false;
    }

    if (HotkeySettings.hotkeyEnabled && !UsesSdkInput && !StartInput()) {
        (void)StopInput();
        context->LogError("RemoteStash: bounded hotkey input worker failed.");
        return false;
    }

    if (HotkeySettings.inventoryButtonEnabled && !RegisterOwnedButton()) {
        context->LogError(
            "RemoteStash: the self-contained Inventory button could not be created."
        );
        CleanupFailedLoadSdkState();
        return false;
    }
    if (HotkeySettings.inventoryButtonEnabled && !RegisterSdkButtonListener()) {
        context->LogError(
            "RemoteStash: SDK inventory button listener unavailable; plugin refused."
        );
        CleanupFailedLoadSdkState();
        return false;
    }

    RegisterStatusCommand();

    char message[820]{};
    std::snprintf(
        message,
        sizeof(message),
        "Remote Stash 2.0.0 by RuffnecKk active for D2R 3.2.92777; "
        "button=%s; placement=%s/%s offset=%d,%d size=%dx%d frames=%u/%u/%u/%u; "
        "sprite=%s; lifecycle=independent; hotkey=%s; binding=%s; mode=%s; "
        "input=%s; config=%s.",
        HotkeySettings.inventoryButtonEnabled
            ? "plugin-owned (remote only)" : "disabled",
        ButtonPlacementName(EffectiveButtonSettings.placement),
        ButtonAnchorName(EffectiveButtonSettings.anchor),
        EffectiveButtonSettings.offsetX,
        EffectiveButtonSettings.offsetY,
        EffectiveButtonSettings.width,
        EffectiveButtonSettings.height,
        EffectiveButtonSettings.normalFrame,
        EffectiveButtonSettings.pressedFrame,
        EffectiveButtonSettings.disabledFrame,
        EffectiveButtonSettings.hoveredFrame,
        ButtonSpriteSource.c_str(),
        HotkeySettings.hotkeyEnabled ? "enabled" : "disabled",
        HotkeySettings.hotkeyText.c_str(),
        HotkeyModeName(HotkeySettings.mode),
        !HotkeySettings.hotkeyEnabled ? "disabled"
            : (UsesSdkInput ? "SDK Input v1" : "native compatibility fallback"),
        LoadedConfigPath.c_str()
    );
    context->LogInfo(message);
    return true;
}

void Unload() noexcept {
    if (Context && HotkeySettings.diagnostics) {
        char message[480]{};
        std::snprintf(
            message,
            sizeof(message),
            "RemoteStash diagnostics: stopped; hotkey dispatched=%llu refused=%llu failed=%llu; button activations=%llu failures=%llu; sessions opened=%llu closed=%llu; item failures=%llu; gold failures=%llu.",
            static_cast<unsigned long long>(
                HotkeyDispatchedRequests.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                HotkeyRefusedRequests.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                HotkeyFailedRequests.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                SdkButtonActivations.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                SdkButtonFailures.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                ServerSessionsOpened.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                ServerSessionsClosed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                RemoteItemFailures.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                RemoteGoldFailures.load(std::memory_order_relaxed)));
        Context->LogInfo(message);
    }
    UnregisterSdkButtonListener();
    UnregisterOwnedButton();
    if (!UsesSdkInput && !StopInput()) return;
    if (InputService && Context
        && InputAction != D2RL::Input::InvalidHandle) {
        (void)InputService->unregisterAction(Context, InputAction);
    }
    if (LifecycleService && Context
        && GameJoinedListener != D2RL::Lifecycle::InvalidHandle) {
        (void)LifecycleService->unregisterGameplayEventListener(
            Context, GameJoinedListener);
    }
    if (LifecycleService && Context
        && GameLeftListener != D2RL::Lifecycle::InvalidHandle) {
        (void)LifecycleService->unregisterGameplayEventListener(
            Context, GameLeftListener);
    }
    DeactivateRemoteClientSession(false);
    RemoteItemScope = false;
    RemoteGoldScope = false;
    RemoteHotkeyOpenTransitionScope = false;
    RemoteMovementUiCloseScope = false;
    try {
        const std::lock_guard lock(RemoteSessionsMutex);
        RemoteSessions.clear();
    } catch (...) {
    }
    HotkeyRequestPending.store(false, std::memory_order_release);
    FindTopLevelPanel = nullptr;
    OriginalConfigurePlayerInventory = nullptr;
    FindChildWidget = nullptr;
    GetWidgetRect = nullptr;
    OriginalOpenInterfaceState = nullptr;
    OriginalCloseInterfaceState = nullptr;
    OriginalMovementUiClose = nullptr;
    OriginalStashInterfaceTransition = nullptr;
    OriginalResetMouseInputState = nullptr;
    OriginalResetMouseInputStateWithFinalize = nullptr;
    OriginalSharedGoldDeposit = nullptr;
    MarkUiDirty = nullptr;
    if (GoldRangeStub) {
        VirtualFree(GoldRangeStub, 0, MEM_RELEASE);
        GoldRangeStub = nullptr;
    }
    GoldRangeTrampoline = nullptr;
    if (CallSiteRelayPage) {
        VirtualFree(CallSiteRelayPage, 0, MEM_RELEASE);
        CallSiteRelayPage = nullptr;
    }
    HotkeySettings = {};
    EffectiveButtonSettings = {};
    ButtonSpriteSource = "embedded RuffnecKk chest";
    InputService = nullptr;
    ThreadService = nullptr;
    LifecycleService = nullptr;
    SharedEventService = nullptr;
    PanelService = nullptr;
    ResourceService = nullptr;
    InputAction = D2RL::Input::InvalidHandle;
    ButtonMessageListener = D2RL::SharedEvents::InvalidHandle;
    GameJoinedListener = D2RL::Lifecycle::InvalidHandle;
    GameLeftListener = D2RL::Lifecycle::InvalidHandle;
    ButtonChildLayout = D2RL::Panels::InvalidChildLayoutHandle;
    ButtonLayoutResource = D2RL::Resources::InvalidHandle;
    ButtonSpriteResource = D2RL::Resources::InvalidHandle;
    ButtonLowendSpriteResource = D2RL::Resources::InvalidHandle;
    UsesSdkInput = false;
    Base = nullptr;
    Context = nullptr;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    return Load(context);
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Unload();
}

} // namespace RuffnecKk::RemoteStash
