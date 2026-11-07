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
	void Initialize()
	{
		if (InputNodeA)
		{
			InputNodeA->Initialize();
		}

		if (InputNodeB && InputNodeB != InputNodeA)
		{
			InputNodeB->Initialize();
		}
	}
	void Update(float DeltaTime)
	{
		if (InputNodeA)
		{
			InputNodeA->Update(DeltaTime);
		}

		if (InputNodeB && InputNodeB != InputNodeA)
		{
			InputNodeB->Update(DeltaTime);
		}
	}
	void Evaluate(FAnimPose& OutPose)
	{
		if (InputNodeA && InputNodeB)
		{
			FAnimPose PoseA;
			InputNodeA->Evaluate(PoseA);
			FAnimPose PoseB;
			InputNodeB->Evaluate(PoseB);
			// TODO(BlendAPI): 공용 정적 포즈 Blend API로 PoseA와 PoseB를 Alpha(B의 가중치)에 따라 혼합한다.
			// API의 반환 방식에 맞춰 혼합 결과를 OutPose에 기록한다.
		}
		else if (InputNodeA)
		{
			InputNodeA->Evaluate(OutPose);
		}
		else if (InputNodeB)
		{
			InputNodeB->Evaluate(OutPose);
		}
		else
		{
			OutPose.LocalTransforms.Reset();
		}
	}

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
