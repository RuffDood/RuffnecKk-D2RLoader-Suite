#include "maps0127_r17_reference.hpp"
#include "maps0125_r16_reference.hpp"
#include "maps0125_r15_reference.hpp"
#include <algorithm>
#include "maps0125_revision2_reference.hpp"
#include "maps0125_reference.hpp"
#include <RuffnecKk/maps_stat_compat.hpp>
#include <RuffnecKk/maps_stat_compat.hpp>
#include "maps_stat_witness.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>

using namespace RuffnecKk::MapsStatCompat;
namespace Stat = RuffnecKk::NativeStatCompat;
namespace {
int Failures{}, Calls{}, Writes{};
std::uint32_t SeenLayer{};
std::int32_t SeenStat{};
std::uint32_t WrittenLayer{};
std::int32_t WrittenStat{}, WrittenValue{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++Failures; } } while(false)
std::int64_t __fastcall CoreRead(void*, std::int32_t stat, std::uint32_t layer) noexcept {
    ++Calls; SeenLayer = layer; SeenStat = stat; return -1234;
}
std::int32_t __fastcall CoreWrite(void*, std::int32_t stat, std::int32_t value, std::uint32_t layer) noexcept {
    ++Writes; WrittenLayer = layer; WrittenStat = stat; WrittenValue = value;
    return 0;
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
    if (argc != 4) return 2;
    const MapsStatProfile* selected{};
    for (const auto& candidate : MapsStatProfiles) {
        if ((std::wcscmp(argv[2], L"0.12.1") == 0 && std::strcmp(candidate.version, "0.12.1") == 0)
            || (std::wcscmp(argv[2], L"0.12.4") == 0 && std::strcmp(candidate.version, "0.12.4") == 0)) selected = &candidate;
    }
    // The fixture knows the new exact reference before production admits it.
    if (std::wcscmp(argv[2], L"0.12.5") == 0) selected = &Maps0125Profile;
    if (std::wcscmp(argv[2], L"0.12.5-revision2") == 0) selected = &Maps0125Revision2Profile;
    if (std::wcscmp(argv[2], L"0.12.5-r15") == 0) selected = &Maps0125R15Profile;
    if (std::wcscmp(argv[2], L"0.12.7-r17") == 0) selected = &Maps0127R17Profile;
    if (std::wcscmp(argv[2], L"0.12.5-r16") == 0) selected = &Maps0125R16Profile;
    if (!selected) return 2;
    const auto& profile = *selected;
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
    const auto reader = reinterpret_cast<std::uintptr_t>(image + profile.readerRva);
    Jump(entry, reinterpret_cast<void*>(reader)); entry[14] = 0xCC;
    Jump(core + 0x100, reinterpret_cast<void*>(&CoreRead));
    std::array<std::uintptr_t, 3> descriptor{mainBase + NativeUnitStatRva,
        NativeUnitStatRva, mainBase + NativeUnitStatRva};
    std::memcpy(core + 0x200, descriptor.data(), sizeof(descriptor));
    D2RL::PluginContext context{.contextSize = D2RL::PluginContextSize,
        .apiVersion = D2RL_PLUGIN_API_VERSION, .exeBase = mainBase};
    const std::array stateOffsets{profile.originalStatSlotRva, profile.redirectSlotAddressRva,
        profile.redirectPreviousValueRva, profile.contextSlotRva, profile.nativeStatRvaSlotRva};
    std::array<std::uintptr_t, 5> before{};
    for (std::size_t i=0;i<stateOffsets.size();++i)
        std::memcpy(&before[i], image + stateOffsets[i], sizeof(std::uintptr_t));
    const auto oldActive = image[profile.statHookActiveRva];
    auto set = [&](std::uintptr_t offset, std::uintptr_t value) {
        std::memcpy(image + offset, &value, sizeof(value));
    };
    set(profile.originalStatSlotRva, coreTarget);
    set(profile.redirectSlotAddressRva, slotAddress);
    set(profile.redirectPreviousValueRva, coreTarget);
    set(profile.contextSlotRva, reinterpret_cast<std::uintptr_t>(&context));
    set(profile.nativeStatRvaSlotRva, NativeUnitStatRva); image[profile.statHookActiveRva] = 1;
    auto validate = [&] { return ValidateReimaginedStatRedirect(nullptr, mainBase,
        NativeUnitStatRva, slotAddress, reader, coreTarget); };
    CHECK(validate());
    if (Failures) return EXIT_FAILURE;

    // Exercise the real adapter -> native slot -> published Maps callback -> Core
    // path. The small fixture contract isolates forwarding; production retains
    // its complete Core graph/unwind/descriptor/import witnesses.
    std::array<Stat::HelperWitness,7> helpers{};
    helpers[0] = {"GetUnitStat", NativeUnitStatRva, "AABB", 14, "", "CC",
        0x200, 0x100, {}, {}};
    constexpr std::uint32_t NativeSetStatRva = 0x2F7D10;
    auto* writerEntry = main + NativeSetStatRva;
    Jump(writerEntry, core + 0x300); writerEntry[14] = 0xCC;
    Jump(core + 0x300, reinterpret_cast<void*>(&CoreWrite));
    std::array<std::uintptr_t, 3> writerDescriptor{mainBase + NativeSetStatRva,
        NativeSetStatRva, mainBase + NativeSetStatRva};
    std::memcpy(core + 0x400, writerDescriptor.data(), sizeof(writerDescriptor));
    helpers[5] = {"SetUnitStat", NativeSetStatRva, "AABB", 14, "", "CC",
        0x400, 0x300, {}, {}};

    const Stat::MemoryReader memory{nullptr, Read, nullptr, nullptr};
    const Stat::AddressRange mainRange{mainBase, 0x300000};
    const Stat::AddressRange coreRange{reinterpret_cast<std::uintptr_t>(core), 0x1000};
    constexpr std::uint32_t NativeBaseStatRva = 0x2F48C0;
    auto* baseEntry = main + NativeBaseStatRva;
    Jump(baseEntry, core + 0x500); baseEntry[14] = 0xCC;
    Jump(core + 0x500, reinterpret_cast<void*>(&CoreRead));
    std::array<std::uintptr_t, 3> baseDescriptor{mainBase + NativeBaseStatRva,
        NativeBaseStatRva, mainBase + NativeBaseStatRva};
    std::memcpy(core + 0x600, baseDescriptor.data(), sizeof(baseDescriptor));
    helpers[4] = {"GetUnitBaseStat", NativeBaseStatRva, "AABB", 14, "", "CC", 0x600, 0x500, {}, {}};
    auto mask = Stat::ToMask(Stat::Helper::GetUnitStat);
    if (std::wcscmp(argv[3], L"getter-setter") == 0) mask |= Stat::ToMask(Stat::Helper::SetUnitStat);
    if (std::wcscmp(argv[3], L"getter-base") == 0) mask |= Stat::ToMask(Stat::Helper::GetUnitBaseStat);
    const Stat::AdmissionContract contract{"test-forwarding", helpers};
    Stat::Adapter writerOnly{};
    CHECK(writerOnly.Bind(memory, mainRange, coreRange, contract, Stat::ToMask(Stat::Helper::SetUnitStat)));
    Stat::Adapter rejected{};
    CHECK(!rejected.Bind(memory, mainRange, coreRange, contract, mask));
    auto diagnostics = Diagnostics(nullptr);
    Stat::Adapter accepted{};
    CHECK(accepted.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
    CHECK(accepted.GetUnitStat(nullptr, 1, 0xABCD) == -1234);
    CHECK(Calls == 1 && SeenLayer == 0xABCD && SeenStat == 1);
    if ((mask & Stat::ToMask(Stat::Helper::SetUnitStat)) != 0) {
        accepted.SetUnitStatWide(nullptr, 6, -2345, 0x1234ABCD);
        CHECK(Writes == 1 && WrittenLayer == 0x1234ABCD && WrittenStat == 6 && WrittenValue == -2345);
    }
    if ((mask & Stat::ToMask(Stat::Helper::GetUnitBaseStat)) != 0) {
        CHECK(accepted.GetUnitBaseStat(nullptr, 7, 0xFEDC) == -1234);
        CHECK(Calls == 2 && SeenLayer == 0xFEDC && SeenStat == 7);
        baseEntry[14] ^= 1; Stat::Adapter badBase{};
        CHECK(!badBase.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
        baseEntry[14] ^= 1; baseDescriptor[1] ^= 1;
        std::memcpy(core + 0x600, baseDescriptor.data(), sizeof(baseDescriptor));
        CHECK(!badBase.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
        baseDescriptor[1] ^= 1;
        std::memcpy(core + 0x600, baseDescriptor.data(), sizeof(baseDescriptor));
    }
    // The getter exception cannot admit a damaged independent setter route.
    if ((mask & Stat::ToMask(Stat::Helper::SetUnitStat)) != 0) {
    writerEntry[14] ^= 1;
    Stat::Adapter damagedWriter{};
    CHECK(!damagedWriter.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
    writerEntry[14] ^= 1;
    writerDescriptor[1] ^= 1;
    std::memcpy(core + 0x400, writerDescriptor.data(), sizeof(writerDescriptor));
    CHECK(!damagedWriter.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
    writerDescriptor[1] ^= 1;
    std::memcpy(core + 0x400, writerDescriptor.data(), sizeof(writerDescriptor));
    }
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
    image[profile.statHookActiveRva] = 0; CHECK(!validate()); image[profile.statHookActiveRva] = 1;
    set(profile.originalStatSlotRva, reader); CHECK(!validate()); set(profile.originalStatSlotRva, coreTarget);
    set(profile.redirectPreviousValueRva, reader); CHECK(!validate()); set(profile.redirectPreviousValueRva, coreTarget);
    set(profile.redirectSlotAddressRva, slotAddress+8); CHECK(!validate()); set(profile.redirectSlotAddressRva, slotAddress);
    set(profile.contextSlotRva, 0); CHECK(!validate()); set(profile.contextSlotRva, reinterpret_cast<std::uintptr_t>(&context));
    context.exeBase += 1; CHECK(!validate()); context.exeBase -= 1;
    set(profile.nativeStatRvaSlotRva, NativeUnitStatRva+1); CHECK(!validate()); set(profile.nativeStatRvaSlotRva, NativeUnitStatRva);
    const auto lastReaderByte = image[profile.readerRva + 690];
    ChangeCode(image + profile.readerRva + 690, lastReaderByte ^ 1U); CHECK(!validate());
    ChangeCode(image + profile.readerRva + 690, lastReaderByte);
    const auto ownerByte = image[profile.ownerRva];
    ChangeCode(image + profile.ownerRva, ownerByte ^ 1U); CHECK(!validate());
    ChangeCode(image + profile.ownerRva, ownerByte); CHECK(validate());
    // Same-address revisions must never mix reader and owner witnesses.
    for (const auto& other : MapsStatProfiles) {
        if (&other == &profile || other.readerRva != profile.readerRva
            || other.ownerRva != profile.ownerRva
            || std::equal(other.ownerWitness.begin(), other.ownerWitness.end(), profile.ownerWitness.begin())) continue;
        for (std::size_t i = 0; i < other.ownerWitness.size(); ++i)
            ChangeCode(image + profile.ownerRva + i, other.ownerWitness[i]);
        CHECK(!validate());
        for (std::size_t i = 0; i < profile.ownerWitness.size(); ++i)
            ChangeCode(image + profile.ownerRva + i, profile.ownerWitness[i]);
        CHECK(validate());
        for (std::size_t i = 0; i < other.readerWitness.size(); ++i)
            ChangeCode(image + profile.readerRva + i, other.readerWitness[i]);
        CHECK(!validate());
        for (std::size_t i = 0; i < profile.readerWitness.size(); ++i)
            ChangeCode(image + profile.readerRva + i, profile.readerWitness[i]);
        CHECK(validate());
    }
    if (profile.readerRva == 0x17E50) {
        const auto callByteRva = profile.readerRva + 0xEB;
        const auto originalCallByte = profile.readerWitness[0xEB];
        ChangeCode(image + callByteRva, originalCallByte ^ 0x01U); CHECK(!validate());
        ChangeCode(image + callByteRva, originalCallByte); CHECK(validate());
    }
    const auto readerTailRva = profile.readerRva + profile.readerWitness.size() - 1;
    ChangeCode(image + readerTailRva, profile.readerWitness.back() ^ 1U); CHECK(!validate());
    ChangeCode(image + readerTailRva, profile.readerWitness.back());
    const auto ownerTailRva = profile.ownerRva + profile.ownerWitness.size() - 1;
    ChangeCode(image + ownerTailRva, profile.ownerWitness.back() ^ 1U); CHECK(!validate());
    ChangeCode(image + ownerTailRva, profile.ownerWitness.back()); CHECK(validate());
    Jump(entry, reinterpret_cast<void*>(coreTarget));
    Stat::Adapter coreOnly{};
    CHECK(coreOnly.Bind(memory, mainRange, coreRange, contract, mask, diagnostics));
    CHECK(coreOnly.GetUnitStat(nullptr, 9, 0x1234) == -1234);
    CHECK(SeenStat == 9 && SeenLayer == 0x1234);
    for (std::size_t i=0;i<stateOffsets.size();++i) set(stateOffsets[i], before[i]);
    image[profile.statHookActiveRva] = oldActive;
    VirtualFree(main,0,MEM_RELEASE); VirtualFree(core,0,MEM_RELEASE); FreeLibrary(module);
    std::printf("Reimagined stat redirect: %s\n", Failures ? "FAILED" : "PASS");
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
