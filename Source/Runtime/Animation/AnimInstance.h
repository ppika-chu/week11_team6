#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class USkeletalMeshComponent;
struct FAnimNode_Base;
struct FAnimPose;

// 컴포넌트별 애니메이션 실행 상태. Game Thread에서 사용한다.
class UAnimInstance : public UObject
{
	DECLARE_CLASS(UAnimInstance, UObject)
public:
	void InitializeAnimation();
	void UninitializeAnimation();
	void UpdateAnimation(float DeltaSeconds);
	void EvaluateAnimation(FAnimPose& OutPose);
	bool IsInitialized() const { return !bUninitialized; }

	USkeletalMeshComponent* GetSkelMeshComponent() const;

	// 파생 Instance가 초기화·정리할 상태가 있을 때 재정의한다.
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUninitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}

protected:
	// 파생 Instance가 Graph를 보관하고 수명 동안 유효한 Root를 제공한다.
	// Graph가 없는 기본 Instance는 Reference Pose를 사용한다.
	virtual FAnimNode_Base* GetCustomRootNode() { return nullptr; }

private:
	FAnimNode_Base* RootNode = nullptr; // 비소유 참조. 여기서 Node를 생성·삭제하지 않는다.
	bool bUninitialized = true;
};
