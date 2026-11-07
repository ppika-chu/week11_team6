#include "EnginePCH.h"
#include "SkeletalMesh.h"

#include "RenderCommand.h"
#include "VertexFactory.h"

namespace
{
FGPUSkinWeight PackSkinWeight(const std::array<uint16, 4>& Bones, const std::array<float, 4>& Weights)
{
	FGPUSkinWeight Result;
	Result.PackedBones[0] = Bones[0] | (Bones[1] << 16);
	Result.PackedBones[1] = Bones[2] | (Bones[3] << 16);
	for (int32 i = 0; i < 4; ++i)
	{
		Result.Weights[i] = Weights[i];
	}
	return Result;
}
}

bool FSkinWeightData::Validate(int32 VertexCount, int32 BoneCount, FString& Error) const
{
	if (InfluenceBones.Num() != VertexCount || InfluenceWeights.Num() != VertexCount)
	{
		Error = "Skin weights must match final mesh vertices";
		return false;
	}
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
	{
		float Total = 0;
		for (int I = 0; I < 4; ++I)
		{
			const float Weight = InfluenceWeights[Vertex][I];
			if (!std::isfinite(Weight) || Weight < 0 || InfluenceBones[Vertex][I] >= BoneCount)
			{
				Error = "Invalid global bone index or weight";
				return false;
			}
			Total += Weight;
		}
		if (std::abs(Total - 1.f) > 1.e-4f) { Error = "Skin weights are not normalized"; return false; }
	}
	return true;
}

void USkeletalMesh::InitResources()
{
	// LOD0만 사용한다
	const FStaticMeshData& Source = GetMeshData();
	const FSkinWeightData& Skin = GetSkinWeights();
	const uint32 VertexCount = static_cast<uint32>(Source.Vertices.Num());
	const uint32 IndexCount = static_cast<uint32>(Source.Indices.Num());
	if (VertexCount == 0 || IndexCount == 0 ||
		Skin.InfluenceBones.Num() != static_cast<int32>(VertexCount) ||
		Skin.InfluenceWeights.Num() != static_cast<int32>(VertexCount))
	{
		return;
	}

	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Source.Vertices.GetData(), VertexCount * sizeof(FVertex), sizeof(FVertex));
	TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Source.Indices.GetData(), IndexCount);
	TUniquePtr<FStructuredBuffer> SB = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FGPUSkinWeight), VertexCount);
	if (!VB || !IB || !SB)
	{
		HTR_LOG(Error, "[Skel] Failed to create skinned render buffers");
		return;
	}

	auto* Dst = static_cast<FGPUSkinWeight*>(RenderCommand::MapWriteDiscard(SB.get()));
	if (!Dst)
	{
		HTR_LOG(Error, "[Skel] Failed to map skin weight buffer");
		return;
	}

	for (uint32 V = 0; V < VertexCount; ++V)
	{
		Dst[V] = PackSkinWeight(Skin.InfluenceBones[V], Skin.InfluenceWeights[V]);
	}

	RenderCommand::Unmap(SB.get());

	VertexBuffer = std::move(VB);
	IndexBuffer = std::move(IB);
	SkinWeightBuffer = std::move(SB);
}

bool USkeletalMesh::CalculateInvRefMatrices(FString& Error)
{
	TArray<FMatrix> ModelPose;
	if (!RefSkeleton.GetModelSpaceMatrices(ModelPose, Error)) return false;
	RefBasesInvMatrix.SetNum(ModelPose.Num());
	for (int32 Index = 0; Index < ModelPose.Num(); ++Index) RefBasesInvMatrix[Index] = ModelPose[Index].Inverse();
	return true;
}
