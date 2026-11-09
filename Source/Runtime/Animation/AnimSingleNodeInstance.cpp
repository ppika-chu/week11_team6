#include "EnginePCH.h"
#include "AnimSingleNodeInstance.h"
#include "AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Math/EngineMath.h"
#include <cmath>

void UAnimSingleNodeInstance::SetAnimationAsset(UAnimationAsset* NewAsset, bool bInIsLooping, float InPlayRate)
{
	ClearQueuedAnimEvents();
	USkeletalMeshComponent* Component = GetSkelMeshComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	// 전체 Bone 배열을 직접 사용하므로 같은 Skeleton의 Sequence만 지원한다.
	CurrentAsset = Mesh && NewAsset && NewAsset->GetSkeleton() && NewAsset->GetSkeleton() == Mesh->Skeleton && Cast<UAnimSequenceBase>(NewAsset) ? NewAsset : nullptr;
	bLooping = bInIsLooping;
	PlayRate = InPlayRate;
	CurrentTime = 0.0f;
}

void UAnimSingleNodeInstance::NativeInitializeAnimation()
{
	SingleNode.Instance = this;
	SetAnimationAsset(CurrentAsset, bLooping, PlayRate);
}

void UAnimSingleNodeInstance::PlayAnim(bool bIsLooping, float InPlayRate, float InStartPosition)
{
	SetPlaying(true);
	SetLooping(bIsLooping);
	SetPlayRate(InPlayRate);
	SetPosition(InStartPosition);
}

void UAnimSingleNodeInstance::SetReverse(bool bInReverse)
{
	bReverse = bInReverse;
	PlayRate = bReverse ? -std::abs(PlayRate) : std::abs(PlayRate);
}

void UAnimSingleNodeInstance::SetPosition(float InPosition, bool bFireNotifies)
{
	const float PreviousTime = CurrentTime;
	CurrentTime = FMath::Clamp(InPosition, 0.0f, GetLength());
	if (bFireNotifies)
	{
		// Seek는 Loop 재생이 아니라 이전·새 시각 사이를 한 번 조회한다.
		ClearQueuedAnimEvents();
		if (auto* Sequence = Cast<UAnimSequenceBase>(CurrentAsset))
		{
			FAnimNotifyContext Context;
			Sequence->GetAnimNotifiesFromDeltaPositions(PreviousTime, CurrentTime, Context);
			NotifyQueue.AddAnimNotifies(Context.ActiveNotifies, 1.0f);
		}
		// Tick을 기다리지 않고 전달한다. Pose는 아직 평가 전이며 경과 시간은 0이다.
		TriggerAnimNotifies(0.0f);
	}
}

float UAnimSingleNodeInstance::GetLength() const
{
	return CurrentAsset ? CurrentAsset->GetPlayLength() : 0.0f;
}

