#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"
#include <unordered_map>

class USkeletalMesh;

// 스켈레톤 본 번호와 메시 본 번호를 이름으로 잇는 표. 공유 스켈레톤은 메시마다 본 수와 순서가 다르다 (UE FSkeletonToMeshLinkup)
struct FSkeletonToMeshLinkup
{
	TArray<int32> SkeletonToMeshTable; // 스켈레톤 본 → 메시 본. 메시에 없으면 INDEX_NONE
	TArray<int32> MeshToSkeletonTable; // 메시 본 → 스켈레톤 본. 스켈레톤에 없으면 INDEX_NONE
};

class USkeleton : public URenderAsset
{
	DECLARE_CLASS(USkeleton, URenderAsset)
public:
	// 이 스켈레톤을 쓰는 메시 중 처음 찾은 것. 없으면 nullptr (UE USkeleton::FindCompatibleMesh)
	USkeletalMesh* FindCompatibleMesh() const;

	// 메시와의 본 번호 표. 처음 부를 때 만들어 둔다 (UE USkeleton::FindOrAddMeshLinkupData)
	const FSkeletonToMeshLinkup& FindOrAddMeshLinkupData(const USkeletalMesh* Mesh);

	FReferenceSkeleton RefSkeleton;
	// 애니메이션 임포트/트랙 매핑은 이 공유 본 테이블에 연결한다.

private:
	// 스켈레톤은 임포트로 바뀌면 새 객체로 교체되므로 본 목록이 바뀐 뒤 옛 표를 쓰지 않는다
	std::unordered_map<const USkeletalMesh*, FSkeletonToMeshLinkup> LinkupCache;
};
