#pragma once

#include "Containers/Array.h"
#include "Math/Transform.h"

// 특정 시점의 본별 위치·회전·스케일을 담는 부모 기준 로컬 포즈.
// 배열의 크기와 본 순서는 대상 메시의 RefSkeleton과 일치해야 한다.
struct FAnimPose
{
	TArray<FTransform> LocalTransforms;
};
