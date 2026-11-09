#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Core/Stats/LightweightStats.h"
#include "Render/SkeletalMesh.h"
#include "Render/Material.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/SkinningSettings.h"

void USkinnedMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsurePoseInitialized();
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
	{
		return;
	}

	SkeletalMesh = InSkeletalMesh;
	InitPoseFromMesh();
	ClearOverrideMaterials();
	RecreateRenderState();
	OnTransformDirty();
}

const FStaticMeshData* USkinnedMeshComponent::GetMeshData() const
{
	return SkeletalMesh ? &SkeletalMesh->GetMeshData() : nullptr;
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? static_cast<int32>(SkeletalMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
	{
		return FString();
	}

	return SkeletalMesh->GetMeshData().MaterialSlots[SlotIndex].Name;
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	// UStaticMesh::GetMaterial과 같은 규칙: 슬롯 머티리얼이 없으면 DefaultMaterial
	const TArray<UMaterial*>& Materials = SkeletalMesh->Materials;
	if (SlotIndex >= 0 && SlotIndex < static_cast<int32>(Materials.Num()) && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

FPrimitiveSceneProxy* USkinnedMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this, MeshObject.get());
}

void USkinnedMeshComponent::CreateRenderState()
{
	if (SkeletalMesh)
	{
		MeshObject = FSkeletalMeshObject::Create(*SkeletalMesh, FSkinningSettings::GetSkinningMode());
	}
	Super::CreateRenderState();
}

void USkinnedMeshComponent::DestroyRenderState()
{
	Super::DestroyRenderState();
	MeshObject.reset();
}

int32 USkinnedMeshComponent::FindBoneIndex(const FString& BoneName) const
{
	return SkeletalMesh ? SkeletalMesh->RefSkeleton.FindBoneIndex(BoneName) : -1;
}

FVector USkinnedMeshComponent::GetBoneLocation(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num())
	{
		return FVector::ZeroVector;
	}

	const FMatrix& M = ComponentSpaceTransforms[BoneIndex];
	return FVector(M.M[3][0], M.M[3][1], M.M[3][2]);
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
	if (!SkeletalMesh)
	{
		return;
	}

	FillComponentSpaceTransforms(SkeletalMesh->RefSkeleton.RefBonePose);
	FinalizeBoneTransform();
}

void USkinnedMeshComponent::InitPoseFromMesh()
{
	ComponentSpaceTransforms.Reset();
	PoseSourceMesh = SkeletalMesh;

	if (!SkeletalMesh)
	{
		return;
	}

	ComponentSpaceTransforms.SetNum(SkeletalMesh->RefSkeleton.RefBonePose.Num());
	RefreshBoneTransforms();
}

void USkinnedMeshComponent::EnsurePoseInitialized()
{
	if (SkeletalMesh != PoseSourceMesh)
	{
		InitPoseFromMesh();
		RecreateRenderState();
	}
}

void USkinnedMeshComponent::FillComponentSpaceTransforms(const TArray<FTransform>& LocalPose)
{
	if (!SkeletalMesh)
	{
		return;
	}

	const FReferenceSkeleton& Skeleton = SkeletalMesh->RefSkeleton;
	if (LocalPose.Num() != ComponentSpaceTransforms.Num())
	{
		HTR_LOG(Error, "[Skel] Local pose count mismatch: {} vs {}", LocalPose.Num(), ComponentSpaceTransforms.Num());
		return;
	}

	// 부모가 항상 앞 번호라서 앞에서부터 한 번만 돌면 된다 (FReferenceSkeleton::Validate가 보장)
	for (int32 i = 0; i < LocalPose.Num(); ++i)
	{
		const int32 Parent = Skeleton.RefBoneInfo[i].ParentIndex;
		const FMatrix Local = LocalPose[i].GetLocalMatrix();
		ComponentSpaceTransforms[i] = (Parent < 0)
			? Local
			: Local * ComponentSpaceTransforms[Parent];
	}
}

void USkinnedMeshComponent::FinalizeBoneTransform()
{
	MarkRenderDynamicDataDirty();
}
