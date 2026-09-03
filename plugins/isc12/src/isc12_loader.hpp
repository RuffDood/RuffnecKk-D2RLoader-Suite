#pragma once

#include "isc12_codec_patch.hpp"
#include "isc12_envelope.hpp"
#include "isc12_item_packet_budget.hpp"
#include "isc12_native_schema_adapter.hpp"
#include "isc12_publication_coordinator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace D2RL {
struct PluginContext;
}

namespace ruffneckk::isc12 {

using InspectFullItemTransportProviderFn =
    FullItemTransportProvider (*)() noexcept;

#if defined(ISC12_CODEC_PATCH_TESTING)
enum class LoaderInstallResult : std::uint8_t {
    Active,
    FailedBeforeMutation,
    QuiescenceRequired,
    PartialCommitColdRestartRequired,
};

template <typename ReserveTailAttempt, typename PatchTail, typename ActivateTail,
          typename PatchCap, typename PublishCap>
auto CommitLoaderMutation(
        const NativePublicationLeaseView& quiescence,
        ReserveTailAttempt&& reserveTailAttempt,
        PatchTail&& patchTail,
        ActivateTail&& activateTail,
        PatchCap&& patchCap,
        PublishCap&& publishCap) noexcept -> LoaderInstallResult {
    if (!quiescence.IsHeld()) {
        return LoaderInstallResult::QuiescenceRequired;
    }
    // The PluginSDK forwards a bool from its patch service but does not
    // guarantee that a false result left the target bytes untouched. Once a
    // native write is attempted, its relay/state lifetime therefore becomes
    // process-bound even before the call returns.
    reserveTailAttempt();
    if (!quiescence.IsHeld()) {
        return LoaderInstallResult::QuiescenceRequired;
    }
    if (!patchTail()) {
        return LoaderInstallResult::PartialCommitColdRestartRequired;
    }
    if (!quiescence.IsHeld()) {
        return LoaderInstallResult::PartialCommitColdRestartRequired;
    }
    activateTail();
    if (!quiescence.IsHeld()) {
        return LoaderInstallResult::PartialCommitColdRestartRequired;
    }
    if (!patchCap()) {
        return LoaderInstallResult::PartialCommitColdRestartRequired;
    }
    if (!quiescence.IsHeld()) {
        return LoaderInstallResult::PartialCommitColdRestartRequired;
    }
    publishCap();
    return LoaderInstallResult::Active;
}
#endif

struct LoaderRuntimeStatus {
    bool prepared{};
    bool persistencePrepared{};
    bool tailPatchInstalled{};
    bool capPatchInstalled{};
    bool persistenceReaderPatchInstalled{};
    bool persistenceWriterPatchInstalled{};
    bool publicationAdaptersBound{};
    bool operational{};
    bool schemaReady{};
    bool persistenceCodecReady{};
    bool itemTransportReady{};
    bool coldRestartRequired{};
    std::uint64_t buildCalls{};
    std::uint64_t lastRowCount{};
    std::uint64_t lastDescriptionCount{};
    std::uint64_t fullItemRoot9C{};
    std::uint64_t fullItemRoot9D{};
    std::uint64_t fullItemTransactionsAccepted{};
    std::uint64_t fullItemTransactionsRejected{};
    std::uint64_t fullItemPacketsCaptured9C{};
    std::uint64_t fullItemPacketsCaptured9D{};
    std::uint64_t fullItemPacketsQueued{};
    std::uint64_t persistenceReadsAccepted{};
    std::uint64_t persistenceReadsRejected{};
    std::uint64_t persistenceWritesDelegated{};
    std::uint64_t persistenceWritesRejected{};
};

inline constexpr std::uintptr_t LoaderCompileCallRva = 0x31EC7B;
inline constexpr std::size_t LoaderCompileCallInstructionOffset = 14U;
inline constexpr std::uintptr_t NativeGenericCompileRva = 0x2FF970;
inline constexpr std::uintptr_t PlayerSaveStatWriterCallRva = 0x5352F6;
inline constexpr std::size_t PlayerSaveStatWriterCallInstructionOffset = 13U;
inline constexpr std::uintptr_t ItemSaveStatWriterCallRva = 0x37F174;
inline constexpr std::size_t ItemSaveStatWriterCallInstructionOffset = 45U;
inline constexpr std::uintptr_t NativeBitWriterRva = 0xA1B710;
inline constexpr std::uintptr_t PlayerSaveDynamicCapacityRva = 0x41E138;
inline constexpr std::uintptr_t PlayerSaveDynamicCallRva = 0x41E1E9;
inline constexpr std::size_t PlayerSaveDynamicCallInstructionOffset = 30U;
inline constexpr std::uintptr_t NativePlayerSaveRva = 0x52F090;
inline constexpr std::uintptr_t D2SContainerVersionForwardRva = 0x52EDFA;
inline constexpr std::size_t D2SContainerVersionForwardCallOffset = 29U;
inline constexpr std::uintptr_t NativeReadItemsByVersionRva = 0x41F0B0;
inline constexpr std::uintptr_t D2SSaveWriterProviderCallRva = 0x9F95C6;
inline constexpr std::size_t D2SSaveWriterProviderCallOffset = 30U;
inline constexpr std::uintptr_t NativeD2SSaveWriterRva = 0x122BFF0;
inline constexpr std::uintptr_t D2SSaveCloseProviderCallRva = 0x9F95E9;
inline constexpr std::size_t D2SSaveCloseProviderCallOffset = 21U;
inline constexpr std::uintptr_t NativeD2SSaveCloseRva = 0x11C7E30;

enum class LoaderCompileProviderKind : std::uint8_t {
    Invalid,
    NativeGenericCompiler,
    D2RCoreLoadExcelTable,
};

enum class PlayerSaveStatWriterProviderKind : std::uint8_t {
    Invalid,
    NativeBitWriter,
    D2RCoreWritePlayerSaveStatId,
};

enum class ItemSaveStatWriterProviderKind : std::uint8_t {
    Invalid,
    NativeBitWriter,
    D2RCoreWriteItemSaveStatId,
};

enum class PlayerSaveProviderKind : std::uint8_t {
    Invalid,
    NativePlayerSave,
    D2RCoreWritePlayerSaveWithEnvironmentCapture,
};

enum class D2SItemReadProviderKind : std::uint8_t {
    Invalid,
    NativeReadItemsByVersion,
    D2RCoreReadItemsByVersion,
};

enum class D2SSaveIoProviderKind : std::uint8_t {
    Invalid,
    NativeWriteAndClose,
    D2RCoreWriteAndCloseWithEnvironment,
};

inline auto CanEncodeRel32(
        std::uintptr_t instructionAddress,
        std::uintptr_t targetAddress) noexcept -> bool {
    if (instructionAddress
            > std::numeric_limits<std::uintptr_t>::max() - 5U) {
        return false;
    }
    const auto next = instructionAddress + 5U;
    if (targetAddress >= next) {
        return targetAddress - next
            <= static_cast<std::uintptr_t>(
                std::numeric_limits<std::int32_t>::max());
    }
    return next - targetAddress
        <= static_cast<std::uintptr_t>(
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max())
            + 1ULL);
}

auto PrepareLoaderExtension(
    const D2RL::PluginContext* context,
    std::uint8_t* base,
    std::size_t imageSize,
    FullItemTransportProvider initialTransportProvider,
    InspectFullItemTransportProviderFn inspectTransportProvider,
    bool diagnostics,
    std::string& error) noexcept -> bool;

auto InspectLoaderCompileProviderContract(
    const std::uint8_t* base,
    std::size_t imageSize) noexcept -> LoaderCompileProviderKind;

auto InspectPlayerSaveStatWriterProviderContract(
    const std::uint8_t* base,
    std::size_t imageSize) noexcept -> PlayerSaveStatWriterProviderKind;

auto InspectItemSaveStatWriterProviderContract(
    const std::uint8_t* base,
    std::size_t imageSize) noexcept -> ItemSaveStatWriterProviderKind;

auto InspectPlayerSaveProviderContract(
    const std::uint8_t* base,
    std::size_t imageSize) noexcept -> PlayerSaveProviderKind;

auto InspectD2SItemReadProviderContract(
    const std::uint8_t* base,
    std::size_t imageSize) noexcept -> D2SItemReadProviderKind;

auto InspectD2SSaveIoProviderContract(
    const std::uint8_t* base,
    std::size_t imageSize) noexcept -> D2SSaveIoProviderKind;

auto ShutdownLoaderExtension() noexcept -> void;
[[noreturn]] auto FailClosedNativePublication(
    const char* reason) noexcept -> void;
auto GetLoaderRuntimeStatus() noexcept -> LoaderRuntimeStatus;
auto TryGetPreparedPublicationCallbacks(
    PublicationCoordinatorCallbacks& output) noexcept -> bool;
auto FinalizePublishedSchemaSnapshot(
    const void* activeRecords,
    std::size_t activeRowCount,
    std::size_t activeRowSize,
    std::uint64_t revision,
    std::string& error) noexcept -> NativeSchemaFinalizeResult;

} // namespace ruffneckk::isc12
