#include "EnginePCH.h"
#include "AnimSingleNodeInstance.h"
#include "AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Math/EngineMath.h"
#include <cmath>

void UAnimSingleNodeInstance::SetAnimationAsset(UAnimationAsset* NewAsset, bool bInIsLooping, float InPlayRate)
{
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

void UAnimSingleNodeInstance::StepForward()
{
	const auto* Sequence = Cast<UAnimSequenceBase>(CurrentAsset);
	if (!Sequence || !Sequence->DataModel || Sequence->DataModel->FrameRate <= 0.0f)
	{
		return;
	}

	const float FrameRate = Sequence->DataModel->FrameRate;
	const int32 LastSequenceFrameNumber = static_cast<int32>(std::lround(Sequence->GetPlayLength() * FrameRate));

	// 조금 앞으로 간 뒤 다음 프레임으로 올림한다
	int32 StepToFrame = static_cast<int32>(std::ceil((CurrentTime + KINDA_SMALL_NUMBER) * FrameRate));
	if (IsLooping())
	{
		// 끝을 넘으면 처음으로
		StepToFrame %= (LastSequenceFrameNumber + 1);
	}
	StepToFrame = FMath::Clamp(StepToFrame, 0, LastSequenceFrameNumber);

	SetPosition(static_cast<float>(StepToFrame) / FrameRate);
}

void UAnimSingleNodeInstance::StepBackward()
{
	const auto* Sequence = Cast<UAnimSequenceBase>(CurrentAsset);
	if (!Sequence || !Sequence->DataModel || Sequence->DataModel->FrameRate <= 0.0f)
	{
		return;
	}

	const float FrameRate = Sequence->DataModel->FrameRate;
	const int32 LastSequenceFrameNumber = static_cast<int32>(std::lround(Sequence->GetPlayLength() * FrameRate));

	// 조금 뒤로 간 뒤 이전 프레임으로 내림한다
	int32 StepToFrame = static_cast<int32>(std::floor((CurrentTime - KINDA_SMALL_NUMBER) * FrameRate));
	if (IsLooping() && StepToFrame < 0)
	{
		// 처음을 넘으면 끝으로
		StepToFrame = LastSequenceFrameNumber;
	}
	StepToFrame = FMath::Clamp(StepToFrame, 0, LastSequenceFrameNumber);

	SetPosition(static_cast<float>(StepToFrame) / FrameRate);
}

float UAnimSingleNodeInstance::GetLength() const
{
	return CurrentAsset ? CurrentAsset->GetPlayLength() : 0.0f;
}

