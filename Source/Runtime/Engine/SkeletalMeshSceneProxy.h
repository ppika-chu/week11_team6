#pragma once

#include "PrimitiveSceneProxy.h"

class FSkeletalMeshObject;
class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent, FSkeletalMeshObject* InMeshObject);

	bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
		const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix,
		const void* ViewContext, TArray<int32>& SortedParticleIndices,
		TArray<float>& ParticleDistancesSquared) const override;
	void GetMeshPackets(FRenderQueue& Out, uint32 LODIndex) const override;
	void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override;

	bool HasMeshPackets() const override { return MeshObject != nullptr; }
	bool HasDeformableMesh() const override { return true; }

protected:
	void UpdateDynamicData() override;
	void OnRenderStateUpdated() override;

private:
	FSkeletalMeshObject* MeshObject = nullptr;
	TArray<FCachedMeshSection> Sections;
};
