#pragma once

#include <memory>
#include "CameraShakeTypes.h"
#include "CameraShakeState.h"
#include "CameraShakeOscillators.h"

// 실제 흔들림을 계산하는 패턴의 공통 인터페이스 (언리얼 UCameraShakePattern 대응).
// PCM을 모르고, 경과 시간을 받아 POV 오프셋만 내놓는다.
class FCameraShakePattern
{
public:
	virtual ~FCameraShakePattern() = default;

	virtual void StartShakePattern() = 0;
	virtual void UpdateShakePattern(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) = 0;
	virtual void StopShakePattern(bool bImmediately) = 0;
	virtual bool IsFinished() const = 0;
};

// Duration·BlendIn/Out을 FCameraShakeState로 처리하는 패턴의 베이스 (USimpleCameraShakePattern 대응).
// 파생 클래스는 UpdateShakePatternImpl에서 가중치를 곱하기 전의 값만 계산한다.
class FSimpleCameraShakePattern : public FCameraShakePattern
{
public:
	FCameraShakeStateParams StateParams{};

public:
	void StartShakePattern() override;
	void UpdateShakePattern(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;
	void StopShakePattern(bool bImmediately) override;
	bool IsFinished() const override;

	const FCameraShakeState& GetState() const { return mState; }

protected:
	// 처음 시작할 때만 호출된다. 재생 중 재시작이면 진동 위상을 유지한다.
	virtual void StartShakePatternImpl() {}
	virtual void UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) = 0;

private:
	FCameraShakeState mState{};
};

// 각 축을 사인파로 흔드는 패턴 (UWaveOscillatorCameraShakePattern 대응)
class FWaveOscillatorCameraShakePattern : public FSimpleCameraShakePattern
{
public:
	float LocationAmplitudeMultiplier{ 1.0f };
	float LocationFrequencyMultiplier{ 1.0f };
	FWaveOscillator X{ 0.0f }, Y{ 0.0f }, Z{ 0.0f };

	float RotationAmplitudeMultiplier{ 1.0f };
	float RotationFrequencyMultiplier{ 1.0f };
	FWaveOscillator Pitch{ 0.0f }, Yaw{ 0.0f }, Roll{ 0.0f };

	FWaveOscillator FOV{ 0.0f };

protected:
	void StartShakePatternImpl() override;
	void UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;

private:
	// 각 축의 현재 위상(라디안)
	FVector mCurrentLocationOffset{ FVector::ZeroVector };
	FVector mCurrentRotationOffset{ FVector::ZeroVector }; // Pitch, Yaw, Roll 순
	float mCurrentFOVOffset{};
};

// 각 축을 Perlin 노이즈로 흔드는 패턴 (UPerlinNoiseCameraShakePattern 대응)
class FPerlinNoiseCameraShakePattern : public FSimpleCameraShakePattern
{
public:
	float LocationAmplitudeMultiplier{ 1.0f };
	float LocationFrequencyMultiplier{ 1.0f };
	FPerlinNoiseShaker X{ 0.0f }, Y{ 0.0f }, Z{ 0.0f };

	float RotationAmplitudeMultiplier{ 1.0f };
	float RotationFrequencyMultiplier{ 1.0f };
	FPerlinNoiseShaker Pitch{ 0.0f }, Yaw{ 0.0f }, Roll{ 0.0f };

	FPerlinNoiseShaker FOV{ 0.0f };

protected:
	void StartShakePatternImpl() override;
	void UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;

private:
	FVector mCurrentLocationOffset{ FVector::ZeroVector };
	FVector mCurrentRotationOffset{ FVector::ZeroVector };
	float mCurrentFOVOffset{};
};

// 자식 패턴들의 결과를 합산하는 패턴 (UCompositeCameraShakePattern 대응)
class FCompositeCameraShakePattern : public FCameraShakePattern
{
public:
	FCameraShakePattern* AddChildPattern(std::unique_ptr<FCameraShakePattern> Pattern);

	// 소유권을 넘겨받는다. 추가한 패턴을 가리키는 포인터를 반환한다.
	template<typename TPattern>
	TPattern* AddChildPattern();

	void StartShakePattern() override;
	void UpdateShakePattern(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;
	void StopShakePattern(bool bImmediately) override;
	bool IsFinished() const override;

private:
	TArray<std::unique_ptr<FCameraShakePattern>> mChildPatterns;
};

template<typename TPattern>
TPattern* FCompositeCameraShakePattern::AddChildPattern()
{
	auto Pattern{ std::make_unique<TPattern>() };
	return static_cast<TPattern*>(AddChildPattern(std::move(Pattern)));
}
