#undef NDEBUG
#include <Windows.h>
#include "caster_body.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
int calls{};
int TargetEnd(void* caster, int skill, int level, void* target, int flag) {
    assert(caster == reinterpret_cast<void*>(0x123456789ULL));
    assert(target == reinterpret_cast<void*>(0x987654321ULL));
    assert(skill == 48 && level == 20 && flag == -17);
    ++calls;
    return -901;
}
int TargetWrapper(void* caster, int skill, int level, void* target, int flag) {
    assert(level == 63 && flag == 1);
    ++calls;
    CastTargetWrapper = reinterpret_cast<void*>(TargetEnd);
    return CastTargetResume(caster, skill, 20, target, -17);
}
int PositionEnd(void* caster, int skill, int level, int x, int y, int flag) {
    assert(caster == reinterpret_cast<void*>(0x123456789ULL));
    assert(skill == 48 && level == 2 && x == -300 && y == 70000 && flag == -29);
    ++calls;
    return 717;
}
int PositionWrapper(void* caster, int skill, int level, int x, int y, int flag) {
    assert(level == 63 && x == -300 && y == 70000 && flag == 1);
    ++calls;
    CastPositionWrapper = reinterpret_cast<void*>(PositionEnd);
    return CastPositionResume(caster, skill, 2, x, y, -29);
}

// Check the assembly unwind records independently of successful call/return.
void CheckUnwind(void* function, size_t offset, bool position) {
    alignas(16) std::array<DWORD64, 40> stack{};
    const size_t frame = position ? 0x98 : 0x88;
    stack[0x70 / 8] = 15;
    stack[0x78 / 8] = 14;
    if (position) {
        stack[0x80 / 8] = 12;
        stack[0x88 / 8] = 7;
        stack[0x90 / 8] = 6;
    } else {
        stack[0x80 / 8] = 7;
        stack[(frame + 0x20) / 8] = 6;
    }
    stack[frame / 8] = 0x123456;
    stack[(frame + 0x10) / 8] = 3;
    stack[(frame + 0x18) / 8] = 5;
    CONTEXT context{};
    context.Rsp = reinterpret_cast<DWORD64>(stack.data());
    context.Rip = reinterpret_cast<DWORD64>(function) + offset;
    DWORD64 imageBase{};
    auto* entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
    assert(entry);
    void* handler{};
    DWORD64 establisher{};
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
        &context, &handler, &establisher, nullptr);
    assert(context.Rip == 0x123456);
    assert(context.Rsp == reinterpret_cast<DWORD64>(stack.data()) + frame + 8);
    assert(context.Rbx == 3 && context.Rbp == 5 && context.Rsi == 6);
    assert(context.Rdi == 7 && context.R14 == 14 && context.R15 == 15);
    if (position) assert(context.R12 == 12);
}
}
int main() {
    constexpr unsigned char targetPrologue[]{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
        0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x70};
    constexpr unsigned char positionPrologue[]{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
        0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x70};
    assert(std::memcmp(reinterpret_cast<void*>(CastTargetResume),
        targetPrologue, sizeof targetPrologue) == 0);
    assert(std::memcmp(reinterpret_cast<void*>(CastPositionResume),
        positionPrologue, sizeof positionPrologue) == 0);
    CastTargetBodyOriginal = reinterpret_cast<void*>(CastTargetBodyDetour);
    CastPositionBodyOriginal = reinterpret_cast<void*>(CastPositionBodyDetour);
    for (int i = 0; i < 1000; ++i) {
        CastTargetWrapper = reinterpret_cast<void*>(TargetWrapper);
        assert(CastTargetResume(reinterpret_cast<void*>(0x123456789ULL), 48, 63,
            reinterpret_cast<void*>(0x987654321ULL), 1) == -901);
        CastPositionWrapper = reinterpret_cast<void*>(PositionWrapper);
        assert(CastPositionResume(reinterpret_cast<void*>(0x123456789ULL),
            48, 63, -300, 70000, 1) == 717);
    }
    assert(calls == 4000);
    CheckUnwind(reinterpret_cast<void*>(CastTargetBodyDetour), 0, false);
    CheckUnwind(reinterpret_cast<void*>(CastPositionBodyDetour), 0, true);
    CheckUnwind(reinterpret_cast<void*>(CastTargetResume), 24, false);
    CheckUnwind(reinterpret_cast<void*>(CastPositionResume), 22, true);
    std::puts("PASS: 2000 nested adapter calls, stack arguments, results, and four unwind records");
}
