#include "EnginePCH.h"
#include "ReferenceSkeleton.h"
#include <cmath>
#include <unordered_set>

int32 FReferenceSkeleton::FindBoneIndex(const FString& Name) const
{
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		if (RefBoneInfo[Index].Name == Name) return Index;
	}
	return -1;
}

bool FReferenceSkeleton::Validate(FString& Error) const
{
	if (RefBoneInfo.IsEmpty() || RefBoneInfo.Num() != RefBonePose.Num() || RefBoneInfo.Num() > 65536)
	{
		Error = "Invalid reference skeleton size";
		return false;
	}
	std::unordered_set<FString> Names;
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		const FMeshBoneInfo& Bone = RefBoneInfo[Index];
		if (Bone.Name.empty() || !Names.insert(Bone.Name).second ||
			(Index == 0 ? Bone.ParentIndex != -1 : Bone.ParentIndex < 0 || Bone.ParentIndex >= Index))
		{
			Error = "Invalid or duplicate bone: " + Bone.Name;
			return false;
		}
		const FMatrix Local = RefBonePose[Index].GetLocalMatrix();
		for (int Row = 0; Row < 4; ++Row)
			for (int Col = 0; Col < 4; ++Col)
				if (!std::isfinite(Local.M[Row][Col])) { Error = "Non-finite bone transform"; return false; }
		if (std::abs(Local.Determinant()) < 1.e-12f) { Error = "Singular bone transform"; return false; }
	}
	return true;
}

bool FReferenceSkeleton::GetModelSpaceMatrices(TArray<FMatrix>& Out, FString& Error) const
{
	if (!Validate(Error)) return false;
	Out.SetNum(GetNum());
	for (int32 Index = 0; Index < GetNum(); ++Index)
	{
		Out[Index] = RefBonePose[Index].GetLocalMatrix();
		const int32 Parent = RefBoneInfo[Index].ParentIndex;
		if (Parent >= 0) Out[Index] = Out[Index] * Out[Parent];
	}
	return true;
}
