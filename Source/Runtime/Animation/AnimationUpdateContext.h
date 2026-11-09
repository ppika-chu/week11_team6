#pragma once

class UAnimInstance;

// Graph Update의 실행 정보. UE의 Context 계약을 사용하고 Proxy 대신 Instance를 직접 전달한다.
struct FAnimationUpdateContext
{
	UAnimInstance* AnimInstance;
	float DeltaTime;
	float FinalBlendWeight = 1.0f;

	float GetDeltaTime() const { return DeltaTime; }
	float GetFinalBlendWeight() const { return FinalBlendWeight; }
	FAnimationUpdateContext FractionalWeight(float Weight) const
	{
		return { AnimInstance, DeltaTime, FinalBlendWeight * Weight };
	}
};
