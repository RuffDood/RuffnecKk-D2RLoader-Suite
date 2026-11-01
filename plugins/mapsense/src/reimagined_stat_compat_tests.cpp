#include "reimagined_stat_compat.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace RuffnecKk::MapSense::Detail;
namespace Stat = RuffnecKk::NativeStatCompat;
namespace {
int Failures{}, Calls{};
std::uint32_t SeenLayer{};
std::int32_t SeenStat{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++Failures; } } while(false)
std::int64_t __fastcall CoreRead(void*, std::int32_t stat, std::uint32_t layer) noexcept {
    ++Calls; SeenLayer = layer; SeenStat = stat; return -1234;
}
bool Read(void*, std::uintptr_t at, std::byte* out, std::size_t size) noexcept {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, size, &count)
        && count == size;
}
void Jump(std::uint8_t* at, void* target) {
    const std::array<std::uint8_t,6> prefix{0xFF,0x25,0,0,0,0};
    std::memcpy(at, prefix.data(), prefix.size()); std::memcpy(at+6, &target, sizeof(target));
}
void ChangeCode(void* at, std::uint8_t value) {
    DWORD before{}, ignored{}; CHECK(VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &before));
    *static_cast<std::uint8_t*>(at) = value;
    CHECK(VirtualProtect(at, 1, before, &ignored)); FlushInstructionCache(GetCurrentProcess(), at, 1);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    // Reference DLL is mapped without imports, DllMain or plugin initialization.
    const auto module = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    CHECK(module != nullptr); if (!module) return 2;
    auto* image = reinterpret_cast<std::uint8_t*>(module);
    auto* main = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x300000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    auto* core = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!main || !core) return 2;
    const auto mainBase = reinterpret_cast<std::uintptr_t>(main);
    const auto coreTarget = reinterpret_cast<std::uintptr_t>(core + 0x100);
    auto* entry = main + NativeUnitStatRva;
    const auto slotAddress = reinterpret_cast<std::uintptr_t>(entry + 6);
    const auto reader = reinterpret_cast<std::uintptr_t>(image + MapsStatReaderRva);
    Jump(entry, reinterpret_cast<void*>(reader)); entry[14] = 0xCC;
    Jump(core + 0x100, reinterpret_cast<void*>(&CoreRead));
    std::array<std::uintptr_t, 3> descriptor{mainBase + NativeUnitStatRva,
        NativeUnitStatRva, mainBase + NativeUnitStatRva};
    std::memcpy(core + 0x200, descriptor.data(), sizeof(descriptor));
    D2RL::PluginContext context{.contextSize = D2RL::PluginContextSize,
        .apiVersion = D2RL_PLUGIN_API_VERSION, .exeBase = mainBase};
    constexpr std::array stateOffsets{MapsOriginalStatSlotRva, MapsRedirectSlotAddressRva,
        MapsRedirectPreviousValueRva, MapsContextSlotRva, MapsNativeStatRvaSlotRva};
    std::array<std::uintptr_t, stateOffsets.size()> before{};
    for (std::size_t i=0;i<stateOffsets.size();++i)
        std::memcpy(&before[i], image + stateOffsets[i], sizeof(std::uintptr_t));
    const auto oldActive = image[MapsStatHookActiveRva];
    auto set = [&](std::uintptr_t offset, std::uintptr_t value) {
        std::memcpy(image + offset, &value, sizeof(value));
    };
    set(MapsOriginalStatSlotRva, coreTarget);
    set(MapsRedirectSlotAddressRva, slotAddress);
    set(MapsRedirectPreviousValueRva, coreTarget);
    set(MapsContextSlotRva, reinterpret_cast<std::uintptr_t>(&context));
    set(MapsNativeStatRvaSlotRva, NativeUnitStatRva); image[MapsStatHookActiveRva] = 1;
    auto validate = [&] { return ValidateReimaginedStatRedirect(nullptr, mainBase,
        NativeUnitStatRva, slotAddress, reader, coreTarget); };
    CHECK(validate());

    // Exercise the real adapter -> native slot -> published Maps callback -> Core
    // path. The small fixture contract isolates forwarding; production retains
    // its complete Core graph/unwind/descriptor/import witnesses.
    std::array<Stat::HelperWitness,7> helpers{};
    helpers[0] = {"GetUnitStat", NativeUnitStatRva, "AABB", 14, "", "CC",
        0x200, 0x100, {}, {}};
    const Stat::AdmissionContract contract{"test-forwarding", helpers};
    const Stat::MemoryReader memory{nullptr, Read, nullptr, nullptr};
    const Stat::AddressRange mainRange{mainBase, 0x300000};
    const Stat::AddressRange coreRange{reinterpret_cast<std::uintptr_t>(core), 0x1000};
    const auto mask = Stat::ToMask(Stat::Helper::GetUnitStat);
    Stat::Adapter rejected{};
    CHECK(!rejected.Bind(memory, mainRange, coreRange, contract, mask));
    Stat::TransformDiagnostics diagnostics{nullptr, nullptr, ValidateReimaginedStatRedirect};
    Stat::Adapter accepted{};
    CHECK(accepted.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
    CHECK(accepted.GetUnitStat(nullptr, 1, 0xABCD) == -1234);
    CHECK(Calls == 1 && SeenLayer == 0xABCD && SeenStat == 1);
    std::uintptr_t liveTarget{}; std::memcpy(&liveTarget, entry + 6, sizeof(liveTarget));
    CHECK(liveTarget == reader); // adapter never bypasses or replaces Maps

    descriptor[1] ^= 1; std::memcpy(core + 0x200, descriptor.data(), sizeof(descriptor));
    Stat::Adapter badDescriptor{};
    CHECK(!badDescriptor.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
    descriptor[1] ^= 1; std::memcpy(core + 0x200, descriptor.data(), sizeof(descriptor));
    entry[14] ^= 1; Stat::Adapter badTail{};
    CHECK(!badTail.Bind(memory, mainRange, coreRange, contract, mask, diagnostics)); entry[14] ^= 1;
    CHECK(!ValidateReimaginedStatRedirect(nullptr, mainBase, 0x2F4190, slotAddress, reader, coreTarget));
    CHECK(!ValidateReimaginedStatRedirect(nullptr, mainBase, NativeUnitStatRva, slotAddress+8, reader, coreTarget));
    CHECK(!ValidateReimaginedStatRedirect(nullptr, mainBase, NativeUnitStatRva, slotAddress, reader+1, coreTarget));
    CHECK(!ValidateReimaginedStatRedirect(nullptr, mainBase, NativeUnitStatRva, slotAddress, reader, coreTarget+1));
    image[MapsStatHookActiveRva] = 0; CHECK(!validate()); image[MapsStatHookActiveRva] = 1;
    set(MapsOriginalStatSlotRva, reader); CHECK(!validate()); set(MapsOriginalStatSlotRva, coreTarget);
    set(MapsRedirectPreviousValueRva, reader); CHECK(!validate()); set(MapsRedirectPreviousValueRva, coreTarget);
    set(MapsRedirectSlotAddressRva, slotAddress+8); CHECK(!validate()); set(MapsRedirectSlotAddressRva, slotAddress);
    set(MapsContextSlotRva, 0); CHECK(!validate()); set(MapsContextSlotRva, reinterpret_cast<std::uintptr_t>(&context));
    context.exeBase += 1; CHECK(!validate()); context.exeBase -= 1;
    set(MapsNativeStatRvaSlotRva, NativeUnitStatRva+1); CHECK(!validate()); set(MapsNativeStatRvaSlotRva, NativeUnitStatRva);
    const auto lastReaderByte = image[MapsStatReaderRva + 690];
    ChangeCode(image + MapsStatReaderRva + 690, lastReaderByte ^ 1U); CHECK(!validate());
    ChangeCode(image + MapsStatReaderRva + 690, lastReaderByte);
    const auto ownerByte = image[MapsStatOwnerRva];
    ChangeCode(image + MapsStatOwnerRva, ownerByte ^ 1U); CHECK(!validate());
    ChangeCode(image + MapsStatOwnerRva, ownerByte); CHECK(validate());
    for (std::size_t i=0;i<stateOffsets.size();++i) set(stateOffsets[i], before[i]);
    image[MapsStatHookActiveRva] = oldActive;
    VirtualFree(main,0,MEM_RELEASE); VirtualFree(core,0,MEM_RELEASE); FreeLibrary(module);
    std::printf("Reimagined stat redirect: %s\n", Failures ? "FAILED" : "PASS");
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
