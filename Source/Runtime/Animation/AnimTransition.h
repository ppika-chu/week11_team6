#pragma once
#include "AnimNodes/AnimNode_TransitionResult.h"
#include "AnimUpdateContext.h"

#include <functional>
#include <utility>

// 두 State 사이의 방향 있는 전이와 선택·혼합 설정을 정의하는 일반 구조체.
// 출발·목적 상태, 조건 결과(TransitionResult), 우선순위, 블렌드 시간 등을 연결한다.
// StateMachine은 이 정의와 조건 결과를 사용해 전이를 선택하고 포즈를 혼합한다.
struct FAnimTransition
{
public:
	using FConditionFunction = std::function<bool(const FAnimUpdateContext&)>;

	void Initialize();
	void Update(float DeltaTime);

	bool EvaluateCondition(const FAnimUpdateContext& Context)
	{
		const bool bCanTransition = ConditionFunction && ConditionFunction(Context);

		if (TransitionResult)
			TransitionResult->SetCanTransition(bCanTransition);

		return bCanTransition;
	}

	void SetTransitionResult(FAnimNode_TransitionResult* InTransitionResult) { TransitionResult = InTransitionResult; }
	FAnimNode_TransitionResult* GetTransitionResult() const { return TransitionResult; }
	void SetPriority(int32 InPriority) { Priority = InPriority; }
	int32 GetPriority() const { return Priority; }
	void SetFromStateIndex(int32 InFromStateIndex) { FromStateIndex = InFromStateIndex;}
	int32 GetFromStateIndex() const { return FromStateIndex; }
	void SetToStateIndex(int32 InToStateIndex) { ToStateIndex = InToStateIndex; }
	int32 GetToStateIndex() const { return ToStateIndex; }
	void SetBlendDuration(float InBlendDuration) { BlendDuration = InBlendDuration; }
	float GetBlendDuration() const { return BlendDuration; }
	void SetConditionFunction(FConditionFunction InFunction) { ConditionFunction = std::move(InFunction); }

private:
	FAnimNode_TransitionResult* TransitionResult = nullptr;
	int32 Priority = 0; // 전이 우선순위
	int32 FromStateIndex = -1; // 출발 상태 인덱스
	int32 ToStateIndex = -1; // 목적 상태 인덱스
	float BlendDuration = 0.2f; // 전이 블렌드 시간
	FConditionFunction ConditionFunction;
};

