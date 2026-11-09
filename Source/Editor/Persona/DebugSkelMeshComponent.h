#pragma once

#include "Components/SkeletalMeshComponent.h"

struct FSkinWeightColorRamp
{
	TArray<FVector4> Stops; // RGB + 위치
	
	static FSkinWeightColorRamp Greyscale();
	static FSkinWeightColorRamp Turbo();
	static FSkinWeightColorRamp Jet();
	static FSkinWeightColorRamp Viridis();
	static FSkinWeightColorRamp Inferno();
};

class UDebugSkelMeshComponent : public USkeletalMeshComponent
{
	DECLARE_CLASS(UDebugSkelMeshComponent, USkeletalMeshComponent)
public:
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetShowBoneWeight(bool bShow);
	bool IsShowingBoneWeight() const { return bDrawBoneInfluences; }

	void SetBoneOfInterest(int32 BoneIndex);
	int32 GetBoneOfInterest() const { return BoneOfInterest; }

	void SetBoneWeightColorRamp(const FSkinWeightColorRamp& InRamp);
	const FSkinWeightColorRamp& GetBoneWeightColorRamp() const { return BoneWeightColorRamp; }

private:
	bool bDrawBoneInfluences = false;
	int32 BoneOfInterest = -1;
	FSkinWeightColorRamp BoneWeightColorRamp;
};
