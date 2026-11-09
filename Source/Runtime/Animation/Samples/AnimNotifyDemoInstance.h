#pragma once

#include "Animation/AnimSingleNodeInstance.h"

// 단일 에셋 재생을 그대로 사용하면서 이름 Notify를 받는 시연용 Instance.
class UAnimNotifyDemoInstance : public UAnimSingleNodeInstance
{
	DECLARE_CLASS(UAnimNotifyDemoInstance, UAnimSingleNodeInstance)
	FUNCTIONS_START()
		FUNCTION(AnimNotify_FootstepNamed)
	FUNCTIONS_END()

public:
	void AnimNotify_FootstepNamed();
};
