#include "EnginePCH.h"
#include "AnimationRuntime.h"
#include "Math/EngineMath.h"
#include <cassert>
#include <cmath>
#include <utility>

namespace
{
	// lerp의 첫번째 항만 처리한다 (t*A + (1-t)B 에서 t*A 부분)
	FTransform WeightTransform(const FTransform& Source, float Weight)
	{
		FTransform Result;
		Result.Location = Source.Location * Weight;
		Result.Scale = Source.Scale * Weight;

		// 쿼터니언에 대한 보간은 slerp가 가장 정확하지만
		// 연산이 무겁기때문에, nlerp로 처리한다
		// 여기선 lerp까지만 진행하고 마지막 처리 부분에서 normalize를 처리하는 방식으로 구현한다
		Result.Rotation = FQuat(Source.Rotation.X * Weight, Source.Rotation.Y * Weight, Source.Rotation.Z * Weight, Source.Rotation.W * Weight);
		return Result;
	}

	// lerp의 두번째 항 (1-t)B를 처리한다
	// 이때 rotation에 대한 최단거리 보정을 해준다
	void AccumulateWithShortestRotation(const FTransform& Source, FTransform& Dest, float Weight)
	{
		const FTransform Weighted = WeightTransform(Source, Weight);

		Dest.Location += Weighted.Location;
		Dest.Scale += Weighted.Scale;

		// 쿼터니언 공간의 회전 벡터의 내적값을 통해 최단거리 계산을 한다
		// 쉽게 생각하면 90도를 넘어가는 회전 범위를 90도 안으로 줄이는 작업이라고 생각하면 된다
		const FQuat& Incoming = Weighted.Rotation;
		FQuat& Rotation = Dest.Rotation;
		const float Dot = Rotation.X * Incoming.X + Rotation.Y * Incoming.Y
			+ Rotation.Z * Incoming.Z + Rotation.W * Incoming.W;
		const float Sign = Dot < 0.0f ? -1.0f : 1.0f;
		Rotation.X += Incoming.X * Sign;
		Rotation.Y += Incoming.Y * Sign;
		Rotation.Z += Incoming.Z * Sign;
		Rotation.W += Incoming.W * Sign;
	}
}

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

void FAnimationRuntime::BlendTwoPosesTogetherInPlace(FAnimPose& InOutPoseOne, const FAnimPose& SourcePoseTwo, float WeightOfPoseOne)
{
	const bool bValid = InOutPoseOne.LocalTransforms.Num() == SourcePoseTwo.LocalTransforms.Num()
		&& std::isfinite(WeightOfPoseOne) && WeightOfPoseOne >= 0.0f && WeightOfPoseOne <= 1.0f;
	assert(bValid);
	if (!bValid)
	{
		return;
	}

	for (int32 BoneIndex = 0; BoneIndex < InOutPoseOne.LocalTransforms.Num(); ++BoneIndex)
	{
		// 동일 Pose가 두 인자로 들어와도 원본을 읽은 뒤 결과를 덮어쓴다.
		FTransform Blended = WeightTransform(InOutPoseOne.LocalTransforms[BoneIndex], WeightOfPoseOne);
		AccumulateWithShortestRotation(SourcePoseTwo.LocalTransforms[BoneIndex], Blended, 1.0f - WeightOfPoseOne);
		Blended.Rotation = Blended.Rotation.Normalized();
		InOutPoseOne.LocalTransforms[BoneIndex] = Blended;
	}
}

void FAnimationRuntime::BlendPosesTogether(const TArray<FAnimPose>& SourcePoses, const TArray<float>& SourceWeights, FAnimPose& OutPose)
{
	const bool bValidCount = !SourcePoses.IsEmpty() && SourcePoses.Num() == SourceWeights.Num();
	assert(bValidCount);
	if (!bValidCount)
	{
		return;
	}

	const int32 BoneCount = SourcePoses[0].LocalTransforms.Num();
	float TotalWeight = 0.0f;
	for (int32 PoseIndex = 0; PoseIndex < SourcePoses.Num(); ++PoseIndex)
	{
		const float Weight = SourceWeights[PoseIndex];
		const bool bValid = SourcePoses[PoseIndex].LocalTransforms.Num() == BoneCount
			&& std::isfinite(Weight) && Weight >= 0.0f && Weight <= 1.0f;
		assert(bValid);
		if (!bValid)
		{
			return;
		}
		TotalWeight += Weight;
	}
	const bool bNormalizedWeights = std::abs(TotalWeight - 1.0f) <= KINDA_SMALL_NUMBER;
	assert(bNormalizedWeights);
	if (!bNormalizedWeights)
	{
		return;
	}

	FAnimPose Result;
	Result.LocalTransforms.SetNum(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		FTransform& Blended = Result.LocalTransforms[BoneIndex];
		Blended = WeightTransform(SourcePoses[0].LocalTransforms[BoneIndex], SourceWeights[0]);
		for (int32 PoseIndex = 1; PoseIndex < SourcePoses.Num(); ++PoseIndex)
		{
			AccumulateWithShortestRotation(SourcePoses[PoseIndex].LocalTransforms[BoneIndex], Blended, SourceWeights[PoseIndex]);
		}
		if (SourcePoses.Num() > 1)
		{
			Blended.Rotation = Blended.Rotation.Normalized();
		}
	}
	OutPose = std::move(Result);
}
