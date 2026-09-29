#pragma once
#include <cstdint>
extern "C" {
extern void* CastTargetWrapper;
extern void* CastPositionWrapper;
extern void* CastTargetBodyOriginal;
extern void* CastPositionBodyOriginal;
void CastTargetBodyDetour();
void CastPositionBodyDetour();
std::int32_t CastTargetResume(void*, std::int32_t, std::int32_t, void*, std::int32_t);
std::int32_t CastPositionResume(void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t);
}
