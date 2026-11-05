#pragma once

#if defined(__x86_64__) || defined(_M_X64)
// 인텔/AMD 환경
#include "VectorRegister_SSE.h"
#else
struct alignas(16) VectorRegister
{
	float v[4];
};
#endif
