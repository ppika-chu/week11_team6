#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimPose.h"
#include "Animation/AnimationRuntime.h"
#include "Core/NameTypes.h"
#include "Math/EngineMath.h"

// StateMachine 등에서 받은 기본 포즈에 지정한 Slot의 Montage 포즈를 혼합하는 노드.
// AnimInstance가 관리하는 Montage 재생 정보와 블렌드 가중치를 사용한다.
// 해당 Slot에 재생 중인 Montage가 없으면 기본 포즈를 그대로 출력한다.
// SlotPose는 외부에서 제공하는 Montage 혼합 입력이며, 최종 혼합 결과는 OutPose에 기록한다.
// MontageWeight가 0이면 기본 포즈만 사용하고, 1이면 Montage 포즈를 전부 반영한다.
struct FAnimNode_Slot : public FAnimNode_Base
{
public:
	void Initialize()
	{
		if (InputNode)
		{
			InputNode->Initialize();
		}
	}
	void Update(const FAnimationUpdateContext& Context)
	{
		if (InputNode)
		{
			InputNode->Update(Context);
		}
	}
	void Evaluate(FAnimPose& OutPose)
	{
		if (!InputNode)
		{
			OutPose.LocalTransforms.Reset();
			return;
		}

		InputNode->Evaluate(OutPose);

		if (MontageWeight <= 0.0f)
		{
			return;
		}

		if (SlotPose.LocalTransforms.Num() != OutPose.LocalTransforms.Num())
		{
			return;
		}

		BlendLocalPoseInPlace(OutPose, SlotPose, MontageWeight);
	}

	void BlendLocalPoseInPlace(FAnimPose& OutPose, const FAnimPose& InPose, float Weight)
	{
		// API는 기본 포즈의 가중치를 받으므로 Montage 가중치를 반전한다.
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutPose, InPose, 1.0f - FMath::Clamp(Weight, 0.0f, 1.0f));
	}

	void SetSlotName(const FName& InSlotName) { SlotName = InSlotName; }
	const FName& GetSlotName() const { return SlotName; }
	void SetInputNode(FAnimNode_Base* InInputNode) { InputNode = InInputNode; }
	FAnimNode_Base* GetInputNode() const { return InputNode; }
	void SetSlotPose(const FAnimPose& InSlotPose) { SlotPose = InSlotPose; }
	const FAnimPose& GetSlotPose() const { return SlotPose; }
	void SetMontageWeight(float InWeight) { MontageWeight = FMath::Clamp(InWeight, 0.0f, 1.0f); }
	float GetMontageWeight() const { return MontageWeight; }

private:
	FName SlotName = NAME_None; // 재생할 Montage를 가진 Slot 이름
	FAnimNode_Base* InputNode = nullptr; // 기본 포즈를 제공하는 입력 노드
	FAnimPose SlotPose;                  // 외부에서 제공하는 Montage 혼합 입력 포즈
	float MontageWeight = 0.0f;          // [0, 1] 혼합 가중치. 기본값은 Montage 미반영
};
