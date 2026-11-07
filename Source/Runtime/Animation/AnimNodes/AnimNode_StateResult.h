#pragma once

#include "AnimNode_Root.h"

// 개별 State 내부 포즈 그래프의 최종 포즈를 StateMachine에 반환하는 출력 노드.
// Root의 출력 역할을 상속하며, 입력으로 연결된 SequencePlayer 등의 평가를 요청한다.
// FAnimState는 이 노드를 해당 상태의 포즈 그래프 출력 지점으로 참조한다.
// 상태의 출력 포즈는 별도 멤버에 저장하지 않고 Evaluate의 OutPose에 기록한다.
// 포즈 입력은 Root의 InputNode를 사용하며, 상태 간 연결은 FAnimTransition에서 관리한다.
struct FAnimNode_StateResult : public FAnimNode_Root
{
public:
	//void Initialize() override;
	//void Update(float DeltaTime) override;
	//void Evaluate(FAnimPose& OutPose) override;
};
