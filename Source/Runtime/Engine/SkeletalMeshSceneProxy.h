#pragma once

#include "PrimitiveSceneProxy.h"
#include "Render/Buffer.h"
#include "Render/GPUSkinning.h"

class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent);

	bool SupportsStaticFastPath() const override { return false; }
	bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
		const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix,
		const void* ViewContext, TArray<int32>& SortedParticleIndices,
		TArray<float>& ParticleDistancesSquared) const override;

	void AddMeshPackets(FRenderQueue& Queue) const;

protected:
	void UpdateDynamicData() override;

private:
	bool bGPUSkinning = false;
	TUniquePtr<FStructuredBuffer> BoneBuffer;
	FSkinningResources Skinning;
};
