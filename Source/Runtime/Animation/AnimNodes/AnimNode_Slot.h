#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimPose.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
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
			// Update 시점에도 대상 메시의 본 수로 입력을 검증한다.
			// Evaluate에서 기본 포즈로 대체될 입력 때문에 기본 Notify 가중치가 줄어들면 안 된다.
			const USkeletalMeshComponent* Component = Context.AnimInstance ? Context.AnimInstance->GetSkelMeshComponent() : nullptr;
			const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
			const float EffectiveWeight = Mesh ? GetEffectiveMontageWeight(Mesh->RefSkeleton.GetNum()) : 0.0f;
			// 가중치가 0이어도 기본 그래프의 재생 시간은 계속 진행한다.
			InputNode->Update(Context.FractionalWeight(1.0f - EffectiveWeight));
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

		const float EffectiveWeight = GetEffectiveMontageWeight(OutPose.LocalTransforms.Num());
		if (EffectiveWeight <= 0.0f)
		{
			return;
		}

		BlendLocalPoseInPlace(OutPose, SlotPose, EffectiveWeight);
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
	// 외부 재생기는 Graph Update 전에 같은 메시의 본 순서를 따르는 포즈와 가중치를 함께 공급한다.
	void SetSlotPose(const FAnimPose& InSlotPose) { SlotPose = InSlotPose; }
	const FAnimPose& GetSlotPose() const { return SlotPose; }
	void SetMontageWeight(float InWeight) { MontageWeight = FMath::Clamp(InWeight, 0.0f, 1.0f); }
	float GetMontageWeight() const { return MontageWeight; }

private:
	float GetEffectiveMontageWeight(int32 BoneCount) const
	{
		return BoneCount > 0 && SlotPose.LocalTransforms.Num() == BoneCount ? MontageWeight : 0.0f;
	}

	FName SlotName = NAME_None; // 재생할 Montage를 가진 Slot 이름
	FAnimNode_Base* InputNode = nullptr; // 기본 포즈를 제공하는 입력 노드
	FAnimPose SlotPose;                  // 외부에서 제공하는 Montage 혼합 입력 포즈
	float MontageWeight = 0.0f;          // [0, 1] 혼합 가중치. 기본값은 Montage 미반영
};
