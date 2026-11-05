#include "EnginePCH.h"
#include "CameraShakeState.h"

void FCameraShakeState::Initialize(const FCameraShakeStateParams& InParams)
{
	mParams = InParams;
	mIsActive = false;
	mIsBlendingIn = false;
	mIsBlendingOut = false;
	mElapsedTime = 0.0f;
	mBlendOutElapsedTime = 0.0f;
	mCurrentWeight = 0.0f;
}

void FCameraShakeState::Start()
{
	// 재생 중에 다시 시작하면 현재 가중치에서 블렌드인을 이어가 화면이 튀지 않게 한다.
	const float ResumeWeight{ mIsActive ? mCurrentWeight : 0.0f };

	mIsActive = true;
	mIsBlendingOut = false;
	mBlendOutElapsedTime = 0.0f;
	mIsBlendingIn = mParams.BlendInTime > 0.0f;
	mElapsedTime = mIsBlendingIn ? ResumeWeight * mParams.BlendInTime : 0.0f;
	mCurrentWeight = mIsBlendingIn ? ResumeWeight : 1.0f;
}

float FCameraShakeState::Update(float DeltaTime)
{
	if (!mIsActive)
	{
		return 0.0f;
	}

	mElapsedTime += DeltaTime;
	if (mIsBlendingOut)
	{
		mBlendOutElapsedTime += DeltaTime;
	}

	// 종료 판정
	const bool bDurationEnded{ !mParams.IsInfinite() && mElapsedTime >= mParams.Duration };
	const bool bBlendOutEnded{ mIsBlendingOut && mBlendOutElapsedTime >= mParams.BlendOutTime };
	if (bDurationEnded || bBlendOutEnded)
	{
		mIsActive = false;
		mIsBlendingIn = false;
		mIsBlendingOut = false;
		mCurrentWeight = 0.0f;
		return 0.0f;
	}

	const float BlendInWeight{ CalcBlendInWeight() };
	mIsBlendingIn = BlendInWeight < 1.0f;

	// 언리얼과 같이 블렌드인/아웃 가중치는 선형이고, 둘을 곱한다.
	mCurrentWeight = BlendInWeight * CalcBlendOutWeight();
	return mCurrentWeight;
}

void FCameraShakeState::Stop(bool bImmediately)
{
	if (!mIsActive)
	{
		return;
	}

	if (bImmediately || mParams.BlendOutTime <= 0.0f)
	{
		mIsActive = false;
		mIsBlendingIn = false;
		mIsBlendingOut = false;
		mCurrentWeight = 0.0f;
		return;
	}

	if (mIsBlendingOut)
	{
		return;
	}

	// Duration 끝의 자연 블렌드아웃이 이미 진행 중이면 그 지점에서 이어간다.
	const float CurrentBlendOutWeight{ CalcBlendOutWeight() };
	mIsBlendingOut = true;
	mBlendOutElapsedTime = (1.0f - CurrentBlendOutWeight) * mParams.BlendOutTime;
}

float FCameraShakeState::CalcBlendInWeight() const
{
	if (mParams.BlendInTime <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(mElapsedTime / mParams.BlendInTime, 0.0f, 1.0f);
}

float FCameraShakeState::CalcBlendOutWeight() const
{
	if (mParams.BlendOutTime <= 0.0f)
	{
		return 1.0f;
	}

	float Weight{ 1.0f };
	if (mIsBlendingOut)
	{
		Weight = 1.0f - mBlendOutElapsedTime / mParams.BlendOutTime;
	}
	if (!mParams.IsInfinite())
	{
		// Duration 끝 BlendOutTime 구간의 자연 블렌드아웃
		const float Remaining{ mParams.Duration - mElapsedTime };
		Weight = FMath::Min(Weight, Remaining / mParams.BlendOutTime);
	}
	return FMath::Clamp(Weight, 0.0f, 1.0f);
}
