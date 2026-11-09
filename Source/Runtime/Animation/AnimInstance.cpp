#include "EnginePCH.h"
#include "AnimInstance.h"
#include "AnimNodes/AnimNode_Base.h"
#include "BlendSpace.h"
#include "AnimSequence.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimInstance::InitializeAnimation()
{
	UninitializeAnimation();
	bUninitialized = false;
	NativeInitializeAnimation();
	RootNode = GetCustomRootNode();
	if (RootNode)
	{
		RootNode->Initialize();
	}
}

void UAnimInstance::UninitializeAnimation()
{
	ClearQueuedAnimEvents();
	bUninitialized = true;
	RootNode = nullptr;
	NativeUninitializeAnimation();
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	if (!IsInitialized())
	{
		return;
	}

	ClearQueuedAnimEvents();
	DeltaTime = DeltaSeconds;
	NativeUpdateAnimation(DeltaSeconds);
	if (RootNode)
	{
		RootNode->Update(DeltaSeconds);
	}
}

void UAnimInstance::EvaluateAnimation(FAnimPose& OutPose)
{
	OutPose.LocalTransforms.Reset();
	USkeletalMeshComponent* Component = GetSkelMeshComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	OutPose.LocalTransforms = Mesh->RefSkeleton.RefBonePose;
	if (IsInitialized() && RootNode)
	{
		RootNode->Evaluate(OutPose);
	}
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

void UAnimInstance::BlendSpaceAdvanceImmediate(UBlendSpace* BlendSpace, const FVector& BlendInput, TArray<FBlendSampleData>& BlendSampleDataCache, bool bLooping, float PlayRate, float DeltaSeconds, float& CurrentTime, int32& CachedTriangulationIndex, float EffectiveBlendWeight)
{
	if (!BlendSpace || !BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleDataCache, CachedTriangulationIndex))
	{
		BlendSampleDataCache.Reset();
		return;
	}
	const float BlendedLength = BlendSpace->GetAnimationLengthFromSampleData(BlendSampleDataCache);
	if (BlendedLength <= 0.0f)
	{
		return;
	}

	// 입력으로 혼합 길이가 바뀌어도 진행률을 유지하고 새 길이를 기준으로 시간을 전진시킨다.
	const float PreviousNormalizedTime = FMath::Clamp(CurrentTime, 0.0f, 1.0f);
	float TimeInSeconds = PreviousNormalizedTime * BlendedLength;
	FAnimationRuntime::AdvanceTime(bLooping, DeltaSeconds * PlayRate, TimeInSeconds, BlendedLength);
	CurrentTime = FMath::Clamp(TimeInSeconds / BlendedLength, 0.0f, 1.0f);

	// 같은 진행률을 각 Sequence 길이에 맞는 시각(초)으로 변환한다.
	for (FBlendSampleData& Sample : BlendSampleDataCache)
	{
		const float SampleLength = Sample.Animation->GetPlayLength();
		Sample.PreviousTime = PreviousNormalizedTime * SampleLength;
		Sample.Time = CurrentTime * SampleLength;
	}
	// Cache는 Weight 내림차순이다. 동률일 때도 첫 Sample 하나만 선택한다.
	const FBlendSampleData& HighestWeightSample = BlendSampleDataCache[0];
	const float NormalizedDelta = bLooping ? DeltaSeconds * PlayRate / BlendedLength : CurrentTime - PreviousNormalizedTime;
	FAnimNotifyContext Context(bLooping);
	HighestWeightSample.Animation->GetAnimNotifies(HighestWeightSample.PreviousTime, NormalizedDelta * HighestWeightSample.Animation->GetPlayLength(), Context);
	NotifyQueue.AddAnimNotifies(Context.ActiveNotifies, EffectiveBlendWeight);
}

void UAnimInstance::SequenceAdvanceImmediate(UAnimSequenceBase* Sequence, bool bLooping, float PlayRate, float DeltaSeconds, float& CurrentTime, float EffectiveBlendWeight)
{
	if (!Sequence)
	{
		return;
	}
	const float PreviousTime = CurrentTime;
	const float MoveDelta = DeltaSeconds * PlayRate * Sequence->RateScale;
	FAnimationRuntime::AdvanceTime(bLooping, MoveDelta, CurrentTime, Sequence->GetPlayLength());
	FAnimNotifyContext Context(bLooping);
	Sequence->GetAnimNotifies(PreviousTime, MoveDelta, Context);
	NotifyQueue.AddAnimNotifies(Context.ActiveNotifies, EffectiveBlendWeight);
}

void UAnimInstance::ClearQueuedAnimEvents()
{
	NotifyQueue.Reset();
	bNeedsQueuedAnimEventsDispatched = false;
	++NotifyDispatchSerial;
}
