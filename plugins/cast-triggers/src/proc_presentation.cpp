// Adapted from CelestialRayOne Cast on Cast, commit c970413adc968634585dac86e64a4dab3532f997.
// Copyright (c) 2026 Bogdan Bulai. MIT; see ../THIRD-PARTY-NOTICES.md.
// Original mechanics credited upstream to ESR's Diablo II 2.4 patch set.
// Cast-event dispatch is deliberately absent: Cast Triggers owns it once.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "proc_presentation.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
namespace RuffnecKk::CastTriggers::ProcPresentation {
namespace {
constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitPathOffset = 0x38;
constexpr std::uint32_t PlayerUnitType = 0;
constexpr std::uint32_t MonsterUnitType = 1;
constexpr std::uint32_t MissileUnitType = 3;

// DynamicPath: precision X/Y dwords at +0x00/+0x04 (PATH_GetX/GetY return
// their high words), first point words at +0x10/+0x12 and target unit at +0x70
// (the set-target-point and get-target-unit accessors), per-tick step at
// +0x96/+0x9A (sub_140380FD0 stores (direction * velocity) >> 8 there, with
// velocity at +0xA0). Identical to 2.4.
constexpr std::size_t PathPrecisionXOffset = 0x00;
constexpr std::size_t PathPrecisionYOffset = 0x04;
constexpr std::size_t PathFirstPointXOffset = 0x10;
constexpr std::size_t PathFirstPointYOffset = 0x12;
constexpr std::size_t PathStepXOffset = 0x96;
constexpr std::size_t PathStepYOffset = 0x9A;

// Item-effect caster sub_140589930 (2.4 PROC_FireSkill): the 12th argument is
// loaded from [rsp+0xF8] and stored as argument 7 (itemEffect) of the call to
// the do-handler at 589C93.
constexpr std::uint64_t ItemEffectAimRva = 0x589C6F;
constexpr auto ItemEffectAimWitness = std::to_array<std::uint8_t>({
    0x8B,0x84,0x24,0xF8,0x00,0x00,0x00,0x44,0x8B,0xCE,0x89,0x44,0x24,0x30,0x44,0x8B,
    0xC5,0xC7,0x44,0x24,0x28,0x01,0x00,0x00,0x00,0x48,0x8B,0xD3,0x49,0x8B,0xCF,0x44,
    0x89,0x6C,0x24,0x20,0xE8,0x18,0x10,0xEB,0xFF});
constexpr auto ItemEffectAimExpected = std::to_array<std::uint8_t>({
    0x8B,0x84,0x24,0xF8,0x00,0x00,0x00});
constexpr auto ItemEffectAimBytes = std::to_array<std::uint8_t>({
    0x31,0xC0,0x90,0x90,0x90,0x90,0x90});

// Client cast executor sub_140216D20(unit, skillId, level, itemProc, flag):
// if (itemProc) offsets the client missile coordinates. je -> jmp, same target.
constexpr std::uint64_t ClientExecutorWitnessRva = 0x216DB3;
constexpr auto ClientExecutorWitness = std::to_array<std::uint8_t>({
    0x44,0x39,0xAC,0x24,0xB8,0x00,0x00,0x00,0x0F,0x84,0xA7,0x00,0x00,0x00});
constexpr std::uint64_t ClientProcBlockRva = 0x216DBB;
constexpr auto ClientProcBlockExpected = std::to_array<std::uint8_t>({
    0x0F,0x84,0xA7,0x00,0x00,0x00});
constexpr auto ClientProcBlockBytes = std::to_array<std::uint8_t>({
    0xE9,0xA8,0x00,0x00,0x00,0x90});

// Client item-effect cast sub_1402310B0(unit, skillId, level, targetUnit,
// targetX, targetY, itemProc). 7 pushes + sub rsp,70h put the packet target
// at [rsp+0xD0]/[rsp+0xD8] and itemProc at [rsp+0xE0].
constexpr std::uint64_t ClientProcCastRva = 0x2310B0;
constexpr auto ClientProcCastPrologue = std::to_array<std::uint8_t>({
    0x44,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,
    0x83,0xEC,0x70});
constexpr std::uint64_t ClientProcCastPositionRva = 0x2311AC;
constexpr auto ClientProcCastPosition = std::to_array<std::uint8_t>({
    0x44,0x8B,0x84,0x24,0xD8,0x00,0x00,0x00,0x8B,0x94,0x24,0xD0,0x00,0x00,0x00,0xE8,
    0x10,0xE1,0x11,0x00});
// itemProc load right before the executor call: rbx = caster, rcx/rdx/r8 and
// the fifth argument are all set after this instruction.
constexpr std::uint64_t ClientProcCastCallRva = 0x2312E3;
constexpr auto ClientProcCastCall = std::to_array<std::uint8_t>({
    0x44,0x0F,0xB7,0x8C,0x24,0xE0,0x00,0x00,0x00,0x45,0x8B,0xC7,0x41,0x8B,0xD6,0x40,
    0x88,0x74,0x24,0x20,0x48,0x8B,0xCB,0xE8,0x21,0x5A,0xFE,0xFF});
constexpr std::uint64_t ClientTargetHookRva = 0x2312E3;
constexpr std::uint64_t ClientTargetResumeRva = 0x2312EC;
constexpr auto ClientTargetHookExpected = std::to_array<std::uint8_t>({
    0x44,0x0F,0xB7,0x8C,0x24,0xE0,0x00,0x00,0x00});

// Client missile builder sub_140213D80 (flags 0x21, the 2.4 "fl=21" builder):
// rcx = command, edx = 0, call the client missile creator sub_1401B7760.
constexpr std::uint64_t ClientMissileBuildRva = 0x213F13;
constexpr auto ClientMissileBuild = std::to_array<std::uint8_t>({
    0x83,0x7C,0x24,0x6C,0x00,0x74,0x15,0x83,0x7C,0x24,0x70,0x00,0x74,0x0E,0x33,0xD2,
    0x48,0x8D,0x4C,0x24,0x40,0xE8,0x33,0x38,0xFA,0xFF,0xEB,0x02,0x33,0xC0});
constexpr std::uint64_t ClientRewindHookRva = 0x213F28;
constexpr std::uint64_t ClientRewindResumeRva = 0x213F2D;
constexpr std::uint64_t ClientMissileCreateRva = 0x1B7760;
constexpr auto ClientRewindHookExpected = std::to_array<std::uint8_t>({
    0xE8,0x33,0x38,0xFA,0xFF});

// Path accessors proving the layout above.
constexpr auto PathGetXBytes = std::to_array<std::uint8_t>({0x0F,0xB7,0x41,0x02,0xC3});
constexpr auto PathGetYBytes = std::to_array<std::uint8_t>({0x0F,0xB7,0x41,0x06,0xC3});
constexpr auto PathGetTargetUnitBytes = std::to_array<std::uint8_t>({
    0x48,0x85,0xC9,0x75,0x03,0x33,0xC0,0xC3,0x48,0x8B,0x41,0x70,0xC3});
constexpr auto PathSetTargetPointBytes = std::to_array<std::uint8_t>({
    0x48,0x85,0xC9,0x74,0x11,0x66,0x89,0x51,0x10,0x66,0x44,0x89,0x41,0x12,0x48,0xC7,
    0x41,0x70,0x00,0x00,0x00,0x00,0xC3});

// Tail transfers use MOV R11 / JMP R11 (ModRM mod=3). A bare FF25
// is recognized as a v1 unwind epilogue although this relay still owns the
// full host stack. R11 is volatile and dead at both witnessed continuations.
constexpr std::array<std::uint8_t, 64> ClientTargetRelayCode{0x48,0x89,0xD9,0x8B,0x94,0x24,0xD0,0x00,0x00,0x00,0x44,0x8B,0x84,0x24,0xD8,0x00,0x00,0x00,0xFF,0x15,0x18,0x00,0x00,0x00,0x44,0x0F,0xB7,0x8C,0x24,0xE0,0x00,0x00,0x00,0x4C,0x8B,0x1D,0x10,0x00,0x00,0x00,0x41,0xFF,0xE3,0xCC,0xCC,0xCC,0xCC,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
constexpr std::size_t ClientTargetRelayCode_HandlerSlot = 0x30;
constexpr std::size_t ClientTargetRelayCode_ResumeSlot = 0x38;
constexpr std::array<std::uint8_t, 72> ClientRewindRelayCode{0xFF,0x15,0x2A,0x00,0x00,0x00,0x48,0x89,0x44,0x24,0x20,0x48,0x89,0xC1,0xFF,0x15,0x24,0x00,0x00,0x00,0x48,0x8B,0x44,0x24,0x20,0x4C,0x8B,0x1D,0x20,0x00,0x00,0x00,0x41,0xFF,0xE3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
constexpr std::size_t ClientRewindRelayCode_CreatorSlot = 0x30;
constexpr std::size_t ClientRewindRelayCode_HandlerSlot = 0x38;
constexpr std::size_t ClientRewindRelayCode_ResumeSlot = 0x40;


// Additional canonical witnesses bind the copied client ABI and path writes.
constexpr auto ClientBuilderPrologue = std::to_array<std::uint8_t>({0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xF9,0x48,0x81,0xEC,0xF8,0x00,0x00,0x00});
constexpr auto DynamicPathLayout = std::to_array<std::uint8_t>({0x8B,0x13,0x8B,0xCA,0x83,0xE9,0x02,0x74,0x20,0x83,0xE9,0x02,0x74,0x1B,0x83,0xF9,0x01,0x74,0x16,0x48,0x8B,0x4B,0x38,0x48,0x85,0xC9,0x74,0x09,0xE8,0xFC,0xAB,0x12,0x00});
constexpr auto PathStepLayout = std::to_array<std::uint8_t>({0x44,0x8B,0x9B,0xA0,0x00,0x00,0x00,0x45,0x8B,0xFB,0x44,0x8B,0xA4,0x24,0x84,0x00,0x00,0x00,0x45,0x8B,0xF3,0x44,0x0F,0xAF,0xF9,0x45,0x0F,0xAF,0xF4,0x41,0xC1,0xFF,0x08,0x41,0xC1,0xFE,0x08,0x44,0x89,0xBC,0x24,0xD0,0x00,0x00,0x00,0x44,0x89,0xB4,0x24,0xD4,0x00,0x00,0x00,0x48,0x8B,0x84,0x24,0xD0,0x00,0x00,0x00,0x48,0x89,0x83,0x96,0x00,0x00,0x00});

constexpr std::size_t RelayPageSize=0x1000, RelayBlockSize=0x200, RelayCodeOffset=0x100;
constexpr std::size_t ClientTargetBlock=0, ClientRewindBlock=1;
constexpr std::uint64_t LowestHookRva=0x213F28;
const D2RL::PluginContext* g_ctx{};
std::uintptr_t g_base{};
Options g_options{};
std::uint8_t* g_relayPage{};
std::array<RUNTIME_FUNCTION,2> g_relayUnwind{};
std::atomic<bool> g_enabled{false};
std::atomic<std::uint64_t> g_targetsReasserted{},g_missilesRewound{};
bool g_prepared{},g_installed{},g_failed{},g_unwindRegistered{};
unsigned g_patches{};
void Log(int, const char* text) noexcept { if(g_ctx) g_ctx->LogError(text); }
template <typename T>
T Read(const void* base, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

bool HasDynamicPath(const void* unit) noexcept {
    const auto type = Read<std::uint32_t>(unit, UnitTypeOffset);
    return type == PlayerUnitType || type == MonsterUnitType || type == MissileUnitType;
}

// 2.4 cave 2C0A00: the client start function overwrites the path's first point
// with the caster's facing before the executor runs. Write the packet's target
// point back right before the executor call.
void OnClientProcCastTarget(void* caster, std::uint32_t targetX, std::uint32_t targetY) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) || !caster || !HasDynamicPath(caster)) return;
    void* path = Read<void*>(caster, UnitPathOffset);
    if (!path) return;
    Write<std::uint16_t>(path, PathFirstPointXOffset, static_cast<std::uint16_t>(targetX));
    Write<std::uint16_t>(path, PathFirstPointYOffset, static_cast<std::uint16_t>(targetY));
    g_targetsReasserted.fetch_add(1, std::memory_order_relaxed);
}

// 2.4 cave 2C0878: step the new missile back by exactly one movement tick. The
// step vector was stored by the path setup during creation, so the rewind
// scales with the missile's own speed and direction.
void OnClientMissileCreated(void* missile) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) || !missile || Read<std::uint32_t>(missile, UnitTypeOffset) != MissileUnitType) return;
    void* path = Read<void*>(missile, UnitPathOffset);
    if (!path) return;
    const auto x = Read<std::uint32_t>(path, PathPrecisionXOffset);
    const auto y = Read<std::uint32_t>(path, PathPrecisionYOffset);
    const auto stepX = Read<std::uint32_t>(path, PathStepXOffset);
    const auto stepY = Read<std::uint32_t>(path, PathStepYOffset);
    Write<std::uint32_t>(path, PathPrecisionXOffset, x - stepX);
    Write<std::uint32_t>(path, PathPrecisionYOffset, y - stepY);
    g_missilesRewound.fetch_add(1, std::memory_order_relaxed);
}

std::uint8_t* AllocateRelayPage() noexcept {
    const auto* image = reinterpret_cast<const std::uint8_t*>(g_base);
    const auto ntHeaders = Read<std::int32_t>(image, 0x3C);
    const auto sizeOfImage = Read<std::uint32_t>(image, static_cast<std::size_t>(ntHeaders) + 0x50);
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity = systemInfo.dwAllocationGranularity ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) { return (value + granularity - 1) & ~(granularity - 1); };
    const std::uintptr_t limit = g_base + LowestHookRva + 0x7FF00000;
    std::uintptr_t address = alignUp(g_base + sizeOfImage);
    while (address + RelayPageSize < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + RelayPageSize <= regionEnd) {
            if (auto* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), RelayPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const auto next = alignUp(regionEnd);
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

void PlaceRelay(std::size_t block, const std::uint8_t* code, std::size_t size) noexcept {
    std::memcpy(g_relayPage + block * RelayBlockSize + RelayCodeOffset, code, size);
}

void SetRelaySlot(std::size_t block, std::size_t slot, std::uint64_t value) noexcept {
    std::memcpy(g_relayPage + block * RelayBlockSize + RelayCodeOffset + slot, &value, sizeof(value));
}

void* RelayAddress(std::size_t block) noexcept {
    return g_relayPage + block * RelayBlockSize + RelayCodeOffset;
}

// Stack-only unwind descriptions reconstructed from the witnessed native
// prologues. The relays do not change RSP. Do not borrow host exception handlers:
// their function-relative scope data is invalid for a relocated function range.
constexpr std::array<std::uint8_t,20> TargetUnwind{
    1,0x13,8,0, 0x13,0xD2, 0x0F,0xF0, 0x0D,0xE0, 0x0B,0xD0,
    0x09,0x70, 0x08,0x60, 0x07,0x50, 0x06,0x30};
constexpr std::array<std::uint8_t,20> RewindUnwind{
    1,0x15,8,0, 0x15,0x01, 0x1F,0, 0x09,0xF0, 0x07,0xE0,
    0x05,0x70, 0x04,0x60, 0x03,0x30, 0x02,0x50};
void PlaceUnwind() noexcept {
    std::memcpy(g_relayPage,TargetUnwind.data(),TargetUnwind.size());
    std::memcpy(g_relayPage+RelayBlockSize,RewindUnwind.data(),RewindUnwind.size());
}
bool RegisterRelayUnwind() noexcept {
    const std::array<bool,2> used{g_options.clientAim,g_options.missileRewind};
    DWORD count=0;
    for(std::size_t block=0;block<used.size();++block) {
        if(!used[block]) continue;
        const auto begin=reinterpret_cast<std::uintptr_t>(g_relayPage)+block*RelayBlockSize;
        const auto rva=static_cast<DWORD>(begin-g_base);
        g_relayUnwind[count++]={rva,rva+static_cast<DWORD>(RelayBlockSize),rva};
    }
    g_unwindRegistered=count && RtlAddFunctionTable(g_relayUnwind.data(),count,g_base)!=0;
    return g_unwindRegistered;
}
bool BuildRelays() noexcept {
    g_relayPage=AllocateRelayPage();
    if(!g_relayPage) return false;
    std::memset(g_relayPage,0xCC,RelayPageSize);
    PlaceRelay(ClientTargetBlock,ClientTargetRelayCode.data(),ClientTargetRelayCode.size());
    SetRelaySlot(ClientTargetBlock,ClientTargetRelayCode_HandlerSlot,reinterpret_cast<std::uint64_t>(&OnClientProcCastTarget));
    SetRelaySlot(ClientTargetBlock,ClientTargetRelayCode_ResumeSlot,g_base+ClientTargetResumeRva);
    PlaceRelay(ClientRewindBlock,ClientRewindRelayCode.data(),ClientRewindRelayCode.size());
    SetRelaySlot(ClientRewindBlock,ClientRewindRelayCode_CreatorSlot,g_base+ClientMissileCreateRva);
    SetRelaySlot(ClientRewindBlock,ClientRewindRelayCode_HandlerSlot,reinterpret_cast<std::uint64_t>(&OnClientMissileCreated));
    SetRelaySlot(ClientRewindBlock,ClientRewindRelayCode_ResumeSlot,g_base+ClientRewindResumeRva);
    PlaceUnwind();
    DWORD oldProtect{};
    if(!VirtualProtect(g_relayPage,RelayPageSize,PAGE_EXECUTE_READ,&oldProtect)) return false;
    if(!FlushInstructionCache(GetCurrentProcess(),g_relayPage,RelayPageSize)) return false;
    return RegisterRelayUnwind();
}
template<std::size_t N> bool Check(std::uint64_t rva,const std::array<std::uint8_t,N>& bytes) noexcept {
    if(g_ctx->CheckExpectedBytes(rva,bytes.data(),static_cast<std::uint32_t>(N))) return true;
    char line[180]{};std::snprintf(line,sizeof(line),"CastTriggers: proc presentation witness rejected at 0x%llX; no new hooks installed.",static_cast<unsigned long long>(rva));
    g_ctx->LogError(line); return false;
}
bool ValidateSelected() noexcept {
    constexpr auto dispatchSite = std::to_array<std::uint8_t>({0x44,0x0F,0xB6,0x76,0x24,0x40,0x32,0xED});
    if(!Check(0x43AF1D,dispatchSite)) return false;
    if((g_options.clientAim || g_options.missileRewind) && !Check(0x216E03,DynamicPathLayout)) return false;
    if(g_options.serverAim && !Check(ItemEffectAimRva,ItemEffectAimWitness)) return false;
    if(g_options.clientAim && !(Check(ClientExecutorWitnessRva,ClientExecutorWitness)
        && Check(ClientProcCastRva,ClientProcCastPrologue)
        && Check(ClientProcCastPositionRva,ClientProcCastPosition)
        && Check(ClientProcCastCallRva,ClientProcCastCall)
        && Check(0x341A20,PathGetXBytes) && Check(0x341A30,PathGetYBytes)
        && Check(0x342A50,PathSetTargetPointBytes))) return false;
    if(g_options.missileRewind && !(Check(ClientMissileBuildRva,ClientMissileBuild)
        && Check(0x213D80,ClientBuilderPrologue) && Check(0x3810E1,PathStepLayout))) return false;
    return true;
}
} // namespace
bool Prepare(const D2RL::PluginContext* ctx,Options options) noexcept {
    if(g_prepared || g_relayPage || g_patches) return false; // no hot reinstall
    g_ctx=ctx;g_options=options;g_base=ctx?ctx->exeBase:0;
    if(!g_base || !ValidateSelected()) return false;
    if((options.clientAim||options.missileRewind) && !BuildRelays()) {
        if(g_unwindRegistered) {RtlDeleteFunctionTable(g_relayUnwind.data());g_unwindRegistered=false;}
        if(g_relayPage) {VirtualFree(g_relayPage,0,MEM_RELEASE);g_relayPage=nullptr;}
        ctx->LogError("CastTriggers: proc presentation relay/unwind preparation failed; no hooks installed.");return false;
    }
    g_prepared=true;return true;
}
bool Install() noexcept {
    if(!g_prepared || g_installed || g_failed) return false;
    // Callbacks must remain resident while Loader-owned jumps can target them.
    if(g_options.clientAim||g_options.missileRewind) {
        HMODULE self{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&OnClientProcCastTarget),&self)) return false;
    }
    auto record=[](bool ok) noexcept {if(ok) ++g_patches; else g_failed=true;return ok;};
    if(g_options.serverAim && !record(g_ctx->PatchBytes(ItemEffectAimRva,ItemEffectAimExpected.data(),7,ItemEffectAimBytes.data(),7))) return false;
    // These are mid-function detours, not function-entry wrappers. Each relay
    // reproduces its complete displaced instruction (9-byte load / 5-byte call)
    // and resumes at the exact following instruction. No trampoline is invoked.
    // Inline registration accepts executable targets outside the game image;
    // relative patch targets are restricted to the image by Loader 1.3.1.
    if(g_options.clientAim) {
        if(!record(g_ctx->PatchBytes(ClientProcBlockRva,ClientProcBlockExpected.data(),6,ClientProcBlockBytes.data(),6))) return false;
        if(!record(g_ctx->InstallInlineHook(ClientTargetHookRva,ClientTargetHookExpected.data(),9,RelayAddress(ClientTargetBlock)))) return false;
    }
    if(g_options.missileRewind && !record(g_ctx->InstallInlineHook(ClientRewindHookRva,ClientRewindHookExpected.data(),5,RelayAddress(ClientRewindBlock)))) return false;
    g_installed=true;return true;
}
void Enable() noexcept {g_enabled.store(g_installed,std::memory_order_release);}
void Stop() noexcept {
    g_enabled.store(false,std::memory_order_release);
    if(g_patches==0) {
        if(g_unwindRegistered) {RtlDeleteFunctionTable(g_relayUnwind.data());g_unwindRegistered=false;}
        if(g_relayPage) {VirtualFree(g_relayPage,0,MEM_RELEASE);g_relayPage=nullptr;}
        g_prepared=false;
    }
}
void Describe(char* out,std::size_t size) noexcept {
    std::snprintf(out,size,"Cast Triggers proc presentation: active=%s; server-aim=%s; client-aim=%s; missile-rewind=%s; patches=%u; targets=%llu; missiles=%llu; failed=%s.",
        g_enabled.load()?"yes":"no",g_options.serverAim?"on":"off",g_options.clientAim?"on":"off",g_options.missileRewind?"on":"off",g_patches,
        static_cast<unsigned long long>(g_targetsReasserted.load()),static_cast<unsigned long long>(g_missilesRewound.load()),g_failed?"yes":"no");
}
} // namespace RuffnecKk::CastTriggers::ProcPresentation
