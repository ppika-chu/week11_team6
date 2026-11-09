#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimInstance.h"
#include "Animation/BlendSpace.h"

// 공유 BlendSpace Asset을 재생한다. 입력 좌표와 재생 진행률은 서로 다른 값이다.
// UE의 Player 계약을 사용하며 시간 진행·Notify 수집은 Instance의 즉시 갱신 API에 맡긴다.
struct FAnimNode_BlendSpacePlayer : public FAnimNode_Base
{
public:
	void Initialize() override
	{
		Reinitialize(true);
		PreviousBlendSpace = BlendSpace;
	}

	void Update(const FAnimationUpdateContext& Context) override
	{
		if (PreviousBlendSpace != BlendSpace)
		{
			Reinitialize(bResetPlayTimeWhenBlendSpaceChanges);
			PreviousBlendSpace = BlendSpace;
		}
		Context.AnimInstance->BlendSpaceAdvanceImmediate(BlendSpace, GetPosition(), BlendSampleDataCache, bLoop, PlayRate, Context.GetDeltaTime(), InternalTimeAccumulator, CachedTriangulationIndex, Context.GetFinalBlendWeight());
	}

	void Evaluate(FAnimPose& OutPose) override
	{
		if (BlendSpace)
		{
			// Sample의 실제 추출 시각(초)은 Cache에 있다. Evaluate에서는 시간을 진행하지 않는다.
			BlendSpace->GetAnimationPose(BlendSampleDataCache, FAnimExtractContext(), OutPose);
		}
		else
		{
			OutPose.LocalTransforms.Reset();
		}
	}

	// Asset·축 입력 — SetPosition은 재생 위치가 아니라 BlendSpace의 입력 좌표다.
	UBlendSpace* GetBlendSpace() const { return BlendSpace; }
	bool SetBlendSpace(UBlendSpace* InBlendSpace) { BlendSpace = InBlendSpace; return true; }
	FVector GetPosition() const { return FVector(X, Y, 0.0f); }
	bool SetPosition(FVector InPosition) { X = InPosition.X; Y = InPosition.Y; return true; }

	// 재생 설정 — StartPosition과 누적 시간은 0~1 진행률. 현재 Asset은 1D의 X축만 사용한다.
	float GetStartPosition() const { return StartPosition; }
	bool SetStartPosition(float InStartPosition) { StartPosition = InStartPosition; return true; }
	float GetPlayRate() const { return PlayRate; }
	bool SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; return true; }
	bool IsLooping() const { return bLoop; }
	bool SetLoop(bool bInLoop) { bLoop = bInLoop; return true; }
	bool ShouldResetPlayTimeWhenBlendSpaceChanges() const { return bResetPlayTimeWhenBlendSpaceChanges; }
	bool SetResetPlayTimeWhenBlendSpaceChanges(bool bReset) { bResetPlayTimeWhenBlendSpaceChanges = bReset; return true; }
	float GetAccumulatedTime() const { return InternalTimeAccumulator; }

private:
	void Reinitialize(bool bResetTime)
	{
		BlendSampleDataCache.Reset();
		CachedTriangulationIndex = -1;
		if (bResetTime)
		{
			InternalTimeAccumulator = FMath::Clamp(StartPosition, 0.0f, 1.0f);
			// UE처럼 시작 위치가 0인 역재생은 끝에서 시작한다.
			if (StartPosition == 0.0f && PlayRate < 0.0f)
			{
				InternalTimeAccumulator = 1.0f;
			}
		}
	}

	// 공유 Asset 참조와 Node별 설정. Asset과 그 Sample들은 재생 중 유효해야 한다.
	UBlendSpace* BlendSpace = nullptr;
	float X = 0.0f;
	float Y = 0.0f;
	float PlayRate = 1.0f;
	float StartPosition = 0.0f;
	bool bLoop = true;
	bool bResetPlayTimeWhenBlendSpaceChanges = true;

	// Node별 실행 상태. Asset 교체 시 이전 Cache를 버리고 새 Sample을 선택한다.
	UBlendSpace* PreviousBlendSpace = nullptr;
	float InternalTimeAccumulator = 0.0f;
	TArray<FBlendSampleData> BlendSampleDataCache;
	int32 CachedTriangulationIndex = -1;
};
