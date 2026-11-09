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
	virtual ~UAnimSequenceBase() override;
	// Asset의 Notify 배치와 객체 설정. 로드는 재생에 연결하기 전에 수행한다.
	void Serialize(json& Handle, bool bIsLoading) override;
	// Editor/로드에서 생성한 Notify 객체는 Sequence가 보관·해제한다.
	UObject* CreateNotifyObject(UClass* NotifyClass);

	virtual float GetPlayLength() const override { return DataModel ? DataModel->GetPlayLength() : 0.0f; }

	// 대상 Skeleton 순서의 전체 Local Pose를 채우며 시간은 전진시키지 않는다.
	virtual void GetAnimationPose(FAnimPose& OutPose, const FAnimExtractContext& ExtractionContext) const = 0;

	// Root Motion을 쓰는 에셋인지 (UE UAnimSequenceBase::HasRootMotion)
	virtual bool HasRootMotion() const { return false; }
	// StartTime에서 DeltaTime만큼 진행하는 동안 루트 본의 변화량. 반복이면 끝을 넘어 이어 붙인다 (UE ExtractRootMotion)
	virtual FTransform ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const { return FTransform::Identity; }

	// 이벤트 조회 — 시간 전진과 실행은 호출자가 담당한다.
	void GetAnimNotifies(float StartTime, float DeltaTime, FAnimNotifyContext& NotifyContext) const;
	void GetAnimNotifiesFromDeltaPositions(float PreviousPosition, float CurrentPosition, FAnimNotifyContext& NotifyContext) const;
	EAnimEventTriggerOffsets::Type CalculateOffsetForNotify(float NotifyDisplayTime) const;

	float RateScale = 1.0f;
	TArray<FAnimNotifyEvent> Notifies;
	UAnimDataModel* DataModel = nullptr;
private:
	TArray<UObject*> OwnedNotifyObjects;
};
