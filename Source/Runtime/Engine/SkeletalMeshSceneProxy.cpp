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
	if (!RenderMesh)
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
		Packet.Mesh = RenderMesh;
		Packet.Material = Section.Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = 0;
		Packet.Skinning = bGPUSkinning ? &Skinning : nullptr;
	}
}

void FSkeletalMeshSceneProxy::UpdateDynamicData()
{
	auto* Component = CastChecked<USkinnedMeshComponent>(GetComponent());
	auto* SkeletalMesh = Component->GetSkeletalMesh();
	bGPUSkinning = 
		FSkinningSettings::GetSkinningMode() == ESkinningMode::GPU && 
		SkeletalMesh && 
		SkeletalMesh->SkinWeightBuffer && 
		Component->GetSkinningMatrices().Num() > 0;
	if (!bGPUSkinning)
	{
		return;
	}

	const TArray<FMatrix>& Matrices = Component->GetSkinningMatrices();
	const uint32 Count = static_cast<uint32>(Matrices.Num());
	if (!BoneBuffer || BoneBuffer->GetMaxCount() < Count)
	{
		BoneBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FMatrix), Count);
	}
	RenderCommand::UpdateBufferData(BoneBuffer.get(), Matrices.GetData(), sizeof(FMatrix) * Count);
	FStatRegistry::AddCounter(SkinningStats::STAT_SkinUploadBytes, sizeof(FMatrix) * Count);

	Skinning.BoneMatrices = BoneBuffer.get();
	Skinning.SkinWeights = SkeletalMesh->SkinWeightBuffer.get();
}
