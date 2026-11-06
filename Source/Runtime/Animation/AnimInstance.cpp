#include "EnginePCH.h"
#include "AnimInstance.h"
#include "AnimNodes/AnimNode_Base.h"
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
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	if (!IsInitialized())
	{
		return;
	}

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
