#include "EnginePCH.h"
#include "AnimInstance.h"
#include "AnimNodes/AnimNode_Base.h"
#include "BlendSpace.h"
#include "AnimSequence.h"
#include "AnimationRuntime.h"
#include "Skeleton.h"
#include "Render/SkeletalMesh.h"
#include "AnimNotifies/AnimNotify.h"
#include "AnimNotifies/AnimNotifyState.h"
#include <utility>
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
	bUninitialized = true;
	RootNode = nullptr;
	NativeUninitializeAnimation();
	EndNotifyStates();
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	if (!IsInitialized())
	{
		return;
	}

	ClearQueuedAnimEvents();
	// 지난 프레임에 아무도 꺼내 가지 않은 변화량은 버린다 (에디터처럼 적용하는 쪽이 없을 때 쌓이지 않게)
	ExtractedRootMotion.Clear();
	DeltaTime = DeltaSeconds;
	NativeUpdateAnimation(DeltaSeconds);
	bNeedsQueuedAnimEventsDispatched = true;
	if (RootNode)
	{
		RootNode->Update(FAnimationUpdateContext{ this, DeltaSeconds });
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

	// 그래프는 스켈레톤 본 순서로 돈다. 공유 스켈레톤은 메시와 본 수, 순서가 다를 수 있다 (UE FBoneContainer)
	// 시작 포즈는 메시에 있는 본은 메시 기준 자세, 없는 본은 스켈레톤 기준 자세다. 컴포넌트가 메시 순서로 바꾼다
	if (USkeleton* Skeleton = Mesh->Skeleton)
	{
		const FSkeletonToMeshLinkup& Linkup = Skeleton->FindOrAddMeshLinkupData(Mesh);
		OutPose.LocalTransforms = Skeleton->RefSkeleton.RefBonePose;
		for (int32 SkeletonIndex = 0; SkeletonIndex < OutPose.LocalTransforms.Num(); ++SkeletonIndex)
		{
			const int32 MeshIndex = Linkup.SkeletonToMeshTable[SkeletonIndex];
			if (MeshIndex != INDEX_NONE)
			{
				OutPose.LocalTransforms[SkeletonIndex] = Mesh->RefSkeleton.RefBonePose[MeshIndex];
			}
		}
	}
	else
	{
		OutPose.LocalTransforms = Mesh->RefSkeleton.RefBonePose;
	}
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
	// UE UAnimSequenceBase::HandleAssetPlayerTickedInternal: 진행한 구간의 루트 변화량을 모은다
	if (RootMotionMode == ERootMotionMode::RootMotionFromEverything && Sequence->HasRootMotion() && MoveDelta != 0.0f)
	{
		ExtractedRootMotion.Accumulate(Sequence->ExtractRootMotion(PreviousTime, MoveDelta, bLooping));
	}
	FAnimNotifyContext Context(bLooping);
	Sequence->GetAnimNotifies(PreviousTime, MoveDelta, Context);
	NotifyQueue.AddAnimNotifies(Context.ActiveNotifies, EffectiveBlendWeight);
}

FRootMotionMovementParams UAnimInstance::ConsumeExtractedRootMotion()
{
	FRootMotionMovementParams RootMotion = ExtractedRootMotion;
	ExtractedRootMotion.Clear();
	return RootMotion;
}

void UAnimInstance::ClearQueuedAnimEvents()
{
	NotifyQueue.Reset();
	bNeedsQueuedAnimEventsDispatched = false;
	++NotifyDispatchSerial;
}

void UAnimInstance::DispatchQueuedAnimEvents()
{
	if (bNeedsQueuedAnimEventsDispatched)
	{
		bNeedsQueuedAnimEventsDispatched = false;
		TriggerAnimNotifies(DeltaTime);
	}
}

void UAnimInstance::TriggerAnimNotifies(float DeltaSeconds)
{
	++NotifyDispatchSerial;
	TArray<FAnimNotifyEventReference> QueuedNotifies = std::move(NotifyQueue.AnimNotifies);
	const uint64 DispatchSerial = NotifyDispatchSerial;
	TArray<FAnimNotifyEvent> NewActiveAnimNotifyState;
	TArray<FAnimNotifyEventReference> NewActiveAnimNotifyEventReference;
	for (const FAnimNotifyEventReference& EventReference : QueuedNotifies)
	{
		const FAnimNotifyEvent* Event = EventReference.GetNotify();
		if (Event && Event->NotifyStateClass && NewActiveAnimNotifyState.Find(*Event) == -1)
		{
			NewActiveAnimNotifyState.Add(*Event);
			NewActiveAnimNotifyEventReference.Add(EventReference);
		}
		if (Event && !Event->NotifyStateClass)
		{
			TriggerSingleAnimNotify(EventReference);
			// Callback은 모드·클립을 바꾸거나 Component를 해제할 수 있다.
			if (FindLiveObjectByAddress(this) != this || NotifyDispatchSerial != DispatchSerial)
			{
				return;
			}
		}
	}

	for (int32 Index = 0; Index < ActiveAnimNotifyState.Num(); ++Index)
	{
		const FAnimNotifyEvent& Event = ActiveAnimNotifyState[Index];
		if (NewActiveAnimNotifyState.Find(Event) == -1)
		{
			FAnimNotifyEventReference Reference = ActiveAnimNotifyEventReference[Index];
			Reference.SetNotify(&Event);
			Event.NotifyStateClass->NotifyEnd(GetSkelMeshComponent(), Cast<UAnimSequenceBase>(const_cast<UObject*>(Reference.GetSourceObject())), Reference);
		}
	}
	for (int32 Index = 0; Index < NewActiveAnimNotifyState.Num(); ++Index)
	{
		const FAnimNotifyEvent& Event = NewActiveAnimNotifyState[Index];
		if (ActiveAnimNotifyState.Find(Event) == -1)
		{
			FAnimNotifyEventReference Reference = NewActiveAnimNotifyEventReference[Index];
			Reference.SetNotify(&Event);
			Event.NotifyStateClass->NotifyBegin(GetSkelMeshComponent(), Cast<UAnimSequenceBase>(const_cast<UObject*>(Reference.GetSourceObject())), Event.GetDuration(), Reference);
		}
	}
	ActiveAnimNotifyState = std::move(NewActiveAnimNotifyState);
	ActiveAnimNotifyEventReference = std::move(NewActiveAnimNotifyEventReference);
	for (int32 Index = 0; Index < ActiveAnimNotifyState.Num(); ++Index)
	{
		const FAnimNotifyEvent& Event = ActiveAnimNotifyState[Index];
		FAnimNotifyEventReference Reference = ActiveAnimNotifyEventReference[Index];
		Reference.SetNotify(&Event);
		Event.NotifyStateClass->NotifyTick(GetSkelMeshComponent(), Cast<UAnimSequenceBase>(const_cast<UObject*>(Reference.GetSourceObject())), DeltaSeconds, Reference);
	}
}

void UAnimInstance::TriggerSingleAnimNotify(const FAnimNotifyEventReference& EventReference)
{
	const FAnimNotifyEvent* Event = EventReference.GetNotify();
	if (!Event)
	{
		return;
	}
	const uint64 DispatchSerial = NotifyDispatchSerial;
	if (HandleNotify(*Event))
	{
		return;
	}
	if (FindLiveObjectByAddress(this) != this || NotifyDispatchSerial != DispatchSerial)
	{
		return;
	}
	if (Event->Notify)
	{
		Event->Notify->Notify(GetSkelMeshComponent(), Cast<UAnimSequenceBase>(const_cast<UObject*>(EventReference.GetSourceObject())), EventReference);
	}
	// TODO: AnimNotify_ 이름 함수 호출 연결 예정. 현재 이름 이벤트는 HandleNotify에서 처리한다.
}

void UAnimInstance::EndNotifyStates()
{
	ClearQueuedAnimEvents();
	TArray<FAnimNotifyEvent> EndedStates = std::move(ActiveAnimNotifyState);
	TArray<FAnimNotifyEventReference> EndedReferences = std::move(ActiveAnimNotifyEventReference);
	for (int32 Index = 0; Index < EndedStates.Num(); ++Index)
	{
		FAnimNotifyEventReference Reference = EndedReferences[Index];
		Reference.SetNotify(&EndedStates[Index]);
		EndedStates[Index].NotifyStateClass->NotifyEnd(GetSkelMeshComponent(), Cast<UAnimSequenceBase>(const_cast<UObject*>(Reference.GetSourceObject())), Reference);
	}
}
