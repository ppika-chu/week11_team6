#include "EnginePCH.h"
#include "DebugSkelMeshSceneProxy.h"

#include "DebugSkelMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"
#include "Render/RenderResourceManager.h"

namespace
{
UMaterial* GetBoneWeightMaterial()
{
	static UMaterial* Material = nullptr;
	if (!Material)
	{
		Material = NewObject<UMaterial>();
		Material->Shader = FRenderResourceManager::FindOrLoadShaderProgram("Resources/Shader/SkinWeightShader.hlsl", nullptr, EVertexFactoryType::GPUSkin);
		Material->ParamLayout = EMaterialParamLayout::BoneWeight;
		Material->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FSkinWeightParams));
		Material->ShadingModel = ELightingModel::Unlit;
	}
	return Material;
}
}

void FDebugSkelMeshSceneProxy::GetMeshPackets(FRenderQueue& Out, uint32 LODIndex) const
{
	const int32 First = Out.Num();
	FSkeletalMeshSceneProxy::GetMeshPackets(Out, LODIndex);
	if (!bDrawBoneInfluences)
	{
		return;
	}

	UMaterial* WeightMaterial = GetBoneWeightMaterial();
	for (int32 i = First; i < Out.Num(); ++i)
	{
		FRenderPacket& Packet = Out[i];
		Packet.Material = WeightMaterial;
		Packet.MaterialParamData = &Params;
		Packet.MaterialParamDataSize = sizeof(Params);
	}
}

void FDebugSkelMeshSceneProxy::AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const
{
	FSkeletalMeshSceneProxy::AddReferencedMaterials(OutMaterials);
	if (bDrawBoneInfluences)
	{
		OutMaterials.Add(GetBoneWeightMaterial());
	}
}

void FDebugSkelMeshSceneProxy::OnRenderStateUpdated()
{
	FSkeletalMeshSceneProxy::OnRenderStateUpdated();

	const auto* Comp = CastChecked<UDebugSkelMeshComponent>(GetComponent());
	bDrawBoneInfluences = Comp->IsShowingBoneWeight() && Comp->MeshObject && Comp->MeshObject->GetSkinningMode() == ESkinningMode::GPU;
	Params.SelectedBone = Comp->GetBoneOfInterest();

	const TArray<FVector4>& Stops = Comp->GetBoneWeightColorRamp().Stops;
	Params.StopCount = FMath::Min(Stops.Num(), MaxSkinWeightRampStops);
	for (int32 i = 0; i < Params.StopCount; ++i)
	{
		Params.RampStops[i] = Stops[i];
	}
}
