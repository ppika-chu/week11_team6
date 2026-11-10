#pragma once

#include "Asset/RenderAsset.h"

class USkeleton;

#include "Math/Transform.h"

// Pose 추출 입력. 시간 단위는 초이며 재생 상태를 보관하지 않는다.
struct FAnimExtractContext
{
	explicit FAnimExtractContext(double InCurrentTime = 0.0, bool bInExtractRootMotion = false)
		: CurrentTime(InCurrentTime), bExtractRootMotion(bInExtractRootMotion) {}
	double CurrentTime;
	// 인스턴스가 Root Motion을 꺼내는 중이면 true. 켜져 있고 에셋도 Root Motion을 쓰면 루트 본을 고정한다
	bool bExtractRootMotion;
};

// Root Motion을 꺼낸 뒤 루트 본을 고정할 위치 (UE ERootMotionRootLock)
namespace ERootMotionRootLock
{
	enum Type : int32
	{
		RefPose,        // 기준 자세의 루트 위치
		AnimFirstFrame, // 애니메이션 0프레임의 루트 위치
		Zero,           // 원점
	};
}

// 이번 프레임에 모은 Root Motion 변화량 (UE FRootMotionMovementParams)
struct FRootMotionMovementParams
{
	bool bHasRootMotion = false;

	void Set(const FTransform& InTransform)
	{
		bHasRootMotion = true;
		RootMotionTransform = InTransform;
	}
	// UE와 같은 순서: 새 변화량을 먼저 적용한 뒤 이미 모은 변화량을 적용한다
	void Accumulate(const FTransform& InTransform)
	{
		if (!bHasRootMotion)
		{
			Set(InTransform);
		}
		else
		{
			RootMotionTransform = InTransform * RootMotionTransform;
		}
	}
	void Clear()
	{
		bHasRootMotion = false;
		RootMotionTransform = FTransform::Identity;
	}
	const FTransform& GetRootMotionTransform() const { return RootMotionTransform; }

private:
	FTransform RootMotionTransform = FTransform::Identity;
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
