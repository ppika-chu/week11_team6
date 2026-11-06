#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimPose.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetSkeletalMesh(UAssetManager::GetAssetByPath<USkeletalMesh>("Models/AllMight/SK_AllMight_root.fbxbin"));
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ClearAnimScriptInstance();
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* NewClass)
{
	if (NewClass && (!NewClass->IsChildOf(UAnimInstance::StaticClass()) || !NewClass->Constructor))
	{
		return;
	}
	if (AnimClass == NewClass)
	{
		return;
	}

	AnimClass = NewClass;
	ClearAnimScriptInstance();
	InitAnim(true);
}

void USkeletalMeshComponent::InitAnim(bool bForceReinit)
{
	if (IsRegistered() && SkeletalMesh)
	{
		InitializeAnimScriptInstance(bForceReinit);
	}
}

bool USkeletalMeshComponent::InitializeAnimScriptInstance(bool bForceReinit)
{
	if (!AnimClass)
	{
		return false;
	}
	if (!AnimScriptInstance)
	{
		AnimScriptInstance = NewObject<UAnimInstance>(this, AnimClass);
		bForceReinit = true;
	}
	if (AnimScriptInstance && (bForceReinit || !AnimScriptInstance->IsInitialized()))
	{
		AnimScriptInstance->InitializeAnimation();
		return true;
	}
	return false;
}

void USkeletalMeshComponent::ClearAnimScriptInstance()
{
	// 전역 종료 정리에서는 소유 Component보다 Instance가 먼저 삭제될 수 있다.
	if (AnimScriptInstance && FindLiveObjectByAddress(AnimScriptInstance))
	{
		AnimScriptInstance->UninitializeAnimation();
		delete AnimScriptInstance;
	}
	AnimScriptInstance = nullptr;
}

void USkeletalMeshComponent::OnRegister()
{
	EnsurePoseInitialized();
	Super::OnRegister();
	InitAnim(true);
}

void USkeletalMeshComponent::OnUnregister()
{
	if (AnimScriptInstance)
	{
		AnimScriptInstance->UninitializeAnimation();
	}
	Super::OnUnregister();
}

void USkeletalMeshComponent::InitializeComponent()
{
	Super::InitializeComponent();
	InitAnim(false);
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	TickAnimation(DeltaTime);
	if (IsRegistered() && AnimScriptInstance && AnimScriptInstance->IsInitialized())
	{
		RefreshBoneTransforms();
	}
}

void USkeletalMeshComponent::TickAnimation(float DeltaTime)
{
	EnsurePoseInitialized();
	if (IsRegistered() && SkeletalMesh && AnimScriptInstance)
	{
		AnimScriptInstance->UpdateAnimation(DeltaTime);
	}
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& LocalTransform)
{
	EnsurePoseInitialized();
	if (!IsValidBoneIndex(BoneIndex))
	{
		return;
	}

	BoneSpaceTransforms[BoneIndex] = LocalTransform;
	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::SetBoneRotation(int32 BoneIndex, const FRotator& Rotation)
{
	EnsurePoseInitialized();
	if (!IsValidBoneIndex(BoneIndex))
	{
		return;
	}

	// 행벡터 규칙: 먼저 본 원점에서 회전, 그다음 기준 자세(부모까지의 거리 포함)
	// 위치 / 스케일은 기준 자세 그대로 두고 회전만 Rotate * RefRotation으로 합친다 (비균등 스케일이면 근사)
	const FTransform& Ref = SkeletalMesh->RefSkeleton.RefBonePose[BoneIndex];
	const FMatrix Rotate = Rotation.Quaternion().ToFMatrix();
	BoneSpaceTransforms[BoneIndex] = Ref;
	BoneSpaceTransforms[BoneIndex].Rotation = MatrixToRotator(Rotate * Ref.Rotation.ToFMatrix());
	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::ResetToRefPose()
{
	EnsurePoseInitialized();
	if (!SkeletalMesh)
	{
		return;
	}

	BoneSpaceTransforms = SkeletalMesh->RefSkeleton.RefBonePose;
	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	if (!SkeletalMesh)
	{
		return;
	}

	if (AnimScriptInstance && AnimScriptInstance->IsInitialized())
	{
		FAnimPose EvaluatedPose;
		AnimScriptInstance->EvaluateAnimation(EvaluatedPose);
		if (!FinalizePoseEvaluationResult(EvaluatedPose))
		{
			return;
		}
	}

	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

bool USkeletalMeshComponent::FinalizePoseEvaluationResult(const FAnimPose& InFinalPose)
{
	if (!SkeletalMesh || InFinalPose.LocalTransforms.Num() != SkeletalMesh->RefSkeleton.RefBonePose.Num())
	{
		HTR_LOG(Error, "[Skel] Evaluated pose must contain every mesh bone");
		return false;
	}

	BoneSpaceTransforms = InFinalPose.LocalTransforms;
	for (FTransform& Transform : BoneSpaceTransforms)
	{
		Transform.Rotation = Transform.Rotation.Normalized();
	}
	return true;
}

void USkeletalMeshComponent::InitPoseFromMesh()
{
	ClearAnimScriptInstance();
	// 임포터가 저장한 기준 자세(FTransform, 부모 상대)를 시작 포즈로 준비한 뒤
	// 부모가 배열 크기와 렌더 메시를 만들고 RefreshBoneTransforms를 부른다
	if (SkeletalMesh)
	{
		BoneSpaceTransforms = SkeletalMesh->RefSkeleton.RefBonePose;
	}
	else
	{
		BoneSpaceTransforms.Reset();
	}

	Super::InitPoseFromMesh();
	InitAnim(true);
}
