#include "EnginePCH.h"
#include "BonePose.h"
#include "Animation/ReferenceSkeleton.h"

namespace
{
	constexpr float ZERO_ANIMWEIGHT_THRESH = 0.00001f;
}

FPoseTransform::FPoseTransform(const FTransform& InTransform)
	: Location(InTransform.Location)
	, Rotation(InTransform.Rotation.Quaternion())
	, Scale(InTransform.Scale)
{
}

void FPoseTransform::Blend(const FPoseTransform& A, const FPoseTransform& B, float Alpha)
{
	if (fabsf(Alpha) <= ZERO_ANIMWEIGHT_THRESH)
	{
		*this = A;
	}
	else if (fabsf(Alpha - 1.0f) <= ZERO_ANIMWEIGHT_THRESH)
	{
		*this = B;
	}
	else
	{
		Location = FMath::Lerp(A.Location, B.Location, Alpha);
		Scale = FMath::Lerp(A.Scale, B.Scale, Alpha);
		Rotation = FQuat::FastLerp(A.Rotation, B.Rotation, Alpha).Normalized();
	}
}

void FCompactPose::ResetToRefPose(const FReferenceSkeleton& RefSkeleton)
{
	const int32 BoneCount = RefSkeleton.GetNum();
	Bones.SetNum(BoneCount);
	for (int32 i = 0; i < BoneCount; ++i)
	{
		Bones[i] = FPoseTransform(RefSkeleton.RefBonePose[i]);
	}
}
