#pragma once
#include "local_provider_reference.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
namespace RuffnecKk::LocalProviderCompat {
struct Memory { bool (*read)(std::uintptr_t, void*, std::size_t) noexcept; };
inline bool Equal(Memory m, std::uintptr_t at, std::span<const std::uint8_t> expected) noexcept {
 std::array<std::uint8_t, 16384> bytes{};
 return expected.size() <= bytes.size() && m.read(at, bytes.data(), expected.size())
  && std::equal(expected.begin(), expected.end(), bytes.begin());
}
inline bool Pointer(Memory m, std::uintptr_t at, std::uintptr_t& value) noexcept { return m.read(at, &value, sizeof(value)); }
inline bool Relative(Memory m, std::uintptr_t at, std::uint8_t opcode, std::uintptr_t& target) noexcept {
 std::array<std::uint8_t,5> b{}; if (!m.read(at,b.data(),b.size()) || b[0]!=opcode) return false;
 std::int32_t d{};std::memcpy(&d,b.data()+1,4);target=at+5+static_cast<std::intptr_t>(d);return true;
}
inline bool Absolute(Memory m, std::uintptr_t at, std::uintptr_t expected) noexcept {
 std::array<std::uint8_t,14> b{};if(!m.read(at,b.data(),b.size()))return false;
 std::uintptr_t target{};
 if(b[0]==0xff && b[1]==0x25 && b[2]==0 && b[3]==0 && b[4]==0 && b[5]==0)std::memcpy(&target,b.data()+6,8);
 else if(b[0]==0x48 && b[1]==0xb8 && b[10]==0xff && b[11]==0xe0)std::memcpy(&target,b.data()+2,8);
 else return false;
 return target==expected;
}
inline bool Seed(Memory m,std::uintptr_t game,std::uintptr_t provider) noexcept {
 const auto at=game+0x326e89;
 if(Equal(m,at,SeedNative))return true;
 if(!provider || !Equal(m,provider+0x193d0,SeedCallbackA) || !Equal(m,provider+0x19415,SeedCallbackB))return false;
 std::array<std::uint8_t,SeedNative.size()> live{};
 if(!m.read(at,live.data(),live.size()))return false;
 constexpr std::array<std::size_t,2> offsets{21,42},displaced{9,6};
 constexpr std::array<std::uint32_t,2> callbacks{0x193d0,0x19415},slots{0x8c010,0x8c018};
 for(std::size_t i=0;i<2;++i){
  std::uintptr_t relay{},trampoline{};
  if(!Relative(m,at+offsets[i],0xe9,relay) || !Absolute(m,relay,provider+callbacks[i])
   || !Pointer(m,provider+slots[i],trampoline)
   || !Equal(m,trampoline,std::span(SeedNative).subspan(offsets[i],displaced[i]))
   || !Absolute(m,trampoline+displaced[i],at+offsets[i]+displaced[i]))return false;
  std::copy_n(SeedNative.begin()+offsets[i],5,live.begin()+offsets[i]);
 }
 return live==SeedNative;
}
inline bool Gold(Memory m,std::uintptr_t game,std::uintptr_t provider) noexcept {
 const auto at=game+0x19329d;
 if(Equal(m,at,MoveGoldNative))return true;
 if(!provider || !Equal(m,provider+GoldTextRva,GoldText))return false;
 std::array<std::uint8_t,MoveGoldNative.size()> live{};
 std::uintptr_t relay{},original{},base{};
 if(!m.read(at,live.data(),live.size()) || !Relative(m,at+47,0xe8,relay)
  || !Absolute(m,relay,provider+0x1450) || !Pointer(m,provider+0x74e8,original)
  || original!=game+0xfe3b0 || !Pointer(m,provider+0x74c0,base) || base!=game)return false;
 std::copy_n(MoveGoldNative.begin()+47,5,live.begin()+47);
 return live==MoveGoldNative;
}
}
