#pragma once

#include "tracked_native_transform_compat.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace RuffnecKk::MapSense::GroundItemTooltip {
namespace {

constexpr std::uint64_t HitTest = 0x1517B30;
constexpr std::uint64_t Lookup = 0x09A5D0;
constexpr std::uint64_t SelectedUnit = 0x0F1900;
constexpr std::uint64_t LocalContext = 0x08B2D0;
constexpr std::uint64_t LocalPlayer = 0x09A480;
constexpr std::uint64_t CheckFlag = 0x36E2D0;
constexpr std::uint64_t BuildTooltip = 0x2BD480;
constexpr std::uint64_t GetAllocator = 0x12063C0;
constexpr auto InlineStorage = std::uint64_t{1} << 63;
constexpr unsigned ItemType = 4U;
constexpr unsigned GroundMode = 3U;
constexpr unsigned Identified = 0x10U;
constexpr ULONGLONG MaximumAge = 100U;

struct NativeString {
    char* data;
    std::uint64_t length;
    std::uint64_t capacity;
    char inlineData[16];
};
struct TooltipInput {
    const void* auxiliary{};
    std::uint8_t mode{};
};
static_assert(offsetof(NativeString, inlineData) == 24);
static_assert(offsetof(TooltipInput, mode) == 8);

using HitTestFn = bool(__fastcall*)(void*, const void*, bool) noexcept;
using LookupFn = const void*(__fastcall*)(std::uint32_t, unsigned) noexcept;
using ContextFn = unsigned(__fastcall*)() noexcept;
using PlayerFn = const void*(__fastcall*)(unsigned) noexcept;
using CheckFlagFn = int(__fastcall*)(const void*, unsigned) noexcept;
using BuilderFn = NativeString*(__fastcall*)(NativeString*, const void*, const void*,
    const void*, unsigned, bool, bool, const void*, TooltipInput*) noexcept;
using AllocatorFn = void*(__fastcall*)() noexcept;
using FreeFn = void(__fastcall*)(void*, void*) noexcept;

HitTestFn Original{};
std::uintptr_t Base{};
std::atomic_bool Ready{};
std::atomic_bool Enabled{};
std::mutex SnapshotMutex;
bool GameplayActive{};
bool Preference{};
std::uint64_t Epoch{};
std::atomic_bool ValidationQueued{};
struct Snapshot {
    std::string text;
    POINT cursor{};
    std::uint32_t id{};
    ULONGLONG seen{};
    ULONGLONG built{};
};
Snapshot Current;

template <typename F>
F At(std::uint64_t rva) noexcept {
    return reinterpret_cast<F>(Base + rva);
}

template <std::size_t N>
bool Matches(const D2RL::PluginContext* context, std::uint64_t rva,
             const unsigned char (&bytes)[N]) noexcept {
    return context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(N));
}

std::uint32_t ReadField(const void* value, std::size_t offset) noexcept {
    std::uint32_t result{};
    std::memcpy(&result, static_cast<const char*>(value) + offset, sizeof(result));
    return result;
}

bool SamePoint(POINT a, POINT b) noexcept {
    return a.x == b.x && a.y == b.y;
}

void ReleaseString(const NativeString& result) noexcept {
    if (!result.data || result.data == result.inlineData
        || (result.capacity & InlineStorage) != 0) return;
    auto* allocator = At<AllocatorFn>(GetAllocator)();
    if (allocator != nullptr) {
        const auto table = *static_cast<const std::uintptr_t* const*>(allocator);
        reinterpret_cast<FreeFn>(table[4])(allocator, result.data);
    }
}

std::string CopyTooltip(const NativeString& result) {
    if (!result.data || result.length == 0 || result.length > 8192
        || result.length > (result.capacity & ~InlineStorage)
        || ((result.capacity & InlineStorage) != 0
            && (result.data != result.inlineData || result.length >= sizeof(result.inlineData)))
        || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result.data,
                               static_cast<int>(result.length), nullptr, 0)) return {};
    std::string plain;
    for (std::size_t i = 0; i < result.length; ++i) {
        const auto* c = result.data + i;
        if (i + 3 < result.length && std::memcmp(c, "\xEE\x81\xBE", 3) == 0) {
            i += 3;
            continue;
        }
        if (static_cast<unsigned char>(*c) >= 32 || *c == '\n') plain.push_back(*c);
    }
    // The game's tooltip stores the last displayed line first.
    std::string display;
    for (auto end = plain.size(); end > 0;) {
        const auto separator = plain.rfind('\n', end - 1);
        const auto start = separator == std::string::npos ? 0 : separator + 1;
        if (start < end) {
            if (!display.empty()) display.push_back('\n');
            display.append(plain, start, end - start);
        }
        end = start == 0 ? 0 : start - 1;
    }
    return display;
}

const void* FindIdentifiedGroundItem(std::uint32_t id) noexcept {
    const auto* item = At<LookupFn>(Lookup)(id, ItemType);
    if (!item || ReadField(item, 0) != ItemType || ReadField(item, 0x0C) != GroundMode
        || !At<CheckFlagFn>(CheckFlag)(item, Identified)) return nullptr;
    return item;
}

bool __fastcall ObserveHit(void* labels, const void* record, bool selected) noexcept {
    if (!Enabled.load(std::memory_order_acquire) || selected)
        return Original(labels, record, selected);
    POINT before{};
    const bool haveBefore = GetCursorPos(&before) != FALSE;
    std::uint64_t epoch{};
    {
        const std::lock_guard lock(SnapshotMutex);
        epoch = Epoch;
    }
    const auto handled = Original(labels, record, selected);
    if (!haveBefore || !Enabled.load(std::memory_order_acquire)) return handled;

    POINT cursor{};
    if (!GetCursorPos(&cursor) || !SamePoint(before, cursor)) return handled;
    const auto id = ReadField(record, 0x10);
    const auto client = At<ContextFn>(LocalContext)();
    // A handled miss can clear native selection and still return true.
    const auto* item = handled && client < 8U ? FindIdentifiedGroundItem(id) : nullptr;
    const auto* active = handled && client < 8U ? At<PlayerFn>(SelectedUnit)(client) : nullptr;

    const auto now = GetTickCount64();
    {
        const std::lock_guard lock(SnapshotMutex);
        if (!Enabled.load(std::memory_order_relaxed) || epoch != Epoch) return handled;
        if (!item || active != item) {
            if (Current.id == id || !SamePoint(Current.cursor, cursor)
                || (active && ReadField(active, 0) == ItemType && ReadField(active, 0x08) == id))
                Current = {};
            return handled;
        }
        if (Current.id == id && !Current.text.empty() && now - Current.built < 250U) {
            Current.cursor = cursor;
            Current.seen = now;
            return handled;
        }
        Current = {};
    }

    const auto* player = At<PlayerFn>(LocalPlayer)(client);
    if (!player) return handled;

    TooltipInput input{};
    NativeString result{};
    // Native caller 0x87E882 passes the same player in RDX and R8,
    // with a zeroed auxiliary input and an output owned by the game's allocator.
    const auto* returned = At<BuilderFn>(BuildTooltip)(
        &result, player, player, item, 0, true, false, nullptr, &input);
    std::string text;
    if (returned == &result) {
        try { text = CopyTooltip(result); } catch (...) {}
        ReleaseString(result);
    }
    if (!text.empty()) {
        const std::lock_guard lock(SnapshotMutex);
        if (Enabled.load(std::memory_order_relaxed) && epoch == Epoch)
            Current = {std::move(text), cursor, id, now, now};
    }
    return handled;
}

void __cdecl ValidateOnUi(const D2RL::PluginContext*, void*) noexcept {
    const std::lock_guard lock(SnapshotMutex);
    ValidationQueued.store(false, std::memory_order_release);
    if (!Enabled.load(std::memory_order_relaxed) || Current.text.empty()) return;
    const auto client = At<ContextFn>(LocalContext)();
    const auto* item = FindIdentifiedGroundItem(Current.id);
    if (client >= 8U || !item || At<PlayerFn>(SelectedUnit)(client) != item) Current = {};
}

void RequestValidation(const D2RL::PluginContext* context,
                       const D2RL::ThreadServiceV1* threadService) noexcept {
    if (!context || !threadService || !threadService->runOnUiThread
        || ValidationQueued.exchange(true, std::memory_order_acq_rel)) return;
    if (threadService->runOnUiThread(context, ValidateOnUi, nullptr)
        != D2RL::Threads::Result::Success)
        ValidationQueued.store(false, std::memory_order_release);
}

bool Initialize(const D2RL::PluginContext* context) noexcept {
    // Ground-label hit-test entry and its selected-item branch, plus native
    // item/player lookup, identification, builder and allocator entry witnesses.
    constexpr std::array<std::uint8_t, 41> lookupBody{0x48, 0x63, 0xc2, 0x48, 0x8b, 0x04, 0xc1, 0x48, 0x85, 0xc0, 0x74, 0x1a,
            0x44, 0x39, 0x40, 0x08, 0x75, 0x05, 0x44, 0x39, 0x08, 0x74, 0x11, 0x48,
            0x8b, 0x88, 0x58, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xc1, 0x48, 0x85, 0xc9,
            0xeb, 0xe4, 0x33, 0xc0, 0xc3};
    if (!Matches(context, HitTest, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x41,0x56,0x41,0x57})
        || !Matches(context, 0x1517BC2, {0x8b,0x4b,0x10,0xba,0x04,0x00,0x00,0x00,0xe8,0x01,0x2a,0xb8,0xfe,0x0f,0x10,0x05,0x32,0x8a,0xf2,0x01,0x49,0x8b,0xd6,0x8b,0xcd,0x0f,0x11,0x43,0x14,0xe8,0x7c,0x9e,0xbd,0xfe})
        || !Matches(context, Lookup, {0x4c,0x63,0xca,0x48,0x8d,0x05,0x36,0x93,0x98,0x02,0x8b,0xd1,0x44,0x8b,0xc1,0x49,0x8b,0xc9,0x83,0xe2,0x7f,0x48,0xc1,0xe1,0x0a,0x48,0x03,0xc8,0xe9,0x7f,0x4c,0x00})
        || !Matches(context, LocalContext, {0x8b,0x05,0x2e,0x84,0x99,0x02,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x8b,0x05,0x76,0x84,0x99,0x02,0xc3,0xcc})
        || !Matches(context, LocalPlayer, {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x83,0xf9,0x08,0x0f,0x83,0x85,0x00,0x00,0x00,0x8b,0xd9,0x48,0x89,0x5c,0x24,0x38,0x48,0x83,0xfb,0x08,0x72,0x19})
        || !Matches(context, CheckFlag, {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b,0xfa,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x74,0x0a,0xe8,0xe7,0xd6,0xfd,0xff,0x83,0xf8,0x04,0x74,0x19,0x48,0x8d})
        || !Matches(context, BuildTooltip, {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0xf8,0xb1,0xff,0xff,0xb8,0x08,0x4f,0x00,0x00,0xe8,0x41,0x3c,0x01,0x01,0x48})
        || !Matches(context, 0x1517B48, {0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x45, 0x0f, 0xb6, 0xe0, 0x48, 0x8b,
            0xda, 0xe8, 0x96, 0x76, 0xc3, 0xfe, 0x8b, 0xf8, 0xe8, 0x9f, 0x76, 0xc3,
            0xfe, 0x8b, 0xf0, 0xe8, 0x68, 0x37, 0xb7, 0xfe, 0x8b, 0xc8, 0x8b, 0xe8,
            0xe8, 0x8f, 0x9d, 0xbd, 0xfe, 0x8b, 0x4b, 0x10, 0xba, 0x04, 0x00, 0x00,
            0x00, 0x4c, 0x8b, 0xf8, 0xe8, 0x4f, 0x2a, 0xb8, 0xfe, 0x0f, 0x10, 0x03,
            0x48, 0x8d, 0x4c, 0x24, 0x20, 0x4c, 0x8b, 0xf0, 0x0f, 0x11, 0x44, 0x24,
            0x20, 0xe8, 0x7a, 0x2b, 0xce, 0xfe, 0x45, 0x84, 0xe4, 0x75, 0x75, 0x44,
            0x8b, 0x44, 0x24, 0x20, 0x41, 0x3b, 0xf8, 0x7c, 0x43, 0x44, 0x03, 0x44,
            0x24, 0x28, 0x41, 0x3b, 0xf8, 0x7f, 0x39, 0x8b, 0x4c, 0x24, 0x24, 0x3b,
            0xf1, 0x7c, 0x31, 0x83, 0xc1, 0x04, 0x03, 0x4c, 0x24, 0x2c, 0x3b, 0xf1,
            0x7f, 0x26})
        || !Matches(context, 0x1517BE8, {0x4d, 0x3b, 0xf7, 0x75, 0x23, 0x8b, 0x4b, 0x10, 0xba, 0x04, 0x00, 0x00,
            0x00, 0xe8, 0xd6, 0x29, 0xb8, 0xfe, 0x0f, 0x10, 0x05, 0x07, 0x8a, 0xf2,
            0x01, 0x8b, 0xcd, 0x0f, 0x11, 0x43, 0x14, 0xe8, 0xb4, 0x99, 0xbd, 0xfe,
            0xb0, 0x01, 0xeb, 0x40})
        || !Matches(context, 0xF1900, {0x40, 0x53, 0x55, 0x56, 0x48, 0x83, 0xec, 0x30, 0x8b, 0xf1, 0x8b, 0xd9,
            0x48, 0x89, 0x5c, 0x24, 0x58, 0x48, 0x83, 0xfe, 0x08, 0x72, 0x19, 0x48,
            0x8d, 0x44, 0x24, 0x58, 0x48, 0x8d, 0x4c, 0x24, 0x60, 0x48, 0x89, 0x44,
            0x24, 0x60, 0xe8, 0x45, 0xf8, 0xff, 0xff, 0x84, 0xc0, 0x74, 0x01, 0xcc,
            0x48, 0x8b, 0x44, 0x24, 0x58, 0x48, 0x8d, 0x2d, 0xf4, 0xfc, 0x94, 0x02,
            0x48, 0x03, 0xc0, 0x80, 0x7c, 0xc5, 0x00, 0x00, 0x0f, 0x84, 0x00, 0x01,
            0x00, 0x00, 0x48, 0x89, 0x7c, 0x24, 0x50, 0x48, 0x8b, 0xc3, 0x48, 0x89,
            0x5c, 0x24, 0x58, 0x48, 0x83, 0xfb, 0x08, 0x72, 0x31, 0x48, 0x8d, 0x44,
            0x24, 0x58, 0x48, 0x8d, 0x4c, 0x24, 0x60, 0x48, 0x89, 0x44, 0x24, 0x60,
            0xe8, 0xff, 0xf7, 0xff, 0xff, 0x84, 0xc0, 0x74, 0x14, 0xcc, 0x48, 0x8b,
            0x44, 0x24, 0x58, 0x48, 0x03, 0xc0, 0x48, 0x89, 0x5c, 0x24, 0x60, 0x8b,
            0x7c, 0xc5, 0x04, 0xeb, 0x17, 0x48, 0x8b, 0x44, 0x24, 0x58, 0x48, 0x03,
            0xc0, 0x48, 0x89, 0x5c, 0x24, 0x60, 0x8b, 0x7c, 0xc5, 0x04, 0x48, 0x83,
            0xfb, 0x08, 0x72, 0x19, 0x48, 0x8d, 0x44, 0x24, 0x60, 0x48, 0x8d, 0x4c,
            0x24, 0x58, 0x48, 0x89, 0x44, 0x24, 0x58, 0xe8, 0xbc, 0xf7, 0xff, 0xff,
            0x84, 0xc0, 0x74, 0x01, 0xcc, 0x48, 0x8b, 0x44, 0x24, 0x60, 0x8b, 0xd7,
            0x48, 0x03, 0xc0, 0x8b, 0x4c, 0xc5, 0x08, 0xe8, 0x04, 0x8c, 0xfa, 0xff,
            0x48, 0x8b, 0xf8, 0x48, 0x85, 0xc0, 0x74, 0x2f, 0x41, 0xb9, 0xff, 0xff,
            0xff, 0xff, 0xc7, 0x44, 0x24, 0x20, 0xff, 0xff, 0xff, 0xff, 0x45, 0x33,
            0xc0, 0x33, 0xd2, 0x48, 0x8b, 0xc8, 0xe8, 0x51, 0x9e, 0xfa, 0xff, 0x85,
            0xc0, 0x74, 0x10, 0x48, 0x8b, 0xc7, 0x48, 0x8b, 0x7c, 0x24, 0x50, 0x48,
            0x83, 0xc4, 0x30, 0x5e, 0x5d, 0x5b, 0xc3, 0x48, 0x89, 0x5c, 0x24, 0x58,
            0x48, 0x83, 0xfb, 0x08, 0x72, 0x19, 0x48, 0x8d, 0x44, 0x24, 0x58, 0x48,
            0x8d, 0x4c, 0x24, 0x60, 0x48, 0x89, 0x44, 0x24, 0x60, 0xe8, 0x4e, 0xf7,
            0xff, 0xff, 0x84, 0xc0, 0x74, 0x01, 0xcc, 0x48, 0x8b, 0x44, 0x24, 0x58,
            0x8b, 0xce, 0x48, 0x03, 0xc0, 0xc6, 0x44, 0xc5, 0x01, 0x00, 0xe8, 0xb5,
            0x0a, 0x00, 0x00, 0x48, 0x8b, 0x7c, 0x24, 0x50, 0x33, 0xc0, 0x48, 0x83,
            0xc4, 0x30, 0x5e, 0x5d, 0x5b, 0xc3, 0x33, 0xc0, 0x48, 0x83, 0xc4, 0x30,
            0x5e, 0x5d, 0x5b, 0xc3})
        || !Matches(context, 0x34B9F6, {0x8b, 0x01, 0x48, 0x83, 0xc4, 0x28, 0xc3})
        || !Matches(context, 0x34AB84, {0x41, 0x0c, 0x48, 0x83, 0xc4, 0x28, 0xc3})
        || !Matches(context, 0x36E2F0, {0x4c, 0x24, 0x30, 0xc6, 0x44, 0x24, 0x30, 0x00, 0xe8, 0x43, 0x94, 0xff,
            0xff, 0x84, 0xc0, 0x74, 0x01, 0xcc, 0x48, 0x85, 0xdb, 0x74, 0x2d, 0x48,
            0x8b, 0xcb, 0xe8, 0xc1, 0xd6, 0xfd, 0xff, 0x83, 0xf8, 0x04, 0x75, 0x20,
            0x48, 0x8b, 0xcb, 0xe8, 0xe4, 0xc1, 0xfd, 0xff, 0x48, 0x85, 0xc0, 0x75,
            0x20, 0x48, 0x8d, 0x4c, 0x24, 0x30, 0x88, 0x44, 0x24, 0x30, 0xe8, 0x81,
            0xbb, 0xff, 0xff, 0x84, 0xc0, 0x74, 0x01, 0xcc, 0x33, 0xc0, 0x48, 0x8b,
            0x5c, 0x24, 0x38, 0x48, 0x83, 0xc4, 0x20, 0x5f, 0xc3, 0x8b, 0x40, 0x18,
            0x48, 0x8b, 0x5c, 0x24, 0x38, 0x23, 0xc7, 0x48, 0x83, 0xc4, 0x20, 0x5f,
            0xc3, 0xcc, 0xcc})
        || !Matches(context, 0x2BE488, {0x48, 0x8b, 0x4d, 0xb8, 0x48, 0x0f, 0xba, 0xe1, 0x3f, 0x72, 0x3a, 0x48,
            0x8b, 0x45, 0xa8, 0x48, 0x8d, 0x7d, 0xc0, 0x49, 0x89, 0x06, 0x48, 0x8b,
            0x45, 0xb0, 0x49, 0x89, 0x46, 0x08, 0x48, 0xb8, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0x7f, 0x48, 0x23, 0xc8, 0x48, 0xc7, 0x45, 0xb0, 0x00,
            0x00, 0x00, 0x00, 0x49, 0x89, 0x4e, 0x10, 0x48, 0x89, 0x7d, 0xa8, 0x4c,
            0x89, 0x7d, 0xb8, 0xc6, 0x45, 0xc0, 0x00, 0xeb, 0x4f, 0x48, 0x8b, 0x7d,
            0xb0, 0x49, 0x8d, 0x46, 0x18, 0x48, 0x8b, 0x75, 0xa8, 0x48, 0x8b, 0xd7,
            0x49, 0x89, 0x06, 0x49, 0x8b, 0xce, 0x49, 0xc7, 0x46, 0x08, 0x00, 0x00,
            0x00, 0x00, 0x4d, 0x89, 0x7e, 0x10, 0xc6, 0x00, 0x00, 0xe8, 0x1a, 0x7d,
            0xdb, 0xff, 0x48, 0x85, 0xff, 0x74, 0x0e, 0x49, 0x8b, 0x0e, 0x4c, 0x8b,
            0xc7, 0x48, 0x8b, 0xd6, 0xe8, 0x57, 0x51, 0x01, 0x01, 0x49, 0x8b, 0x06,
            0xc6, 0x04, 0x07, 0x00, 0x4c, 0x8b, 0x7d, 0xb8, 0x49, 0x89, 0x7e, 0x08,
            0x48, 0x8b, 0x7d, 0xa8, 0x49, 0x0f, 0xba, 0xe7, 0x3f, 0x72, 0x12, 0xe8,
            0x98, 0x7e, 0xf4, 0x00, 0x48, 0x8b, 0xd7, 0x4c, 0x8b, 0x00, 0x48, 0x8b,
            0xc8, 0x41, 0xff, 0x50, 0x20, 0x49, 0x8b, 0xc6, 0x41, 0xc6, 0x45, 0x08,
            0x00})
        || !Matches(context, 0x0F15EC, {0x48, 0x8b, 0x44, 0x24, 0x38, 0x48, 0x8d, 0x0d, 0x39, 0x00, 0x95, 0x02,
            0x48, 0x03, 0xc0, 0xc6, 0x04, 0xc1, 0x00, 0x8b, 0xcb, 0x48, 0x83, 0xc4,
            0x20, 0x5b, 0xe9, 0xe5, 0x0e, 0x00, 0x00})
        || !Detail::ValidateClientUnitHashLookup(context, lookupBody, true)
        || !Matches(context, GetAllocator, {0x48,0x83,0xec,0x28,0x65,0x48,0x8b,0x04,0x25,0x58,0x00,0x00,0x00,0x8b,0x0d,0xf5,0xe0,0x3a,0x02,0xba,0x6c,0x12,0x00,0x00})) return false;

    Base = static_cast<std::uintptr_t>(context->exeBase);
    constexpr unsigned char entry[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x41,0x56,0x41,0x57};
    const auto installed = context->InstallInlineHook(
        HitTest, entry, static_cast<std::uint32_t>(sizeof(entry)), ObserveHit, &Original);
    Ready.store(installed, std::memory_order_release);
    return installed;
}

void UpdateEnablementLocked() noexcept {
    const bool enabled = GameplayActive && Preference && Ready.load(std::memory_order_acquire);
    if (Enabled.exchange(enabled, std::memory_order_acq_rel) != enabled) {
        ++Epoch;
        Current = {};
    }
}

void SetGameplayActive(bool active) noexcept {
    const std::lock_guard lock(SnapshotMutex);
    GameplayActive = active;
    UpdateEnablementLocked();
}

void SetPreference(bool enabled) noexcept {
    const std::lock_guard lock(SnapshotMutex);
    Preference = enabled;
    UpdateEnablementLocked();
}

bool HasFreshTooltip() noexcept {
    if (!Enabled.load(std::memory_order_acquire)) return false;
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    const std::lock_guard lock(SnapshotMutex);
    return !Current.text.empty() && GetTickCount64() - Current.seen <= MaximumAge
        && SamePoint(Current.cursor, cursor);
}

void Draw(const D2RL::PluginContext* context,
          const D2RL::ThreadServiceV1* threadService) noexcept {
    if (!HasFreshTooltip()) return;
    const auto window = GetD3D12ImGuiHostStatus().gameWindow;
    if (!window || GetForegroundWindow() != window) return;
    // Validate even if pickup removed the label and no further hit test runs.
    RequestValidation(context, threadService);
    const auto* viewport = ImGui::GetMainViewport();
    const std::lock_guard lock(SnapshotMutex);
    POINT screen{};
    if (Current.text.empty() || GetTickCount64() - Current.seen > MaximumAge
        || !GetCursorPos(&screen) || !SamePoint(Current.cursor, screen)
        || !ScreenToClient(window, &screen)) return;
    const ImVec2 cursor{viewport->Pos.x + static_cast<float>(screen.x),
                        viewport->Pos.y + static_cast<float>(screen.y)};
    constexpr float padding = 8.0F;
    constexpr float maximumWrap = 480.0F;
    const float left = viewport->WorkPos.x;
    const float top = viewport->WorkPos.y;
    const float right = left + viewport->WorkSize.x;
    const float bottom = top + viewport->WorkSize.y;
    if (cursor.x < left || cursor.x >= right || cursor.y < top || cursor.y >= bottom)
        return;

    // Reserve room around the whole cursor, including when the tooltip flips
    // at a viewport edge. Clamp placement within a strip outside this area.
    const float gap = 48.0F * (std::max)(1.0F, GetDpiForWindow(window) / 96.0F);
    enum class Side { Above, Below, Left, Right };
    struct Region { Side side; float left, top, right, bottom; };
    const std::array regions{
        Region{Side::Above, left, top, right, cursor.y - gap},
        Region{Side::Below, left, cursor.y + gap, right, bottom},
        Region{Side::Left, left, top, cursor.x - gap, bottom},
        Region{Side::Right, cursor.x + gap, top, right, bottom},
    };

    auto* font = GetD3D12ImGuiLocalizedFont();
    if (!font) font = ImGui::GetFont();
    const float preferredFontSize = ImGui::GetFontSize();
    struct Layout {
        ImVec2 origin{};
        ImVec2 box{};
        float wrap{};
        float fontSize{};
        float overflow{};
    };
    const auto measure = [&](const Region& region, float fontSize, float wrapLimit) {
        const float width = region.right - region.left;
        const float height = region.bottom - region.top;
        const float wrap = (std::min)(wrapLimit, width - 2 * padding);
        if (wrap < fontSize || height < fontSize + 2 * padding) return Layout{};

        const auto textSize = font->CalcTextSizeA(
            fontSize, FLT_MAX, wrap, Current.text.c_str());
        const ImVec2 box{(std::min)(width, textSize.x + 2 * padding),
                         (std::min)(height, textSize.y + 2 * padding)};
        const float x = region.side == Side::Left ? region.right - box.x : cursor.x;
        const float y = region.side == Side::Above ? region.bottom - box.y : cursor.y;
        return Layout{
            {std::clamp(x, region.left, region.right - box.x),
             std::clamp(y, region.top, region.bottom - box.y)},
            box, wrap, fontSize, (std::max)(0.0F, textSize.y + 2 * padding - height)};
    };

    Layout layout;
    Layout overflowLayout;
    float smallestOverflow = FLT_MAX;
    for (const float scale : {1.0F, 0.9F, 0.8F, 0.7F}) {
        for (const float limit : {maximumWrap, FLT_MAX}) {
            for (const auto& region : regions) {
                const auto candidate = measure(region, preferredFontSize * scale, limit);
                if (candidate.fontSize <= 0) continue;
                if (candidate.overflow == 0) {
                    layout = candidate;
                    break;
                }
                // For overflow, keep the regular font and pick the region
                // with the least text outside the visible area.
                if (scale == 1.0F && candidate.overflow < smallestOverflow) {
                    overflowLayout = candidate;
                    smallestOverflow = candidate.overflow;
                }
            }
            if (layout.fontSize > 0) break;
        }
        if (layout.fontSize > 0) break;
    }
    if (layout.fontSize <= 0) layout = overflowLayout;
    if (layout.fontSize <= 0) return;

    // Scroll only when the complete tooltip cannot fit. Keep the panel
    // passive so the player can still click the ground item normally.
    struct OverflowScroll {
        std::uint64_t epoch{};
        std::uint32_t item{};
        POINT cursor{};
        ULONGLONG started{};
        ULONGLONG lastDraw{};
    };
    static OverflowScroll scroll;
    float offset = 0.0F;
    if (layout.overflow > 0) {
        const auto now = GetTickCount64();
        if (scroll.epoch != Epoch || scroll.item != Current.id
            || !SamePoint(scroll.cursor, Current.cursor)
            || scroll.lastDraw == 0 || now - scroll.lastDraw > MaximumAge) {
            scroll = {Epoch, Current.id, Current.cursor, now, now};
        }
        scroll.lastDraw = now;
        constexpr float pauseSeconds = 1.5F;
        const float speed = layout.fontSize * 1.5F;
        const float duration = layout.overflow / speed;
        const float elapsed = std::fmod(static_cast<float>(now - scroll.started) * 0.001F,
                                       duration + 2 * pauseSeconds);
        offset = std::clamp((elapsed - pauseSeconds) * speed, 0.0F, layout.overflow);
    } else {
        scroll = {};
    }

    auto* draw = ImGui::GetForegroundDrawList();
    const auto origin = layout.origin;
    const ImVec2 end{origin.x + layout.box.x, origin.y + layout.box.y};
    draw->AddRectFilled(origin, end, IM_COL32(15, 12, 9, 240), 3);
    draw->AddRect(origin, end, IM_COL32(164, 136, 91, 255), 3);
    const ImVec4 clip{origin.x + padding, origin.y + padding,
                      end.x - padding, end.y - padding};
    draw->AddText(font, layout.fontSize,
        {clip.x, clip.y - offset}, IM_COL32(230, 220, 196, 255),
        Current.text.c_str(), nullptr, layout.wrap, &clip);
}

} // namespace
} // namespace RuffnecKk::MapSense::GroundItemTooltip
