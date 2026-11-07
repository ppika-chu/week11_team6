#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimPose.h"
#include "Math/EngineMath.h"

// 두 입력 노드 A와 B의 로컬 포즈를 Alpha에 따라 혼합하는 노드.
// Alpha가 0이면 A, 1이면 B의 포즈를 출력하며, 중간 값에서는 두 포즈를 혼합한다.
// 혼합 비율을 결정하는 규칙은 외부에서 계산하고 SetAlpha로 전달한다.
// 입력 노드는 비소유 참조이며, 혼합 결과는 Evaluate의 OutPose에 기록한다.
struct FAnimNode_TwoWayBlend : public FAnimNode_Base
{
public:
	void Initialize() override;
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

	void SetInputNodeA(FAnimNode_Base* InInputNodeA) { InputNodeA = InInputNodeA; }
	FAnimNode_Base* GetInputNodeA() const { return InputNodeA; }
	void SetInputNodeB(FAnimNode_Base* InInputNodeB) { InputNodeB = InInputNodeB; }
	FAnimNode_Base* GetInputNodeB() const { return InputNodeB; }
	void SetAlpha(float InAlpha) { Alpha = FMath::Clamp(InAlpha, 0.0f, 1.0f); }
	float GetAlpha() const { return Alpha; }

private:
	FAnimNode_Base* InputNodeA = nullptr; // Alpha가 0일 때 사용하는 입력 노드
	FAnimNode_Base* InputNodeB = nullptr; // Alpha가 1일 때 사용하는 입력 노드
	float Alpha = 0.0f;                 // B의 혼합 가중치. A의 가중치는 1 - Alpha
};
