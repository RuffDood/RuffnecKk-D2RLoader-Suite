#include "bulk_currency_deposit_policy.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace ruffneckk::bulk_currency_deposit;

[[noreturn]] void FailRequirement(
        const char* expression,
        const char* file,
        int line) {
    std::cerr << file << ':' << line
        << ": requirement failed: " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

void Require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) FailRequirement(expression, file, line);
}

#define REQUIRE(expression) \
    Require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

std::string_view Slice(
        const std::string& source,
        std::string_view beginMarker,
        std::string_view endMarker) {
    const auto begin = source.find(beginMarker);
    REQUIRE(begin != std::string::npos);
    const auto end = source.find(endMarker, begin + beginMarker.size());
    REQUIRE(end != std::string::npos);
    return std::string_view(source).substr(begin, end - begin);
}

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

std::uint16_t ReadU16(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset) {
    REQUIRE(offset + sizeof(std::uint16_t) <= bytes.size());
    return static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(bytes[offset + 1]) << 8;
}

std::uint32_t ReadU32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset) {
    REQUIRE(offset + sizeof(std::uint32_t) <= bytes.size());
    return static_cast<std::uint32_t>(bytes[offset])
        | static_cast<std::uint32_t>(bytes[offset + 1]) << 8
        | static_cast<std::uint32_t>(bytes[offset + 2]) << 16
        | static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

void VerifySpriteLayout(
        const std::filesystem::path& path,
        std::uint16_t expectedFrameWidth,
        std::uint32_t expectedWidth,
        std::uint32_t expectedHeight,
        std::uint32_t expectedFrameCount) {
    const auto bytes = ReadBinaryFile(path);
    REQUIRE(bytes.size() >= 40);
    REQUIRE(std::memcmp(bytes.data(), "SPa1", 4) == 0
        || std::memcmp(bytes.data(), "SpA1", 4) == 0);
    REQUIRE(ReadU16(bytes, 6) == expectedFrameWidth);
    REQUIRE(ReadU32(bytes, 8) == expectedWidth);
    REQUIRE(ReadU32(bytes, 12) == expectedHeight);
    REQUIRE(ReadU32(bytes, 20) == expectedFrameCount);
    REQUIRE(bytes.size()
        == 40ULL + 4ULL * expectedWidth * expectedHeight);
}

std::array<std::uint64_t, 4> ReadFrameBrightness(
        const std::filesystem::path& path) {
    const auto bytes = ReadBinaryFile(path);
    REQUIRE(bytes.size() >= 40);
    REQUIRE(std::memcmp(bytes.data(), "SPa1", 4) == 0
        || std::memcmp(bytes.data(), "SpA1", 4) == 0);
    const auto frameWidth = ReadU16(bytes, 6);
    const auto width = ReadU32(bytes, 8);
    const auto height = ReadU32(bytes, 12);
    const auto frameCount = ReadU32(bytes, 20);
    REQUIRE(frameCount == 4);
    REQUIRE(width == frameWidth * frameCount);
    REQUIRE(bytes.size() == 40ULL + 4ULL * width * height);

    std::array<std::uint64_t, 4> brightness{};
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
            for (std::uint32_t x = 0; x < frameWidth; ++x) {
                const auto pixel = 40ULL
                    + 4ULL * (y * width + frame * frameWidth + x);
                const auto alpha = bytes[pixel + 3];
                brightness[frame] += alpha
                    * (static_cast<std::uint64_t>(bytes[pixel])
                        + bytes[pixel + 1]
                        + bytes[pixel + 2]);
            }
        }
    }
    return brightness;
}

void VerifyButtonFrameOrder(const std::filesystem::path& path) {
    const auto brightness = ReadFrameBrightness(path);
    REQUIRE(brightness[1] < brightness[0]);
    REQUIRE(brightness[2] > brightness[1]);
    REQUIRE(brightness[3] > brightness[0]);
}

} // namespace

int main() {
    const auto rune = PackItemCode("r01");
    const auto material = PackItemCode("wms");
    REQUIRE(rune && material);
    REQUIRE(UnpackItemCode(*rune) == "r01");
    REQUIRE(!PackItemCode(""));
    REQUIRE(!PackItemCode("abcde"));

    Config parsed{};
    std::string error;
    constexpr std::string_view valid = R"toml(
[deposit]
enabled = true
inventory_button_enabled = true
item_delay_ms = 250
include_item_codes = [
    "r01",
    "gcw",
]
exclude_item_codes = ["wms"]

[button]
x = 7
y = 900
tooltip = 'Deposit "Currency" \ Now'
)toml";
    REQUIRE(ParseToml(valid, parsed, error));
    REQUIRE(parsed.enabled);
    REQUIRE(parsed.inventoryButtonEnabled);
    REQUIRE(parsed.itemDelayMs == 250);
    REQUIRE(parsed.button.x == 7);
    REQUIRE(parsed.button.y == 900);
    REQUIRE(parsed.button.tooltip == "Deposit \"Currency\" \\ Now");
    REQUIRE(parsed.includeItemCodes.size() == 2);
    REQUIRE(parsed.excludeItemCodes.size() == 1);
    REQUIRE(MatchesItemCodeFilter(parsed, *rune));
    REQUIRE(!MatchesItemCodeFilter(parsed, *material));
    REQUIRE(!MatchesItemCodeFilter(parsed, *PackItemCode("gcv")));

    REQUIRE(!ParseToml("[deposit]\nitem_delay_ms = 49\n", parsed, error));
    REQUIRE(!ParseToml(
        "[deposit]\nhotkey = \"SHIFT+D\"\n",
        parsed,
        error));
    REQUIRE(!ParseToml("[deposit]\nunknown = true\n", parsed, error));
    REQUIRE(!ParseToml("[deposit]\n[button]\nx = 32768\n", parsed, error));
    REQUIRE(!ParseToml("[deposit]\n[button]\nunknown = 1\n", parsed, error));
    REQUIRE(!ParseToml("[deposit]\n[button]\ntooltip = \"\"\n", parsed, error));
    const auto oversizedTooltip = std::string{"[deposit]\n[button]\ntooltip = \""}
        + std::string(MaximumButtonTooltipBytes + 1, 'x') + "\"\n";
    REQUIRE(!ParseToml(oversizedTooltip, parsed, error));
    REQUIRE(!ParseToml(
        "[deposit]\ninclude_item_codes = [\"r01\", \"r01\"]\n",
        parsed,
        error));
    REQUIRE(!ParseToml("enabled = true\n", parsed, error));

    Config packaged{};
    const auto packagedConfig =
        ReadTextFile(BULK_CURRENCY_DEPOSIT_CONFIG_FILE);
    REQUIRE(ParseToml(packagedConfig, packaged, error));
    REQUIRE(packaged.enabled);
    REQUIRE(!packaged.inventoryButtonEnabled);
    REQUIRE(packaged.itemDelayMs == 100);
    REQUIRE(packaged.button.tooltip == DefaultButtonTooltip);

    std::array<std::uint8_t, 0x56> itemData{};
    itemData[ItemDataInventoryPageOffset] = MainInventoryPage;
    REQUIRE(ReadInventoryPageFromItemData(itemData.data()) == MainInventoryPage);
    REQUIRE(ReadInventoryPageFromItemData(nullptr) == InvalidInventoryPage);

    const auto paths = BuildConfigCandidates(
        std::filesystem::path("mod/config"),
        std::filesystem::path("scope/config"),
        std::filesystem::path("global/config"),
        std::filesystem::path("ruffneckk-bulk-currency-deposit.toml"));
    REQUIRE(paths.size() == 3);
    REQUIRE(paths.front().generic_string()
        == "mod/config/ruffneckk-bulk-currency-deposit.toml");

    REQUIRE(IsDepositUiMessage(
        "PanelManager", "OpenPanel", "RuffnecKkBulkCurrencyDeposit"));
    REQUIRE(!IsDepositUiMessage(
        "PanelManager", "OpenPanel", "RuffnecKkRemoteStash"));
    const auto layout = BuildButtonLayoutJson(parsed.button);
    REQUIRE(layout.find("\"x\": 7") != std::string::npos);
    REQUIRE(layout.find("\"y\": 900") != std::string::npos);
    REQUIRE(layout.find("\"hoveredFrame\": 3") != std::string::npos);
    REQUIRE(layout.find("RuffnecKkBulkCurrencyDeposit") != std::string::npos);
    REQUIRE(layout.find(
        "\"tooltipString\": \"Deposit \\\"Currency\\\" \\\\ Now\"")
        != std::string::npos);
    REQUIRE(layout.find("@RuffnecKkBulkCurrencyDepositTooltip")
        == std::string::npos);
    REQUIRE(EscapeJsonString("line\n\"quoted\"\\path\x01")
        == "line\\n\\\"quoted\\\"\\\\path\\u0001");

    REQUIRE(IsFreshRequest(120, 100, 20));
    REQUIRE(!IsFreshRequest(121, 100, 20));

    BindingCaptureSet captures;
    const auto primary = PackActionBinding('D', 1);
    const auto secondary = PackActionBinding('F', 2);
    const auto third = PackActionBinding('G', 3);
    REQUIRE(PackActionBinding(0, 1) == 0);
    REQUIRE(captures.Capture(primary));
    REQUIRE(captures.Contains(primary));
    REQUIRE(captures.Capture(primary));
    REQUIRE(captures.Capture(secondary));
    REQUIRE(captures.Contains(secondary));
    REQUIRE(!captures.Capture(third));
    REQUIRE(!captures.Release(third));
    REQUIRE(captures.Release(primary));
    REQUIRE(!captures.Contains(primary));
    REQUIRE(captures.Contains(secondary));
    REQUIRE(captures.Capture(third));
    REQUIRE(captures.Release(secondary));
    REQUIRE(captures.Release(third));
    REQUIRE(!captures.Release(primary));
    captures.Reset();
    REQUIRE(!captures.Contains(primary));
    REQUIRE(!captures.Contains(secondary));

    CallbackRundownState rundown;
    rundown.Reset();
    REQUIRE(rundown.Enter());
    REQUIRE(rundown.ActiveCount() == 1);
    REQUIRE(rundown.CanProcess());
    rundown.Stop();
    REQUIRE(!rundown.CanProcess());
    REQUIRE(!rundown.Enter());
    REQUIRE(rundown.ActiveCount() == 2);
    rundown.Leave();
    rundown.Leave();
    REQUIRE(rundown.ActiveCount() == 0);

    REQUIRE(AcceptUiStateEntry(
        UiStateEntryStatus::Unchanged, true, false, 0, {}));
    REQUIRE(!AcceptUiStateEntry(
        UiStateEntryStatus::Unchanged, false, true, 1,
        "ruffneckk-remote-stash"));
    REQUIRE(AcceptUiStateEntry(
        UiStateEntryStatus::TrackedInlineHook, false, true, 1,
        "ruffneckk-remote-stash"));
    REQUIRE(!AcceptUiStateEntry(
        UiStateEntryStatus::TrackedInlineHook, false, false, 1,
        "ruffneckk-remote-stash"));
    REQUIRE(!AcceptUiStateEntry(
        UiStateEntryStatus::TrackedInlineHook, false, true, 2,
        "ruffneckk-remote-stash"));
    REQUIRE(!AcceptUiStateEntry(
        UiStateEntryStatus::TrackedInlineHook, false, true, 1,
        "another-plugin"));
    REQUIRE(!AcceptUiStateEntry(
        UiStateEntryStatus::Other, true, true, 1,
        "ruffneckk-remote-stash"));

    const auto source = ReadTextFile(BULK_CURRENCY_DEPOSIT_SOURCE_FILE);
    REQUIRE(source.find(".logicalId = \"bulk-currency-deposit\"")
        != std::string::npos);
    REQUIRE(source.find(".displayName = \"Bulk Currency Deposit\"")
        != std::string::npos);
    REQUIRE(source.find(".version = \"1.1.1\"") != std::string::npos);
    REQUIRE(source.find(".category = \"RuffnecKk Suite\"")
        != std::string::npos);
    REQUIRE(source.find("D2RL::Input::Key::D") != std::string::npos);
    REQUIRE(source.find("D2RL::Input::Modifier::Shift")
        != std::string::npos);
    REQUIRE(source.find("ActionEventKind::Released") != std::string::npos);
    REQUIRE(source.find("ActionResult::Handled") != std::string::npos);
    REQUIRE(source.find("event->binding.key") != std::string::npos);
    REQUIRE(source.find("CapturedInputBindings.Release(binding)")
        != std::string::npos);
    REQUIRE(source.find("std::atomic<D2RL::Input::ActionHandle> DepositAction")
        != std::string::npos);
    REQUIRE(source.find("DepositAction.exchange(") != std::string::npos);
    REQUIRE(source.find("D2RL::HasContext(context)") != std::string::npos);
    REQUIRE(source.find("DiagnosticsService->queryHookStatus")
        != std::string::npos);
    REQUIRE(source.find("IsSupportedBuild") == std::string::npos);
    REQUIRE(source.find("only D2R builds") == std::string::npos);
    REQUIRE(source.find("D2RL::GetBuildName") != std::string::npos);
    REQUIRE(source.find("D2RL::GetBuildVersion") != std::string::npos);
    REQUIRE(source.find("ButtonLocalizationVirtualPath") == std::string::npos);
    REQUIRE(source.find("ButtonLocalizationResource") == std::string::npos);
    REQUIRE(source.find("strings.json") == std::string::npos);
    REQUIRE(source.find("65101") == std::string::npos);
    REQUIRE(source.find("ValidateNativeFingerprint")
        != std::string::npos);
    REQUIRE(source.find("native fingerprint accepted")
        != std::string::npos);
    REQUIRE(source.find("WH_KEYBOARD_LL") == std::string::npos);
    REQUIRE(source.find("WH_MOUSE_LL") == std::string::npos);
    REQUIRE(source.find("GetAsyncKeyState") == std::string::npos);
    REQUIRE(source.find("D2RL::ThreadServiceV1") != std::string::npos);
    REQUIRE(source.find("ThreadService->runOnUiThread")
        != std::string::npos);
    REQUIRE(source.find("ThreadService->runOnGameThread")
        == std::string::npos);
    REQUIRE(source.find("WH_GETMESSAGE") == std::string::npos);
    REQUIRE(source.find("SetWindowsHookEx") == std::string::npos);
    REQUIRE(source.find("UnhookWindowsHookEx") == std::string::npos);
    REQUIRE(source.find("FindGameWindow") == std::string::npos);
    REQUIRE(source.find("PostThreadMessage") == std::string::npos);

    const auto callbackQueue = Slice(
        source,
        "bool QueueDepositRequest(bool controlsSource)",
        "auto __cdecl OnDepositUiMessage");
    REQUIRE(callbackQueue.find("ThreadService") == std::string_view::npos);
    REQUIRE(callbackQueue.find("ProcessDepositRequest")
        == std::string_view::npos);
    REQUIRE(callbackQueue.find("ProcessNextItem") == std::string_view::npos);

    const auto buttonCallback = Slice(
        source,
        "auto __cdecl OnDepositUiMessage",
        "bool UnregisterButtonListener");
    REQUIRE(buttonCallback.find("CallbackGuard") != std::string_view::npos);
    REQUIRE(buttonCallback.find("QueueDepositRequest(false)")
        != std::string_view::npos);
    REQUIRE(buttonCallback.find("ProcessDepositRequest")
        == std::string_view::npos);
    REQUIRE(buttonCallback.find("ProcessNextItem")
        == std::string_view::npos);

    const auto buttonServices = Slice(
        source,
        "bool QueryButtonServices()",
        "bool LoadEmbeddedResource");
    REQUIRE(buttonServices.find(
        "if (!Settings.inventoryButtonEnabled) return true;")
        == std::string_view::npos);
    REQUIRE(buttonServices.find("D2RL::ServiceId::SharedEvent")
        != std::string_view::npos);
    REQUIRE(buttonServices.find("D2RL::ServiceId::Resource")
        != std::string_view::npos);
    REQUIRE(buttonServices.find("Settings.inventoryButtonEnabled")
        < buttonServices.find("D2RL::ServiceId::Panel"));

    const auto ownedButton = Slice(
        source,
        "bool RegisterOwnedButton()",
        "void ResetCountersAndBatch()");
    REQUIRE(ownedButton.find("DepositButtonVirtualPath")
        < ownedButton.find("if (!Settings.inventoryButtonEnabled) return true;"));
    REQUIRE(ownedButton.find("ButtonLocalizationVirtualPath")
        == std::string_view::npos);
    REQUIRE(ownedButton.find("if (!Settings.inventoryButtonEnabled) return true;")
        < ownedButton.find("ButtonLayoutVirtualPath"));
    REQUIRE(ownedButton.find("ButtonLayoutVirtualPath")
        < ownedButton.find("registerChildLayout"));

    const auto inputCallback = Slice(
        source,
        "D2RL::Input::ActionResult __cdecl OnControlsAction",
        "bool RegisterControlsAction");
    REQUIRE(inputCallback.find("CallbackGuard") != std::string_view::npos);
    REQUIRE(inputCallback.find("QueueDepositRequest(true)")
        != std::string_view::npos);
    REQUIRE(inputCallback.find("ProcessDepositRequest")
        == std::string_view::npos);
    REQUIRE(inputCallback.find("ProcessNextItem")
        == std::string_view::npos);

    const auto workerDispatch = Slice(
        source,
        "void DispatchPendingControlsRequest()",
        "void DispatchDueBatchStep()");
    REQUIRE(workerDispatch.find("ThreadService->runOnUiThread")
        != std::string_view::npos);
    REQUIRE(workerDispatch.find("ProcessInitialRequestOnUiThread")
        != std::string_view::npos);

    const auto initialUiCallback = Slice(
        source,
        "void __cdecl ProcessInitialRequestOnUiThread",
        "void __cdecl ProcessNextItemOnUiThread");
    REQUIRE(initialUiCallback.find("CallbackGuard")
        != std::string_view::npos);
    REQUIRE(initialUiCallback.find("ProcessInitialRequest()")
        != std::string_view::npos);
    const auto stepUiCallback = Slice(
        source,
        "void __cdecl ProcessNextItemOnUiThread",
        "D2RL::Input::ActionResult __cdecl OnControlsAction");
    REQUIRE(stepUiCallback.find("CallbackGuard")
        != std::string_view::npos);
    REQUIRE(stepUiCallback.find("ProcessNextItem()")
        != std::string_view::npos);

    const auto worker = Slice(
        source,
        "DWORD WINAPI InputThreadProc",
        "bool StartInput");
    REQUIRE(worker.find("WaitForSingleObject") != std::string_view::npos);
    REQUIRE(worker.find("InputStopEvent") != std::string_view::npos);
    REQUIRE(worker.find("FreeLibraryAndExitThread(module, 0)")
        != std::string_view::npos);

    const auto unregisterInput = Slice(
        source,
        "void UnregisterControlsAction()",
        "void DispatchPendingControlsRequest()");
    REQUIRE(unregisterInput.find("unregisterAction")
        < unregisterInput.find("CapturedInputBindings.Reset()"));

    const auto load = Slice(
        source,
        "D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin",
        "D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin");
    REQUIRE(load.find("RegisterButtonListener()")
        < load.find("RegisterOwnedButton()"));
    REQUIRE(load.find(
        "Settings.inventoryButtonEnabled\n            && (!RegisterButtonListener()")
        == std::string_view::npos);
    REQUIRE(load.find("QueryThreadService()") != std::string_view::npos);
    const auto unload = Slice(
        source,
        "D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin",
        "ResetBatchState();");
    REQUIRE(unload.find("UnregisterOwnedButton()")
        < unload.find("UnregisterButtonListener()"));
    REQUIRE(unload.find("UnregisterButtonListener()")
        < unload.find("StopInput()"));
    REQUIRE(unload.find("CallbackRundown.Stop()")
        < unload.find("UnregisterControlsAction()"));
    REQUIRE(unload.find("StopInput()")
        < unload.find("WaitForCallbackRundown()"));
    REQUIRE(unload.find("AcquireTeardownModuleReference()")
        < unload.find("CallbackRundown.Stop()"));
    REQUIRE(unload.find("if (!WaitForCallbackRundown()) return;")
        != std::string_view::npos);

    const auto statusCallback = Slice(
        source,
        "auto Status(",
        "} // namespace");
    REQUIRE(statusCallback.find("CallbackGuard")
        != std::string_view::npos);

    VerifySpriteLayout(
        BULK_CURRENCY_DEPOSIT_BUTTON_MOLD_FILE,
        54,
        54,
        141,
        1);
    VerifySpriteLayout(
        BULK_CURRENCY_DEPOSIT_BUTTON_MOLD_LOWEND_FILE,
        27,
        27,
        71,
        1);
    VerifyButtonFrameOrder(BULK_CURRENCY_DEPOSIT_BUTTON_FILE);
    VerifyButtonFrameOrder(BULK_CURRENCY_DEPOSIT_BUTTON_LOWEND_FILE);
    std::cout << "Bulk Currency Deposit policy tests passed.\n";
}
