#pragma once

namespace FMath
{
// 1D Perlin 노이즈. 정수 지점에서 0이고, 결과는 대략 [-1, 1] 범위다.
float PerlinNoise1D(float Value);

// [0, 1) 균등 난수
float FRand();

// [Min, Max) 균등 난수
float FRandRange(float Min, float Max);
}
