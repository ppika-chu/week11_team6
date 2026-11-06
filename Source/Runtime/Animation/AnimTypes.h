#pragma once

#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Math/EngineMath.h"

// 배열 길이는 1 또는 전체 키 수다. 1이면 클립 내내 같은 값이다.
struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;
};

struct FAnimNotifyEvent
{
	FName NotifyName;
	float TriggerTime = 0.0f;
};
