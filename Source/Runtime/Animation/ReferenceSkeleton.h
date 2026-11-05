#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Transform.h"

struct FMeshBoneInfo
{
	FString Name;
	int32 ParentIndex = -1;
};

// 부모가 먼저 오는 메시 기준 본 테이블. RefBonePose는 부모 상대 로컬 자세다.
struct FReferenceSkeleton
{
	TArray<FMeshBoneInfo> RefBoneInfo;
	TArray<FTransform> RefBonePose;

	int32 GetNum() const { return RefBoneInfo.Num(); }
	int32 FindBoneIndex(const FString& Name) const;
	bool Validate(FString& Error) const;
	bool GetModelSpaceMatrices(TArray<FMatrix>& Out, FString& Error) const;
};
