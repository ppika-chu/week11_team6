#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"

class USkeletalMesh;

class USkeleton : public URenderAsset
{
	DECLARE_CLASS(USkeleton, URenderAsset)
public:
	// 이 스켈레톤을 쓰는 메시 중 처음 찾은 것. 없으면 nullptr (UE USkeleton::FindCompatibleMesh)
	USkeletalMesh* FindCompatibleMesh() const;

	FReferenceSkeleton RefSkeleton;
	// 애니메이션 임포트/트랙 매핑은 이 공유 본 테이블에 연결한다.
};
