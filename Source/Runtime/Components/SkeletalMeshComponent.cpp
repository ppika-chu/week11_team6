#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetSkeletalMesh(UAssetManager::GetAssetByPath<USkeletalMesh>("Models/AllMight/SK_AllMight_root.fbxbin"));
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// TODO: 애니메이션 평가 → BoneSpaceTransforms → RefreshBoneTransforms
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& LocalTransform)
{
	EnsurePoseInitialized();
	if (!IsValidBoneIndex(BoneIndex))
	{
		return;
	}

	BoneSpaceTransforms[BoneIndex] = LocalTransform;
	RefreshBoneTransforms();
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
	RefreshBoneTransforms();
}

void USkeletalMeshComponent::ResetToRefPose()
{
	EnsurePoseInitialized();
	if (!SkeletalMesh)
	{
		return;
	}

	BoneSpaceTransforms = SkeletalMesh->RefSkeleton.RefBonePose;
	RefreshBoneTransforms();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	if (!SkeletalMesh)
	{
		return;
	}

	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::InitPoseFromMesh()
{
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
}
