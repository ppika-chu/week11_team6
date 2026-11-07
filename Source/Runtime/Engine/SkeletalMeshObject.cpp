#include "EnginePCH.h"
#include "SkeletalMeshObject.h"

#include "Components/SkinnedMeshComponent.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/SkinningStats.h"
#include "Render/RenderCommand.h"
#include "Render/SkeletalMesh.h"

bool FSkeletalMeshObject::Update(const USkinnedMeshComponent& Component)
{
	const TArray<FMatrix>& ComponentSpaceTransforms = Component.GetComponentSpaceTransforms();
	const TArray<FMatrix>& InvBind = Mesh->RefBasesInvMatrix;
	if (ComponentSpaceTransforms.IsEmpty() || ComponentSpaceTransforms.Num() != InvBind.Num())
	{
		HTR_LOG(Error, std::format("ComponentSpaceTransforms and InvBind have different sizes: {} vs {}", ComponentSpaceTransforms.Num(), InvBind.Num()));
		return false;
	}

	ReferenceToLocal.SetNum(ComponentSpaceTransforms.Num());
	for (int32 i = 0; i < ComponentSpaceTransforms.Num(); i++)
	{
		ReferenceToLocal[i] = InvBind[i] * ComponentSpaceTransforms[i];
	}

	return UpdateSkin();
}

FIndexBuffer* FSkeletalMeshObject::GetIndexBuffer() const
{
	return Mesh->IndexBuffer.get();
}

TUniquePtr<FSkeletalMeshObject> FSkeletalMeshObject::Create(const USkeletalMesh& InMesh, ESkinningMode Mode)
{
	if (!InMesh.VertexBuffer || !InMesh.IndexBuffer)
	{
		return nullptr;
	}
	if (Mode == ESkinningMode::GPU && InMesh.SkinWeightBuffer)
	{
		return MakeUnique<FSkeletalMeshObjectGPUSkin>(InMesh);
	}
	return MakeUnique<FSkeletalMeshObjectCPUSkin>(InMesh);
}

FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectGPUSkin(const USkeletalMesh& InMesh)
	: FSkeletalMeshObject(InMesh)
{
	const uint32 BoneCount = static_cast<uint32>(InMesh.RefBasesInvMatrix.Num());
	BoneBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FMatrix), BoneCount);

	VertexFactory.Type = EVertexFactoryType::GPUSkin;
	VertexFactory.VertexBuffer = BoneBuffer ? InMesh.VertexBuffer.get() : nullptr;
	VertexFactory.BoneMatrices = BoneBuffer.get();
	VertexFactory.SkinWeights = InMesh.SkinWeightBuffer.get();
}

bool FSkeletalMeshObjectGPUSkin::UpdateSkin()
{
	const uint32 Count = static_cast<uint32>(ReferenceToLocal.Num());

	if (!BoneBuffer || BoneBuffer->GetMaxCount() < Count)
	{
		return false;
	}

	const uint32 Bytes = sizeof(FMatrix) * Count;
	FStatRegistry::AddCounter(SkinningStats::STAT_SkinUploadBytes, Bytes);
	RenderCommand::UpdateBufferData(BoneBuffer.get(), ReferenceToLocal.GetData(), Bytes);

	return true;
}

FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectCPUSkin(const USkeletalMesh& InMesh) 
	: FSkeletalMeshObject(InMesh)
{
	const uint32 Bytes = sizeof(FVertex) * static_cast<uint32>(InMesh.GetMeshData().Vertices.Num());
	SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(Bytes, sizeof(FVertex));
	VertexFactory.Type = EVertexFactoryType::Local;
	VertexFactory.VertexBuffer = SkinnedVertexBuffer.get();
}

bool FSkeletalMeshObjectCPUSkin::UpdateSkin()
{
	const FStaticMeshData& Source = Mesh->GetMeshData();
	const FSkinWeightData& Skin = Mesh->GetSkinWeights();
	const int32 VertexCount = Source.Vertices.Num();
	const int32 BoneCount = ReferenceToLocal.Num();
	if (VertexCount == 0 || Skin.InfluenceBones.Num() != VertexCount || Skin.InfluenceWeights.Num() != VertexCount)
	{
		return false;
	}

	const uint32 Bytes = sizeof(FVertex) * static_cast<uint32>(VertexCount);
	if (!SkinnedVertexBuffer)
	{
		return false;
	}

	SkinnedVertices.SetNum(VertexCount);
	for (int32 V = 0; V < VertexCount; ++V)
	{
		const FVertex& Bind = Source.Vertices[V];
		const std::array<uint16, 4>& Bones = Skin.InfluenceBones[V];
		const std::array<float, 4>& Weights = Skin.InfluenceWeights[V];
		const FVector BindTarget(Bind.Tangent.X, Bind.Tangent.Y, Bind.Tangent.Z);

		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		FVector Tangent = FVector::ZeroVector;

		for (int32 K = 0; K < 4; ++K)
		{
			const float W = Weights[K];
			if (W <= 0.0f || Bones[K] >= BoneCount)
			{
				continue;
			}
			const FMatrix& M = ReferenceToLocal[Bones[K]];
			Position += M.TransformPosition(Bind.Position) * W;
			Normal += M.TransformVector(Bind.Normal) * W;
			Tangent += M.TransformVector(BindTarget) * W;
		}

		FVertex& Out = SkinnedVertices[V];
		Out = Bind;
		Out.Position = Position;
		Out.Normal = Normal.GetSafeNormal();
		const FVector T = Tangent.GetSafeNormal();
		Out.Tangent = FVector4(T.X, T.Y, T.Z, Bind.Tangent.W);
	}

	FStatRegistry::AddCounter(SkinningStats::STAT_SkinnedVertices, VertexCount);
	FStatRegistry::AddCounter(SkinningStats::STAT_SkinUploadBytes, Bytes);
	RenderCommand::UpdateBufferData(SkinnedVertexBuffer.get(), SkinnedVertices.GetData(), Bytes);

	return true;
}