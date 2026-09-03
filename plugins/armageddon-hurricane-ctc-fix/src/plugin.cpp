#define NOMINMAX

#include <D2RLPlugin/api.h>

#include "armageddon_ctc_policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RuffnecKk::ArmageddonCtCFix {
namespace {

using namespace ruffneckk::armageddon_ctc;

constexpr wchar_t ConfigFileName[] =
    L"ruffneckk-armageddon-hurricane-ctc-fix.toml";
constexpr wchar_t SingletonName[] =
    L"Local\\RuffnecKk.ArmageddonCtCFix.Singleton";

constexpr std::size_t GameDataContextOffset = 0x106;
constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitIdOffset = 0x08;
constexpr std::size_t UnitSkillListOffset = 0x100;
constexpr std::size_t SkillsAuraStateOffset = 0xA0;
constexpr std::size_t SkillsItemEffectOffset = 0x20A;
constexpr std::size_t SkillListFirstOffset = 0x00;
constexpr std::size_t SkillListUsedOffset = 0x18;
constexpr std::size_t SkillRecordOffset = 0x00;
constexpr std::size_t SkillNextOffset = 0x08;
constexpr std::size_t SkillParam1Offset = 0x24;
constexpr std::size_t SkillLevelOffset = 0x40;
constexpr std::size_t SkillOwnerGuidOffset = 0x4C;
constexpr std::size_t SkillSelectionFlagOffset = 0x54;
constexpr std::size_t SyntheticSkillSize = 0x60;
constexpr std::size_t SyntheticSkillListSize = 0x20;

constexpr std::uintptr_t GetSkillsRecordRva = 0x097790;
constexpr std::uintptr_t CheckStateRva = 0x3351B0;
constexpr std::uintptr_t HighestLevelSkillRva = 0x33DD40;
constexpr std::uintptr_t GetSkillListRva = 0x34B6E0;
constexpr std::uintptr_t ArmageddonActiveRva = 0x574E90;
constexpr std::uintptr_t SharedStartRva = 0x575DE0;
constexpr std::uintptr_t ItemEffectHelperRva = 0x589930;

constexpr auto GetSkillsRecordExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x20,0x57,0x48,0x83,0xEC,0x30,0x48,
    0x63,0xF2,0xE8,0xE9,0x92,0x26,0x00,0x48,
    0x8B,0xF8,0x48,0x8B,0xDE,0x85,0xF6,0x78,
    0x09,0x48,0x3B,0x98,0xB8,0x11,0x00,0x00,
});
constexpr auto CheckStateExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,
    0xDA,0x48,0x8B,0xF1,0xE8,0x07,0x68,0x01,
    0x00,0x85,0xC0,0x74,0x0E,0x83,0xE8,0x01,
});
constexpr auto HighestLevelSkillExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,
    0xEC,0x20,0x41,0x0F,0xB6,0xD8,0x8B,0xFA,
    0xE8,0x8B,0xD9,0x00,0x00,0x48,0x85,0xC0,
});
constexpr auto GetSkillListExpected = std::to_array<std::uint8_t>({
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xD9,0x48,0x85,0xC9,0x75,0x20,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,
    0xD4,0x98,0xFF,0xFF,0x84,0xC0,0x74,0x01,
    0xCC,0x48,0x8B,0x83,0x00,0x01,0x00,0x00,
});
constexpr auto ArmageddonActiveExpected = std::to_array<std::uint8_t>({
    0x44,0x89,0x4C,0x24,0x20,0x48,0x89,0x4C,
    0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x54,
    0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x60,
    0x48,0x8B,0xFA,0x48,0x8B,0xE9,
});
constexpr auto SharedStartExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x10,0x44,0x89,0x4C,
    0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,
    0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,
    0x40,0x48,0x8B,0xF2,0x48,0x8B,0xE9,
});
constexpr auto ItemEffectHelperExpected = std::to_array<std::uint8_t>({
    0x44,0x89,0x4C,0x24,0x20,0x53,0x55,0x56,
    0x57,0x41,0x55,0x41,0x57,0x48,0x83,0xEC,
    0x68,0x4C,0x8B,0xF9,0x49,0x63,0xE8,0x0F,
    0xB6,0x89,0x06,0x01,0x00,0x00,
});

using GetSkillsRecordFn = std::uint8_t*(__fastcall*)(
    std::uint8_t, std::int32_t) noexcept;
using CheckStateFn = std::int32_t(__fastcall*)(
    void*, std::int32_t) noexcept;
using HighestLevelSkillFn = void*(__fastcall*)(
    void*, std::int32_t, bool) noexcept;
using GetSkillListFn = void*(__fastcall*)(void*) noexcept;
using ServerSkillFn = std::int32_t(__fastcall*)(
    void*, void*, std::int32_t, std::int32_t) noexcept;
using ItemEffectHelperFn = std::int32_t(__fastcall*)(
    void*,
    void*,
    std::int32_t,
    std::int32_t,
    void*,
    std::int32_t,
    std::int32_t,
    std::int32_t*,
    std::int32_t*,
    std::int32_t*,
    std::int32_t*,
    std::int32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"built-in defaults"};
std::string RuntimeBuildName{"unknown"};
HANDLE SingletonHandle{};
std::atomic_bool Operational{};

GetSkillsRecordFn GetSkillsRecord{};
CheckStateFn CheckState{};
HighestLevelSkillFn GetHighestLevelSkill{};
GetSkillListFn GetSkillList{};
ServerSkillFn OriginalArmageddonActive{};
ServerSkillFn OriginalSharedStart{};
ItemEffectHelperFn OriginalItemEffectHelper{};

std::recursive_mutex BridgeMutex;

struct ItemScopeState {
    void* game{};
    void* unit{};
    std::int32_t skillId{-1};
    std::int32_t skillLevel{};
    std::uint32_t depth{};
};

thread_local ItemScopeState ActiveItemScope{};

struct UnitKey {
    void* game{};
    void* unit{};
    std::int32_t unitType{};
    std::int32_t unitId{-1};
    std::int32_t skillId{};

    auto operator==(const UnitKey&) const noexcept -> bool = default;
};

struct UnitKeyHash {
    std::size_t operator()(const UnitKey& key) const noexcept {
        auto value = reinterpret_cast<std::uintptr_t>(key.game);
        value ^= reinterpret_cast<std::uintptr_t>(key.unit)
            + 0x9E3779B97F4A7C15ULL + (value << 6U) + (value >> 2U);
        value ^= static_cast<std::uint32_t>(key.unitType)
            + 0x9E3779B9U + (value << 6U) + (value >> 2U);
        value ^= static_cast<std::uint32_t>(key.unitId)
            + 0x85EBCA6BU + (value << 6U) + (value >> 2U);
        value ^= static_cast<std::uint32_t>(key.skillId)
            + 0xC2B2AE35U + (value << 6U) + (value >> 2U);
        return static_cast<std::size_t>(value);
    }
};

std::unordered_map<UnitKey, std::uint32_t, UnitKeyHash> ArmageddonSeeds;

std::atomic_uint64_t ItemEffectCalls{};
std::atomic_uint64_t ItemEffectRecordBridges{};
std::atomic_uint64_t ExistingSkillStarts{};
std::atomic_uint64_t SyntheticStarts{};
std::atomic_uint64_t SyntheticActiveCalls{};
std::atomic_uint64_t ExpiredSeedCleanups{};

template <typename Fn>
Fn At(std::uintptr_t rva) noexcept {
    return reinterpret_cast<Fn>(Base + rva);
}

template <typename Value>
Value ReadValue(const void* base, std::size_t offset) noexcept {
    Value result{};
    if (base) {
        const auto* bytes = static_cast<const std::uint8_t*>(base);
        std::memcpy(&result, bytes + offset, sizeof(result));
    }
    return result;
}

template <typename Value>
void WriteValue(void* base, std::size_t offset, const Value& value) noexcept {
    if (base) {
        auto* bytes = static_cast<std::uint8_t*>(base);
        std::memcpy(bytes + offset, &value, sizeof(value));
    }
}

class ItemScope final {
public:
    ItemScope(
            void* game,
            void* unit,
            std::int32_t skillId,
            std::int32_t skillLevel) noexcept
        : previous_(ActiveItemScope) {
        ActiveItemScope = {
            game,
            unit,
            skillId,
            skillLevel,
            previous_.depth + 1U,
        };
    }

    ~ItemScope() { ActiveItemScope = previous_; }

    ItemScope(const ItemScope&) = delete;
    auto operator=(const ItemScope&) -> ItemScope& = delete;

private:
    ItemScopeState previous_{};
};

class WordPatch final {
public:
    WordPatch(void* base, std::size_t offset, std::uint16_t value) noexcept
        : base_(base), offset_(offset), previous_(ReadValue<std::uint16_t>(
              base,
              offset)) {
        WriteValue(base_, offset_, value);
    }

    ~WordPatch() { WriteValue(base_, offset_, previous_); }

    WordPatch(const WordPatch&) = delete;
    auto operator=(const WordPatch&) -> WordPatch& = delete;

private:
    void* base_{};
    std::size_t offset_{};
    std::uint16_t previous_{};
};

class PointerPatch final {
public:
    PointerPatch(void* base, std::size_t offset, void* value) noexcept
        : base_(base), offset_(offset), previous_(ReadValue<void*>(
              base,
              offset)) {
        WriteValue(base_, offset_, value);
    }

    ~PointerPatch() { WriteValue(base_, offset_, previous_); }

    PointerPatch(const PointerPatch&) = delete;
    auto operator=(const PointerPatch&) -> PointerPatch& = delete;

private:
    void* base_{};
    std::size_t offset_{};
    void* previous_{};
};

class SkillListScope final {
public:
    explicit SkillListScope(void* unit) noexcept
        : unit_(unit), original_(unit ? GetSkillList(unit) : nullptr) {
        if (!unit_) return;
        if (original_) {
            current_ = original_;
            return;
        }
        synthetic_.fill(0);
        current_ = synthetic_.data();
        WriteValue(unit_, UnitSkillListOffset, current_);
        installed_ = true;
    }

    ~SkillListScope() {
        if (installed_) {
            WriteValue(unit_, UnitSkillListOffset, original_);
        }
    }

    void* get() const noexcept { return current_; }

    SkillListScope(const SkillListScope&) = delete;
    auto operator=(const SkillListScope&) -> SkillListScope& = delete;

private:
    alignas(16) std::array<std::uint8_t, SyntheticSkillListSize> synthetic_{};
    void* unit_{};
    void* original_{};
    void* current_{};
    bool installed_{};
};

class SyntheticSkill final {
public:
    SyntheticSkill(
            void* record,
            std::int32_t level,
            std::uint32_t seed,
            void* next = nullptr) noexcept {
        bytes_.fill(0);
        WriteValue(bytes_.data(), SkillRecordOffset, record);
        WriteValue(bytes_.data(), SkillNextOffset, next);
        WriteValue(bytes_.data(), SkillParam1Offset, seed);
        WriteValue(bytes_.data(), SkillLevelOffset, level);
        WriteValue(
            bytes_.data(),
            SkillOwnerGuidOffset,
            static_cast<std::int32_t>(-1));
        WriteValue(
            bytes_.data(),
            SkillSelectionFlagOffset,
            static_cast<std::int32_t>(0));
    }

    void* get() noexcept { return bytes_.data(); }

    std::uint32_t seed() const noexcept {
        return ReadValue<std::uint32_t>(bytes_.data(), SkillParam1Offset);
    }

private:
    alignas(16) std::array<std::uint8_t, SyntheticSkillSize> bytes_{};
};

void LogDiagnostic(const char* message) noexcept {
    if (Settings.diagnostics && Context && message) {
        Context->LogInfo(message);
    }
}

std::uint8_t* SkillRecord(
        void* game,
        std::int32_t skillId) noexcept {
    if (!game || !GetSkillsRecord) return nullptr;
    const auto context = ReadValue<std::uint8_t>(
        game,
        GameDataContextOffset);
    return GetSkillsRecord(context, skillId);
}

std::int32_t SkillIdFromNode(void* skill) noexcept {
    const auto* record = ReadValue<const std::uint8_t*>(
        skill,
        SkillRecordOffset);
    return record ? ReadValue<std::uint16_t>(record, 0) : -1;
}

bool HasSkillState(
        void* unit,
        const std::uint8_t* skillRecord) noexcept {
    if (!unit || !skillRecord || !CheckState) return false;
    const auto stateId = ReadValue<std::int16_t>(
        skillRecord,
        SkillsAuraStateOffset);
    return stateId >= 0 && CheckState(unit, stateId) != 0;
}

bool ScopeMatches(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::int32_t skillLevel) noexcept {
    return ActiveItemScope.depth != 0
        && ActiveItemScope.game == game
        && ActiveItemScope.unit == unit
        && ActiveItemScope.skillId == skillId
        && ActiveItemScope.skillLevel == skillLevel;
}

UnitKey MakeUnitKey(
        void* game,
        void* unit,
        std::int32_t skillId) noexcept {
    return {
        game,
        unit,
        ReadValue<std::int32_t>(unit, UnitTypeOffset),
        ReadValue<std::int32_t>(unit, UnitIdOffset),
        skillId,
    };
}

std::vector<std::filesystem::path> ConfigCandidates() {
    std::vector<std::filesystem::path> directories;
    if (Context && Context->activeMod && Context->activeMod[0] != '\0'
            && Context->modSupportDirectory
            && Context->modSupportDirectory[0] != L'\0') {
        directories.emplace_back(
            std::filesystem::path(Context->modSupportDirectory) / L"config");
    }
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        directories.emplace_back(
            std::filesystem::path(Context->pluginConfigPath).parent_path());
    }
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    if (!error) directories.emplace_back(current / L"d2rloader" / L"config");
    return BuildConfigCandidates(directories, ConfigFileName);
}

bool LoadConfig() noexcept {
    Settings = {};
    LoadedConfigPath = "built-in defaults";
    try {
        for (const auto& path : ConfigCandidates()) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) continue;
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("configuration file cannot be opened");
            }
            const std::string text{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            Config parsed{};
            std::string parseError;
            if (!ParseToml(text, parsed, parseError)) {
                throw std::runtime_error(parseError);
            }
            Settings = parsed;
            LoadedConfigPath = path.string();
            return true;
        }
        return true;
    } catch (const std::exception& exception) {
        const auto message = std::string("ArmageddonCtCFix: invalid config (")
            + exception.what() + ").";
        Context->LogError(message.c_str());
        return false;
    }
}

template <std::size_t Size>
bool Check(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected,
        const char* label) noexcept {
    if (MatchesFingerprint(Base + rva, expected)) {
        return true;
    }
    const auto message = std::string("ArmageddonCtCFix: ") + label
        + " signature mismatch; plugin refused.";
    Context->LogError(message.c_str());
    return false;
}

bool ValidateRuntime() noexcept {
    return Check(
            GetSkillsRecordRva,
            GetSkillsRecordExpected,
            "SkillsTxt lookup")
        && Check(
            CheckStateRva,
            CheckStateExpected,
            "state predicate")
        && Check(
            HighestLevelSkillRva,
            HighestLevelSkillExpected,
            "highest-level skill resolver")
        && Check(
            GetSkillListRva,
            GetSkillListExpected,
            "skill-list resolver")
        && Check(
            ArmageddonActiveRva,
            ArmageddonActiveExpected,
            "Armageddon active callback")
        && Check(
            SharedStartRva,
            SharedStartExpected,
            "Armageddon/Hurricane start callback")
        && Check(
            ItemEffectHelperRva,
            ItemEffectHelperExpected,
            "item-effect helper");
}

void ResolveNativeFunctions() noexcept {
    GetSkillsRecord = At<GetSkillsRecordFn>(GetSkillsRecordRva);
    CheckState = At<CheckStateFn>(CheckStateRva);
    GetHighestLevelSkill = At<HighestLevelSkillFn>(HighestLevelSkillRva);
    GetSkillList = At<GetSkillListFn>(GetSkillListRva);
}

std::int32_t __fastcall HookSharedStart(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::int32_t skillLevel) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !IsSkillEnabled(Settings, skillId)
            || !ScopeMatches(game, unit, skillId, skillLevel)
            || !game
            || !unit) {
        return OriginalSharedStart(game, unit, skillId, skillLevel);
    }

    std::lock_guard lock(BridgeMutex);
    SkillListScope skillList(unit);
    auto* const list = skillList.get();
    if (!list) {
        return OriginalSharedStart(game, unit, skillId, skillLevel);
    }

    void* const used = ReadValue<void*>(list, SkillListUsedOffset);
    if (used && SkillIdFromNode(used) == skillId) {
        ExistingSkillStarts.fetch_add(1, std::memory_order_relaxed);
        return OriginalSharedStart(game, unit, skillId, skillLevel);
    }

    void* realSkill{};
    if (ReadValue<void*>(unit, UnitSkillListOffset)) {
        realSkill = GetHighestLevelSkill(unit, skillId, false);
    }
    if (realSkill) {
        PointerPatch usedPatch(list, SkillListUsedOffset, realSkill);
        const auto result = OriginalSharedStart(
            game,
            unit,
            skillId,
            skillLevel);
        if (result != 0) {
            ExistingSkillStarts.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    auto* const record = SkillRecord(game, skillId);
    if (!record) {
        return OriginalSharedStart(game, unit, skillId, skillLevel);
    }

    const auto key = MakeUnitKey(game, unit, skillId);
    bool insertedSeedEntry{};
    if (skillId == ArmageddonSkillId) {
        try {
            const auto [iterator, inserted] = ArmageddonSeeds.try_emplace(
                key,
                0U);
            (void)iterator;
            insertedSeedEntry = inserted;
        } catch (const std::exception& exception) {
            const auto message = std::string(
                "ArmageddonCtCFix: seed bridge allocation failed (")
                + exception.what() + ").";
            Context->LogError(message.c_str());
            return 0;
        }
    }

    SyntheticSkill synthetic(record, skillLevel, 0U);
    PointerPatch usedPatch(list, SkillListUsedOffset, synthetic.get());
    const auto result = OriginalSharedStart(
        game,
        unit,
        skillId,
        skillLevel);

    if (skillId == ArmageddonSkillId) {
        const auto iterator = ArmageddonSeeds.find(key);
        if (result != 0) {
            if (iterator != ArmageddonSeeds.end()) {
                iterator->second = synthetic.seed();
            }
        } else if (insertedSeedEntry && iterator != ArmageddonSeeds.end()) {
            ArmageddonSeeds.erase(iterator);
        }
    }

    if (result != 0) {
        SyntheticStarts.fetch_add(1, std::memory_order_relaxed);
        if (Settings.diagnostics) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "ArmageddonCtCFix diagnostic: synthetic start skill=%d level=%d.",
                skillId,
                skillLevel);
            LogDiagnostic(message);
        }
    }
    return result;
}

std::int32_t __fastcall HookArmageddonActive(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::int32_t skillLevel) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !Settings.armageddon
            || skillId != ArmageddonSkillId
            || !game
            || !unit) {
        return OriginalArmageddonActive(game, unit, skillId, skillLevel);
    }

    std::lock_guard lock(BridgeMutex);
    const auto key = MakeUnitKey(game, unit, skillId);
    auto seedEntry = ArmageddonSeeds.find(key);
    if (seedEntry == ArmageddonSeeds.end()) {
        return OriginalArmageddonActive(game, unit, skillId, skillLevel);
    }
    const auto retainedSeed = seedEntry->second;

    auto* const record = SkillRecord(game, skillId);
    if (!record) {
        ArmageddonSeeds.erase(seedEntry);
        ExpiredSeedCleanups.fetch_add(1, std::memory_order_relaxed);
        return OriginalArmageddonActive(game, unit, skillId, skillLevel);
    }
    const auto statePresentBefore = HasSkillState(unit, record);
    if (ShouldEraseExpiredSeedBeforeActive(statePresentBefore)) {
        ArmageddonSeeds.erase(seedEntry);
        ExpiredSeedCleanups.fetch_add(1, std::memory_order_relaxed);
        return OriginalArmageddonActive(game, unit, skillId, skillLevel);
    }

    void* const existingList = GetSkillList(unit);
    void* realSkill{};
    if (existingList) {
        realSkill = GetHighestLevelSkill(unit, skillId, false);
    }
    if (realSkill) {
        WriteValue(realSkill, SkillParam1Offset, retainedSeed);
        ArmageddonSeeds.erase(seedEntry);
        return OriginalArmageddonActive(game, unit, skillId, skillLevel);
    }

    SkillListScope skillList(unit);
    auto* const list = skillList.get();
    if (!list) {
        return OriginalArmageddonActive(game, unit, skillId, skillLevel);
    }

    void* const first = ReadValue<void*>(list, SkillListFirstOffset);
    SyntheticSkill synthetic(record, skillLevel, retainedSeed, first);
    PointerPatch firstPatch(list, SkillListFirstOffset, synthetic.get());
    const auto result = OriginalArmageddonActive(
        game,
        unit,
        skillId,
        skillLevel);

    const auto updatedSeedEntry = ArmageddonSeeds.find(key);
    if (updatedSeedEntry != ArmageddonSeeds.end()) {
        const auto statePresentAfter = result != 0
            || HasSkillState(unit, record);
        if (ShouldEraseExpiredSeedAfterActive(
                result,
                statePresentAfter)) {
            ArmageddonSeeds.erase(updatedSeedEntry);
            ExpiredSeedCleanups.fetch_add(1, std::memory_order_relaxed);
        } else {
            updatedSeedEntry->second = synthetic.seed();
        }
    }
    SyntheticActiveCalls.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::int32_t __fastcall HookItemEffectHelper(
        void* game,
        void* unit,
        std::int32_t skillId,
        std::int32_t skillLevel,
        void* target,
        std::int32_t x,
        std::int32_t y,
        std::int32_t* unitType,
        std::int32_t* unitGuid,
        std::int32_t* targetX,
        std::int32_t* targetY,
        std::int32_t flag) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !IsSkillEnabled(Settings, skillId)
            || !game
            || !unit) {
        return OriginalItemEffectHelper(
            game,
            unit,
            skillId,
            skillLevel,
            target,
            x,
            y,
            unitType,
            unitGuid,
            targetX,
            targetY,
            flag);
    }

    std::lock_guard lock(BridgeMutex);
    auto* const record = SkillRecord(game, skillId);
    if (!record) {
        return OriginalItemEffectHelper(
            game,
            unit,
            skillId,
            skillLevel,
            target,
            x,
            y,
            unitType,
            unitGuid,
            targetX,
            targetY,
            flag);
    }

    ItemEffectCalls.fetch_add(1, std::memory_order_relaxed);
    const auto previousItemEffect = ReadValue<std::uint16_t>(
        record,
        SkillsItemEffectOffset);
    const ItemScope scope(game, unit, skillId, skillLevel);

    if (previousItemEffect == 0) {
        WordPatch itemEffectPatch(record, SkillsItemEffectOffset, 1U);
        const auto result = OriginalItemEffectHelper(
            game,
            unit,
            skillId,
            skillLevel,
            target,
            x,
            y,
            unitType,
            unitGuid,
            targetX,
            targetY,
            flag);
        ItemEffectRecordBridges.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    return OriginalItemEffectHelper(
        game,
        unit,
        skillId,
        skillLevel,
        target,
        x,
        y,
        unitType,
        unitGuid,
        targetX,
        targetY,
        flag);
}

bool InstallHooks() noexcept {
    if (!Context->InstallInlineHook(
            ArmageddonActiveRva,
            ArmageddonActiveExpected.data(),
            static_cast<std::uint32_t>(ArmageddonActiveExpected.size()),
            HookArmageddonActive,
            &OriginalArmageddonActive)) {
        Context->LogError(
            "ArmageddonCtCFix: Armageddon active callback is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            SharedStartRva,
            SharedStartExpected.data(),
            static_cast<std::uint32_t>(SharedStartExpected.size()),
            HookSharedStart,
            &OriginalSharedStart)) {
        Context->LogError(
            "ArmageddonCtCFix: shared start callback is already owned or unavailable.");
        return false;
    }
    if (!Context->InstallInlineHook(
            ItemEffectHelperRva,
            ItemEffectHelperExpected.data(),
            static_cast<std::uint32_t>(ItemEffectHelperExpected.size()),
            HookItemEffectHelper,
            &OriginalItemEffectHelper)) {
        Context->LogError(
            "ArmageddonCtCFix: item-effect helper is already owned or unavailable.");
        return false;
    }
    return true;
}

bool AcquireSingleton() noexcept {
    SingletonHandle = CreateMutexW(nullptr, FALSE, SingletonName);
    if (!SingletonHandle) {
        Context->LogError(
            "ArmageddonCtCFix: process singleton could not be created.");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(SingletonHandle);
        SingletonHandle = nullptr;
        Context->LogError(
            "ArmageddonCtCFix: duplicate global/mod-local installation refused.");
        return false;
    }
    return true;
}

void ReleaseSingleton() noexcept {
    if (SingletonHandle) {
        CloseHandle(SingletonHandle);
        SingletonHandle = nullptr;
    }
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    std::size_t retainedSeeds{};
    {
        std::lock_guard lock(BridgeMutex);
        retainedSeeds = ArmageddonSeeds.size();
    }
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "Armageddon-Hurricane CtC Fix 1.0.0: %s; helper calls=%llu; record bridges=%llu; real-skill starts=%llu; synthetic starts=%llu; synthetic active calls=%llu; expired seed cleanups=%llu; retained seeds=%zu; config=%s.",
        Operational.load(std::memory_order_acquire) ? "active" : "disabled",
        static_cast<unsigned long long>(
            ItemEffectCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ItemEffectRecordBridges.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ExistingSkillStarts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            SyntheticStarts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            SyntheticActiveCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            ExpiredSeedCleanups.load(std::memory_order_relaxed)),
        retainedSeeds,
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    ActiveItemScope = {};
    {
        std::lock_guard lock(BridgeMutex);
        ArmageddonSeeds.clear();
    }
    ItemEffectCalls.store(0, std::memory_order_relaxed);
    ItemEffectRecordBridges.store(0, std::memory_order_relaxed);
    ExistingSkillStarts.store(0, std::memory_order_relaxed);
    SyntheticStarts.store(0, std::memory_order_relaxed);
    SyntheticActiveCalls.store(0, std::memory_order_relaxed);
    ExpiredSeedCleanups.store(0, std::memory_order_relaxed);
}

} // namespace
} // namespace RuffnecKk::ArmageddonCtCFix

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-armageddon-hurricane-ctc-fix",
    .name = "Armageddon-Hurricane CtC Fix",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description = "Lets Armageddon trigger from chance-to-cast effects.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    using namespace RuffnecKk::ArmageddonCtCFix;
    Context = context;
    Base = context ? reinterpret_cast<std::uint8_t*>(context->exeBase) : nullptr;
    ResetState();
    if (!Context || !Base) return false;
    if (!AcquireSingleton()) return false;

    const auto* runtimeBuild = D2RL::GetBuildName(context);
    RuntimeBuildName = runtimeBuild ? runtimeBuild : "unknown";
    if (!LoadConfig()) {
        ReleaseSingleton();
        return false;
    }
    if (!Settings.enabled) {
        const auto message = std::string(
            "Armageddon-Hurricane CtC Fix 1.0.0 by RuffnecKk loaded disabled; config=")
            + LoadedConfigPath + "; build=" + RuntimeBuildName + ".";
        context->LogInfo(message.c_str());
        return true;
    }
    if (!Settings.armageddon && !Settings.hurricane) {
        context->LogError(
            "ArmageddonCtCFix: no supported skill is enabled; plugin refused.");
        ReleaseSingleton();
        return false;
    }
    if (!ValidateRuntime()) {
        ReleaseSingleton();
        return false;
    }
    {
        const auto message = std::string(
            "ArmageddonCtCFix: build=") + RuntimeBuildName
            + "; native fingerprint accepted.";
        context->LogInfo(message.c_str());
    }
    ResolveNativeFunctions();
    try {
        ArmageddonSeeds.reserve(256);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "ArmageddonCtCFix: seed bridge reservation failed (")
            + exception.what() + ").";
        context->LogError(message.c_str());
        ReleaseSingleton();
        return false;
    }
    if (!InstallHooks()) {
        ReleaseSingleton();
        return false;
    }

    Operational.store(true, std::memory_order_release);
    if (!context->RegisterConsoleCommand(
            "armageddon-ctc-fix",
            Status,
            "Show Armageddon-Hurricane CtC Fix status and counters.")) {
        context->LogWarn(
            "ArmageddonCtCFix: status command could not be registered.");
    }
    const auto message = std::string(
        "Armageddon-Hurricane CtC Fix 1.0.0 by RuffnecKk active; config=")
        + LoadedConfigPath + "; build=" + RuntimeBuildName + ".";
    context->LogInfo(message.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace RuffnecKk::ArmageddonCtCFix;
    Operational.store(false, std::memory_order_release);
    {
        std::lock_guard lock(BridgeMutex);
        ArmageddonSeeds.clear();
    }
    ReleaseSingleton();
}
