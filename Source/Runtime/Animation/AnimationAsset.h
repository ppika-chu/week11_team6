#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class USkeleton;

// Pose 추출 입력. 시간 단위는 초이며 재생 상태를 보관하지 않는다.
struct FAnimExtractContext
{
	explicit FAnimExtractContext(double InCurrentTime = 0.0) : CurrentTime(InCurrentTime) {}
	double CurrentTime;
};

// 공유 애니메이션 Asset의 최소 계약.
// TODO: 데이터/Import 저장 구현 예정.
class UAnimationAsset : public UObject
{
	DECLARE_CLASS(UAnimationAsset, UObject)
public:
	virtual float GetPlayLength() const = 0;
	USkeleton* GetSkeleton() const { return Skeleton; }
	void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }

private:
	USkeleton* Skeleton = nullptr; // 비소유 참조. Asset 사용 기간 동안 유효해야 한다.
};
