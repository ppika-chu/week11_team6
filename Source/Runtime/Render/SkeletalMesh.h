#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"
#include "Render/StaticMeshData.h"
#include <array>

#include "Buffer.h"

class UMaterial;
class USkeleton;

// 각 배열은 해당 LOD의 최종 정점 인덱스로 접근한다.
struct FSkinWeightData
{
	TArray<std::array<uint16, 4>> InfluenceBones;
	TArray<std::array<float, 4>> InfluenceWeights;
	bool Validate(int32 VertexCount, int32 BoneCount, FString& Error) const;
};
static_assert(sizeof(std::array<uint16, 4>) == 8);

struct FSkeletalMeshLODResource
{
	FStaticMeshData Data;
	FSkinWeightData SkinWeights;
};

class USkeletalMesh : public URenderAsset
{
	DECLARE_CLASS(USkeletalMesh, URenderAsset)
public:
	FStaticMeshData MeshData;
	FSkinWeightData SkinWeights;
	TArray<FSkeletalMeshLODResource> AdditionalLODs;
	TArray<UMaterial*> Materials;
	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };
	USkeleton* Skeleton = nullptr;
	FReferenceSkeleton RefSkeleton;
	TArray<FMatrix> RefBasesInvMatrix;
	TUniquePtr<FVertexBuffer> VertexBuffer; // 바인드 포즈, Static
	TUniquePtr<FIndexBuffer> IndexBuffer; // Static
	TUniquePtr<FStructuredBuffer> SkinWeightBuffer; // FGPUSkinWeight[]

	void InitResources();

	uint32 GetLODCount() const { return 1u + static_cast<uint32>(AdditionalLODs.Num()); }
	const FStaticMeshData& GetMeshData(uint32 LOD = 0) const { return LOD == 0 ? MeshData : AdditionalLODs[LOD - 1].Data; }
	const FSkinWeightData& GetSkinWeights(uint32 LOD = 0) const { return LOD == 0 ? SkinWeights : AdditionalLODs[LOD - 1].SkinWeights; }
	bool CalculateInvRefMatrices(FString& Error);
};
