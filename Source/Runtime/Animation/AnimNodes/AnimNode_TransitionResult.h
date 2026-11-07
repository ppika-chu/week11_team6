#pragma once

#include "AnimNode_Base.h"

// 전이 조건의 계산 결과인 전이 가능 여부(bool)를 StateMachine에 제공하는 결과 노드.
// FAnimTransition은 이 노드를 조건의 출력 지점으로 참조한다.
// 출력은 포즈가 아닌 조건 값이며, StateMachine이 전이 우선순위와 함께 판단에 사용한다.
// 조건 계산은 FAnimTransition의 콜백이 담당하고, 이 노드는 계산된 bool만 보관한다.
struct FAnimNode_TransitionResult : public FAnimNode_Base
{
public:
	void Initialize() override;
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

	void SetCanTransition(bool bInCanTransition) { bCanTransition = bInCanTransition; }
	bool GetCanTransition() const { return bCanTransition; }

private:
	bool bCanTransition = false; // 전이 가능 여부, TransitionResult의 출력
};
