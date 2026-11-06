#include "EnginePCH.h"
#include "AnimationRuntime.h"
#include "Math/EngineMath.h"
#include <cmath>

ETypeAdvanceAnim FAnimationRuntime::AdvanceTime(bool bAllowLooping, float MoveDelta, float& InOutTime, float EndTime)
{
	float NewTime = InOutTime + MoveDelta;
	if (NewTime < 0.0f || NewTime > EndTime)
	{
		if (!bAllowLooping)
		{
			InOutTime = FMath::Clamp(NewTime, 0.0f, EndTime);
			return ETAA_Finished;
		}

		if (EndTime != 0.0f)
		{
			NewTime = std::fmod(NewTime, EndTime);
			// 역재생의 음수 나머지를 클립 안의 시각으로 옮긴다.
			if (NewTime < 0.0f)
			{
				NewTime += EndTime;
			}
		}
		else
		{
			NewTime = 0.0f;
		}
		InOutTime = NewTime;
		return ETAA_Looped;
	}

	InOutTime = NewTime;
	return ETAA_Default;
}
