#pragma once

#include <cmath>

template<class T>
T InterpEaseIn(const T& A, const T& B, float Alpha, float Exp)
{
	float const ModifiedAlpha = std::pow(Alpha, Exp);
	return std::lerp(A, B, ModifiedAlpha);
}

template<class T>
T InterpEaseOut(const T& A, const T& B, float Alpha, float Exp)
{
	float const ModifiedAlpha = 1.f - std::pow(1.f - Alpha, Exp);
	return std::lerp(A, B, ModifiedAlpha);
}

template<class T>
T InterpEaseInOut(const T& A, const T& B, float Alpha, float Exp)
{
	return std::lerp(A, B, (Alpha < 0.5f) ? InterpEaseIn(0.f, 1.f, Alpha * 2.f, Exp) * 0.5f : InterpEaseOut(0.f, 1.f, Alpha * 2.f - 1.f, Exp) * 0.5f + 0.5f);
}
