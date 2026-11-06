#pragma once
#include "local_provider_compat.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace RuffnecKk::LocalProviderCompat {
inline bool Read(std::uintptr_t at,void* output,std::size_t size) noexcept {
 if(!at || !size || at>UINTPTR_MAX-size)return false;
 for(auto cursor=at;cursor<at+size;){
  MEMORY_BASIC_INFORMATION info{};
  if(!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof(info)) || info.State!=MEM_COMMIT
   || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(info.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
  const auto end=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
  if(end<=cursor)return false;cursor=end;
 }
 std::memcpy(output,reinterpret_cast<void*>(at),size);return true;
}
inline bool SeedSelector(std::uintptr_t game) noexcept {
 return Seed({Read},game,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"d2rl-map-seed-selector.dll")));
}
}
