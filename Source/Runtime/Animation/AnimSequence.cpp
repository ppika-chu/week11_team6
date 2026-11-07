#include "EnginePCH.h"
#include "AnimSequence.h"
#include "Animation/BonePose.h"
#include "Animation/Skeleton.h"

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

void UAnimSequence::GetBonePose(FCompactPose& OutPose, const FAnimExtractContext& Context) const
{
	const USkeleton* Skeleton = GetSkeleton();
	if (!Skeleton)
	{
		return;
	}

	OutPose.ResetToRefPose(Skeleton->RefSkeleton);
	if (!DataModel)
	{
		return;
	}

	int32 KeyIndex1, KeyIndex2;
	float Alpha;
	GetKeyIndicesFromTime(KeyIndex1, KeyIndex2, Alpha, Context.CurrentTime, DataModel->NumberOfKeys, DataModel->GetPlayLength(), DataModel->FrameRate);

	for (const FBoneAnimationTrack& AnimationTrack : DataModel->BoneAnimationTracks)
	{
		if (AnimationTrack.BoneTreeIndex < 0 || AnimationTrack.BoneTreeIndex >= OutPose.Bones.Num())
		{
			continue;
		}

		const FRawAnimSequenceTrack& Track = AnimationTrack.InternalTrackData;
		const FTransform Key1 = ExtractTransformForKey(Track, KeyIndex1);
		OutPose.Bones[AnimationTrack.BoneTreeIndex] = Alpha > 0.0f
			? FTransform::Blend(Key1, ExtractTransformForKey(Track, KeyIndex2), Alpha)
			: Key1;
	}
}
