#ifdef NDEBUG
#undef NDEBUG
#endif
#include "proc_presentation.cpp"
#include "cast_triggers_policy.hpp"
#include <cassert>
#include <iostream>
using namespace RuffnecKk::CastTriggers::ProcPresentation;
extern "C" unsigned PresentationTargetProbe(void*,void*,unsigned,unsigned);
extern "C" void PresentationTargetResume();
extern "C" void* PresentationRewindProbe(void*,void*);
extern "C" void PresentationRewindResume();
unsigned creations{}, checks{}, failCheck{}, writes{}, failWrite{};
bool CheckBytes(const D2RL::PluginContext*,std::uint64_t,const void*,std::uint32_t) noexcept {
    return ++checks!=failCheck;
}
bool PatchBytes(const D2RL::PluginContext*,std::uint64_t,const void*,std::uint32_t,const void*,std::uint32_t) noexcept {
    return ++writes!=failWrite;
}
bool InlineHook(const D2RL::PluginContext*,const D2RL::InlineHookRegistration* hook) noexcept {
    assert(hook && hook->original == nullptr);
    if(hook->rva == ClientTargetHookRva) {
        assert(hook->expectedSize == 9 && hook->target == RelayAddress(ClientTargetBlock));
        assert(std::memcmp(hook->expected,ClientTargetHookExpected.data(),9)==0);
    } else {
        assert(hook->rva == ClientRewindHookRva && hook->expectedSize == 5);
        assert(hook->target == RelayAddress(ClientRewindBlock));
        assert(std::memcmp(hook->expected,ClientRewindHookExpected.data(),5)==0);
    }
    return ++writes!=failWrite;
}
void* Creator(void* command,int zero) noexcept {assert(zero==0);++creations;return command;}
int main() {
    ruffneckk::cast_triggers::Config config{};std::string error;
    assert(ruffneckk::cast_triggers::ParseToml("",config,error));
    assert(config.serverProcAim && config.clientProcAim && config.clientMissileRewind);
    assert(ruffneckk::cast_triggers::ParseToml("[proc_presentation]\nserver_proc_aim=false\nclient_proc_aim=false\nclient_missile_rewind=false",config,error));
    assert(!config.serverProcAim && !config.clientProcAim && !config.clientMissileRewind);
    assert(!ruffneckk::cast_triggers::ParseToml("[proc_presentation]\nclient_proc_aim=1",config,error));
    assert(!ruffneckk::cast_triggers::ParseToml("[proc_presentation]\ncast_trigger=true",config,error));
    D2RL::PluginApi api{};api.apiSize=sizeof(api);
    api.checkExpectedBytes=&CheckBytes;api.patchBytes=&PatchBytes;api.installInlineHook=&InlineHook;
    D2RL::PluginContext ctx{};ctx.contextSize=sizeof(ctx);ctx.api=&api;ctx.exeBase=0x140000000;
    g_ctx=&ctx;g_options={};checks=0;assert(ValidateSelected());const unsigned checkCount=checks;
    assert(checkCount>=13);
    // Reject every selected witness in turn, with zero patch attempts/allocation.
    for(unsigned fail=1;fail<=checkCount;++fail) {
        checks=0;failCheck=fail;
        assert(!Prepare(&ctx,{}));assert(writes==0 && !g_prepared && !g_relayPage);
    }
    failCheck=0;
    assert(Prepare(&ctx,{false,false,false}));assert(Install());Enable();
    assert(writes==0);Stop();g_installed=false;
    g_options={};
    std::array<std::uint8_t,0x80> unit{};std::array<std::uint8_t,0x100> path{};
    Write<void*>(unit.data(),0x38,path.data());
    g_enabled=true;
    OnClientProcCastTarget(unit.data(),0x12345,0x23456);
    assert(Read<std::uint16_t>(path.data(),0x10)==0x2345);
    assert(Read<std::uint16_t>(path.data(),0x12)==0x3456);
    auto unchanged=path;Write<std::uint32_t>(unit.data(),0,2);
    OnClientProcCastTarget(unit.data(),7,8);assert(path==unchanged);
    OnClientProcCastTarget(nullptr,7,8);
    Write<std::uint32_t>(unit.data(),0,3);
    Write<std::uint32_t>(path.data(),0,100);
    Write<std::uint32_t>(path.data(),4,200);
    Write<std::uint32_t>(path.data(),0x96,30);
    Write<std::uint32_t>(path.data(),0x9A,static_cast<std::uint32_t>(-40));
    OnClientMissileCreated(unit.data());
    assert(Read<std::uint32_t>(path.data(),0)==70);
    assert(Read<std::uint32_t>(path.data(),4)==240);
    Stop();unchanged=path;OnClientMissileCreated(unit.data());assert(path==unchanged);
    // Execute the copied x64 relays in representative host frames. Verify stack
    // arguments, restored item-proc flag, creator called once, and RAX preserved.
    g_relayPage=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,RelayPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(g_relayPage);
    PlaceRelay(0,ClientTargetRelayCode.data(),ClientTargetRelayCode.size());
    SetRelaySlot(0,ClientTargetRelayCode_HandlerSlot,reinterpret_cast<std::uint64_t>(&OnClientProcCastTarget));
    SetRelaySlot(0,ClientTargetRelayCode_ResumeSlot,reinterpret_cast<std::uint64_t>(&PresentationTargetResume));
    PlaceRelay(1,ClientRewindRelayCode.data(),ClientRewindRelayCode.size());
    SetRelaySlot(1,ClientRewindRelayCode_CreatorSlot,reinterpret_cast<std::uint64_t>(&Creator));
    SetRelaySlot(1,ClientRewindRelayCode_HandlerSlot,reinterpret_cast<std::uint64_t>(&OnClientMissileCreated));
    SetRelaySlot(1,ClientRewindRelayCode_ResumeSlot,reinterpret_cast<std::uint64_t>(&PresentationRewindResume));
    PlaceUnwind();
    g_base=reinterpret_cast<std::uintptr_t>(g_relayPage);
    assert(RegisterRelayUnwind());
    DWORD old{};assert(VirtualProtect(g_relayPage,RelayPageSize,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),g_relayPage,RelayPageSize));
    // Verify both reconstructed native frames independently of the harness.
    for(unsigned block=0;block<2;++block) {
        const std::array<unsigned,7> points=block==0
            ? std::array<unsigned,7>{0,3,10,18,24,33,40}
            : std::array<unsigned,7>{0,6,11,14,20,25,32};
        for(const auto point:points) {
        std::array<DWORD64,64> stack{};
        CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
        context.Rsp=reinterpret_cast<DWORD64>(stack.data());
        const unsigned offset=block==0?0x70:0xF8;
        const unsigned count=block==0?7:6;
        for(unsigned i=0;i<count;++i) stack[offset/8+i]=100+i;
        stack[offset/8+count]=0x12345678;
        context.Rip=g_base+block*RelayBlockSize+RelayCodeOffset+point;
        PVOID handlerData{};DWORD64 frame{};
        auto handler=RtlVirtualUnwind(UNW_FLAG_NHANDLER,g_base,context.Rip,&g_relayUnwind[block],&context,&handlerData,&frame,nullptr);
        assert(handler==nullptr && context.Rip==0x12345678);
        assert(context.Rsp==reinterpret_cast<DWORD64>(stack.data())+offset+8*(count+1));
        assert(context.R15==100 && context.R14==101);
        if(block==0) assert(context.R13==102 && context.Rdi==103 && context.Rsi==104 && context.Rbp==105 && context.Rbx==106);
        else assert(context.Rdi==102 && context.Rsi==103 && context.Rbx==104 && context.Rbp==105);
    }
        }
    // Fail each Loader registration independently. Never mark the module active
    // or issue later writes after failure; report how many patches remain.
    for(unsigned fail=1;fail<=4;++fail) {
        writes=0;failWrite=fail;g_prepared=true;g_installed=false;g_failed=false;g_patches=0;
        assert(!Install());Enable();
        assert(!g_enabled && g_failed && writes==fail && g_patches==fail-1);
    }
    writes=0;failWrite=0;g_failed=false;g_patches=0;
    assert(Install());assert(g_patches==4 && writes==4);Enable();
    g_enabled=true;
    for(unsigned i=0;i<1000;++i) {
        assert(PresentationTargetProbe(g_relayPage+RelayCodeOffset,unit.data(),i,i+1)==0xBEEF);
        assert(Read<std::uint16_t>(path.data(),0x10)==i);
        assert(Read<std::uint16_t>(path.data(),0x12)==i+1);
        assert(PresentationRewindProbe(g_relayPage+RelayBlockSize+RelayCodeOffset,unit.data())==unit.data());
    }
    assert(creations==1000);
    assert(PresentationRewindProbe(g_relayPage+RelayBlockSize+RelayCodeOffset,nullptr)==nullptr);
    Stop();unchanged=path;
    assert(PresentationRewindProbe(g_relayPage+RelayBlockSize+RelayCodeOffset,unit.data())==unit.data());
    assert(path==unchanged && creations==1002);
    g_patches=0;Stop();assert(g_relayPage==nullptr && !g_unwindRegistered);
    std::cout<<"Presentation config, target guards, signed rewind and 1000 relay executions passed.\n";
}
