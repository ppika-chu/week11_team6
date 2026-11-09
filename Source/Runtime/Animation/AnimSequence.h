#pragma once

#include "Animation/AnimSequenceBase.h"

class UAnimSequence : public UAnimSequenceBase
{
	DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)

	// 리플렉션 프로퍼티는 UAnimSequenceBase::Serialize(json)가 Notify와 함께 .fbxbin의 편집 데이터(NotifyData)에 저장한다
	REFLECT_START(UAnimSequence)
		PROPERTY(bEnableRootMotion)
		PROPERTY_ENUM(RootMotionRootLock, "Ref Pose", "Anim First Frame", "Zero")
		PROPERTY(bForceRootLock)
	REFLECT_END()

public:
	virtual void GetAnimationPose(FAnimPose& OutPose, const FAnimExtractContext& ExtractionContext) const override;

	// Context.CurrentTime 시각의 포즈를 계산한다. 트랙이 없는 본은 기본 자세를 쓴다.
	void GetBonePose(FAnimPose& OutPose, const FAnimExtractContext& Context) const;

	virtual bool HasRootMotion() const override { return bEnableRootMotion; }
	virtual FTransform ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const override;
	// 두 시각 사이 루트 본의 변화량. 루트 기준 자세에 대한 상대값이라 컴포넌트에 바로 적용할 수 있다
	FTransform ExtractRootMotionFromRange(float StartTime, float EndTime) const;
	// 그 시각 루트 본(0번)의 부모 기준 변환. 트랙이 없으면 기준 자세
	FTransform ExtractRootTrackTransform(float Time) const;

	// Root Motion 설정. FBX에는 없고 에디터에서 정해 .fbxbin의 편집 데이터에 저장한다 (UE 에셋 디테일 > Root Motion)
	// UE bUseNormalizedRootMotionScale은 두지 않고 항상 켠 것처럼 크기 변화를 버린다
	bool bEnableRootMotion = false;
	ERootMotionRootLock::Type RootMotionRootLock = ERootMotionRootLock::RefPose;
	bool bForceRootLock = false;
};
