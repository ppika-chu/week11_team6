#pragma once

#include "SkinnedMeshComponent.h"

class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimationAsset;
struct FAnimPose;

namespace EAnimationMode
{
	// AnimationClass 은 UE에서 AnimationBlueprint의 역할이다.
	// 다만 현재 AnimationBlueprint가 구현되어있지 않기때문에, 이름을 다르게 구분지었다. 
	enum Type { AnimationClass, AnimationSingleNode };
}

// 언리얼 USkeletalMeshComponent에 해당한다.
// 부모 기준 로컬 포즈(BoneSpaceTransforms)를 가지고 포즈를 만든다. 스키닝과 렌더는 USkinnedMeshComponent가 맡는다.
// AnimInstance의 생성과 수명을 관리한다.
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	virtual void TickComponent(float DeltaTime) override;
	virtual void InitializeComponent() override;

	void SetAnimInstanceClass(UClass* NewClass);
	UAnimInstance* GetAnimInstance() const { return AnimScriptInstance; }
	void InitAnim(bool bForceReinit);
	void TickAnimation(float DeltaTime);
	void SetAnimationMode(EAnimationMode::Type InAnimationMode, bool bForceInitAnimScriptInstance = true);
	EAnimationMode::Type GetAnimationMode() const { return AnimationMode; }
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping);
	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping);
	void Stop();
	bool IsPlaying() const;
	void SetPosition(float InPos, bool bFireNotifies = true);
	float GetPosition() const;
	void SetPlayRate(float Rate);
	float GetPlayRate() const;

	// 부모 기준 자세 하나를 바꾸고 다시 누적 + 스키닝한다
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& LocalTransform);

	void SetBoneRotation(int32 BoneIndex, const FRotator& Rotation);

	void ResetToRefPose();

	// 부모 기준. 언리얼 USkeletalMeshComponent처럼 FTransform으로 두어 에디터가 위치 / 회전 / 스케일을 그대로 고친다
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }

	virtual void RefreshBoneTransforms() override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void InitPoseFromMesh() override;
	bool InitializeAnimScriptInstance(bool bForceReinit);
	void ClearAnimScriptInstance();
	bool FinalizePoseEvaluationResult(const FAnimPose& InFinalPose);

private:
	bool IsValidBoneIndex(int32 BoneIndex) const { return BoneIndex >= 0 && BoneIndex < BoneSpaceTransforms.Num(); }

	TArray<FTransform> BoneSpaceTransforms;
	UClass* AnimClass = nullptr;
	EAnimationMode::Type AnimationMode = EAnimationMode::AnimationClass;
	UAnimInstance* AnimScriptInstance = nullptr;
};
