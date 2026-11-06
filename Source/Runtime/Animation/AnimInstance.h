#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class USkeletalMeshComponent;

// 컴포넌트별 애니메이션 실행 상태. Game Thread에서 사용한다.
class UAnimInstance : public UObject
{
	DECLARE_CLASS(UAnimInstance, UObject)
public:
	void InitializeAnimation();
	void UninitializeAnimation();
	bool IsInitialized() const { return !bUninitialized; }

	USkeletalMeshComponent* GetSkelMeshComponent() const;

	// 파생 Instance가 초기화·정리할 상태가 있을 때 재정의한다.
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUninitializeAnimation() {}

private:
	bool bUninitialized = true;
};
