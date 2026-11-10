#include "EnginePCH.h"
#include "AnimSequence.h"
#include "Animation/AnimPose.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationRuntime.h"

namespace
{
	// 시각에 해당하는 앞뒤 키 번호와 두 키 사이의 비율을 구한다. (UE FAnimationRuntime::GetKeyIndicesFromTime)
	void GetKeyIndicesFromTime(int32& OutKeyIndex1, int32& OutKeyIndex2, float& OutAlpha, double Time, int32 NumKeys, double SequenceLength, double FramesPerSecond)
	{
		OutAlpha = 0.0f;
		if (Time <= 0.0 || NumKeys == 1)
		{
			OutKeyIndex1 = 0;
			OutKeyIndex2 = 0;
			return;
		}

		const int32 LastIndex = NumKeys - 1;
		if (Time >= SequenceLength)
		{
			OutKeyIndex1 = LastIndex;
			OutKeyIndex2 = LastIndex;
			return;
		}

		const double KeyPos = Time * FramesPerSecond;
		OutKeyIndex1 = FMath::Clamp(static_cast<int32>(std::floor(KeyPos)), 0, LastIndex);
		OutKeyIndex2 = FMath::Min(OutKeyIndex1 + 1, LastIndex);
		OutAlpha = static_cast<float>(KeyPos - OutKeyIndex1);
	}

	// 배열 길이가 1이면 클립 내내 같은 값이므로 0번 키를 쓴다. (UE FAnimationUtils::ExtractTransformForFrameFromTrack)
	FTransform ExtractTransformForKey(const FRawAnimSequenceTrack& Track, int32 KeyIndex)
	{
		return FTransform(
			Track.RotKeys[FMath::Min(KeyIndex, Track.RotKeys.Num() - 1)],
			Track.PosKeys[FMath::Min(KeyIndex, Track.PosKeys.Num() - 1)],
			Track.ScaleKeys[FMath::Min(KeyIndex, Track.ScaleKeys.Num() - 1)]);
	}
}

void UAnimSequence::GetAnimationPose(FAnimPose& OutPose, const FAnimExtractContext& ExtractionContext) const
{
	GetBonePose(OutPose, ExtractionContext);
}

namespace
{
	// 그 시각 트랙 하나의 변환. 두 키 사이는 보간한다
	FTransform ExtractTrackTransform(const FRawAnimSequenceTrack& Track, const UAnimDataModel& DataModel, double Time)
	{
		int32 KeyIndex1, KeyIndex2;
		float Alpha;
		GetKeyIndicesFromTime(KeyIndex1, KeyIndex2, Alpha, Time, DataModel.NumberOfKeys, DataModel.GetPlayLength(), DataModel.FrameRate);
		const FTransform Key1 = ExtractTransformForKey(Track, KeyIndex1);
		return Alpha > 0.0f ? FTransform::Blend(Key1, ExtractTransformForKey(Track, KeyIndex2), Alpha) : Key1;
	}

	const FRawAnimSequenceTrack* FindRootTrack(const UAnimDataModel* DataModel)
	{
		if (DataModel)
		{
			for (const FBoneAnimationTrack& AnimationTrack : DataModel->BoneAnimationTracks)
			{
				if (AnimationTrack.BoneTreeIndex == 0)
				{
					return &AnimationTrack.InternalTrackData;
				}
			}
		}
		return nullptr;
	}
}

void UAnimSequence::GetBonePose(FAnimPose& OutPose, const FAnimExtractContext& Context) const
{
	const USkeleton* Skeleton = GetSkeleton();
	if (!Skeleton)
	{
		return;
	}

	// 인스턴스가 메시 기준 자세로 채워 넘긴 포즈면 트랙 없는 본에 그 값을 쓴다. 빈 포즈면 스켈레톤 기준 자세로 시작한다
	if (OutPose.LocalTransforms.Num() != Skeleton->RefSkeleton.GetNum())
	{
		OutPose.LocalTransforms = Skeleton->RefSkeleton.RefBonePose;
	}
	if (!DataModel)
	{
		return;
	}

	for (const FBoneAnimationTrack& AnimationTrack : DataModel->BoneAnimationTracks)
	{
		if (AnimationTrack.BoneTreeIndex < 0 || AnimationTrack.BoneTreeIndex >= OutPose.LocalTransforms.Num())
		{
			continue;
		}
		OutPose.LocalTransforms[AnimationTrack.BoneTreeIndex] = ExtractTrackTransform(AnimationTrack.InternalTrackData, *DataModel, Context.CurrentTime);
	}

	// 꺼낸 이동은 액터가 하므로 루트 본은 고정 위치로 되돌린다 (UE FRootMotionReset::ResetRootBoneForRootMotion)
	if (!OutPose.LocalTransforms.IsEmpty() && ((Context.bExtractRootMotion && bEnableRootMotion) || bForceRootLock))
	{
		FTransform& RootTransform = OutPose.LocalTransforms[0];
		switch (RootMotionRootLock)
		{
		case ERootMotionRootLock::AnimFirstFrame:
			RootTransform = ExtractRootTrackTransform(0.0f);
			break;
		case ERootMotionRootLock::Zero:
			RootTransform = FTransform::Identity;
			break;
		default:
			RootTransform = Skeleton->RefSkeleton.RefBonePose[0];
			break;
		}
	}
}

FTransform UAnimSequence::ExtractRootTrackTransform(float Time) const
{
	const USkeleton* Skeleton = GetSkeleton();
	const FRawAnimSequenceTrack* RootTrack = FindRootTrack(DataModel);
	if (RootTrack)
	{
		return ExtractTrackTransform(*RootTrack, *DataModel, Time);
	}
	return Skeleton && Skeleton->RefSkeleton.GetNum() > 0 ? Skeleton->RefSkeleton.RefBonePose[0] : FTransform::Identity;
}

FTransform UAnimSequence::ExtractRootMotionFromRange(float StartTime, float EndTime) const
{
	const USkeleton* Skeleton = GetSkeleton();
	if (!Skeleton || Skeleton->RefSkeleton.GetNum() == 0)
	{
		return FTransform::Identity;
	}

	// UE bUseNormalizedRootMotionScale: 크기는 GetRelativeTransform을 망가뜨리므로 버린다
	FTransform StartTransform = ExtractRootTrackTransform(StartTime);
	FTransform EndTransform = ExtractRootTrackTransform(EndTime);
	StartTransform.Scale = FVector(1.0f, 1.0f, 1.0f);
	EndTransform.Scale = FVector(1.0f, 1.0f, 1.0f);

	// 루트 기준 자세에 대한 상대값으로 바꿔 컴포넌트 공간 변화량으로 만든다.
	// 루트가 바닥이 아닌 골반(Mixamo Hips)이어도 컴포넌트를 옮긴 결과와 루트 고정 결과가 맞물린다
	const FTransform RootToComponent = Skeleton->RefSkeleton.RefBonePose[0].Inverse();
	StartTransform = RootToComponent * StartTransform;
	EndTransform = RootToComponent * EndTransform;
	return EndTransform.GetRelativeTransform(StartTransform);
}

FTransform UAnimSequence::ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;
	if (DeltaTime == 0.0f)
	{
		return FTransform::Identity;
	}

	// UE UAnimSequence::ExtractRootMotion: 끝(역재생이면 처음)까지 끊어 구하고, 반복이면 반대쪽 끝에서 이어 간다
	const bool bPlayingBackwards = DeltaTime < 0.0f;
	const float Length = GetPlayLength();
	float PreviousPosition = StartTime;
	float CurrentPosition = StartTime;
	float DesiredDeltaMove = DeltaTime;
	// 아주 짧은 클립을 큰 Delta로 돌 때 멈추지 않도록 횟수를 제한한다
	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, Length);
		RootMotionParams.Accumulate(ExtractRootMotionFromRange(PreviousPosition, CurrentPosition));

		if (AdvanceType != ETAA_Finished || !bAllowLooping || Length <= 0.0f)
		{
			break;
		}
		DesiredDeltaMove -= CurrentPosition - PreviousPosition;
		PreviousPosition = bPlayingBackwards ? Length : 0.0f;
		CurrentPosition = PreviousPosition;
	}
	return RootMotionParams.GetRootMotionTransform();
}
