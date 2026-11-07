#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class USkeletalMeshComponent;
struct FAnimNode_Base;
struct FAnimPose;

// 컴포넌트별 애니메이션 실행 상태와 Graph 실행을 관리한다.
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
	// 파생 AnimInstance가 Graph의 Node들을 보관하고 실행 시작점의 주소를 반환한다.
	// Graph가 없는 기본 Instance는 Reference Pose를 사용한다.
	virtual FAnimNode_Base* GetCustomRootNode() { return nullptr; }

private:
	// 파생 AnimInstance가 보관한 Graph의 실행 시작점. GetCustomRootNode에서 받은 주소다.
	FAnimNode_Base* RootNode = nullptr;
	bool bUninitialized = true;
};
