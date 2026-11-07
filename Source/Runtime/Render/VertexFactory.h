#pragma once

class FStructuredBuffer;
class FVertexBuffer;

enum class EVertexFactoryType : uint8
{
	Local,
	GPUSkin,
	Count,
};

struct FVertexFactoryTypeInfo
{
	EVertexFactoryType Type;
	const char* Define; // HLSL 매크로
};

inline constexpr FVertexFactoryTypeInfo GVertexFactoryTypes[] = {
	{ EVertexFactoryType::Local, nullptr },
	{ EVertexFactoryType::GPUSkin, "VF_GPU_SKIN" },
};

struct FVertexFactory
{
	EVertexFactoryType Type = EVertexFactoryType::Local;
	FVertexBuffer* VertexBuffer = nullptr;

	// GPUSkin 전용
	FStructuredBuffer* BoneMatrices = nullptr;
	FStructuredBuffer* SkinWeights = nullptr;

	uint16 SortID = NextSortID++;

private:
	static inline uint16 NextSortID = 0;
};

struct FGPUSkinWeight
{
	uint32 PackedBones[2]; // [0] = b0 | b1 << 16, [1] = b2 | b3 << 16
	float Weights[4];
};
static_assert(sizeof(FGPUSkinWeight) == 24);

namespace GPUSkinVF
{
inline constexpr uint32 BoneMatricesSlot = 20;
inline constexpr uint32 SkinWeightsSlot = 21;
}
