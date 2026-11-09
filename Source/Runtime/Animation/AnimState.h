#pragma once

#include "AnimNodes/AnimNode_StateResult.h"

// Idle·Run처럼 StateMachine이 선택할 개별 상태를 정의하는 일반 구조체.
// 상태 내부 포즈 그래프의 출력(StateResult)과 이 상태에서 나가는 전이들을 연결한다.
// StateResult의 입력에는 SequencePlayer·BlendSpacePlayer·혼합 Node 등 일반 포즈 그래프를 연결한다.
// 재생 상태는 각 Player가 관리하며, State 초기화는 구성된 입력 연결을 유지한다.
struct FAnimState
{
public:
	void Initialize()
	{
		if (StateResult)
		{
			StateResult->Initialize();
		}
	}
	void Update(const FAnimationUpdateContext& Context)
	{
		if (StateResult)
		{
			StateResult->Update(Context);
		}
	}
	void Evaluate(FAnimPose& OutPose)
	{
		if (!StateResult)
		{
			OutPose.LocalTransforms.Reset();
			return;
		}

		StateResult->Evaluate(OutPose);
	}

	void SetStateResult(FAnimNode_StateResult* InStateResult) { StateResult = InStateResult; }
	FAnimNode_StateResult* GetStateResult() const { return StateResult; }

private:
	FAnimNode_StateResult* StateResult = nullptr; // 외부 Graph가 보관하는 상태 내부 포즈 그래프의 출력 Node
};
