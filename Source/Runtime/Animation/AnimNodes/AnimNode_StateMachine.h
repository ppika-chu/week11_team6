#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimState.h"
#include "Animation/AnimTransition.h"
#include "Animation/AnimUpdateContext.h"

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
	void Update(float DeltaTime) override
	{
		if (!IsValidStateIndex(CurrentStateIndex))
		{
			return;
		}
		                                         
		const float FrameDeltaTime = FMath::Max(DeltaTime, 0.0f);

		if (ActiveTransitionIndex != -1)
		{
			FAnimTransition* Transition = Transitions.IsValidIndex(ActiveTransitionIndex)
				? Transitions[ActiveTransitionIndex] : nullptr;
			if (!Transition ||
				!IsValidStateIndex(Transition->GetFromStateIndex()) ||
				!IsValidStateIndex(Transition->GetToStateIndex()))
			{
				ActiveTransitionIndex = -1;
				TransitionElapsedTime = 0.0f;
				States[CurrentStateIndex]->Update(FrameDeltaTime);
				return;
			}

			const int32 FromIndex = Transition->GetFromStateIndex();
			const int32 ToIndex = Transition->GetToStateIndex();
			States[FromIndex]->Update(FrameDeltaTime);
			if (ToIndex != FromIndex)
			{
				States[ToIndex]->Update(FrameDeltaTime);
			}

			const float Duration = FMath::Max(Transition->GetBlendDuration(), 0.0f);
			TransitionElapsedTime = FMath::Min(TransitionElapsedTime + FrameDeltaTime, Duration);
			if (TransitionElapsedTime >= Duration)
			{
				ActiveTransitionIndex = -1;
				TransitionElapsedTime = 0.0f;
			}
			return;
		}

		States[CurrentStateIndex]->Update(FrameDeltaTime);

		// 현재 상태에서 출발하는 유효한 전이만 검사한다. 자기 전이는 첫 구현에서 제외한다.
		TArray<int32> Candidates;
		for (int32 Index = 0; Index < Transitions.Num(); ++Index)
		{
			FAnimTransition* Transition = Transitions[Index];
			if (!Transition || Transition->GetFromStateIndex() != CurrentStateIndex ||
				!IsValidStateIndex(Transition->GetToStateIndex()) ||
				Transition->GetToStateIndex() == CurrentStateIndex)
			{
				continue;
			}
			Candidates.Add(Index);
		}

		// 우선순위 숫자가 작을수록 먼저 검사하고, 동률이면 등록 순서를 유지한다.
		std::stable_sort(Candidates.begin(), Candidates.end(), [this](int32 A, int32 B){
			return Transitions[A]->GetPriority() < Transitions[B]->GetPriority();
		});

		const FAnimUpdateContext Context{ AnimInstance, FrameDeltaTime };
		for (int32 Index : Candidates)
		{
			FAnimTransition* Transition = Transitions[Index];
			if (!Transition->EvaluateCondition(Context))
			{
				continue;
			}

			CurrentStateIndex = Transition->GetToStateIndex();
			ActiveTransitionIndex = Transition->GetBlendDuration() > 0.0f ? Index : -1;
			TransitionElapsedTime = 0.0f;

			// 목적 상태는 진입 시 재시작하며, 시작 프레임에도 재생 시간을 한 번 진행한다.
			States[CurrentStateIndex]->Initialize();
			States[CurrentStateIndex]->Update(FrameDeltaTime);
			return;
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

		// TODO(BlendAPI): 아래 본별 혼합 루프를 공용 포즈 Blend API 호출로 교체한다.
		// 출발 포즈 OutPose와 목적 포즈 ToPose를 Alpha(목적 포즈의 가중치)로 혼합해 OutPose에 기록한다.
		// 입력과 출력이 같은 OutPose를 사용하므로 API의 제자리 혼합 지원 여부를 확인한다.
		for (int32 Index = 0; Index < OutPose.LocalTransforms.Num(); ++Index)
		{
			OutPose.LocalTransforms[Index] = FTransform::Blend(OutPose.LocalTransforms[Index],ToPose.LocalTransforms[Index],Alpha);
		}
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
	void SetAnimInstance(const UAnimInstance* InAnimInstance) { AnimInstance = InAnimInstance; }
	const UAnimInstance* GetAnimInstance() const { return AnimInstance; }

private:
	bool IsValidStateIndex(int32 StateIndex) const
	{
		return States.IsValidIndex(StateIndex) && States[StateIndex] != nullptr;
	}

	TArray<FAnimState*> States; // StateMachine이 선택할 수 있는 상태들의 배열
	TArray<FAnimTransition*> Transitions; // 상태 간 전이들의 배열
	const UAnimInstance* AnimInstance = nullptr; // 조건 콜백에 전달할 소유 AnimInstance의 비소유 참조

	// 공유 가능한 구성 정보와 캐릭터별 실행 진행 상태를 구분한다.
	int32 InitialStateIndex = -1; // 구성에서 지정하는 최초 진입 상태
	int32 CurrentStateIndex = -1; // 실행 중인 현재 상태. 초기화 전에는 미지정
	int32 ActiveTransitionIndex = -1; // 진행 중인 전이. -1이면 전이 없음
	float TransitionElapsedTime = 0.0f; // 진행 중인 전이의 경과 시간
};
