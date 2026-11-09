#pragma once

#include "Engine/SkeletalMeshSceneProxy.h"
#include "Render/Material.h"

class FDebugSkelMeshSceneProxy : public FSkeletalMeshSceneProxy
{
public:
	using FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy;

	void GetMeshPackets(FRenderQueue& Out, uint32 LODIndex) const override;
	void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override;

protected:
	void OnRenderStateUpdated() override;

private:
	bool bDrawBoneInfluences = false;
	FSkinWeightParams Params;
};
