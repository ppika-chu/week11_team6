#pragma once

#include "Animation/LuaAnimInstance.h"

// 그래프는 Lua가 구성하고, 투구 이름 이벤트만 같은 Actor의 스크립트로 전달한다.
class UPitchingAnimInstance : public ULuaAnimInstance
{
	DECLARE_CLASS(UPitchingAnimInstance, ULuaAnimInstance)
	FUNCTIONS_START()
		FUNCTION(AnimNotify_ReleaseBall)
	FUNCTIONS_END()

public:
	void AnimNotify_ReleaseBall();
};
