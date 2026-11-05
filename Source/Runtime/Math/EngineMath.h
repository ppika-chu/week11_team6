#pragma once
#include <cmath>
#include "Vector.h"
#include "Vector2.h"
#include "Vector4.h"
#include "Matrix.h"
#include "Quat.h"
#include "Rotator.h"
#include "Collision/Ray.h"
#include "VectorRegister.h"
#include "MatrixRegister.h"

#define PI 3.141592f
#define INDEX_NONE -1

namespace FMath
{
template<typename T>
static inline T Min(T A, T B)
{
	return (A >= B ? B : A);
}

template<typename T>
static inline T Max(T A, T B)
{
	return (A >= B ? A : B);
}

template<typename T>
static inline T Clamp(T InValue, T InMin, T InMax)
{
	return Max(InMin, Min(InValue, InMax));
}

static inline bool IsNearlyZero(float Value, float ErrorTolerance = 1e-20f)
{
	return std::abs(Value) <= ErrorTolerance;
}

static inline bool IsNearlyEqual(float Value1, float Value2, float ErrorTolerance = 1e-4f)
{
	return std::abs(Value1 - Value2) <= ErrorTolerance;
}


static inline float RadiansToDegrees(float Radian)
{
	return Radian * (180.0f / PI);
}

static inline float DegreesToRadians(float Degree)
{
	return Degree * (PI / 180.0f);
}
}