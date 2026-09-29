#undef NDEBUG
#include <Windows.h>
#include "damage_body.hpp"
#include <array>
#include <cassert>
#include <cstdio>
extern "C" void DamageTestProbe();
namespace {
int calls{};
DWORD64 cookie = 0x123456789abcdef0ULL;
void Terminal(void* game, void* attacker, void* defender, void* damage, int mode, unsigned char src) {
    assert(game == reinterpret_cast<void*>(1) && attacker == reinterpret_cast<void*>(2));
    assert(defender == reinterpret_cast<void*>(3) && damage == reinterpret_cast<void*>(4));
    assert(mode == -29 && src == 192);
    ++calls;
}
void Wrapper(void* game, void* attacker, void* defender, void* damage, int mode, unsigned char src) {
    assert(mode == 0 && src == 128);
    ++calls;
    DamageBodyWrapper = reinterpret_cast<void*>(Terminal);
    DamageBodyResume(game, attacker, defender, damage, -29, 192);
}
void Unwind(void* address) {
    alignas(16) std::array<DWORD64, 180> stack{};
    stack[0x510/8]=12; stack[0x518/8]=7; stack[0x520/8]=6; stack[0x528/8]=0xabc;
    CONTEXT c{};c.Rsp=reinterpret_cast<DWORD64>(stack.data());c.Rip=reinterpret_cast<DWORD64>(address);
    DWORD64 image{},frame{};void* handler{};
    auto* f=RtlLookupFunctionEntry(c.Rip,&image,nullptr);assert(f);
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,f,&c,&handler,&frame,nullptr);
    assert(c.Rip==0xabc && c.Rsp==reinterpret_cast<DWORD64>(stack.data())+0x530);
    assert(c.R12==12 && c.Rdi==7 && c.Rsi==6);
}
}
int main() {
    DamageSecurityCookie=&cookie;
    DamageBodyOriginal=reinterpret_cast<void*>(DamageTestProbe);
    for(int i=0;i<1000;++i) {
        DamageBodyWrapper=reinterpret_cast<void*>(Wrapper);
        DamageBodyResume(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
            reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0,128);
    }
    assert(calls==2000);
    Unwind(reinterpret_cast<void*>(DamageBodyDetour));
    // MASM omits the redundant REX prefix from native PUSH RSI: prologue is 11 bytes.
    Unwind(reinterpret_cast<char*>(DamageBodyResume)+11);
    std::puts("PASS: nested damage adapters, six arguments, cookie, and inherited/resume unwind");
}
