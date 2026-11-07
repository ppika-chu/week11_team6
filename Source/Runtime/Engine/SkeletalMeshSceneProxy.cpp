#include "EnginePCH.h"
#include "SkeletalMeshSceneProxy.h"

#include "Components/SkinnedMeshComponent.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/SkinningStats.h"
#include "Render/RenderCommand.h"
#include "Render/SkinningSettings.h"

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent, FSkeletalMeshObject* InMeshObject)
	: FPrimitiveSceneProxy(InComponent)
	, MeshObject(InMeshObject)
{}

bool FSkeletalMeshSceneProxy::GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue, const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext, TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const
{
	AddMeshPackets(RenderQueue);
	return true;
}

void FSkeletalMeshSceneProxy::AddMeshPackets(FRenderQueue& Queue) const
{
	if (!MeshObject)
	{
		return;
	}

	for (const auto& Section : Sections)
	{
		FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
		Packet.Proxy = this;
		Packet.VertexFactory = &MeshObject->GetVertexFactory();
		Packet.IndexBuffer = MeshObject->GetIndexBuffer();
		Packet.Material = Section.Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = 0;
	}
}

void FSkeletalMeshSceneProxy::AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const
{
	for (const auto& Section : Sections)
	{
		OutMaterials.Add(Section.Material);
	}
}

void FSkeletalMeshSceneProxy::UpdateDynamicData()
{
	if (!MeshObject || !bRenderDynamicDataDirty)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CPU);
	const auto* Component = CastChecked<USkinnedMeshComponent>(GetComponent());
	if (!MeshObject->Update(*Component))
	{
		return;
	}
	bRenderDynamicDataDirty = false;
	MarkShadowDirty();
}

void FSkeletalMeshSceneProxy::OnRenderStateUpdated()
{
	Sections.Reset();
	const auto* Component = CastChecked<USkinnedMeshComponent>(GetComponent());
	const USkeletalMesh* SkeletalMesh = Component->GetSkeletalMesh();
	if (!SkeletalMesh)
	{
		return;
	}
	
	for (const auto& Section : SkeletalMesh->GetMeshData().Sections)
	{
		if (UMaterial* Material = Component->GetMaterial(Section.MaterialSlotIndex))
		{
			Sections.Add({
				.Material = Material,
				.StartIndex = Section.StartIndex,
				.IndexCount = Section.IndexCount,
			});
		}
	}
}
