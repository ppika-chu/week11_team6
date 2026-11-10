#pragma once

#include "SkinnedMeshComponent.h"
// PROPERTY(AnimToPlay)가 UObject 파생인지 검사하므로 전방 선언이 아니라 정의가 필요하다
#include "Animation/AnimationAsset.h"

class UAnimInstance;
class UAnimSingleNodeInstance;
struct FAnimPose;

namespace EAnimationMode
{
	// 기존 Class·SingleNode 저장값을 유지한다. Default는 Lua Graph를 구성할 엔진 제공 Instance다.
	enum Type { AnimationClass, AnimationSingleNode, AnimationDefault };
}

// AnimInstance를 생성·정리하고 평가 결과를 BoneSpaceTransforms에 반영한다.
// Component Space 변환·스키닝·렌더 데이터 갱신은 USkinnedMeshComponent의 처리를 사용한다.
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	// UE는 AnimToPlay~SavedPlayRate를 FSingleAnimationPlayData AnimationData로 묶는다.
	// PROPERTY가 구조체 안 멤버를 Details에 펼치지 못해 같은 이름으로 컴포넌트에 둔다.
	REFLECT_START(USkeletalMeshComponent)
		PROPERTY_ENUM(AnimationMode, "Custom Anim Instance", "Single Node Instance", "Default Anim Instance")
		PROPERTY_FLAGS(AnimClassName, CPF_None)
		PROPERTY(AnimToPlay)
		PROPERTY(bSavedLooping)
		PROPERTY(bSavedPlaying)
		PROPERTY(SavedPosition)
		PROPERTY(SavedPlayRate)
	REFLECT_END()

public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void InitializeComponent() override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;
	virtual bool CanEditChange(const FProperty& Property) const override;

	// Use Animation Asset 모드에서 인스턴스를 만들 때 넘기는 값 (UE FSingleAnimationPlayData)
	UAnimationAsset* AnimToPlay = nullptr;
	bool bSavedLooping = true;
	bool bSavedPlaying = true;
	float SavedPosition = 0.0f;
	float SavedPlayRate = 1.0f;

	// Instance 구성과 갱신 — Class·재생 모드에 맞는 Instance를 준비하고 Update를 호출한다.
	void SetAnimInstanceClass(UClass* NewClass);
	UClass* GetAnimInstanceClass() const;
	const FString& GetAnimInstanceClassName() const { return AnimClassName; }
	UAnimInstance* GetAnimInstance() const { return AnimScriptInstance; }
	void InitAnim(bool bForceReinit);
	void TickAnimation(float DeltaTime);
	void SetAnimationMode(EAnimationMode::Type InAnimationMode, bool bForceInitAnimScriptInstance = true);
	EAnimationMode::Type GetAnimationMode() const { return AnimationMode; }

	// 단일 Asset 재생 제어 — PlayAnimation은 SingleNode 모드로 전환한다.
	// SetAnimation부터 GetPlayRate까지는 SingleNodeInstance가 있을 때만 상태를 제어·조회한다.
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

	// 부모 Bone 기준 Local Transform 배열. 편집과 Pose 평가 결과 반영에 사용한다.
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }

	virtual void RefreshBoneTransforms() override;

	// 인스턴스가 이번 Update에서 모은 Root Motion을 꺼낸다. 컴포넌트 공간 변화량 (UE USkeletalMeshComponent::ConsumeRootMotion)
	FRootMotionMovementParams ConsumeRootMotion();
	// 컴포넌트 공간 변화량을 액터에 줄 월드 변화량으로 바꾼다. 메시가 루트가 아니어도 맞는다 (UE ConvertLocalRootMotionToWorld)
	FTransform ConvertLocalRootMotionToWorld(const FTransform& LocalRootMotion) const;

protected:
	// UE에서는 UCharacterMovementComponent가 ConsumeRootMotion으로 꺼내 액터를 옮긴다.
	// 그런 이동 컴포넌트가 없어 Tick에서 메시가 직접 옮긴다. 프리뷰 컴포넌트는 따로 처리하려고 재정의한다
	virtual void ApplyRootMotionToOwner();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void InitPoseFromMesh() override;
	bool InitializeAnimScriptInstance(bool bForceReinit);
	void ClearAnimScriptInstance();
	bool FinalizePoseEvaluationResult(const FAnimPose& InFinalPose);

private:
	UClass* GetAnimationInstanceClassForMode() const;
	bool IsValidBoneIndex(int32 BoneIndex) const { return BoneIndex >= 0 && BoneIndex < BoneSpaceTransforms.Num(); }

	TArray<FTransform> BoneSpaceTransforms;
	// 클래스 참조는 이름으로 저장하고 등록된 클래스에서 찾는다. Details는 클래스 목록으로 편집한다.
	FString AnimClassName;
	EAnimationMode::Type AnimationMode = EAnimationMode::AnimationDefault;
	UAnimInstance* AnimScriptInstance = nullptr; // 이 Component가 생성하고 ClearAnimScriptInstance에서 정리하는 Instance.
};
