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
