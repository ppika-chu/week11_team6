#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimState.h"
#include "Animation/AnimTransition.h"

// FAnimState와 FAnimTransition의 관계를 바탕으로 현재 상태와 진행 중인 전이를 관리하는 노드.
// TransitionResult의 조건 결과로 전이를 선택하고, 선택한 상태의 StateResult에서 포즈를 얻는다.
// 전이 중에는 이전 상태와 다음 상태의 포즈를 전이 가중치에 따라 혼합해 출력한다.
struct FAnimNode_StateMachine : public FAnimNode_Base
{
public:
	void Initialize() override;
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

	// 등록한 인덱스를 반환한다. 이 머신의 상태 인덱스로 전이를 구성한다.
	int32 AddState(const FAnimState& State) { return static_cast<int32>(States.Add(State)); }
	int32 AddTransition(const FAnimTransition& Transition)
	{
		if (!States.IsValidIndex(Transition.GetFromStateIndex()) ||
			!States.IsValidIndex(Transition.GetToStateIndex()))
		{
			return -1;
		}

		return static_cast<int32>(Transitions.Add(Transition));
	}

	// 상태 등록 후 초기 상태를 지정한다. 잘못된 인덱스이면 기존 설정을 유지한다.
	bool SetInitialStateIndex(int32 InStateIndex)
	{
		if (!States.IsValidIndex(InStateIndex))
		{
			return false;
		}

		InitialStateIndex = InStateIndex;
		return true;
	}
	int32 GetInitialStateIndex() const { return InitialStateIndex; }
	int32 GetCurrentStateIndex() const { return CurrentStateIndex; }
	int32 GetActiveTransitionIndex() const { return ActiveTransitionIndex; }
	float GetTransitionElapsedTime() const { return TransitionElapsedTime; }

private:
	TArray<FAnimState> States; // StateMachine이 선택할 수 있는 상태들의 배열
	TArray<FAnimTransition> Transitions; // 상태 간 전이들의 배열

	// 공유 가능한 구성 정보와 캐릭터별 실행 진행 상태를 구분한다.
	int32 InitialStateIndex = -1; // 구성에서 지정하는 최초 진입 상태
	int32 CurrentStateIndex = -1; // 실행 중인 현재 상태. 초기화 전에는 미지정
	int32 ActiveTransitionIndex = -1; // 진행 중인 전이. -1이면 전이 없음
	float TransitionElapsedTime = 0.0f; // 진행 중인 전이의 경과 시간
};
