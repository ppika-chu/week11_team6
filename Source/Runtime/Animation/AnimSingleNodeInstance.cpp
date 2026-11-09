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
	CurrentTime = FMath::Clamp(InPosition, 0.0f, GetLength());
	// TODO: bFireNotifies에 따른 Seek Notify 전달 구현 예정.
}

float UAnimSingleNodeInstance::GetLength() const
{
	return CurrentAsset ? CurrentAsset->GetPlayLength() : 0.0f;
}

