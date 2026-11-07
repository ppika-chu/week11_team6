#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimPose.h"

#include <string>

// StateMachine 등에서 받은 기본 포즈에 지정한 Slot의 Montage 포즈를 혼합하는 노드.
// AnimInstance가 관리하는 Montage 재생 정보와 블렌드 가중치를 사용한다.
// 해당 Slot에 재생 중인 Montage가 없으면 기본 포즈를 그대로 출력한다.
// SlotPose는 외부에서 제공하는 Montage 혼합 입력이며, 최종 혼합 결과는 OutPose에 기록한다.
// MontageWeight가 0이면 기본 포즈만 사용하고, 1이면 Montage 포즈를 전부 반영한다.
struct FAnimNode_Slot : public FAnimNode_Base
{
public:
	void Initialize() override;
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

	void SetSlotName(const std::string& InSlotName) { SlotName = InSlotName; }
	const std::string& GetSlotName() const { return SlotName; }
	void SetInputNode(FAnimNode_Base* InInputNode) { InputNode = InInputNode; }
	FAnimNode_Base* GetInputNode() const { return InputNode; }
	void SetSlotPose(const FAnimPose& InSlotPose) { SlotPose = InSlotPose; }
	const FAnimPose& GetSlotPose() const { return SlotPose; }
	void SetMontageWeight(float InWeight) { MontageWeight = FMath::Clamp(InWeight, 0.0f, 1.0f); }
	float GetMontageWeight() const { return MontageWeight; }

private:
	std::string SlotName; // 재생할 Montage를 가진 Slot 이름
	FAnimNode_Base* InputNode = nullptr; // 기본 포즈를 제공하는 입력 노드
	FAnimPose SlotPose;                  // 외부에서 제공하는 Montage 혼합 입력 포즈
	float MontageWeight = 0.0f;          // [0, 1] 혼합 가중치. 기본값은 Montage 미반영
};
