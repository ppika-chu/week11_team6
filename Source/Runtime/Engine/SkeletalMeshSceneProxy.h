#pragma once

#include "PrimitiveSceneProxy.h"
#include "Render/Buffer.h"
#include "Render/SkinningSettings.h"
#include "Render/VertexFactory.h"

struct FVertex;
class USkeletalMesh;
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
	bool UpdateRefToLocalMatrices(const USkinnedMeshComponent& Component, const USkeletalMesh& Mesh);
	void UpdateGPUSkin(const USkeletalMesh& Mesh);
	void UpdateCPUSkin(const USkeletalMesh& Mesh);

	TArray<FMatrix> ReferenceToLocal;
	ESkinningMode LastSkinningMode = ESkinningMode::GPU;
	bool bGPUSkinning = false;

	// GPU 스키닝
	TUniquePtr<FStructuredBuffer> BoneBuffer;
	FVertexFactory GPUSkinVertexFactory;

	// CPU 스키닝
	TArray<FVertex> SkinnedVertices;
	TUniquePtr<FVertexBuffer> CPUSkinVertexBuffer;
	FVertexFactory CPUSkinVertexFactory;
};
