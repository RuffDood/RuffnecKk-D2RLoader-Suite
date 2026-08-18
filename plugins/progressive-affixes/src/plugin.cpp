#include <D2RLPlugin/api.h>

#include "policy.hpp"
#include "rare_patch.hpp"
#include "relay.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RuffnecKk::ProgressiveAffixes {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::size_t MaximumConfigBytes = 65'536;

constexpr std::uintptr_t GeneralSeedRollRva = 0x153B00;
constexpr std::uintptr_t PowerOfTwoSeedRollRva = 0x367160;
constexpr std::uintptr_t GetDataTablesRva = 0x300A90;
constexpr std::uintptr_t GetItemContextRva = 0x34A0E0;
constexpr std::uintptr_t GetPrimaryItemTypeRva = 0x372C90;
constexpr std::uintptr_t CheckItemTypeRva = 0x373890;

constexpr std::uintptr_t MagicGeneratorRva = 0x442C60;
constexpr std::uintptr_t MagicPrefixValueRva = 0x442C78;
constexpr std::uintptr_t MagicSuffixValueRva = 0x442CDC;
constexpr std::uintptr_t MagicSuffixMinimumRva = 0x442CF0;

constexpr std::uintptr_t CraftedGeneratorRva = 0x58A120;
constexpr std::uintptr_t CraftedVanillaMinimumRva = 0x58A1F7;
constexpr std::uintptr_t CraftedRollCallRva = 0x58A21B;
constexpr std::uintptr_t CraftedMinimumClampRva = 0x58A220;

constexpr std::uintptr_t RareGeneratorRva = 0x58BBA0;
constexpr std::uintptr_t RareJewelBranchRva = 0x58BC65;
constexpr std::uintptr_t RareJewelVanillaPathRva = 0x58BC67;
constexpr std::uintptr_t RareSelectionRva = 0x58BC90;
constexpr std::uintptr_t RareSelectionReturnRva = 0x58BCAE;
constexpr std::uintptr_t RareVanillaCountTableRva = 0x1D3EE77;

constexpr std::uintptr_t ItemTypesRecordsOffset = 0x1348;
constexpr std::uintptr_t ItemTypesCountOffset = 0x1350;
constexpr std::size_t GenerationItemLevelOffset = 0x18;
constexpr std::int32_t VanillaRareJewelTypeId = 0x3A;

constexpr std::size_t RelayCount = 4;
constexpr std::size_t MagicPrefixRelayIndex = 0;
constexpr std::size_t MagicSuffixRelayIndex = 1;
constexpr std::size_t RareSelectionRelayIndex = 2;
constexpr std::size_t CraftedSelectionRelayIndex = 3;

using SeedRollFn = std::uint32_t(__fastcall*)(void*, std::int32_t) noexcept;
using GetDataTablesFn = std::uint8_t*(__fastcall*)(std::uint8_t) noexcept;
using GetItemContextFn = std::uint8_t(__fastcall*)(const void*) noexcept;
using GetPrimaryItemTypeFn = std::int32_t(__fastcall*)(const void*) noexcept;
using CheckItemTypeFn = std::int32_t(__fastcall*)(const void*, std::int32_t) noexcept;

struct ResolvedCategory {
    std::array<std::int32_t, MaxTypesPerCategory> ids{};
    std::size_t idCount{};
    bool wildcard{};
};

struct ResolvedTypeCache {
    const void* dataTables{};
    const void* records{};
    std::uint64_t recordCount{};
    bool initialized{};
    bool valid{};
    std::array<ResolvedCategory, MaxCategories> magic{};
    std::array<ResolvedCategory, MaxCategories> rare{};
    std::array<ResolvedCategory, MaxCategories> crafted{};
};

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"ruffneckk-progressive-affixes.toml"};
SeedRollFn GeneralSeedRoll{};
SeedRollFn PowerOfTwoSeedRoll{};
GetDataTablesFn GetDataTables{};
GetItemContextFn GetItemContext{};
GetPrimaryItemTypeFn GetPrimaryItemType{};
CheckItemTypeFn CheckItemType{};
void* RelayPage{};
std::atomic<bool> Operational{};
std::atomic<bool> RuntimeConfigRejected{};
std::atomic<std::uint32_t> ResolvedTypeCount{};
std::atomic<std::uint32_t> UnresolvedTypeCount{};
std::atomic<std::uint64_t> MagicSelections{};
std::atomic<std::uint64_t> RareSelections{};
std::atomic<std::uint64_t> CraftedSelections{};
std::atomic_flag RuntimeConfigErrorLogged = ATOMIC_FLAG_INIT;
std::atomic_bool TypeResolutionReported{};
std::atomic_bool MagicSelectionReported{};
std::atomic_bool RareSelectionReported{};
std::atomic_bool CraftedSelectionReported{};
thread_local ResolvedTypeCache TypeCache{};

template <typename Function>
Function At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<Function>(Base + rva);
}

bool LoadConfig() noexcept {
    Settings = {};
    LoadedConfigPath = "ruffneckk-progressive-affixes.toml";

    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "ProgressiveAffixes: configuration exceeds 65535 bytes."
            : "ProgressiveAffixes: configuration could not be read.");
        return false;
    }

    try {
        std::istringstream input{std::string(buffer.data())};
        Settings = ParseConfig(input);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "ProgressiveAffixes: invalid TOML (")
            + exception.what()
            + "); no relay or patch was installed.";
        Context->LogError(message.c_str());
        return false;
    }
    return true;
}

void LogFirstSelection(
        std::atomic_bool& reported,
        const char* quality,
        const std::string& category,
        std::int32_t itemLevel,
        std::int32_t affixCount) noexcept {
    if (!Settings.diagnostics || !Context
            || reported.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "ProgressiveAffixes diagnostics: first %s selection used category=%s, ilvl=%d, affixes=%d.",
        quality,
        category.c_str(),
        itemLevel,
        affixCount);
    Context->LogInfo(message);
}

template <typename Category>
bool ResolveCategories(
        const std::vector<Category>& configured,
        std::array<ResolvedCategory, MaxCategories>& resolved,
        const void* records,
        std::uint64_t recordCount,
        std::uint32_t& resolvedCount,
        std::uint32_t& unresolvedCount) noexcept {
    for (std::size_t categoryIndex = 0; categoryIndex < configured.size(); ++categoryIndex) {
        const auto& source = configured[categoryIndex];
        auto& target = resolved[categoryIndex];
        target = {};
        target.wildcard = source.itemTypes.size() == 1 && source.itemTypes.front().wildcard;
        if (target.wildcard) continue;
        for (const auto& code : source.itemTypes) {
            const auto id = FindItemTypeId(records, recordCount, code);
            if (id < 0) {
                ++unresolvedCount;
                continue;
            }
            target.ids[target.idCount++] = id;
            ++resolvedCount;
        }
        if (target.idCount != source.itemTypes.size()) return false;
    }
    return true;
}

bool RefreshTypeCache(const void* item) noexcept {
    if (!item || !GetItemContext || !GetDataTables) return false;
    const auto itemContext = GetItemContext(item);
    auto* dataTables = GetDataTables(itemContext);
    if (!dataTables) return false;
    const auto* records = *reinterpret_cast<const std::uint8_t* const*>(
        dataTables + ItemTypesRecordsOffset);
    const auto recordCount = *reinterpret_cast<const std::uint64_t*>(
        dataTables + ItemTypesCountOffset);
    if (!records || recordCount == 0 || recordCount > 4096) return false;
    if (TypeCache.initialized
            && TypeCache.dataTables == dataTables
            && TypeCache.records == records
            && TypeCache.recordCount == recordCount) {
        return TypeCache.valid;
    }

    TypeCache = {};
    TypeCache.initialized = true;
    TypeCache.dataTables = dataTables;
    TypeCache.records = records;
    TypeCache.recordCount = recordCount;
    std::uint32_t resolvedCount{};
    std::uint32_t unresolvedCount{};
    const auto valid =
        ResolveCategories(
            Settings.magic,
            TypeCache.magic,
            records,
            recordCount,
            resolvedCount,
            unresolvedCount)
        && ResolveCategories(
            Settings.rare,
            TypeCache.rare,
            records,
            recordCount,
            resolvedCount,
            unresolvedCount)
        && ResolveCategories(
            Settings.crafted,
            TypeCache.crafted,
            records,
            recordCount,
            resolvedCount,
            unresolvedCount);
    TypeCache.valid = valid;
    ResolvedTypeCount.store(resolvedCount, std::memory_order_relaxed);
    UnresolvedTypeCount.store(unresolvedCount, std::memory_order_relaxed);
    if (valid && Settings.diagnostics && Context
            && !TypeResolutionReported.exchange(
                true, std::memory_order_acq_rel)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "ProgressiveAffixes diagnostics: itemtypes cache ready; records=%llu, resolved=%u, unresolved=%u.",
            static_cast<unsigned long long>(recordCount),
            resolvedCount,
            unresolvedCount);
        Context->LogInfo(message);
    }
    if (!valid) {
        RuntimeConfigRejected.store(true, std::memory_order_release);
        if (!RuntimeConfigErrorLogged.test_and_set(std::memory_order_relaxed)) {
            Context->LogError(
                "ProgressiveAffixes: one or more configured item type codes "
                "do not exist in the active itemtypes table; progressive selection refused.");
        }
    }
    return valid;
}

template <typename Category>
const Category* FindCategory(
        const std::vector<Category>& categories,
        const std::array<ResolvedCategory, MaxCategories>& resolved,
        const void* item) noexcept {
    if (!item || !RefreshTypeCache(item)) return nullptr;
    for (std::size_t categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex) {
        const auto& runtime = resolved[categoryIndex];
        if (runtime.wildcard) return &categories[categoryIndex];
        for (std::size_t typeIndex = 0; typeIndex < runtime.idCount; ++typeIndex) {
            if (CheckItemType(item, runtime.ids[typeIndex]) != 0) {
                return &categories[categoryIndex];
            }
        }
    }
    return nullptr;
}

std::int32_t ItemLevel(const void* generation) noexcept {
    if (!generation) return 0;
    std::int32_t level{};
    std::memcpy(
        &level,
        static_cast<const std::uint8_t*>(generation) + GenerationItemLevelOffset,
        sizeof(level));
    return level >= 1 && level <= 255 ? level : 0;
}

std::uint32_t CustomRoll(void* seed, const WeightedStep& step) noexcept {
    const auto total = TotalWeight(step);
    return seed && GeneralSeedRoll && total != 0
        ? GeneralSeedRoll(seed, static_cast<std::int32_t>(total))
        : 0;
}

std::int32_t VanillaRareCount(void* seed, const void* item) noexcept {
    if (!seed || !item || !GetPrimaryItemType || !PowerOfTwoSeedRoll) return 0;
    if (GetPrimaryItemType(item) == VanillaRareJewelTypeId) {
        std::uint32_t low{};
        std::uint32_t high{};
        std::memcpy(&low, seed, sizeof(low));
        std::memcpy(&high, static_cast<std::uint8_t*>(seed) + sizeof(low), sizeof(high));
        const auto next = static_cast<std::uint64_t>(low) * 0x6AC690C5ULL + high;
        low = static_cast<std::uint32_t>(next);
        high = static_cast<std::uint32_t>(next >> 32U);
        std::memcpy(seed, &low, sizeof(low));
        std::memcpy(static_cast<std::uint8_t*>(seed) + sizeof(low), &high, sizeof(high));
        return static_cast<std::int32_t>((low & 1U) + 3U);
    }
    const auto index = PowerOfTwoSeedRoll(seed, 8);
    std::int32_t count{};
    std::memcpy(&count, Base + RareVanillaCountTableRva + index * sizeof(count), sizeof(count));
    return count >= 3 && count <= 6 ? count : 0;
}

std::int32_t VanillaCraftedCount(void* seed, std::int32_t itemLevel) noexcept {
    if (!seed || !GeneralSeedRoll || itemLevel == 0) return 0;
    auto minimum = 1;
    if (itemLevel > 30) minimum = 2;
    if (itemLevel > 50) minimum = 3;
    if (itemLevel > 70) minimum = 4;
    return std::max<std::int32_t>(
        static_cast<std::int32_t>(GeneralSeedRoll(seed, 5)),
        minimum);
}

extern "C" std::int32_t __fastcall SelectMagicPrefixValue(
        const void* wrapper,
        const void* generation) noexcept {
    if (!generation) return 0;
    std::int32_t nativeValue{};
    std::memcpy(
        &nativeValue,
        static_cast<const std::uint8_t*>(generation) + 0xA8,
        sizeof(nativeValue));
    if (!Operational.load(std::memory_order_acquire) || !wrapper) return nativeValue;
    const auto* item = *static_cast<const void* const*>(wrapper);
    const auto level = ItemLevel(generation);
    const auto* category = FindCategory(Settings.magic, TypeCache.magic, item);
    const auto* step = category ? FindStep(category->steps, level) : nullptr;
    if (step && step->minimumAffixes == 2) {
        MagicSelections.fetch_add(1, std::memory_order_relaxed);
        LogFirstSelection(
            MagicSelectionReported,
            "Magic",
            category->name,
            level,
            2);
        return 1;
    }
    return nativeValue;
}

extern "C" std::int32_t __fastcall SelectMagicSuffixValue(
        const void* wrapper,
        const void* generation) noexcept {
    if (!generation) return 0;
    std::int32_t nativeValue{};
    std::memcpy(
        &nativeValue,
        static_cast<const std::uint8_t*>(generation) + 0xB4,
        sizeof(nativeValue));
    if (!Operational.load(std::memory_order_acquire) || !wrapper) return nativeValue;
    const auto* item = *static_cast<const void* const*>(wrapper);
    const auto level = ItemLevel(generation);
    const auto* category = FindCategory(Settings.magic, TypeCache.magic, item);
    const auto* step = category ? FindStep(category->steps, level) : nullptr;
    return step && step->minimumAffixes == 2 ? 1 : nativeValue;
}

extern "C" std::int32_t __fastcall SelectRareCount(
        void* seed,
        const void* item,
        const void* generation) noexcept {
    const auto level = ItemLevel(generation);
    if (!Operational.load(std::memory_order_acquire) || level == 0) {
        return VanillaRareCount(seed, item);
    }
    const auto* category = FindCategory(Settings.rare, TypeCache.rare, item);
    const auto* step = category ? FindStep(category->steps, level) : nullptr;
    if (!category || !step) return VanillaRareCount(seed, item);
    const auto count = PickWeightedCount(*category, *step, CustomRoll(seed, *step));
    RareSelections.fetch_add(1, std::memory_order_relaxed);
    LogFirstSelection(
        RareSelectionReported,
        "Rare",
        category->name,
        level,
        count);
    return count;
}

extern "C" std::int32_t __fastcall SelectCraftedCount(
        void* seed,
        const void* item,
        const void* generation) noexcept {
    const auto level = ItemLevel(generation);
    if (!Operational.load(std::memory_order_acquire) || level == 0) {
        return VanillaCraftedCount(seed, level);
    }
    const auto* category = FindCategory(Settings.crafted, TypeCache.crafted, item);
    const auto* step = category ? FindStep(category->steps, level) : nullptr;
    if (!category || !step) return VanillaCraftedCount(seed, level);
    const auto count = PickWeightedCount(*category, *step, CustomRoll(seed, *step));
    CraftedSelections.fetch_add(1, std::memory_order_relaxed);
    LogFirstSelection(
        CraftedSelectionReported,
        "Crafted",
        category->name,
        level,
        count);
    return count;
}

void* AllocateRelayNear(std::uintptr_t instructionAddress) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto minimumAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const auto maximumAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    constexpr auto maximumDistance = static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max());
    const auto lowerBound = std::max({
        minimumAddress,
        reinterpret_cast<std::uintptr_t>(Base),
        instructionAddress > maximumDistance
            ? instructionAddress - maximumDistance
            : minimumAddress});
    const auto upperBound = std::min(
        maximumAddress,
        instructionAddress < maximumAddress - maximumDistance
            ? instructionAddress + maximumDistance
            : maximumAddress);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    for (auto cursor = lowerBound; cursor < upperBound;) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) == 0) break;
        const auto regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const auto regionEnd = regionBase + region.RegionSize;
        if (region.State == MEM_FREE) {
            const auto first = std::max(regionBase, lowerBound);
            const auto candidate = (first + granularity - 1) & ~(granularity - 1);
            if (candidate < upperBound && candidate + systemInfo.dwPageSize <= regionEnd) {
                if (void* memory = VirtualAlloc(
                        reinterpret_cast<void*>(candidate),
                        systemInfo.dwPageSize,
                        MEM_RESERVE | MEM_COMMIT,
                        PAGE_READWRITE)) {
                    return memory;
                }
            }
        }
        if (regionEnd <= cursor) break;
        cursor = regionEnd;
    }
    return nullptr;
}

bool WriteBridge(
        std::size_t index,
        const std::uint8_t* setup,
        std::size_t setupSize,
        const void* target) noexcept {
    if (!RelayPage || index >= RelayCount) return false;
    std::array<std::uint8_t, RelayStride> relay{};
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    if (!BuildTailRelay(relay, setup, setupSize, address)) return false;
    std::memcpy(
        static_cast<std::uint8_t*>(RelayPage) + index * RelayStride,
        relay.data(),
        relay.size());
    return true;
}

bool WritePreservingArgumentBridge(
        std::size_t index,
        const void* target) noexcept {
    if (!RelayPage || index >= RelayCount) return false;
    std::array<std::uint8_t, RelayStride> relay{};
    BuildPreservingFirstTwoArgumentsRelay(
        relay,
        reinterpret_cast<std::uintptr_t>(target));
    std::memcpy(
        static_cast<std::uint8_t*>(RelayPage) + index * RelayStride,
        relay.data(),
        relay.size());
    return true;
}

bool CreateRelays() noexcept {
    RelayPage = AllocateRelayNear(
        reinterpret_cast<std::uintptr_t>(Base + RareSelectionRva));
    if (!RelayPage) {
        Context->LogError("ProgressiveAffixes: could not allocate rel32 relay memory.");
        return false;
    }
    constexpr std::array<std::uint8_t, 6> suffixSetup{
        0x48, 0x8B, 0xCF, // mov rcx, rdi
        0x48, 0x8B, 0xD6, // mov rdx, rsi
    };
    constexpr std::array<std::uint8_t, 9> rareSetup{
        0x48, 0x8B, 0xCB, // mov rcx, rbx
        0x48, 0x8B, 0xD7, // mov rdx, rdi
        0x4D, 0x8B, 0xC5, // mov r8, r13
    };
    constexpr std::array<std::uint8_t, 6> craftedSetup{
        0x49, 0x8B, 0xD7, // mov rdx, r15 (rcx already holds the seed)
        0x4D, 0x8B, 0xC4, // mov r8, r12
    };
    if (!WritePreservingArgumentBridge(
            MagicPrefixRelayIndex,
            reinterpret_cast<const void*>(&SelectMagicPrefixValue))
        || !WriteBridge(
            MagicSuffixRelayIndex,
            suffixSetup.data(),
            suffixSetup.size(),
            reinterpret_cast<const void*>(&SelectMagicSuffixValue))
        || !WriteBridge(
            RareSelectionRelayIndex,
            rareSetup.data(),
            rareSetup.size(),
            reinterpret_cast<const void*>(&SelectRareCount))
        || !WriteBridge(
            CraftedSelectionRelayIndex,
            craftedSetup.data(),
            craftedSetup.size(),
            reinterpret_cast<const void*>(&SelectCraftedCount))) {
        Context->LogError("ProgressiveAffixes: could not build relay code.");
        return false;
    }
    DWORD oldProtection{};
    if (!VirtualProtect(
            RelayPage,
            RelayStride * RelayCount,
            PAGE_EXECUTE_READ,
            &oldProtection)) {
        Context->LogError("ProgressiveAffixes: could not protect relay code.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayStride * RelayCount);
    return true;
}

std::uintptr_t RelayAddress(std::size_t index) noexcept {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + index * RelayStride;
}

bool EncodeRel32(
        std::uint8_t* output,
        std::uint8_t opcode,
        std::uintptr_t instruction,
        std::uintptr_t target) noexcept {
    const auto displacement = static_cast<std::int64_t>(target)
        - static_cast<std::int64_t>(instruction + 5);
    if (displacement < std::numeric_limits<std::int32_t>::min()
            || displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    output[0] = opcode;
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(output + 1, &relative, sizeof(relative));
    return true;
}

template <std::size_t Size>
bool PatchCallWithPadding(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected,
        std::uintptr_t target) noexcept {
    static_assert(Size >= 5);
    std::array<std::uint8_t, Size> bytes{};
    bytes.fill(0x90);
    if (!EncodeRel32(
            bytes.data(),
            0xE8,
            reinterpret_cast<std::uintptr_t>(Base + rva),
            target)) {
        return false;
    }
    return Context->PatchBytes(
        rva,
        expected.data(),
        static_cast<std::uint32_t>(expected.size()),
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size()));
}

template <std::size_t Size>
bool Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept {
    return Context->CheckExpectedBytes(
        rva,
        expected.data(),
        static_cast<std::uint32_t>(expected.size()));
}

template <std::size_t EntrySize, std::size_t BodySize>
bool ValidateComposableItemTypeEntry(
    const std::array<std::uint8_t, EntrySize>& entry,
    const std::array<std::uint8_t, BodySize>& body
) noexcept {
    if (!Matches(CheckItemTypeRva + EntrySize, body)) {
        Context->LogError(
            "ProgressiveAffixes: item-type helper body signature mismatch.");
        return false;
    }
    if (Matches(CheckItemTypeRva, entry)) return true;

    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (Context->QueryService(
            D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version,
            &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(
            diagnostics,
            D2RL::DiagnosticsServiceV1RequiredSize)
        || !diagnostics->queryHookStatus) {
        Context->LogError(
            "ProgressiveAffixes: Diagnostics v1 is required to validate the shared item-type entry.");
        return false;
    }

    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = CheckItemTypeRva,
        .expected = entry.data(),
        .expectedSize = static_cast<std::uint32_t>(entry.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    if (diagnostics->queryHookStatus(Context, &query, &status)
            != D2RL::Diagnostics::Result::Success
        || status.state != D2RL::Diagnostics::ModificationState::Tracked
        || status.kind != D2RL::Diagnostics::ModificationKind::InlineHook
        || status.ownerCount == 0) {
        Context->LogError(
            "ProgressiveAffixes: item-type entry has an untracked or non-composable modification.");
        return false;
    }

    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "ProgressiveAffixes: composing through loader-owned item-type hook (%.*s).",
        63,
        status.ownerPluginId);
    if (Settings.diagnostics) Context->LogInfo(message);
    return true;
}

bool ValidateSignatures() noexcept {
    constexpr std::array<std::uint8_t, 32> generalRoll{
        0x44,0x8B,0xCA,0x48,0x8B,0xD1,0x45,0x85,
        0xC9,0x7F,0x03,0x33,0xC0,0xC3,0x8B,0x01,
        0x8B,0x49,0x04,0x4C,0x69,0xC0,0xC5,0x90,
        0xC6,0x6A,0x4C,0x03,0xC1,0x41,0x8D,0x49,
    };
    constexpr std::array<std::uint8_t, 35> powerOfTwoRoll{
        0x8B,0x01,0x4C,0x69,0xC0,0xC5,0x90,0xC6,
        0x6A,0x8B,0x41,0x04,0x4C,0x03,0xC0,0x49,
        0x8B,0xC0,0x44,0x89,0x01,0x48,0xC1,0xE8,
        0x20,0x89,0x41,0x04,0x8D,0x42,0xFF,0x41,
        0x23,0xC0,0xC3,
    };
    constexpr std::array<std::uint8_t, 24> getDataTables{
        0x48,0x83,0xEC,0x28,0x0F,0xB6,0xC1,0x48,
        0x89,0x44,0x24,0x38,0x48,0x83,0xF8,0x04,
        0x72,0x19,0x48,0x8D,0x44,0x24,0x38,0x48,
    };
    constexpr std::array<std::uint8_t, 24> getItemContext{
        0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,
        0x1A,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
        0x24,0x30,0xE8,0x49,0xC7,0xFF,0xFF,0x84,
    };
    constexpr std::array<std::uint8_t, 24> primaryType{
        0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0xF9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,
        0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,
    };
    constexpr std::array<std::uint8_t, 15> checkTypeEntry{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,
        0x24,0x18,0x48,0x89,0x74,0x24,0x20,
    };
    constexpr std::array<std::uint8_t, 9> checkTypeBody{
        0x57,
        0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20,
    };
    constexpr std::array<std::uint8_t, 24> magicGenerator{
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
        0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
        0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30,
    };
    constexpr std::array<std::uint8_t, 15> craftedGenerator{
        0x48,0x89,0x54,0x24,0x10,0x57,0x41,0x54,
        0x41,0x57,0x48,0x83,0xEC,0x70,0x4C,
    };
    constexpr std::array<std::uint8_t, 15> rareGenerator{
        0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,
        0x41,0x55,0x41,0x57,0x48,0x83,0xEC,
    };
    constexpr std::array<std::uint8_t, 6> magicPrefix{0x8B,0x82,0xA8,0x00,0x00,0x00};
    constexpr std::array<std::uint8_t, 6> magicSuffix{0x8B,0x86,0xB4,0x00,0x00,0x00};
    constexpr std::array<std::uint8_t, 3> magicSuffixMinimum{0x0F,0x4F,0xEB};
    constexpr std::array<std::uint8_t, 13> craftedVanillaMinimum{
        0xB8,0x03,0x00,0x00,0x00,0x41,0x8B,0xDD,
        0x0F,0x9F,0xC3,0xFF,0xC3,
    };
    constexpr std::array<std::uint8_t, 5> craftedRoll{0xE8,0xE0,0x98,0xBC,0xFF};
    constexpr std::array<std::uint8_t, 5> craftedClamp{0x3B,0xD8,0x0F,0x47,0xC3};
    constexpr std::array<std::uint8_t, 2> rareJewelBranch{0x75,0x29};
    constexpr std::array<std::uint8_t, 41> rareJewelVanillaPath{
        0x8B,0x0B,0x4C,0x8B,0xCB,0x4C,0x69,0xE1,
        0xC5,0x90,0xC6,0x6A,0x8B,0x4B,0x04,0x4C,
        0x03,0xE1,0x49,0x8B,0xCC,0x44,0x89,0x23,
        0x48,0xC1,0xE9,0x20,0x41,0x83,0xE4,0x01,
        0x89,0x4B,0x04,0x41,0x83,0xC4,0x03,0xEB,
        0x1E,
    };
    constexpr std::array<std::uint8_t, 30> rareSelection{
        0xBA,0x08,0x00,0x00,0x00,0x48,0x8B,0xCB,
        0xE8,0xC3,0xB4,0xDD,0xFF,0x48,0x63,0xC8,
        0x4C,0x8D,0x25,0xC9,0x31,0x7B,0x01,0x4C,
        0x8B,0xCB,0x45,0x8B,0x24,0x8C,
    };
    return Matches(GeneralSeedRollRva, generalRoll)
        && Matches(PowerOfTwoSeedRollRva, powerOfTwoRoll)
        && Matches(GetDataTablesRva, getDataTables)
        && Matches(GetItemContextRva, getItemContext)
        && Matches(GetPrimaryItemTypeRva, primaryType)
        && ValidateComposableItemTypeEntry(checkTypeEntry, checkTypeBody)
        && Matches(MagicGeneratorRva, magicGenerator)
        && Matches(CraftedGeneratorRva, craftedGenerator)
        && Matches(RareGeneratorRva, rareGenerator)
        && Matches(MagicPrefixValueRva, magicPrefix)
        && Matches(MagicSuffixValueRva, magicSuffix)
        && Matches(MagicSuffixMinimumRva, magicSuffixMinimum)
        && Matches(CraftedVanillaMinimumRva, craftedVanillaMinimum)
        && Matches(CraftedRollCallRva, craftedRoll)
        && Matches(CraftedMinimumClampRva, craftedClamp)
        && Matches(RareJewelBranchRva, rareJewelBranch)
        && Matches(RareJewelVanillaPathRva, rareJewelVanillaPath)
        && Matches(RareSelectionRva, rareSelection);
}

bool InstallPatches() noexcept {
    constexpr std::array<std::uint8_t, 6> magicPrefix{0x8B,0x82,0xA8,0x00,0x00,0x00};
    constexpr std::array<std::uint8_t, 6> magicSuffix{0x8B,0x86,0xB4,0x00,0x00,0x00};
    constexpr std::array<std::uint8_t, 5> craftedRoll{0xE8,0xE0,0x98,0xBC,0xFF};
    constexpr std::array<std::uint8_t, 5> craftedClamp{0x3B,0xD8,0x0F,0x47,0xC3};
    constexpr std::array<std::uint8_t, 2> rareJewelBranch{0x75,0x29};
    constexpr std::array<std::uint8_t, 2> rareJewelToCommon{0xEB,0x29};
    constexpr std::array<std::uint8_t, 30> rareSelectionExpected{
        0xBA,0x08,0x00,0x00,0x00,0x48,0x8B,0xCB,
        0xE8,0xC3,0xB4,0xDD,0xFF,0x48,0x63,0xC8,
        0x4C,0x8D,0x25,0xC9,0x31,0x7B,0x01,0x4C,
        0x8B,0xCB,0x45,0x8B,0x24,0x8C,
    };

    static_assert(rareSelectionExpected.size() == RareSelectionPatchSize);
    std::array<std::uint8_t, RareSelectionPatchSize> rareSelection{};
    const auto rareSite = reinterpret_cast<std::uintptr_t>(Base + RareSelectionRva);
    if (!BuildRareSelectionPatch(
            rareSelection,
            rareSite,
            RelayAddress(RareSelectionRelayIndex),
            reinterpret_cast<std::uintptr_t>(Base + RareSelectionReturnRva))) {
        return false;
    }

    return PatchCallWithPadding(
            MagicPrefixValueRva,
            magicPrefix,
            RelayAddress(MagicPrefixRelayIndex))
        && PatchCallWithPadding(
            MagicSuffixValueRva,
            magicSuffix,
            RelayAddress(MagicSuffixRelayIndex))
        && PatchCallWithPadding(
            CraftedRollCallRva,
            craftedRoll,
            RelayAddress(CraftedSelectionRelayIndex))
        && Context->PatchNop(
            CraftedMinimumClampRva,
            craftedClamp.data(),
            static_cast<std::uint32_t>(craftedClamp.size()),
            static_cast<std::uint32_t>(craftedClamp.size()))
        && Context->PatchBytes(
            RareJewelBranchRva,
            rareJewelBranch.data(),
            static_cast<std::uint32_t>(rareJewelBranch.size()),
            rareJewelToCommon.data(),
            static_cast<std::uint32_t>(rareJewelToCommon.size()))
        && Context->PatchBytes(
            RareSelectionRva,
            rareSelectionExpected.data(),
            static_cast<std::uint32_t>(rareSelectionExpected.size()),
            rareSelection.data(),
            static_cast<std::uint32_t>(rareSelection.size()));
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "Progressive Affixes 0.3.3: %s; diagnostics=%s; config=%s; categories magic=%zu rare=%zu crafted=%zu; types resolved=%u unresolved=%u; selections magic=%llu rare=%llu crafted=%llu.",
        RuntimeConfigRejected.load(std::memory_order_acquire)
            ? "runtime configuration rejected"
            : (Operational.load(std::memory_order_acquire) ? "active" : "disabled"),
        Settings.diagnostics ? "true" : "false",
        LoadedConfigPath.c_str(),
        Settings.magic.size(),
        Settings.rare.size(),
        Settings.crafted.size(),
        ResolvedTypeCount.load(std::memory_order_relaxed),
        UnresolvedTypeCount.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(MagicSelections.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RareSelections.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CraftedSelections.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

} // namespace
} // namespace RuffnecKk::ProgressiveAffixes

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-progressive-affixes",
    .name = "Progressive Affixes",
    .version = "0.3.3",
    .author = "RuffnecKk",
    .description = "Increases generated item affix counts as item levels rise.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    using namespace RuffnecKk::ProgressiveAffixes;
    if (!D2RL::HasContext(context)
        || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    Operational.store(false, std::memory_order_release);
    RuntimeConfigRejected.store(false, std::memory_order_release);
    ResolvedTypeCount.store(0, std::memory_order_relaxed);
    UnresolvedTypeCount.store(0, std::memory_order_relaxed);
    MagicSelections.store(0, std::memory_order_relaxed);
    RareSelections.store(0, std::memory_order_relaxed);
    CraftedSelections.store(0, std::memory_order_relaxed);
    RuntimeConfigErrorLogged.clear(std::memory_order_relaxed);
    TypeResolutionReported.store(false, std::memory_order_relaxed);
    MagicSelectionReported.store(false, std::memory_order_relaxed);
    RareSelectionReported.store(false, std::memory_order_relaxed);
    CraftedSelectionReported.store(false, std::memory_order_relaxed);
    TypeCache = {};
    if (!Base) {
        Context->LogError(
            "ProgressiveAffixes: D2R executable base is unavailable.");
        return false;
    }
    if (!LoadConfig()) return false;
    if (!Settings.enabled) {
        const auto message = std::string(
            "Progressive Affixes 0.3.3 by RuffnecKk loaded disabled; config=")
            + LoadedConfigPath + ".";
        Context->LogInfo(message.c_str());
        static_cast<void>(Context->RegisterConsoleCommand(
            "progressive-affixes",
            Status,
            "Show progressive affix configuration and counters."));
        return true;
    }
    if (context->modDataVersionBuild != 0
            && context->modDataVersionBuild != SupportedBuild) {
        Context->LogError(
            "ProgressiveAffixes: supports only D2R 3.2 build 92777.");
        return false;
    }
    GeneralSeedRoll = At<SeedRollFn>(GeneralSeedRollRva);
    PowerOfTwoSeedRoll = At<SeedRollFn>(PowerOfTwoSeedRollRva);
    GetDataTables = At<GetDataTablesFn>(GetDataTablesRva);
    GetItemContext = At<GetItemContextFn>(GetItemContextRva);
    GetPrimaryItemType = At<GetPrimaryItemTypeFn>(GetPrimaryItemTypeRva);
    CheckItemType = At<CheckItemTypeFn>(CheckItemTypeRva);
    if (!ValidateSignatures()) {
        Context->LogError(
            "ProgressiveAffixes: build signature mismatch or conflicting affix owner; plugin refused.");
        return false;
    }
    if (!CreateRelays() || !InstallPatches()) {
        Context->LogError(
            "ProgressiveAffixes: affix-selection relays or patches could not be installed.");
        return false;
    }
    Operational.store(true, std::memory_order_release);
    if (!Context->RegisterConsoleCommand(
            "progressive-affixes",
            Status,
            "Show progressive affix configuration and counters.")) {
        Context->LogWarn(
            "ProgressiveAffixes: optional status command could not be registered.");
    }
    const auto message = std::string(
        "Progressive Affixes 0.3.3 by RuffnecKk active; config=")
        + LoadedConfigPath + ".";
    Context->LogInfo(message.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace RuffnecKk::ProgressiveAffixes;
    Operational.store(false, std::memory_order_release);
    if (Context && Settings.diagnostics) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "ProgressiveAffixes diagnostics: stopped; resolved=%u, unresolved=%u, selections magic=%llu rare=%llu crafted=%llu.",
            ResolvedTypeCount.load(std::memory_order_relaxed),
            UnresolvedTypeCount.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(
                MagicSelections.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                RareSelections.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                CraftedSelections.load(std::memory_order_relaxed)));
        Context->LogInfo(message);
    }
    if (RelayPage) {
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
    }
    CheckItemType = nullptr;
    GetPrimaryItemType = nullptr;
    GetItemContext = nullptr;
    GetDataTables = nullptr;
    PowerOfTwoSeedRoll = nullptr;
    GeneralSeedRoll = nullptr;
    Base = nullptr;
    Context = nullptr;
}
