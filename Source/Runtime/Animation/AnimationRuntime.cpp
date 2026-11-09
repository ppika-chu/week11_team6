#include "EnginePCH.h"
#include "AnimationRuntime.h"
#include "Math/EngineMath.h"
#include <cassert>
#include <cmath>
#include <utility>

namespace
{
	// lerp의 첫 번째 항만 처리한다 (t*A + (1-t)*B에서 t*A 부분).
	FTransform WeightTransform(const FTransform& Source, float Weight)
	{
		FTransform Result;
		Result.Location = Source.Location * Weight;
		Result.Scale = Source.Scale * Weight;

		// Quaternion도 lerp처럼 성분별로 가중합을 구하고 마지막에 normalize한다.
		// 두 Pose를 혼합할 때는 이 과정이 nlerp가 된다.
		Result.Rotation = FQuat(Source.Rotation.X * Weight, Source.Rotation.Y * Weight, Source.Rotation.Z * Weight, Source.Rotation.W * Weight);
		return Result;
	}

	// lerp의 다음 항 (1-t)*B를 더한다. 여러 Pose를 혼합할 때는 각 Pose의 Weight를 사용한다.
	// 이때 rotation은 최단 경로로 혼합되도록 Quaternion 부호를 맞춘다.
	void AccumulateWithShortestRotation(const FTransform& Source, FTransform& Dest, float Weight)
	{
		const FTransform Weighted = WeightTransform(Source, Weight);

		Dest.Location += Weighted.Location;
		Dest.Scale += Weighted.Scale;

		// 내적이 음수이면 같은 회전을 나타내는 반대 부호를 사용해 최단 경로로 혼합한다.
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
