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
// 더 높은 우선순위의 AnyState 전이는 진행 중인 전이를 중단하고 현재 혼합 포즈에서 이어 간다.
struct FAnimNode_StateMachine : public FAnimNode_Base
{
public:
	void Initialize() override
	{
		CurrentStateIndex = -1;
		ActiveTransitionIndex = -1;
		ActiveFromStateIndex = -1;
		bBlendFromSnapshot = false;
		InterruptedSourcePose.LocalTransforms.Reset();
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
		{
			TArray<int32> Candidates;
			for (int32 Index = 0; Index < Transitions.Num(); ++Index)
			{
				FAnimTransition* Transition = Transitions[Index];
				// AnyState도 일반 전이와 같은 우선순위를 사용하며 자기 전이는 허용하지 않는다.
				if (Transition && (Transition->IsAnyState() || Transition->GetFromStateIndex() == CurrentStateIndex)
					&& IsValidStateIndex(Transition->GetToStateIndex()) && Transition->GetToStateIndex() != CurrentStateIndex)
				{
					// Blend 중에는 현재 전이보다 Priority 숫자가 작은 AnyState만 조건을 평가한다.
					// 후보에서 먼저 제외해 보류해야 할 Lua 요청이 조건 함수에서 소비되지 않게 한다.
					if (ActiveTransitionIndex != -1 && (!Transition->IsAnyState()
						|| Transition->GetPriority() >= Transitions[ActiveTransitionIndex]->GetPriority()))
					{
						continue;
					}
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
					const bool bInterrupting = ActiveTransitionIndex != -1;
					if (bInterrupting && Transition->GetBlendDuration() > 0.0f)
					{
						// 목적 상태를 초기화하기 전에 이전 Blend 결과를 보존한다.
						// 재차 중단될 때도 직전 Snapshot과 목적 포즈를 혼합한 결과를 새 출발점으로 쓴다.
						FAnimPose CurrentPose;
						Evaluate(CurrentPose);
						InterruptedSourcePose = std::move(CurrentPose);
						bBlendFromSnapshot = true;
					}
					else
					{
						InterruptedSourcePose.LocalTransforms.Reset();
						bBlendFromSnapshot = false;
					}
					// AnyState 정의는 공유하므로 실제 출발 상태는 머신의 실행 상태로 보관한다.
					ActiveFromStateIndex = !bInterrupting && Transition->GetBlendDuration() > 0.0f ? CurrentStateIndex : -1;
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
		if (Alpha < 1.0f && !bBlendFromSnapshot)
		{
			States[ActiveFromStateIndex]->Update(Context.FractionalWeight(1.0f - Alpha));
		}
		if (Alpha > 0.0f)
		{
			States[CurrentStateIndex]->Update(Context.FractionalWeight(Alpha));
		}
		if (Alpha >= 1.0f)
		{
			ActiveTransitionIndex = -1;
			ActiveFromStateIndex = -1;
			bBlendFromSnapshot = false;
			InterruptedSourcePose.LocalTransforms.Reset();
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
			(!bBlendFromSnapshot && !IsValidStateIndex(ActiveFromStateIndex)) ||
			!IsValidStateIndex(Transition->GetToStateIndex()))
		{
			OutPose.LocalTransforms.Reset();
			return;
		}

		const int32 FromIndex = ActiveFromStateIndex;
		const int32 ToIndex = Transition->GetToStateIndex();
		const float Duration = Transition->GetBlendDuration();

		if (Duration <= 0.0f)
		{
			States[ToIndex]->Evaluate(OutPose);
			return;
		}

		// 중단 전의 혼합 포즈 또는 일반 전이의 출발 상태를 출력 버퍼에 받는다.
		if (bBlendFromSnapshot)
		{
			OutPose = InterruptedSourcePose;
		}
		else
		{
			States[FromIndex]->Evaluate(OutPose);
		}

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

		if ((!Transition->IsAnyState() && !IsValidStateIndex(Transition->GetFromStateIndex())) ||
			!IsValidStateIndex(Transition->GetToStateIndex()))
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
	int32 ActiveFromStateIndex = -1; // 전이 시작 시점의 실제 출발 상태. AnyState 정의와 분리한다.
	bool bBlendFromSnapshot = false; // 중단 후에는 이전 상태를 갱신하지 않고 고정된 혼합 포즈에서 전이한다.
	FAnimPose InterruptedSourcePose; // 연속 중단에도 포즈가 튀지 않도록 중단 직전의 Blend 결과를 보존한다.
	float TransitionElapsedTime = 0.0f; // 진행 중인 전이의 경과 시간
};
