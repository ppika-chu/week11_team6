#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Components/ParticleSubUVComponent.h"

// 시뮬레이션과 분리한 파티클 스냅샷과 View 공통 SubUV 상수를 보관한다.
class FParticleSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSceneProxy(UParticleSubUVComponent* Component)
		: FPrimitiveSceneProxy(Component)
	{
	}

	bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
		const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext,
		TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const override;

protected:
	// 기존 FParticle을 재사용하며 생존 인덱스와 상수는 원본 인덱스에 대응한다.
	void UpdateDynamicData() override;
	void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override
	{
		FPrimitiveSceneProxy::AddReferencedMaterials(OutMaterials);
		OutMaterials.Add(Material);
	}

private:
	// Quad는 StaticMesh 빠른 경로를 선택하는 기본 Proxy의 Mesh와 구분한다.
	UStaticMesh* QuadMesh = nullptr;
	UMaterial* Material = nullptr;
	TArray<FParticle> Particles;
	TArray<FSubUVConstants> Constants;
	TArray<int32> AliveParticleIndices;
};
