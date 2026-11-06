#pragma once

#include "Animation/AnimSequenceBase.h"

struct FCompactPose;

class UAnimSequence : public UAnimSequenceBase
{
	DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)
public:
	// 7단계에서 구현한다.
	void GetBonePose(FCompactPose& OutPose, const FAnimExtractContext& Context) const;
};
