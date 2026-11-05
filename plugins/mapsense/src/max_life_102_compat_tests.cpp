#include "max_life_102_compat.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <climits>

using RuffnecKk::MapSense::Detail::ValidateMaxLife102Hook;
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while(false)
template<class T> void Put(std::uint8_t* at, T value) { std::memcpy(at, &value, sizeof(value)); }
void Absolute(std::uint8_t* at, const void* target) {
    const std::array<std::uint8_t, 6> code{0xFF,0x25,0,0,0,0};
    std::memcpy(at, code.data(), code.size()); Put(at + 6, target);
}
void Relative(std::uint8_t* at, const std::uint8_t* target) {
    const auto delta = reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(at + 5);
    CHECK(delta >= INT32_MIN && delta <= INT32_MAX); Put(at + 1, static_cast<std::int32_t>(delta));
}
void Change(std::uint8_t* at, std::uint8_t value) {
    DWORD old{}, ignored{}; CHECK(VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &old));
    *at = value; CHECK(VirtualProtect(at, 1, old, &ignored));
}
bool Read(void*, std::uintptr_t at, std::byte* out, std::size_t count) noexcept {
    SIZE_T read{}; return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, count, &read) && read == count;
}
bool Unwind(void*, std::uintptr_t, std::uint32_t, std::size_t, std::uint32_t, bool) noexcept { return true; }
bool Executable(void*, std::uintptr_t) noexcept { return true; }
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    // Map the exact DLL without running DllMain or plugin initialization.
    auto module = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    CHECK(module != nullptr); if (!module) return 2;
    auto* dll = reinterpret_cast<std::uint8_t*>(module);
    auto* core = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x840000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!core) return 2;
    std::uint8_t* page{};
    const auto start = (reinterpret_cast<std::uintptr_t>(core) + 0x840000 + 0xFFFF) & ~std::uintptr_t{0xFFFF};
    for (std::uintptr_t at = start; at < start + 0x10000000 && !page; at += 0x10000)
        page = static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    CHECK(page != nullptr); if (!page) return 2;
    constexpr std::array<std::uint8_t,16> expected{0x48,0x83,0xEC,0x28,0xE8,0xE7,0x6D,0xBA,0xFF,0x89,0xC0,0x48,0x83,0xC4,0x28,0xC3};
    auto* entry = core + 0x831DE0;
    auto* original = page + 0x200;
    std::memcpy(entry, expected.data(), expected.size());
    std::memcpy(original, expected.data(), 9);
    Relative(original + 4, core + 0x3D8BD0); Absolute(original + 9, entry + 9);
    Absolute(page, dll + 0x1040);
    entry[0] = 0xE9; Relative(entry, page); std::memset(entry + 5, 0x90, 4);
    D2RL::PluginContext context{.contextSize=D2RL::PluginContextSize,.apiVersion=D2RL_PLUGIN_API_VERSION,.exeBase=0x140000000};
    Put(dll + 0xB600, page); Put(dll + 0xB3E8, original); Put(dll + 0xB050, entry);
    Put(dll + 0xB428, &context); dll[0xB048]=1; std::memcpy(dll + 0xB058, entry, 9);
    const auto bytes = std::as_bytes(std::span(expected));
    const auto base = reinterpret_cast<std::uintptr_t>(core);
    auto validate = [&] { return ValidateMaxLife102Hook(&context,0x2F5020,base,0x831DE0,bytes); };
    CHECK(validate() == 9);
    CHECK(ValidateMaxLife102Hook(nullptr,0x2F5020,base,0x831DE0,bytes) == 0);
    CHECK(ValidateMaxLife102Hook(&context,0x2F5021,base,0x831DE0,bytes) == 0);
    CHECK(ValidateMaxLife102Hook(&context,0x2F5020,base,0x831DE1,bytes) == 0);
    CHECK(ValidateMaxLife102Hook(&context,0x2F5020,base,0x831DE0,bytes.first(9)) == 0);
    const std::array sites{entry,entry+5,entry+8,entry+9,page,page+6,original,original+5,original+9,original+15,
        dll+0xB048,dll+0xB050,dll+0xB058,dll+0xB038,dll+0xB039,dll+0xB030,dll+0xB028,
        dll+0xB3E8,dll+0xB600,dll+0x1040,dll+0x2CE0,dll+0x3470};
    for (auto* site : sites) {
        const auto old = *site; Change(site, static_cast<std::uint8_t>(old ^ 1)); CHECK(validate() == 0); Change(site, old); CHECK(validate() == 9);
    }
    D2RL::PluginContext foreign{.contextSize=D2RL::PluginContextSize,.apiVersion=D2RL_PLUGIN_API_VERSION,.exeBase=0x150000000};
    Put(dll+0xB428,&foreign); CHECK(validate()==0); Put(dll+0xB428,&context);
    DWORD old{}, ignored{}; CHECK(VirtualProtect(page,0x1000,PAGE_READWRITE,&old)); CHECK(validate()==0);
    CHECK(VirtualProtect(page,0x1000,old,&ignored)); CHECK(validate()==9);

    // Exercise the actual DLL's callback and its relocated original path.
    // The synthetic Core helper returns -1234; only downstream Core code is stubbed.
    core[0x3D8BD0]=0xB8; Put(core+0x3D8BD1,std::int32_t{-1234}); core[0x3D8BD5]=0xC3;
    using Reader = std::int32_t(__fastcall*)(void*,std::int32_t,std::uint32_t);
    const auto read = reinterpret_cast<Reader>(entry);
    std::array<std::uint32_t,8> unit{};
    dll[0xB43A]=0;
    CHECK(read(unit.data(),7,0)==-1234);
    CHECK(read(unit.data(),8,0)==-1234);
    dll[0xB43A]=1;
    CHECK(read(unit.data(),7,0)==256);
    CHECK(read(unit.data(),7,1)==-1234);
    CHECK(read(unit.data(),8,0)==-1234);
    CHECK(validate()==9);

    // Common admission must opt in explicitly and still enforce its other witnesses.
    namespace Stat = RuffnecKk::NativeStatCompat;
    auto* main = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if (!main) return 2;
    main[0x2F5020]=0xFF; main[0x2F5021]=0x25; Put(main+0x2F5022,std::int32_t{10});
    Put(main+0x2F5030,entry); main[0x2F5026]=0xCC;
    std::array<std::uint64_t,3> descriptor{reinterpret_cast<std::uintptr_t>(main+0x2F5020),0x2F5020,reinterpret_cast<std::uintptr_t>(main+0x2F5020)};
    std::memcpy(core+0x100,descriptor.data(),sizeof(descriptor));
    const std::array functions{Stat::FunctionWitness{0x831DE0,"4883ec28e8e76dbaff89c04883c428c3",{0,""},{},{},{},8}};
    std::array<Stat::HelperWitness,7> helpers{};
    helpers[0]={"GetUnitStat",0x2F5020,"90",6,"","cc",0x100,0x831DE0,functions,{}};
    const Stat::AdmissionContract contract{"synthetic-core-with-exact-max-life-hook",helpers};
    const Stat::MemoryReader memory{nullptr,Read,Unwind,Executable};
    Stat::Adapter adapter;
    const Stat::AddressRange mainRange{reinterpret_cast<std::uintptr_t>(main),0x300000}, coreRange{base,0x840000};
    CHECK(!adapter.Bind(memory,mainRange,coreRange,contract,Stat::ToMask(Stat::Helper::GetUnitStat)));
    const Stat::TransformDiagnostics proof{&context,nullptr,nullptr,ValidateMaxLife102Hook};
    CHECK(adapter.Bind(memory,mainRange,coreRange,contract,Stat::ToMask(Stat::Helper::GetUnitStat),proof));
    CHECK(adapter.GetUnitStat(unit.data(),7,0)==256);
    CHECK(adapter.GetUnitStat(unit.data(),8,0)==-1234);
    main[0x2F5026]=0xCD;
    CHECK(!adapter.Bind(memory,mainRange,coreRange,contract,Stat::ToMask(Stat::Helper::GetUnitStat),proof));
    main[0x2F5026]=0xCC; core[0x100]^=1;
    CHECK(!adapter.Bind(memory,mainRange,coreRange,contract,Stat::ToMask(Stat::Helper::GetUnitStat),proof));
    core[0x100]^=1;
    CHECK(adapter.Bind(memory,mainRange,coreRange,contract,Stat::ToMask(Stat::Helper::GetUnitStat),proof));
    VirtualFree(main,0,MEM_RELEASE); VirtualFree(page,0,MEM_RELEASE); VirtualFree(core,0,MEM_RELEASE); FreeLibrary(module);
    std::printf("Max Life One 1.0.2 exact-hook checks: %d failures\n",failures);
    return failures ? 1 : 0;
}
