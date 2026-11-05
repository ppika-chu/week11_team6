#pragma once

#include "SkinnedMeshComponent.h"

// 언리얼 USkeletalMeshComponent에 해당한다.
// 부모 기준 로컬 포즈(BoneSpaceTransforms)를 가지고 포즈를 만든다. 스키닝과 렌더는 USkinnedMeshComponent가 맡는다.
// 나중에 애니메이션(AnimInstance)과 물리 바디가 여기에 붙는다.
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	virtual void TickComponent(float DeltaTime) override;

	// 부모 기준 자세 하나를 바꾸고 다시 누적 + 스키닝한다
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& LocalTransform);

	void SetBoneRotation(int32 BoneIndex, const FRotator& Rotation);

	void ResetToRefPose();

	// 부모 기준. 언리얼 USkeletalMeshComponent처럼 FTransform으로 두어 에디터가 위치 / 회전 / 스케일을 그대로 고친다
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }

	virtual void RefreshBoneTransforms() override;

protected:
	virtual void InitPoseFromMesh() override;

private:
	bool IsValidBoneIndex(int32 BoneIndex) const { return BoneIndex >= 0 && BoneIndex < BoneSpaceTransforms.Num(); }

	TArray<FTransform> BoneSpaceTransforms;
};
