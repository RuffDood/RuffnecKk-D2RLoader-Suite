#include "callsite_witnesses.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ruffneckk::cast_triggers::callsites;
void Require(bool condition, const char* what) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int providerCalls{}, nativeCalls{}, beforeCalls{}, afterCalls{};
int Native(int a, int b, int c, int d, int e, int f, int g) {
    ++nativeCalls;
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
}
int Provider(int a, int b, int c, int d, int e, int f, int g) {
    ++providerCalls;
    return Native(a, b, c, d, e, f, g) + 19;
}
bool active{true};
int Wrapper(int a, int b, int c, int d, int e, int f, int g) {
    if (!active) return Provider(a, b, c, d, e, f, g);
    ++beforeCalls;
    const auto result = Provider(a, b, c, d, e, f, g);
    ++afterCalls;
    return result;
}
int OneArgument(int a) { return a * 7 + 3; }

int main() {
    std::array<std::uint8_t, 5> encoded{};
    Require(EncodeCall(0x1000, 0x2000, encoded), "forward rel32");
    Require(EncodeCall(0x2000, 0x1000, encoded), "backward rel32");
    Require(!EncodeCall(0x1000, 0x90000000, encoded), "reject out-of-range rel32");
    std::vector<std::uint8_t> fixture(0x600000);
    for (const auto& site : NativeSites)
        std::memcpy(fixture.data() + site.rva - 8, site.witness.data(), site.witness.size());
    Require(Validate(fixture.data(), NativeSites), "all 21 witnesses accepted");
    for (const auto& site : NativeSites) {
        fixture[site.rva + 6] ^= 1;
        Require(!Validate(fixture.data(), NativeSites), "reject changed surrounding witness");
        fixture[site.rva + 6] ^= 1;
        fixture[site.rva + 1] ^= 1;
        Require(!Validate(fixture.data(), NativeSites), "reject changed call destination");
        fixture[site.rva + 1] ^= 1;
    }
    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(code != nullptr, "allocate synthetic native caller");
    const std::array<void*, 4> wrappers{reinterpret_cast<void*>(Wrapper),
        reinterpret_cast<void*>(OneArgument), reinterpret_cast<void*>(Wrapper),
        reinterpret_cast<void*>(Wrapper)};
    auto* relays = Allocate(reinterpret_cast<std::uintptr_t>(code), wrappers);
    Require(relays != nullptr, "allocate nearby executable relays");
    const auto relay = reinterpret_cast<decltype(&Wrapper)>(relays);
    Require(relay(2, 3, 5, 7, 11, 13, 17) == 322, "seven arguments and return value preserved");
    Require(providerCalls == 1 && nativeCalls == 1 && beforeCalls == 1 && afterCalls == 1,
        "wrapper, provider, native each called exactly once");
    active = false;
    Require(relay(2, 3, 5, 7, 11, 13, 17) == 322, "inactive wrapper forwards");
    Require(providerCalls == 2 && nativeCalls == 2 && beforeCalls == 1 && afterCalls == 1,
        "partial activation does not dispatch behavior");
    // Synthetic Windows x64 caller: reserve shadow space, CALL relay, restore,
    // return. This executes the same five-byte call encoding used in the DLL.
    const std::array<std::uint8_t, 14> caller{
        0x48, 0x83, 0xEC, 0x28, 0xE8, 0, 0, 0, 0, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    std::memcpy(code, caller.data(), caller.size());
    Require(EncodeCall(reinterpret_cast<std::uintptr_t>(code + 4),
        reinterpret_cast<std::uintptr_t>(relays + 16), encoded), "encode native-to-relay call");
    std::memcpy(code + 4, encoded.data(), encoded.size());
    DWORD old{};
    Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old) != 0, "seal native caller");
    Require(FlushInstructionCache(GetCurrentProcess(), code, 14) != 0, "flush native caller");
    Require(reinterpret_cast<int(*)(int)>(code)(9) == 66, "execute patched native CALL through relay");
    VirtualFree(code, 0, MEM_RELEASE);
    VirtualFree(relays, 0, MEM_RELEASE);
    std::puts("PASS: 21 witness sites; executable rel32 forwarding; seven arguments; provider/native once; inactive fallback.");
}
