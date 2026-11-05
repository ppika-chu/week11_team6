#pragma once

// 한 축의 사인파 진동기. Value = Amplitude * sin(2π * Frequency * t + 초기위상)
enum class EInitialWaveOscillatorOffsetType : uint8
{
	Random, // 초기 위상을 [0, 2π)에서 무작위로 고른다
	Zero,   // 항상 0에서 시작한다
};

struct FWaveOscillator
{
	float Amplitude{ 1.0f };
	float Frequency{ 1.0f }; // Hz
	EInitialWaveOscillatorOffsetType InitialOffsetType{ EInitialWaveOscillatorOffsetType::Random };

	// 초기 위상(라디안)을 OutOffset에 쓰고, 그 시점의 값을 반환한다.
	float Initialize(float& OutOffset) const;

	// 위상을 진행시키고 현재 값을 반환한다.
	float Update(float DeltaTime, float AmplitudeMultiplier, float FrequencyMultiplier, float& InOutOffset) const;
};

// 한 축의 Perlin 노이즈 진동기. 사인파보다 불규칙한 흔들림을 만든다.
struct FPerlinNoiseShaker
{
	float Amplitude{ 1.0f };
	float Frequency{ 1.0f }; // 초당 노이즈 공간 이동량

	// 노이즈 공간의 시작 위치를 무작위로 골라 OutOffset에 쓴다.
	void Initialize(float& OutOffset) const;

	float Update(float DeltaTime, float AmplitudeMultiplier, float FrequencyMultiplier, float& InOutOffset) const;
};
