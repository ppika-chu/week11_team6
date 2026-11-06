#pragma once

#include "Asset/RenderAsset.h"

class USkeleton;

struct FAnimExtractContext
{
	double CurrentTime = 0.0;
	bool bExtractRootMotion = false;
	bool bLooping = false;
};

class UAnimationAsset : public URenderAsset
{
	DECLARE_CLASS(UAnimationAsset, URenderAsset)
public:
	virtual float GetPlayLength() const { return 0.0f; }

	USkeleton* GetSkeleton() const { return Skeleton; }
	void SetSkeleton(USkeleton* NewSkeleton) { Skeleton = NewSkeleton; }

private:
	USkeleton* Skeleton = nullptr;
};
