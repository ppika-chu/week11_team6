#pragma once

#include "AnimNode_Base.h"

class UAnimSingleNodeInstance;

// UAnimSingleNodeInstance의 멤버 Node. 해당 Instance의 재생 상태를 사용한다.
struct FAnimNode_SingleNode : public FAnimNode_Base
{
	friend class UAnimSingleNodeInstance;

	// 재생 상태 초기화는 UAnimSingleNodeInstance::NativeInitializeAnimation에서 수행한다.
	void Initialize() override {}
	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FAnimPose& OutPose) override;

private:
	UAnimSingleNodeInstance* Instance = nullptr; // 이 Node를 멤버로 보관하는 Instance의 주소.
};
