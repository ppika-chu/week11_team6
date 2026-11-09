#pragma once

#include "Containers/Array.h"
#include "Math/Transform.h"

// 특정 시점의 본별 위치·회전·스케일을 담는 부모 기준 로컬 포즈.
// 배열의 크기와 본 순서는 스켈레톤(USkeleton::RefSkeleton)을 따른다. 스켈레톤이 없는 메시만 메시 순서다.
// 컴포넌트가 FinalizePoseEvaluationResult에서 메시 순서로 바꾼다 (공유 스켈레톤은 메시마다 본 수, 순서가 다르다)
struct FAnimPose
{
	TArray<FTransform> LocalTransforms;
};
