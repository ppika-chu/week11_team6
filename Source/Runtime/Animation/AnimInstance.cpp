#include "EnginePCH.h"
#include "AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimInstance::InitializeAnimation()
{
	UninitializeAnimation();
	bUninitialized = false;
	NativeInitializeAnimation();
}

void UAnimInstance::UninitializeAnimation()
{
	bUninitialized = true;
	NativeUninitializeAnimation();
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}
