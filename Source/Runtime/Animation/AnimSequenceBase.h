#pragma once

#include "Animation/AnimationAsset.h"
#include "Animation/AnimDataModel.h"

struct FAnimPose;
struct FAnimNotifyContext;

enum ETypeAdvanceAnim : int
{
	ETAA_Default,
	ETAA_Finished,
	ETAA_Looped
};

class UAnimSequenceBase : public UAnimationAsset
{
	DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)
public:
	virtual ~UAnimSequenceBase() override
	{
		// 엔진 종료 때 전역 정리가 DataModel을 먼저 지울 수 있다.
		if (DataModel && FindLiveObjectByAddress(DataModel))
		{
			delete DataModel;
		}
	}

	virtual float GetPlayLength() const override { return DataModel ? DataModel->GetPlayLength() : 0.0f; }

	// 대상 Skeleton 순서의 전체 Local Pose를 채우며 시간은 전진시키지 않는다.
	virtual void GetAnimationPose(FAnimPose& OutPose, const FAnimExtractContext& ExtractionContext) const = 0;

	// 이벤트 조회 — 시간 전진과 실행은 호출자가 담당한다.
	void GetAnimNotifies(float StartTime, float DeltaTime, FAnimNotifyContext& NotifyContext) const;
	void GetAnimNotifiesFromDeltaPositions(float PreviousPosition, float CurrentPosition, FAnimNotifyContext& NotifyContext) const;
	EAnimEventTriggerOffsets::Type CalculateOffsetForNotify(float NotifyDisplayTime) const;

	float RateScale = 1.0f;
	TArray<FAnimNotifyEvent> Notifies;
	UAnimDataModel* DataModel = nullptr;
};
