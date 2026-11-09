#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimState.h"
#include "Animation/AnimTransition.h"
#include "Animation/AnimationUpdateContext.h"
#include "Animation/AnimationRuntime.h"

#include <algorithm>

// FAnimState와 FAnimTransition의 관계를 바탕으로 현재 상태와 진행 중인 전이를 관리하는 노드.
// TransitionResult의 조건 결과로 전이를 선택하고, 선택한 상태의 StateResult에서 포즈를 얻는다.
// 전이 중에는 이전 상태와 다음 상태의 포즈를 전이 가중치에 따라 혼합해 출력한다.
struct FAnimNode_StateMachine : public FAnimNode_Base
{
public:
	void Initialize() override
	{
		CurrentStateIndex = -1;
		ActiveTransitionIndex = -1;
		TransitionElapsedTime = 0.0f;

		for (FAnimState* State : States)
		{
			if (State)
			{
				State->Initialize();
			}
		}
		for (FAnimTransition* Transition : Transitions)
		{
			if (Transition)
			{
				Transition->Initialize();
			}
		}

		if (IsValidStateIndex(InitialStateIndex))
		{
			CurrentStateIndex = InitialStateIndex;
		}
	}
	void Update(const FAnimationUpdateContext& Context) override
	{
		if (!IsValidStateIndex(CurrentStateIndex))
		{
			return;
		}

		// 자식 Update 전에 전이를 선택한다. Notify 수집과 Pose 평가에 같은 Weight를 사용한다.
		if (ActiveTransitionIndex == -1)
		{
			TArray<int32> Candidates;
			for (int32 Index = 0; Index < Transitions.Num(); ++Index)
			{
				FAnimTransition* Transition = Transitions[Index];
				if (Transition && Transition->GetFromStateIndex() == CurrentStateIndex && IsValidStateIndex(Transition->GetToStateIndex()) && Transition->GetToStateIndex() != CurrentStateIndex)
				{
					Candidates.Add(Index);
				}
			}
			std::stable_sort(Candidates.begin(), Candidates.end(), [this](int32 A, int32 B){
				return Transitions[A]->GetPriority() < Transitions[B]->GetPriority();
			});
			for (int32 Index : Candidates)
			{
				FAnimTransition* Transition = Transitions[Index];
				if (Transition->EvaluateCondition(Context))
				{
					CurrentStateIndex = Transition->GetToStateIndex();
					ActiveTransitionIndex = Transition->GetBlendDuration() > 0.0f ? Index : -1;
					TransitionElapsedTime = 0.0f;
					States[CurrentStateIndex]->Initialize();
					break;
				}
			}
		}

		if (ActiveTransitionIndex == -1)
		{
			States[CurrentStateIndex]->Update(Context);
			return;
		}

		FAnimTransition* Transition = Transitions[ActiveTransitionIndex];
		const float Duration = Transition->GetBlendDuration();
		TransitionElapsedTime = FMath::Min(TransitionElapsedTime + Context.GetDeltaTime(), Duration);
		const float Alpha = TransitionElapsedTime / Duration;
		if (Alpha < 1.0f)
		{
			States[Transition->GetFromStateIndex()]->Update(Context.FractionalWeight(1.0f - Alpha));
		}
		if (Alpha > 0.0f)
		{
			States[CurrentStateIndex]->Update(Context.FractionalWeight(Alpha));
		}
		if (Alpha >= 1.0f)
		{
			ActiveTransitionIndex = -1;
			TransitionElapsedTime = 0.0f;
		}
	}
	void Evaluate(FAnimPose& OutPose)
	{
		if (!IsValidStateIndex(CurrentStateIndex))
		{
			OutPose.LocalTransforms.Reset();
			return;
		}

		// 전이 중이 아니면 현재 상태의 포즈를 그대로 출력한다.
		if (ActiveTransitionIndex == -1)
		{
			States[CurrentStateIndex]->Evaluate(OutPose);
			return;
		}

		// 전이 중인 경우 이전과 다음 상태를 blend한다.
		FAnimTransition* Transition = Transitions.IsValidIndex(ActiveTransitionIndex)
			? Transitions[ActiveTransitionIndex]
			: nullptr;

		if (!Transition ||
			!IsValidStateIndex(Transition->GetFromStateIndex()) ||
			!IsValidStateIndex(Transition->GetToStateIndex()))
		{
			OutPose.LocalTransforms.Reset();
			return;
		}

		const int32 FromIndex = Transition->GetFromStateIndex();
		const int32 ToIndex = Transition->GetToStateIndex();
		const float Duration = Transition->GetBlendDuration();

		if (Duration <= 0.0f)
		{
			States[ToIndex]->Evaluate(OutPose);
			return;
		}

		// 이전 상태의 포즈를 출력 버퍼에 받는다.
		States[FromIndex]->Evaluate(OutPose);

		// 다음 상태의 포즈를 작업 버퍼에 받는다.
		FAnimPose ToPose;
		States[ToIndex]->Evaluate(ToPose);

		// 혼합할 수 없으면 이전 상태의 포즈를 유지한다.
		if (OutPose.LocalTransforms.Num() != ToPose.LocalTransforms.Num())
		{
			return;
		}

		const float Alpha = FMath::Clamp(TransitionElapsedTime / Duration, 0.0f, 1.0f);

		// API는 첫 포즈의 가중치를 받으므로 목적 포즈의 Alpha를 반전한다.
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutPose, ToPose, 1.0f - Alpha);
	}

	// 등록한 인덱스를 반환한다. 이 머신의 상태 인덱스로 전이를 구성한다.
	int32 AddState(FAnimState* State)
	{
		if (!State)
		{
			return -1;
		}
		return static_cast<int32>(States.Add(State));
	}
	int32 AddTransition(FAnimTransition* Transition)
	{
		if (!Transition)
		{
			return -1;
		}

		if (!States.IsValidIndex(Transition->GetFromStateIndex()) ||
			!States.IsValidIndex(Transition->GetToStateIndex()))
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
	bool IsValidStateIndex(int32 StateIndex) const
	{
		return States.IsValidIndex(StateIndex) && States[StateIndex] != nullptr;
	}

	TArray<FAnimState*> States; // StateMachine이 선택할 수 있는 상태들의 배열
	TArray<FAnimTransition*> Transitions; // 상태 간 전이들의 배열

	// 공유 가능한 구성 정보와 캐릭터별 실행 진행 상태를 구분한다.
	int32 InitialStateIndex = -1; // 구성에서 지정하는 최초 진입 상태
	int32 CurrentStateIndex = -1; // 실행 중인 현재 상태. 초기화 전에는 미지정
	int32 ActiveTransitionIndex = -1; // 진행 중인 전이. -1이면 전이 없음
	float TransitionElapsedTime = 0.0f; // 진행 중인 전이의 경과 시간
};
