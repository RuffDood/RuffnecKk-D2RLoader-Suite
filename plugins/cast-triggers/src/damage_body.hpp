#pragma once
#include <cstdint>
extern "C" {
extern void* DamageBodyWrapper;
extern void* DamageBodyOriginal;
extern void* DamageSecurityCookie;
void DamageBodyDetour();
void DamageBodyResume(void*, void*, void*, void*, std::int32_t, std::uint8_t);
}
