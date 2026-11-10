#include "EnginePCH.h"
#include "Skeleton.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

USkeletalMesh* USkeleton::FindCompatibleMesh() const
{
	USkeletalMesh* Found = nullptr;
	UAssetManager::ForEachAsset([&](URenderAsset& Asset)
		{
			USkeletalMesh* Mesh = Cast<USkeletalMesh>(&Asset);
			if (!Found && Mesh && Mesh->Skeleton == this)
			{
				Found = Mesh;
			}
		});
	return Found;
}

const FSkeletonToMeshLinkup& USkeleton::FindOrAddMeshLinkupData(const USkeletalMesh* Mesh)
{
	// 지운 메시 주소가 다른 메시로 다시 쓰일 수 있어 본 수가 맞을 때만 캐시를 쓴다
	if (auto It = LinkupCache.find(Mesh); It != LinkupCache.end() &&
		It->second.SkeletonToMeshTable.Num() == RefSkeleton.GetNum() &&
		It->second.MeshToSkeletonTable.Num() == (Mesh ? Mesh->RefSkeleton.GetNum() : 0))
	{
		return It->second;
	}

	FSkeletonToMeshLinkup& Linkup = LinkupCache[Mesh];
	Linkup = FSkeletonToMeshLinkup();
	const int32 SkeletonBoneCount = RefSkeleton.GetNum();
	Linkup.SkeletonToMeshTable.Init(INDEX_NONE, SkeletonBoneCount);
	if (Mesh)
	{
		const int32 MeshBoneCount = Mesh->RefSkeleton.GetNum();
		Linkup.MeshToSkeletonTable.Init(INDEX_NONE, MeshBoneCount);
		for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; ++MeshIndex)
		{
			const int32 SkeletonIndex = RefSkeleton.FindBoneIndex(Mesh->RefSkeleton.RefBoneInfo[MeshIndex].Name);
			Linkup.MeshToSkeletonTable[MeshIndex] = SkeletonIndex;
			if (SkeletonIndex != INDEX_NONE)
			{
				Linkup.SkeletonToMeshTable[SkeletonIndex] = MeshIndex;
			}
		}
	}
	return Linkup;
}
