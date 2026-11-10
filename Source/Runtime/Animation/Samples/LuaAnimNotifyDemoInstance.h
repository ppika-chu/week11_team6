#pragma once

#include "Animation/LuaAnimInstance.h"

// Lua Graph는 그대로 사용하고 Named Notify 수신만 추가하는 시연용 Instance.
class ULuaAnimNotifyDemoInstance : public ULuaAnimInstance
{
	DECLARE_CLASS(ULuaAnimNotifyDemoInstance, ULuaAnimInstance)
	FUNCTIONS_START()
		FUNCTION(AnimNotify_FootstepNamed)
	FUNCTIONS_END()

public:
	void AnimNotify_FootstepNamed();

private:
	int32 NotifyCount = 0; // 실행 인스턴스별 호출 횟수. 씬에 저장하지 않는다.
};
