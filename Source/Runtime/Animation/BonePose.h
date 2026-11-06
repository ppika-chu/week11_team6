#pragma once

#include "Containers/Array.h"
#include "Math/Transform.h"

struct FReferenceSkeleton;

// 본 하나의 위치, 회전, 크기. 회전은 FQuat로 저장한다.
struct FPoseTransform
{
	FPoseTransform() = default;
	explicit FPoseTransform(const FTransform& InTransform);

	// A와 B를 Alpha 비율로 섞어 이 값에 저장한다.
	void Blend(const FPoseTransform& A, const FPoseTransform& B, float Alpha);

	FVector Location = FVector(0, 0, 0);
	FQuat Rotation;
	FVector Scale = FVector(1, 1, 1);
};

// 한순간의 모든 본 상태. Bones 순서는 FReferenceSkeleton의 본 번호와 같다.
struct FCompactPose
{
	// 모든 본을 기본 자세로 채운다.
	void ResetToRefPose(const FReferenceSkeleton& RefSkeleton);

	TArray<FPoseTransform> Bones;
};
