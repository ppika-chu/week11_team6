#pragma once

#include "AnimSequenceBase.h"

class FAnimationRuntime
{
public:
	// 시간 단위는 초. MoveDelta는 재생 속도와 방향이 반영된 이동량이다.
	// EndTime은 0 이상이며, 정확히 양 끝에 도착한 경우는 ETAA_Default다.
	static ETypeAdvanceAnim AdvanceTime(bool bAllowLooping, float MoveDelta, float& InOutTime, float EndTime);
};
