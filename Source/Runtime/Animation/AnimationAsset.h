#pragma once

#include "Asset/RenderAsset.h"

class USkeleton;

// Pose 추출 입력. 시간 단위는 초이며 재생 상태를 보관하지 않는다.
struct FAnimExtractContext
{
	explicit FAnimExtractContext(double InCurrentTime = 0.0) : CurrentTime(InCurrentTime) {}
	double CurrentTime;
};

// 공유 애니메이션 Asset의 최소 계약.
// .fbxbin 로더가 에셋으로 등록하므로 URenderAsset을 상속한다.
class UAnimationAsset : public URenderAsset
{
	DECLARE_CLASS(UAnimationAsset, URenderAsset)
public:
	virtual float GetPlayLength() const = 0;
	USkeleton* GetSkeleton() const { return Skeleton; }
	void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }

private:
	USkeleton* Skeleton = nullptr; // 외부에서 지정한 Skeleton. 이 Asset이 삭제하지 않으며 사용 기간 동안 유효해야 한다.
};
