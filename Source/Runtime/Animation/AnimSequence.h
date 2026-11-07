#pragma once

#include "Animation/AnimSequenceBase.h"

struct FCompactPose;

class UAnimSequence : public UAnimSequenceBase
{
	DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)
public:
	// Context.CurrentTime 시각의 포즈를 계산한다. 트랙이 없는 본은 기본 자세를 쓴다.
	void GetBonePose(FCompactPose& OutPose, const FAnimExtractContext& Context) const;
};
