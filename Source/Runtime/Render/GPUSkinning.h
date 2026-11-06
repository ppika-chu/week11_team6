#pragma once

class FStructuredBuffer;

struct FGPUSkinWeight
{
	uint32 PackedBones[2]; // [0] = b0 | b1 << 16, [1] = b2 | b3 << 16
	float Weights[4];
};
static_assert(sizeof(FGPUSkinWeight) == 24);

struct FSkinningResources
{
	FStructuredBuffer* BoneMatrices = nullptr;
	FStructuredBuffer* SkinWeights = nullptr;
};

namespace GPUSkinning
{
inline constexpr uint32 BoneMatricesSlot = 20;
inline constexpr uint32 SkinWeightsSlot = 21;
}
