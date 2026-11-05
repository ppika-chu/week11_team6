#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"

class USkeleton : public URenderAsset
{
	DECLARE_CLASS(USkeleton, URenderAsset)
public:
	FReferenceSkeleton RefSkeleton;
	// 애니메이션 임포트/트랙 매핑은 이 공유 본 테이블에 연결한다.
};
