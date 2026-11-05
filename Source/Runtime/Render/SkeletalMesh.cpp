#include "EnginePCH.h"
#include "SkeletalMesh.h"
#include <cmath>

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

bool USkeletalMesh::CalculateInvRefMatrices(FString& Error)
{
	TArray<FMatrix> ModelPose;
	if (!RefSkeleton.GetModelSpaceMatrices(ModelPose, Error)) return false;
	RefBasesInvMatrix.SetNum(ModelPose.Num());
	for (int32 Index = 0; Index < ModelPose.Num(); ++Index) RefBasesInvMatrix[Index] = ModelPose[Index].Inverse();
	return true;
}
