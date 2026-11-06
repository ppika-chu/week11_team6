#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/SkinningStats.h"
#include "Render/RenderCommand.h"
#include "Render/SkeletalMesh.h"
#include "Render/Material.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/GPUSkinning.h"
#include "Render/SkinningSettings.h"

USkinnedMeshComponent::USkinnedMeshComponent()
{
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
	DestroyRenderMesh();
}

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
	MarkRenderStateDirty();
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
	SkinningMatrices.Reset();
	SkinnedVertices.Reset();
	DestroyRenderMesh();
	PoseSourceMesh = SkeletalMesh;

	if (!SkeletalMesh)
	{
		return;
	}

	const int32 BoneCount = SkeletalMesh->RefSkeleton.RefBonePose.Num();
	ComponentSpaceTransforms.SetNum(BoneCount);
	SkinningMatrices.SetNum(BoneCount);

	CreateRenderMesh();
	RefreshBoneTransforms();
}

void USkinnedMeshComponent::EnsurePoseInitialized()
{
	if (SkeletalMesh != PoseSourceMesh)
	{
		InitPoseFromMesh();
		MarkRenderStateDirty();
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
	if (!SkeletalMesh)
	{
		return;
	}

	const TArray<FMatrix>& InvBind = SkeletalMesh->RefBasesInvMatrix;
	if (InvBind.Num() != ComponentSpaceTransforms.Num())
	{
		HTR_LOG(Error, "[Skel] RefBasesInvMatrix count mismatch: {} vs {}", InvBind.Num(), ComponentSpaceTransforms.Num());
		return;
	}

	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CPU);
	FStatRegistry::AddCounter(SkinningStats::STAT_SkinnedVertices, static_cast<uint64>(SkeletalMesh->GetMeshData().Vertices.Num()));

	for (int32 i = 0; i < ComponentSpaceTransforms.Num(); ++i)
	{
		SkinningMatrices[i] = InvBind[i] * ComponentSpaceTransforms[i];
	}

	if (FSkinningSettings::GetSkinningMode() == ESkinningMode::CPU)
	{
		UpdateSkinnedVertices();
		bVertexBufferIsBindPose = false;
	}
	else
	{
		if (!bVertexBufferIsBindPose && RenderMesh)
		{
			const auto& Bind = SkeletalMesh->GetMeshData().Vertices;
			RenderCommand::UpdateBufferData(RenderMesh->VertexBuffer.get(), Bind.GetData(), sizeof(FVertex) * Bind.Num());
			bVertexBufferIsBindPose = true;
		}

		if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
		{
			Proxy->MarkShadowDirty();
		}
	}
}

void USkinnedMeshComponent::CreateRenderMesh()
{
	DestroyRenderMesh();
	if (!SkeletalMesh)
	{
		return;
	}

	// LOD0만 사용한다
	const FStaticMeshData& Source = SkeletalMesh->GetMeshData();
	const uint32 VertexCount = static_cast<uint32>(Source.Vertices.Num());
	const uint32 IndexCount = static_cast<uint32>(Source.Indices.Num());
	if (VertexCount == 0 || IndexCount == 0)
	{
		return;
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->bInternalOnly = true;

	Mesh->MeshData = Source;

	Mesh->VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FVertex) * VertexCount, sizeof(FVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Source.Indices.GetData(), IndexCount);
	if (!Mesh->VertexBuffer || !Mesh->IndexBuffer)
	{
		HTR_LOG(Error, "[Skel] Failed to create skinned render buffers");
		delete Mesh;
		return;
	}

	for (int32 Slot = 0; Slot < GetNumMaterials(); ++Slot)
	{
		Mesh->Materials.Add(GetDefaultMaterial(Slot));
	}

	Mesh->RebuildRenderData();
	RenderMesh = Mesh;
	SkinnedVertices = Source.Vertices;

	bVertexBufferIsBindPose = false;
}

void USkinnedMeshComponent::DestroyRenderMesh()
{
	delete RenderMesh;
	RenderMesh = nullptr;
}

void USkinnedMeshComponent::UpdateSkinnedVertices()
{
	if (!SkeletalMesh || !RenderMesh)
	{
		return;
	}

	const FStaticMeshData& Source = SkeletalMesh->GetMeshData();
	const FSkinWeightData& Skin = SkeletalMesh->GetSkinWeights();
	const int32 VertexCount = Source.Vertices.Num();
	const int32 BoneCount = SkinningMatrices.Num();
	if (Skin.InfluenceBones.Num() != VertexCount || Skin.InfluenceWeights.Num() != VertexCount)
	{
		HTR_LOG(Error, "[Skel] Skin weight count mismatch: vertices={} weights={}", VertexCount, Skin.InfluenceWeights.Num());
		return;
	}
	SkinnedVertices.SetNum(VertexCount);

	for (int32 v = 0; v < VertexCount; ++v)
	{
		const FVertex& Bind = Source.Vertices[v];
		const std::array<uint16, 4>& Bones = Skin.InfluenceBones[v];
		const std::array<float, 4>& Weights = Skin.InfluenceWeights[v];
		const FVector BindTangent(Bind.Tangent.X, Bind.Tangent.Y, Bind.Tangent.Z);

		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		FVector Tangent = FVector::ZeroVector;

		// LBS: 붙어 있는 본(최대 4개)의 행렬로 각각 옮긴 뒤 가중치만큼 섞는다
		for (int32 k = 0; k < 4; ++k)
		{
			const float W = Weights[k];
			if (W <= 0.0f || Bones[k] >= BoneCount)
			{
				continue;
			}

			const FMatrix& M = SkinningMatrices[Bones[k]];
			Position += M.TransformPosition(Bind.Position) * W;
			Normal += M.TransformVector(Bind.Normal) * W;
			Tangent += M.TransformVector(BindTangent) * W;
		}

		FVertex& Out = SkinnedVertices[v];
		Out = Bind;
		Out.Position = Position;
		Out.Normal = Normal.GetSafeNormal();
		const FVector T = Tangent.GetSafeNormal();
		Out.Tangent = FVector4(T.X, T.Y, T.Z, Bind.Tangent.W);
	}

	FStatRegistry::AddCounter(SkinningStats::STAT_SkinUploadBytes, sizeof(FVertex) * VertexCount);
	RenderCommand::UpdateBufferData(RenderMesh->VertexBuffer.get(), SkinnedVertices.GetData(),
		sizeof(FVertex) * static_cast<uint32>(VertexCount));

	// 정점이 바뀌었으므로 캐시된 그림자(Spot/Point는 투영이 고정이라 계속 Hit)를 무효화한다
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->MarkShadowDirty();
	}
}

void USkinnedMeshComponent::SubmitToRenderQueue(FRenderQueue& Queue)
{
	if (auto* Proxy = static_cast<FSkeletalMeshSceneProxy*>(GetSceneProxy()))
	{
		Proxy->PrepareDynamicData();
		Proxy->AddMeshPackets(Queue);
	}
}

void USkinnedMeshComponent::LogBoneTransforms() const
{
	if (!SkeletalMesh)
	{
		HTR_LOG(Warning, "[SkelTest] No skeletal mesh");
		return;
	}

	const FReferenceSkeleton& Skeleton = SkeletalMesh->RefSkeleton;
	for (int32 i = 0; i < ComponentSpaceTransforms.Num(); ++i)
	{
		const FVector Location = GetBoneLocation(i);
		HTR_LOG(Info, "[SkelTest] Bone{} '{}' pos=({:.2f}, {:.2f}, {:.2f})",
			i, Skeleton.RefBoneInfo[i].Name, Location.X, Location.Y, Location.Z);
	}
}

void USkinnedMeshComponent::LogSkinningMatrices() const
{
	if (!SkeletalMesh)
	{
		return;
	}

	const FReferenceSkeleton& Skeleton = SkeletalMesh->RefSkeleton;
	for (int32 i = 0; i < SkinningMatrices.Num(); ++i)
	{
		float MaxError = 0.0f;
		for (int32 r = 0; r < 4; ++r)
		{
			for (int32 c = 0; c < 4; ++c)
			{
				const float Expected = (r == c) ? 1.0f : 0.0f;
				MaxError = FMath::Max(MaxError, std::fabs(SkinningMatrices[i].M[r][c] - Expected));
			}
		}
		HTR_LOG(Info, "[SkelTest] Skin{} '{}' identityError={:.4f}", i, Skeleton.RefBoneInfo[i].Name, MaxError);
	}
}
