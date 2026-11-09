#pragma once

#include "Animation/AnimSequenceBase.h"

class UAnimSequence : public UAnimSequenceBase
{
	DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)
public:
	virtual void GetAnimationPose(FAnimPose& OutPose, const FAnimExtractContext& ExtractionContext) const override;

	// Context.CurrentTime 시각의 포즈를 계산한다. 트랙이 없는 본은 기본 자세를 쓴다.
	void GetBonePose(FAnimPose& OutPose, const FAnimExtractContext& Context) const;
};
