#pragma once

#include "AnimationAsset.h"

struct FAnimPose;

enum ETypeAdvanceAnim : int
{
	ETAA_Default,
	ETAA_Finished,
	ETAA_Looped
};

// TODO: UAnimDataModel 연동 예정.
class UAnimSequenceBase : public UAnimationAsset
{
	DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)
public:
	// TODO: Pose 추출 구현 예정. 대상 Skeleton 순서의 전체 Local Pose를 채우며 시간은 전진시키지 않는다.
	virtual void GetAnimationPose(FAnimPose& OutPose, const FAnimExtractContext& ExtractionContext) const = 0;
};
