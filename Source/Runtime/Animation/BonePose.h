#pragma once

#include "Containers/Array.h"
#include "Math/Transform.h"

struct FReferenceSkeleton;

// 한순간의 모든 본 상태. Bones 순서는 FReferenceSkeleton의 본 번호와 같다.
struct FCompactPose
{
	// 모든 본을 기본 자세로 채운다.
	void ResetToRefPose(const FReferenceSkeleton& RefSkeleton);

	TArray<FTransform> Bones;
};
