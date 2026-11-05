#include "EnginePCH.h"
#include "CameraShakeOscillators.h"
#include "Math/Noise.h"

float FWaveOscillator::Initialize(float& OutOffset) const
{
	OutOffset = (InitialOffsetType == EInitialWaveOscillatorOffsetType::Random)
		? FMath::FRand() * (2.0f * PI)
		: 0.0f;
	return Amplitude * std::sin(OutOffset);
}

float FWaveOscillator::Update(float DeltaTime, float AmplitudeMultiplier, float FrequencyMultiplier, float& InOutOffset) const
{
	const float TotalAmplitude{ Amplitude * AmplitudeMultiplier };
	if (TotalAmplitude == 0.0f)
	{
		return 0.0f;
	}

	InOutOffset += DeltaTime * Frequency * FrequencyMultiplier * (2.0f * PI);
	// 오래 재생해도 float 정밀도가 떨어지지 않게 위상을 [0, 2π)로 감는다.
	InOutOffset = std::fmod(InOutOffset, 2.0f * PI);
	return TotalAmplitude * std::sin(InOutOffset);
}

void FPerlinNoiseShaker::Initialize(float& OutOffset) const
{
	OutOffset = FMath::FRandRange(0.0f, 256.0f);
}

float FPerlinNoiseShaker::Update(float DeltaTime, float AmplitudeMultiplier, float FrequencyMultiplier, float& InOutOffset) const
{
	const float TotalAmplitude{ Amplitude * AmplitudeMultiplier };
	if (TotalAmplitude == 0.0f)
	{
		return 0.0f;
	}

	InOutOffset += DeltaTime * Frequency * FrequencyMultiplier;
	// 순열 테이블 주기(256)로 감아 정밀도를 유지한다.
	InOutOffset = std::fmod(InOutOffset, 256.0f);
	return TotalAmplitude * FMath::PerlinNoise1D(InOutOffset);
}
