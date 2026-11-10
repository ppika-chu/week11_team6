#pragma once

#include "Components/SkeletalMeshComponent.h"

struct FSkinWeightColorRamp
{
	TArray<FVector4> Stops; // RGB + 위치
	
	static FSkinWeightColorRamp Greyscale();
	static FSkinWeightColorRamp Turbo();
	static FSkinWeightColorRamp Jet();
	static FSkinWeightColorRamp Viridis();
	static FSkinWeightColorRamp Inferno();
};

// 뷰어 프리뷰 메시가 Root Motion을 어떻게 적용할지 (UE EProcessRootMotionMode). 미리보기 전용이라 저장하지 않는다
enum class EProcessRootMotionMode : uint8
{
	Ignore,       // 적용하지 않는다 (루트가 고정되어 제자리)
	Loop,         // 계속 이동한다. 바닥을 벗어나면 반대편으로 넘긴다
	LoopAndReset, // 이동하다가 애니메이션이 반복될 때마다 시작 위치로 돌아온다
};

class UDebugSkelMeshComponent : public USkeletalMeshComponent
{
	DECLARE_CLASS(UDebugSkelMeshComponent, USkeletalMeshComponent)
public:
	// 프리뷰 씬이 월드 Tick 뒤에 부른다. 꺼낸 Root Motion으로 이 컴포넌트를 옮긴다 (UE UDebugSkelMeshComponent::ConsumeRootMotion)
	void ConsumeRootMotion(const FVector& FloorMin, const FVector& FloorMax);
	using USkeletalMeshComponent::ConsumeRootMotion;
	// 사용자가 고른 값. 지금 에셋이 못 쓰면 Ignore로 두었다가 쓸 수 있게 되면 이 값으로 돌아간다
	void SetProcessRootMotionMode(EProcessRootMotionMode Mode);
	EProcessRootMotionMode GetProcessRootMotionMode() const { return ProcessRootMotionMode; }
	EProcessRootMotionMode GetRequestedProcessRootMotionMode() const { return RequestedProcessRootMotionMode; }
	// Loop 계열은 Root Motion을 켠 에셋에서만 고를 수 있다
	bool CanUseProcessRootMotionMode(EProcessRootMotionMode Mode) const;
	bool DoesCurrentAssetHaveRootMotion() const;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetShowBoneWeight(bool bShow);
	bool IsShowingBoneWeight() const { return bDrawBoneInfluences; }

	void SetBoneOfInterest(int32 BoneIndex);
	int32 GetBoneOfInterest() const { return BoneOfInterest; }

	void SetBoneWeightColorRamp(const FSkinWeightColorRamp& InRamp);
	const FSkinWeightColorRamp& GetBoneWeightColorRamp() const { return BoneWeightColorRamp; }

protected:
	// 프리뷰 씬이 ConsumeRootMotion에서 처리하므로 액터를 옮기지 않는다
	virtual void ApplyRootMotionToOwner() override {}

private:
	void SetProcessRootMotionModeInternal(EProcessRootMotionMode Mode);
	// 모드를 바꿀 때 컴포넌트 위치를 처음으로 되돌린다 (UE ResetRootTransform)
	void ResetRootTransform();

	// UE 기본값: 요청은 LoopAndReset, 실제는 Ignore에서 시작해 에셋이 Root Motion을 쓰면 요청 값으로 바뀐다
	EProcessRootMotionMode RequestedProcessRootMotionMode = EProcessRootMotionMode::LoopAndReset;
	EProcessRootMotionMode ProcessRootMotionMode = EProcessRootMotionMode::Ignore;
	float ConsumeRootMotionPreviousPlaybackTime = 0.0f;

	bool bDrawBoneInfluences = false;
	int32 BoneOfInterest = -1;
	FSkinWeightColorRamp BoneWeightColorRamp;
};
