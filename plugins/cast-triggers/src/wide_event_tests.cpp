#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wide_event_bridge.hpp"
#include "wide_event_contract.hpp"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace ruffneckk::cast_triggers::wide_events;
namespace {
int calls{}, observed{}, forwarded{};
bool kill{};
void** expectedContext{};
int EffectCall(void*, int, void*, void*, void* damage, std::uint64_t key, int a, int b, void** context) {
    ++calls;
    assert(context == expectedContext);
    assert(*context == reinterpret_cast<void*>(0x5678));
    assert(StatId(key) == 404 || StatId(key) == 397);
    assert(a == 17 && b == 29);
    if (kill) assert(damage == nullptr);
    return 7;
}
bool Filter(void*, int event, void*, void*, void*& damage, std::uint64_t key, Effect) {
    if (event == 10 && StatId(key) == 397) return false;
    if (kill) damage = nullptr;
    return true;
}
void Observe(void*, void*, void*, void* damage, Effect, int result) {
    assert(damage == reinterpret_cast<void*>(0x1234));
    observed += result;
}
int Prior(void*, void** g, int* e, void** o, void** t, void** d,
        std::uint64_t* k, int* a, int* b, void** x, Effect* fn) {
    ++forwarded;
    return (*fn)(*g,*e,*o,*t,*d,*k,*a,*b,x);
}
int Run(Bridge& bridge, int event, int stat) {
    void *g{}, *o{}, *t{}, *d=reinterpret_cast<void*>(0x1234), *x=reinterpret_cast<void*>(0x5678);
    expectedContext = &x;
    auto key=(static_cast<std::uint64_t>(stat)<<32)|3135;
    int a=17,b=29; Effect fn=EffectCall;
    // Native dispatcher stack+0x48 is &context; stack+0x50 is &function.
    auto result=bridge.table->invoke(&bridge,&g,&event,&o,&t,&d,&key,&a,&b,&x,&fn);
    assert(d==reinterpret_cast<void*>(0x1234));
    return result;
}
}
int main() {
    // Replays Core's borrowed callback ABI, including empty and heap callbacks.
    std::uintptr_t empty=1;
    Bridge bridge(&empty,Filter,Observe);
    assert(Run(bridge,10,397)==0 && calls==0 && observed==0);
    assert(Run(bridge,5,397)==7 && calls==1 && observed==7);
    kill=true;
    assert(Run(bridge,9,404)==7 && calls==2 && observed==14);
    Table table{nullptr,Prior}; const Table* object=&table;
    bridge.previous=&object;
    assert(Run(bridge,9,404)==7 && forwarded==1);
    std::uintptr_t tagged=reinterpret_cast<std::uintptr_t>(&object)|1;
    bridge.previous=&tagged;
    assert(Run(bridge,9,404)==7 && forwarded==2);
    bridge.previous=nullptr;
    assert(Run(bridge,9,404)==7 && forwarded==2);

    // Refuse changed provider code, callback table ownership, caller context,
    // and dispatch destination before the caller is eligible for a patch.
    std::vector<std::uint8_t> core(DispatchExportRva + DispatchExportBytes.size());
    std::vector<std::uint8_t> native(0x3E2A420 + sizeof(void*));
    const auto copy = [&core](auto rva, const auto& bytes) {
        std::memcpy(core.data()+rva,bytes.data(),bytes.size());
    };
    copy(DispatchExportRva,DispatchExportBytes);
    copy(DispatchBodyRva,DispatchBodyBytes);
    copy(Effect15Rva,Effect15Bytes);
    copy(Effect16Rva,Effect16Bytes);
    copy(Effect20Rva,Effect20Bytes);
    copy(SkillEffectBodyRva,SkillEffectBodyBytes);
    for (auto [slot,rva] : {std::pair{15,Effect15Rva},
            std::pair{16,Effect16Rva},std::pair{20,Effect20Rva}}) {
        const void* pointer=core.data()+rva;
        std::memcpy(core.data()+0x630260+slot*sizeof(void*),&pointer,sizeof pointer);
    }
    std::memcpy(native.data()+NativeCallerRva,NativeCallerBytes.data(),NativeCallerBytes.size());
    std::memcpy(native.data()+0x5881E0,NativeProviderJump.data(),NativeProviderJump.size());
    const void* destination=core.data()+DispatchExportRva;
    std::memcpy(native.data()+0x3E2A420,&destination,sizeof destination);
    assert(Validate(core.data()) && ValidateNativeProvider(native.data(),core.data()));
    core[DispatchBodyRva]^=1;
    assert(!Validate(core.data()));
    core[DispatchBodyRva]^=1;
    core[0x630260+20*sizeof(void*)]^=1;
    assert(!Validate(core.data()));
    native[NativeCallerRva]^=1;
    assert(!ValidateNativeProvider(native.data(),core.data()));
    native[NativeCallerRva]^=1;
    native[0x3E2A420]^=1;
    assert(!ValidateNativeProvider(native.data(),core.data()));
}
