#include "EnginePCH.h"
#include "BonePose.h"
#include "Animation/ReferenceSkeleton.h"

void FCompactPose::ResetToRefPose(const FReferenceSkeleton& RefSkeleton)
{
	const int32 BoneCount = RefSkeleton.GetNum();
	Bones.SetNum(BoneCount);
	for (int32 i = 0; i < BoneCount; ++i)
	{
		Bones[i] = RefSkeleton.RefBonePose[i];
	}
}
