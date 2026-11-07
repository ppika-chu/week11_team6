#include "EnginePCH.h"
#include "SkeletalMeshSceneProxy.h"

#include "Components/SkinnedMeshComponent.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/SkinningStats.h"
#include "Render/RenderCommand.h"
#include "Render/SkinningSettings.h"

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{}

bool FSkeletalMeshSceneProxy::GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue, const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext, TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const
{
	AddMeshPackets(RenderQueue);
	return true;
}

void FSkeletalMeshSceneProxy::AddMeshPackets(FRenderQueue& Queue) const
{
	auto* RenderMesh = GetComponent()->GetRenderMesh();
	auto* Component = CastChecked<USkinnedMeshComponent>(GetComponent());
	auto* SkeletalMesh = Component->GetSkeletalMesh();
	if (!RenderMesh || !SkeletalMesh)
	{
		return;
	}

	const FMeshRenderState* State = GetRenderState();
	if (!State)
	{
		return;
	}

	if (State->LODCount == 0)
	{
		return;
	}

	// 스켈레탈 메시는 LOD0만 쓰므로 LOD0의 모든 섹션(머티리얼 슬롯)을 제출한다
	const FCachedMeshLOD& LOD = State->LODs[0];
	for (uint32 I = 0; I < LOD.NumSections; ++I)
	{
		const FCachedMeshSection& Section = State->Sections[LOD.FirstSection + I];
		FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
		Packet.Proxy = this;
		Packet.VertexFactory = bGPUSkinning ? &GPUSkinVertexFactory : &CPUSkinVertexFactory;
		Packet.IndexBuffer = SkeletalMesh->IndexBuffer.get();
		Packet.Material = Section.Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = 0;
	}
}

void FSkeletalMeshSceneProxy::UpdateDynamicData()
{
	auto* Component = CastChecked<USkinnedMeshComponent>(GetComponent());
	auto* SkeletalMesh = Component->GetSkeletalMesh();
	const ESkinningMode Mode = FSkinningSettings::GetSkinningMode();

	if (!SkeletalMesh || (!bRenderDynamicDataDirty && Mode == LastSkinningMode))
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CPU);
	if (!UpdateRefToLocalMatrices(*Component, *SkeletalMesh))
	{
		return;
	}
	bGPUSkinning = Mode == ESkinningMode::GPU && SkeletalMesh->SkinWeightBuffer;
	if (bGPUSkinning)
	{
		UpdateGPUSkin(*SkeletalMesh);
	}
	else
	{
		UpdateCPUSkin(*SkeletalMesh);
	}

	bRenderDynamicDataDirty = false;
	LastSkinningMode = Mode;
	MarkShadowDirty();
}

bool FSkeletalMeshSceneProxy::UpdateRefToLocalMatrices(const USkinnedMeshComponent& Component, const USkeletalMesh& Mesh)
{
	const TArray<FMatrix>& ComponentSpaceTransforms = Component.GetComponentSpaceTransforms();
	const TArray<FMatrix>& InvBind = Mesh.RefBasesInvMatrix;
	if (ComponentSpaceTransforms.IsEmpty() || ComponentSpaceTransforms.Num() != InvBind.Num())
	{
		return false;
	}

	ReferenceToLocal.SetNum(ComponentSpaceTransforms.Num());
	for (int32 i = 0; i < ComponentSpaceTransforms.Num(); i++)
	{
		ReferenceToLocal[i] = InvBind[i] * ComponentSpaceTransforms[i];
	}
	return true;
}

void FSkeletalMeshSceneProxy::UpdateGPUSkin(const USkeletalMesh& Mesh)
{
	const uint32 Count = static_cast<uint32>(ReferenceToLocal.Num());
	if (!BoneBuffer || BoneBuffer->GetMaxCount() < Count)
	{
		BoneBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FMatrix), Count);
	}

	if (!BoneBuffer)
	{
		return;
	}

	const uint32 Bytes = sizeof(FMatrix) * Count;
	FStatRegistry::AddCounter(SkinningStats::STAT_SkinUploadBytes, Bytes);
	RenderCommand::UpdateBufferData(BoneBuffer.get(), ReferenceToLocal.GetData(), Bytes);

	GPUSkinVertexFactory.Type = EVertexFactoryType::GPUSkin;
	GPUSkinVertexFactory.VertexBuffer = Mesh.VertexBuffer.get();
	GPUSkinVertexFactory.BoneMatrices = BoneBuffer.get();
	GPUSkinVertexFactory.SkinWeights = Mesh.SkinWeightBuffer.get();
}

void FSkeletalMeshSceneProxy::UpdateCPUSkin(const USkeletalMesh& Mesh)
{
	const FStaticMeshData& Source = Mesh.GetMeshData();
	const FSkinWeightData& Skin = Mesh.GetSkinWeights();
	const int32 VertexCount = Source.Vertices.Num();
	const int32 BoneCount = ReferenceToLocal.Num();
	if (VertexCount == 0 || Skin.InfluenceBones.Num() != VertexCount || Skin.InfluenceWeights.Num() != VertexCount)
	{
		return;
	}

	const uint32 Bytes = sizeof(FVertex) * static_cast<uint32>(VertexCount);
	if (!CPUSkinVertexBuffer || CPUSkinVertexBuffer->GetBufferSize() < Bytes)
	{
		CPUSkinVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(Bytes, sizeof(FVertex));
		CPUSkinVertexFactory.Type = EVertexFactoryType::Local;
		CPUSkinVertexFactory.VertexBuffer = CPUSkinVertexBuffer.get();
	}
	if (!CPUSkinVertexBuffer)
	{
		return;
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
	RenderCommand::UpdateBufferData(CPUSkinVertexBuffer.get(), SkinnedVertices.GetData(), Bytes);
}
