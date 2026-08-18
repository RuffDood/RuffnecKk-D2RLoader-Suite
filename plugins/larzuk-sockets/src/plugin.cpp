#include <D2RLPlugin/api.h>
#include <D2RLPlugin/diagnostics.h>

#include "native_contract.hpp"
#include "policy.hpp"

#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace RuffnecKk::LarzukSockets {
namespace {

struct ItemSeed {
    std::uint32_t low{};
    std::uint32_t high{};
};

using AddSocketsFn = void(__fastcall*)(void*, std::int32_t) noexcept;
using GetItemSeedFn = ItemSeed*(__fastcall*)(void*) noexcept;
using GetItemQualityFn = std::int32_t(__fastcall*)(void*) noexcept;
using SetItemFlagFn = void(__fastcall*)(
    void*, std::uint32_t, std::int32_t) noexcept;
using GetMaxSocketsFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetItemDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using GetItemsTxtRecordFn = std::uint8_t*(__fastcall*)(
    std::uint8_t, std::int32_t) noexcept;
using GetStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::uint32_t) noexcept;
using SetUnitStatFn = void(__fastcall*)(
    void*, std::int32_t, std::int32_t, std::uint32_t) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::filesystem::path LoadedConfigPath;

AddSocketsFn OriginalAddSockets{};
GetItemSeedFn GetItemSeed{};
GetItemQualityFn GetItemQuality{};
SetItemFlagFn SetItemFlag{};
GetMaxSocketsFn GetMaxSockets{};
GetItemDataContextFn GetItemDataContext{};
GetItemsTxtRecordFn GetItemsTxtRecord{};
GetStatFn GetStat{};
SetUnitStatFn SetUnitStat{};

std::atomic<std::uint64_t> ConfiguredRewards{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-larzuk-sockets",
    .name = "Larzuk Sockets",
    .version = "1.0.1",
    .author = "RuffnecKk",
    .description =
        "Configures Larzuk socket rewards by difficulty and item quality.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

template<class T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

auto PathForLog(const std::filesystem::path& path) -> std::string {
    const auto utf8 = path.u8string();
    return {
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size(),
    };
}

auto AdvanceItemRng(ItemSeed* seed) noexcept -> std::uint32_t {
    const auto next = static_cast<std::uint64_t>(seed->low)
        * 0x6AC690C5ULL + seed->high;
    seed->low = static_cast<std::uint32_t>(next);
    seed->high = static_cast<std::uint32_t>(next >> 32);
    return seed->low;
}

auto LarzukCallerGame() noexcept -> void* {
    void* game{};
    const auto* returnSlot = reinterpret_cast<const std::uint8_t*>(
        _AddressOfReturnAddress());
    std::memcpy(
        &game,
        returnSlot + NativeContract::LarzukCallerGameOffset,
        sizeof(game));
    return game;
}

auto LegalMaximum(void* item) noexcept -> std::uint8_t {
    std::uint32_t classId{};
    std::memcpy(
        &classId,
        static_cast<const std::uint8_t*>(item)
            + NativeContract::UnitClassIdOffset,
        sizeof(classId));
    const auto dataContext = GetItemDataContext(item);
    const auto* itemsRecord = GetItemsTxtRecord(
        dataContext,
        static_cast<std::int32_t>(classId));
    if (itemsRecord == nullptr) return 0;
    return EffectiveLegalMaximum(
        GetMaxSockets(item),
        itemsRecord[NativeContract::ItemsInventoryWidthOffset],
        itemsRecord[NativeContract::ItemsInventoryHeightOffset]);
}

__declspec(noinline) void __fastcall HookAddSockets(
    void* item,
    std::int32_t vanillaSockets
) noexcept {
    const auto returnRva = reinterpret_cast<std::uintptr_t>(_ReturnAddress())
        - reinterpret_cast<std::uintptr_t>(Base);
    if (returnRva != NativeContract::LarzukReturnRva || item == nullptr) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    void* game = LarzukCallerGame();
    if (game == nullptr
        || GetStat(item, NativeContract::NumberOfSocketsStat, 0) > 0) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    const auto difficulty = *(
        static_cast<const std::uint8_t*>(game)
        + NativeContract::GameDifficultyOffset);
    const auto quality = GetItemQuality(item);
    const auto* configured = FindRule(Settings.rules, difficulty, quality);
    if (configured == nullptr || !configured->has_value()) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    const auto legalMaximum = LegalMaximum(item);
    if (legalMaximum == 0) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    const auto rule = **configured;
    const auto clampedMinimum = (std::min)(rule.minSockets, legalMaximum);
    const auto clampedMaximum = (std::min)(rule.maxSockets, legalMaximum);
    std::uint32_t rawRoll{};
    if (clampedMinimum < clampedMaximum) {
        auto* seed = GetItemSeed(item);
        if (seed == nullptr) {
            OriginalAddSockets(item, vanillaSockets);
            return;
        }
        rawRoll = AdvanceItemRng(seed);
    }

    const auto sockets = ResolveSockets(rule, legalMaximum, rawRoll);
    if (sockets == 0) {
        OriginalAddSockets(item, vanillaSockets);
        return;
    }

    SetItemFlag(item, NativeContract::SocketedItemFlag, 1);
    SetUnitStat(item, NativeContract::NumberOfSocketsStat, sockets, 0);
    ConfiguredRewards.fetch_add(1, std::memory_order_relaxed);

    if (Settings.diagnostics) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "LarzukSockets: difficulty=%u quality=%d configured=%u-%u legalMaximum=%u result=%u.",
            static_cast<unsigned>(difficulty),
            quality,
            static_cast<unsigned>(rule.minSockets),
            static_cast<unsigned>(rule.maxSockets),
            static_cast<unsigned>(legalMaximum),
            static_cast<unsigned>(sockets));
        Context->LogInfo(message);
    }
}

auto ValidateComposableHelper(
    const NativeContract::HelperContract& contract
) noexcept -> bool {
    if (Context->CheckExpectedBytes(
            contract.rva,
            contract.expected.data(),
            static_cast<std::uint32_t>(contract.expected.size()))) {
        return true;
    }

    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (Context->QueryService(
            D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version,
            &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(
            diagnostics,
            D2RL::DiagnosticsServiceV1RequiredSize)
        || diagnostics->queryHookStatus == nullptr) {
        Context->LogError(
            "LarzukSockets: Diagnostics v1 is required to validate a modified helper entry.");
        return false;
    }

    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = contract.rva,
        .expected = contract.expected.data(),
        .expectedSize = static_cast<std::uint32_t>(contract.expected.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    const bool composable = diagnostics->queryHookStatus(
            Context, &query, &status)
            == D2RL::Diagnostics::Result::Success
        && status.state == D2RL::Diagnostics::ModificationState::Tracked
        && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook
        && status.ownerCount == 1;
    if (!composable) {
        char message[224]{};
        std::snprintf(
            message,
            sizeof(message),
            "LarzukSockets: %s has an untracked or non-composable modification.",
            contract.label);
        Context->LogError(message);
    }
    return composable;
}

auto ValidateNativeContract() noexcept -> bool {
    if (!Context->CheckExpectedBytes(
            NativeContract::LarzukCallerRva,
            NativeContract::LarzukCallerExpected.data(),
            static_cast<std::uint32_t>(
                NativeContract::LarzukCallerExpected.size()))) {
        Context->LogError(
            "LarzukSockets: Larzuk reward caller ABI signature mismatch.");
        return false;
    }
    for (const auto& helper : NativeContract::Helpers) {
        if (!ValidateComposableHelper(helper)) return false;
    }
    return true;
}

auto BindNativeFunctions() noexcept -> void {
    GetItemSeed = At<GetItemSeedFn>(NativeContract::Helpers[0].rva);
    GetItemQuality = At<GetItemQualityFn>(NativeContract::Helpers[1].rva);
    SetItemFlag = At<SetItemFlagFn>(NativeContract::Helpers[2].rva);
    GetMaxSockets = At<GetMaxSocketsFn>(NativeContract::Helpers[3].rva);
    GetItemDataContext = At<GetItemDataContextFn>(
        NativeContract::Helpers[4].rva);
    GetItemsTxtRecord = At<GetItemsTxtRecordFn>(
        NativeContract::Helpers[5].rva);
    GetStat = At<GetStatFn>(NativeContract::Helpers[6].rva);
    SetUnitStat = At<SetUnitStatFn>(NativeContract::Helpers[7].rva);
}

auto InstallHook() noexcept -> bool {
    if (!Context->InstallInlineHook(
            NativeContract::AddSocketsRva,
            NativeContract::AddSocketsExpected.data(),
            static_cast<std::uint32_t>(
                NativeContract::AddSocketsExpected.size()),
            HookAddSockets,
            &OriginalAddSockets)) {
        Context->LogError(
            "LarzukSockets: ITEMS_AddSockets hook installation failed.");
        return false;
    }
    return true;
}

auto ResetState() noexcept -> void {
    Settings = {};
    LoadedConfigPath.clear();
    OriginalAddSockets = nullptr;
    GetItemSeed = nullptr;
    GetItemQuality = nullptr;
    SetItemFlag = nullptr;
    GetMaxSockets = nullptr;
    GetItemDataContext = nullptr;
    GetItemsTxtRecord = nullptr;
    GetStat = nullptr;
    SetUnitStat = nullptr;
    Base = nullptr;
    Context = nullptr;
}

} // namespace
} // namespace RuffnecKk::LarzukSockets

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &RuffnecKk::LarzukSockets::Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    using namespace RuffnecKk::LarzukSockets;
    if (context == nullptr) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (Base == nullptr) {
        context->LogError("LarzukSockets: D2R executable base is unavailable.");
        ResetState();
        return false;
    }
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != NativeContract::SupportedBuild) {
        context->LogError(
            "LarzukSockets: only D2R build 92777 is supported.");
        ResetState();
        return false;
    }

    try {
        auto loaded = LoadConfigFromCandidates(
            ResolveConfigCandidates(context));
        Settings = std::move(loaded.config);
        LoadedConfigPath = std::move(loaded.source);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "LarzukSockets: configuration refused; no hook installed: ")
            + exception.what() + ".";
        context->LogError(message.c_str());
        ResetState();
        return false;
    } catch (...) {
        context->LogError(
            "LarzukSockets: configuration refused by an unknown error; no hook installed.");
        ResetState();
        return false;
    }

    if (!Settings.enabled) {
        try {
            const auto message = std::string(
                "LarzukSockets 1.0.1 by RuffnecKk loaded disabled; no hook installed; config=")
                + PathForLog(LoadedConfigPath) + ".";
            context->LogInfo(message.c_str());
        } catch (...) {
            context->LogInfo(
                "LarzukSockets 1.0.1 by RuffnecKk loaded disabled; no hook installed.");
        }
        return true;
    }

    if (!HasRules(Settings.rules)) {
        try {
            const auto message = std::string(
                "LarzukSockets 1.0.1 loaded; all rules delegate to vanilla; hook not installed; config=")
                + PathForLog(LoadedConfigPath) + ".";
            context->LogInfo(message.c_str());
        } catch (...) {
            context->LogInfo(
                "LarzukSockets 1.0.1 loaded; all rules delegate to vanilla; hook not installed.");
        }
        return true;
    }

    if (!ValidateNativeContract()) {
        ResetState();
        return false;
    }
    BindNativeFunctions();
    if (!InstallHook()) {
        ResetState();
        return false;
    }

    try {
        const auto message = std::string(
            "LarzukSockets 1.0.1 by RuffnecKk loaded; configured hook active; config=")
            + PathForLog(LoadedConfigPath) + ".";
        context->LogInfo(message.c_str());
    } catch (...) {
        context->LogInfo(
            "LarzukSockets 1.0.1 by RuffnecKk loaded; configured hook active.");
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    RuffnecKk::LarzukSockets::ResetState();
}
