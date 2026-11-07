#include "EnginePCH.h"
#include "BlendSpace.h"
#include "AnimSequence.h"
#include "AnimationRuntime.h"
#include "Skeleton.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace
{
	constexpr float SampleWeightThreshold = 0.00001f;
}

void FBlendSpaceData::GetSamples1D(TArray<FWeightedBlendSample>& OutWeightedSamples, float NormalizedSamplePosition, int32& InOutSegmentIndex) const
{
	OutWeightedSamples.Reset();
	if (Segments.IsEmpty() || !std::isfinite(NormalizedSamplePosition))
	{
		InOutSegmentIndex = -1;
		return;
	}
	if (Segments.Num() == 1 && Segments[0].SampleIndices[0] == Segments[0].SampleIndices[1])
	{
		InOutSegmentIndex = 0;
		OutWeightedSamples.Add({ Segments[0].SampleIndices[0], 1.0f });
		return;
	}
	if (!Segments.IsValidIndex(InOutSegmentIndex))
	{
		InOutSegmentIndex = Segments.Num() / 2;
	}

	for (int32 Attempt = 0; Attempt < Segments.Num(); ++Attempt)
	{
		const FBlendSpaceSegment& Segment = Segments[InOutSegmentIndex];
		if (NormalizedSamplePosition < Segment.Vertices[0])
		{
			if (InOutSegmentIndex == 0)
			{
				OutWeightedSamples.Add({ Segment.SampleIndices[0], 1.0f });
				return;
			}
			--InOutSegmentIndex;
		}
		else if (NormalizedSamplePosition > Segment.Vertices[1])
		{
			if (InOutSegmentIndex == Segments.Num() - 1)
			{
				OutWeightedSamples.Add({ Segment.SampleIndices[1], 1.0f });
				return;
			}
			++InOutSegmentIndex;
		}
		else
		{
			const float Fraction = FMath::Clamp((NormalizedSamplePosition - Segment.Vertices[0]) / (Segment.Vertices[1] - Segment.Vertices[0]), 0.0f, 1.0f);
			OutWeightedSamples.Add({ Segment.SampleIndices[0], 1.0f - Fraction });
			OutWeightedSamples.Add({ Segment.SampleIndices[1], Fraction });
			return;
		}
	}
}

const FBlendParameter& UBlendSpace::GetBlendParameter(int32 Index) const
{
	assert(Index >= 0 && Index < 3);
	return BlendParameters[Index];
}

bool UBlendSpace::SetBlendParameter(int32 Index, const FBlendParameter& Parameter)
{
	const float Range = Parameter.Max - Parameter.Min;
	if (DimensionIndices.Num() != 1 || Index != DimensionIndices[0] || !std::isfinite(Parameter.Min) || !std::isfinite(Parameter.Max) || !std::isfinite(Range) || Range <= 0.0f)
	{
		return false;
	}
	for (const FBlendSample& Sample : SampleData)
	{
		if (Sample.SampleValue[Index] < Parameter.Min || Sample.SampleValue[Index] > Parameter.Max)
		{
			return false;
		}
	}
	BlendParameters[Index] = Parameter;
	BlendSpaceData.Segments.Reset();
	return true;
}

bool UBlendSpace::ValidateSampleValue(const FVector& SampleValue, int32 OriginalIndex) const
{
	if (DimensionIndices.Num() != 1)
	{
		return false;
	}
	const int32 Axis = DimensionIndices[0];
	const FBlendParameter& Parameter = BlendParameters[Axis];
	if (!std::isfinite(SampleValue[Axis]) || SampleValue[Axis] < Parameter.Min || SampleValue[Axis] > Parameter.Max)
	{
		return false;
	}
	for (int32 OtherAxis = 0; OtherAxis < 3; ++OtherAxis)
	{
		if (OtherAxis != Axis && SampleValue[OtherAxis] != 0.0f)
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < SampleData.Num(); ++Index)
	{
		if (Index != OriginalIndex && SampleData[Index].SampleValue[Axis] == SampleValue[Axis])
		{
			return false;
		}
	}
	return true;
}

int32 UBlendSpace::AddSample(UAnimSequence* AnimationSequence, const FVector& SampleValue)
{
	FBlendSample Sample;
	Sample.Animation = AnimationSequence;
	Sample.SampleValue = SampleValue;
	if (!IsSampleValid(Sample) || !ValidateSampleValue(SampleValue))
	{
		return -1;
	}
	SampleData.Add(Sample);
	BlendSpaceData.Segments.Reset();
	return SampleData.Num() - 1;
}

bool UBlendSpace::EditSampleValue(int32 BlendSampleIndex, const FVector& NewValue)
{
	if (!SampleData.IsValidIndex(BlendSampleIndex) || !ValidateSampleValue(NewValue, BlendSampleIndex))
	{
		return false;
	}
	SampleData[BlendSampleIndex].SampleValue = NewValue;
	BlendSpaceData.Segments.Reset();
	return true;
}

bool UBlendSpace::ReplaceSampleAnimation(int32 BlendSampleIndex, UAnimSequence* AnimationSequence)
{
	if (!SampleData.IsValidIndex(BlendSampleIndex))
	{
		return false;
	}
	FBlendSample Sample = SampleData[BlendSampleIndex];
	Sample.Animation = AnimationSequence;
	if (!IsSampleValid(Sample))
	{
		return false;
	}
	SampleData[BlendSampleIndex] = Sample;
	BlendSpaceData.Segments.Reset();
	return true;
}

bool UBlendSpace::SetSampleRateScale(int32 BlendSampleIndex, float RateScale)
{
	if (!SampleData.IsValidIndex(BlendSampleIndex))
	{
		return false;
	}
	FBlendSample Sample = SampleData[BlendSampleIndex];
	Sample.RateScale = RateScale;
	if (!IsSampleValid(Sample))
	{
		return false;
	}
	SampleData[BlendSampleIndex] = Sample;
	BlendSpaceData.Segments.Reset();
	return true;
}

bool UBlendSpace::DeleteSample(int32 BlendSampleIndex)
{
	if (!SampleData.IsValidIndex(BlendSampleIndex))
	{
		return false;
	}
	SampleData.RemoveAtSwap(BlendSampleIndex);
	BlendSpaceData.Segments.Reset();
	return true;
}

void UBlendSpace::ResampleData()
{
	// Sample 삭제나 이동으로 기존 index/구간이 달라지므로 재구성 전에 검색 자료를 비운다.
	BlendSpaceData.Segments.Reset();
	if (DimensionIndices.Num() == 1)
	{
		ResampleData1D();
	}
}

bool UBlendSpace::IsSampleValid(const FBlendSample& Sample) const
{
	if (!GetSkeleton() || !Sample.Animation || Sample.Animation->GetSkeleton() != GetSkeleton())
	{
		return false;
	}
	const float Length = Sample.Animation->GetPlayLength();
	const float SequenceRate = Sample.Animation->RateScale;
	if (!std::isfinite(Length) || Length <= 0.0f || !std::isfinite(SequenceRate) || SequenceRate <= 0.0f || !std::isfinite(Sample.RateScale) || Sample.RateScale <= 0.0f)
	{
		return false;
	}
	const float EffectiveLength = static_cast<float>(static_cast<double>(Length) / (static_cast<double>(SequenceRate) * Sample.RateScale));
	return std::isfinite(EffectiveLength) && EffectiveLength > 0.0f;
}

bool UBlendSpace::GetSamplesFromBlendInput(const FVector& BlendInput, TArray<FBlendSampleData>& OutSampleDataList, int32& InOutCachedTriangulationIndex) const
{
	OutSampleDataList.Reset();
	if (DimensionIndices.Num() != 1)
	{
		InOutCachedTriangulationIndex = -1;
		return false;
	}
	const int32 Axis = DimensionIndices[0];
	if (!std::isfinite(BlendInput[Axis]))
	{
		return false;
	}
	const FBlendParameter& Parameter = BlendParameters[Axis];
	const float Range = Parameter.Max - Parameter.Min;
	if (!std::isfinite(Range) || Range <= 0.0f)
	{
		return false;
	}
	const float NormalizedInput = (FMath::Clamp(BlendInput[Axis], Parameter.Min, Parameter.Max) - Parameter.Min) / Range;
	TArray<FWeightedBlendSample> WeightedSamples;
	BlendSpaceData.GetSamples1D(WeightedSamples, NormalizedInput, InOutCachedTriangulationIndex);
	float TotalWeight = 0.0f;
	for (const FWeightedBlendSample& WeightedSample : WeightedSamples)
	{
		if (WeightedSample.SampleWeight <= SampleWeightThreshold)
		{
			continue;
		}
		const FBlendSample& Sample = SampleData[WeightedSample.SampleIndex];
		FBlendSampleData Result;
		Result.SampleDataIndex = WeightedSample.SampleIndex;
		Result.Animation = Sample.Animation;
		Result.TotalWeight = WeightedSample.SampleWeight;
		Result.SamplePlayRate = Sample.RateScale;
		OutSampleDataList.Add(Result);
		TotalWeight += Result.TotalWeight;
	}
	std::stable_sort(OutSampleDataList.begin(), OutSampleDataList.end(), [](const FBlendSampleData& A, const FBlendSampleData& B) { return A.TotalWeight > B.TotalWeight; });
	for (FBlendSampleData& Sample : OutSampleDataList)
	{
		Sample.TotalWeight /= TotalWeight;
	}
	return !OutSampleDataList.IsEmpty();
}

float UBlendSpace::GetAnimationLengthFromSampleData(const TArray<FBlendSampleData>& SampleDataList) const
{
	double BlendedNormalizedSpeed = 0.0;
	for (const FBlendSampleData& SampleEntry : SampleDataList)
	{
		BlendedNormalizedSpeed += static_cast<double>(SampleEntry.TotalWeight) * SampleEntry.Animation->RateScale * SampleEntry.SamplePlayRate / SampleEntry.Animation->GetPlayLength();
	}
	if (BlendedNormalizedSpeed <= 0.0)
	{
		return 0.0f;
	}
	const float Length = static_cast<float>(1.0 / BlendedNormalizedSpeed);
	return std::isfinite(Length) && Length > 0.0f ? Length : 0.0f;
}

void UBlendSpace::GetAnimationPose(const TArray<FBlendSampleData>& BlendSampleDataCache, const FAnimExtractContext& ExtractionContext, FAnimPose& OutPose) const
{
	OutPose.LocalTransforms.Reset();
	if (!GetSkeleton())
	{
		return;
	}
	OutPose.LocalTransforms = GetSkeleton()->RefSkeleton.RefBonePose;
	if (BlendSampleDataCache.IsEmpty())
	{
		return;
	}

	TArray<FAnimPose> ChildPoses;
	TArray<float> ChildWeights;
	for (const FBlendSampleData& SampleEntry : BlendSampleDataCache)
	{
		FAnimExtractContext ChildContext = ExtractionContext;
		// 길이가 다른 Sequence는 같은 진행률에서도 읽을 시각(초)이 다르므로 Sample별 시간을 사용한다.
		ChildContext.CurrentTime = FMath::Clamp(SampleEntry.Time, 0.0f, SampleEntry.Animation->GetPlayLength());
		FAnimPose ChildPose = OutPose;
		SampleEntry.Animation->GetAnimationPose(ChildPose, ChildContext);
		ChildPoses.Add(std::move(ChildPose));
		ChildWeights.Add(SampleEntry.TotalWeight);
	}
	FAnimationRuntime::BlendPosesTogether(ChildPoses, ChildWeights, OutPose);
}

void UBlendSpace::ResampleData1D()
{
	const int32 Axis = DimensionIndices[0];
	const FBlendParameter& Parameter = BlendParameters[Axis];
	const float Range = Parameter.Max - Parameter.Min;
	if (!GetSkeleton() || !std::isfinite(Parameter.Min) || !std::isfinite(Parameter.Max) || !std::isfinite(Range) || Range <= 0.0f)
	{
		return;
	}
	TArray<int32> SortedIndices;
	for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
	{
		const FBlendSample& Sample = SampleData[SampleIndex];
		if (!IsSampleValid(Sample))
		{
			return;
		}
		SortedIndices.Add(SampleIndex);
	}
	std::sort(SortedIndices.begin(), SortedIndices.end(), [this, Axis](int32 A, int32 B) { return SampleData[A].SampleValue[Axis] < SampleData[B].SampleValue[Axis]; });

	FBlendSpaceData NewData;
	if (SortedIndices.Num() == 1)
	{
		FBlendSpaceSegment Segment;
		Segment.SampleIndices[0] = Segment.SampleIndices[1] = SortedIndices[0];
		Segment.Vertices[1] = 1.0f;
		NewData.Segments.Add(Segment);
	}
	for (int32 Index = 1; Index < SortedIndices.Num(); ++Index)
	{
		FBlendSpaceSegment Segment;
		Segment.SampleIndices[0] = SortedIndices[Index - 1];
		Segment.SampleIndices[1] = SortedIndices[Index];
		Segment.Vertices[0] = (SampleData[Segment.SampleIndices[0]].SampleValue[Axis] - Parameter.Min) / Range;
		Segment.Vertices[1] = (SampleData[Segment.SampleIndices[1]].SampleValue[Axis] - Parameter.Min) / Range;
		if (Segment.Vertices[0] >= Segment.Vertices[1])
		{
			return;
		}
		NewData.Segments.Add(Segment);
	}
	BlendSpaceData = std::move(NewData);
	return;
}
