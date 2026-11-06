#pragma once

#include "AnimNodes/AnimNode_StateResult.h"
#include "AnimNodes/AnimNode_SequencePlayer.h"

// Idle·Run처럼 StateMachine이 선택할 개별 상태를 정의하는 일반 구조체.
// 상태 내부 포즈 그래프의 출력(StateResult)과 이 상태에서 나가는 전이들을 연결한다.
// 클립의 재생 시간 관리와 포즈 생성은 상태 그래프의 SequencePlayer가 담당한다.
struct FAnimState
{
public:
	void Initialize();
	void Update(float DeltaTime);
	void Evaluate(FAnimPose& OutPose);

	void AddSequencePlayer(FAnimNode_SequencePlayer* SequencePlayer) { SequencePlayers.Add(SequencePlayer); }
	void SetStateResult(FAnimNode_StateResult* InStateResult) { StateResult = InStateResult; }
	FAnimNode_StateResult* GetStateResult() const { return StateResult; }

private:
	TArray<FAnimNode_SequencePlayer*> SequencePlayers; // 상태 내부 포즈 그래프의 SequencePlayer 노드들
	FAnimNode_StateResult* StateResult = nullptr;                // 상태 내부 포즈 그래프의 최종 출력 노드
};
