#pragma once

#include "PrimitiveSceneProxy.h"

class FSkeletalMeshObject;
struct FVertex;
class USkeletalMesh;
class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent, FSkeletalMeshObject* InMeshObject);

	bool SupportsStaticFastPath() const override { return false; }
	bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
		const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix,
		const void* ViewContext, TArray<int32>& SortedParticleIndices,
		TArray<float>& ParticleDistancesSquared) const override;
	void AddMeshPackets(FRenderQueue& Queue) const;
	void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override;

protected:
	void UpdateDynamicData() override;
	void OnRenderStateUpdated() override;

private:
	FSkeletalMeshObject* MeshObject = nullptr;
	TArray<FCachedMeshSection> Sections;
};
