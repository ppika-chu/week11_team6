#pragma once

#include "AnimationAsset.h"
#include "AnimPose.h"

class UAnimSequence;

struct FBlendParameter
{
	FString DisplayName = "None";
	float Min = 0.0f;
	float Max = 100.0f;
};

struct FBlendSample
{
	UAnimSequence* Animation = nullptr;
	FVector SampleValue = FVector::ZeroVector;
	float RateScale = 1.0f;
};

// 재생자별 결과. 공유 Asset에는 현재 시간이나 Sample Cache를 저장하지 않는다.
struct FBlendSampleData
{
	int32 SampleDataIndex = -1;
	UAnimSequence* Animation = nullptr;
	float TotalWeight = 0.0f;
	float SamplePlayRate = 1.0f;
	float PreviousTime = 0.0f;
	float Time = 0.0f;

	float GetClampedWeight() const { return FMath::Clamp(TotalWeight, 0.0f, 1.0f); }
};

struct FWeightedBlendSample
{
	int32 SampleIndex = -1;
	float SampleWeight = 0.0f;
};

struct FBlendSpaceSegment
{
	int32 SampleIndices[2] = { -1, -1 };
	float Vertices[2] = { 0.0f, 0.0f };
};

struct FBlendSpaceData
{
	TArray<FBlendSpaceSegment> Segments;
	// 정규화된 축 입력이 속한 구간을 찾아 양끝 SampleData index와 Weight를 출력한다.
	// OutWeightedSamples는 먼저 비운다. 구간 밖/단일 Sample이면 끝 Sample 하나에 Weight 1을 준다.
	// 구간 안에서는 양끝 둘을 출력하며, 경계에서 생기는 Weight 0도 여기서는 유지한다.
	// InOutSegmentIndex는 이전 구간을 탐색 시작점으로 재사용하고 이번에 찾은 구간으로 갱신한다.
	// Sequence 평가나 시간 전진은 하지 않는다. 출력은 FBlendSampleData가 아닌 특정 FBlendSample에 대한 index/Weight 목록이다.
	void GetSamples1D(TArray<FWeightedBlendSample>& OutWeightedSamples, float NormalizedSamplePosition, int32& InOutSegmentIndex) const;
};

class UBlendSpace : public UAnimationAsset
{
	DECLARE_CLASS(UBlendSpace, UAnimationAsset)
public:
	// Asset 정보 조회 — 재생 위치는 초가 아닌 0~1 진행률이다.
	float GetPlayLength() const override { return 1.0f; }
	const FBlendParameter& GetBlendParameter(int32 Index = 0) const;
	const TArray<FBlendSample>& GetBlendSamples() const { return SampleData; }
	const FBlendSpaceData& GetBlendSpaceData() const { return BlendSpaceData; }

	// 축·Sample 구성 — SetSkeleton 뒤 재생 전에 설정한다. 개별 변경 실패 시 기존 값을 유지한다.
	// 현재는 단일 축만 지원하며, UBlendSpace1D는 X축을 사용한다.
	// TODO: Asset 생성·편집 기능 연결 예정. 축과 Sample을 구성한 뒤 ResampleData를 호출한다.
	bool SetBlendParameter(int32 Index, const FBlendParameter& Parameter);
	int32 AddSample(UAnimSequence* AnimationSequence, const FVector& SampleValue);
	bool EditSampleValue(int32 BlendSampleIndex, const FVector& NewValue);
	bool ReplaceSampleAnimation(int32 BlendSampleIndex, UAnimSequence* AnimationSequence);
	bool SetSampleRateScale(int32 BlendSampleIndex, float RateScale);
	bool DeleteSample(int32 BlendSampleIndex);

	// 검색 자료 재구성 — 설정된 축과 Sample로 1D 구간을 만든다.
	// 위 구성 함수로 변경하면 기존 구간이 무효화되므로 조회 전에 재구성해야 한다.
	void ResampleData();

	// 재생 계산 — 입력으로 Sample·Weight를 선택하고, 혼합 길이와 지정 시각의 Pose를 계산한다.
	// TODO: Player에서 선택 결과의 Sample별 시간을 갱신한 뒤 GetAnimationPose에 전달하도록 연결 예정.
	// Sample Cache는 호출자가 보관한다. 재생 중에는 Asset 구성과 참조한 Skeleton/Sequence의 수명을 유지한다.
	bool GetSamplesFromBlendInput(const FVector& BlendInput, TArray<FBlendSampleData>& OutSampleDataList, int32& InOutCachedTriangulationIndex) const;
	float GetAnimationLengthFromSampleData(const TArray<FBlendSampleData>& SampleDataList) const;
	void GetAnimationPose(const TArray<FBlendSampleData>& BlendSampleDataCache, const FAnimExtractContext& ExtractionContext, FAnimPose& OutPose) const;

protected:
	// 구성 검증과 구간 생성에 사용하는 내부 처리.
	bool IsSampleValid(const FBlendSample& Sample) const;
	bool ValidateSampleValue(const FVector& SampleValue, int32 OriginalIndex = -1) const;
	void ResampleData1D();

	// 공유 구성 데이터 — 파생 Asset이 사용할 축을 지정한다. 현재는 단일 차원만 지원한다.
	FBlendParameter BlendParameters[3];
	TArray<int32> DimensionIndices;
	TArray<FBlendSample> SampleData;

	// 구성 데이터로부터 재구성한 검색 자료.
	FBlendSpaceData BlendSpaceData;
};
