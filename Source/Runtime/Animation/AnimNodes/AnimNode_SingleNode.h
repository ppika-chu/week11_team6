#pragma once

#include "AnimNode_Base.h"

class UAnimSingleNodeInstance;

// 단일 재생 Instance가 소유하며, 그 Instance의 재생 상태를 사용한다.
struct FAnimNode_SingleNode : public FAnimNode_Base
{
	friend class UAnimSingleNodeInstance;

	// 재생 상태 초기화는 소유 Instance의 NativeInitializeAnimation에서 수행한다.
	void Initialize() override {}
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

private:
	UAnimSingleNodeInstance* Instance = nullptr; // 비소유 참조. 소유 Instance와 수명이 같다.
};
