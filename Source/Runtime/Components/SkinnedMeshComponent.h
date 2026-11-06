#pragma once

#include "MeshComponent.h"
// PROPERTY(SkeletalMesh) 리플렉션이 UObject 파생 여부를 검사하므로 전방 선언이 아닌 완전한 정의가 필요하다
#include "Render/SkeletalMesh.h"
#include "Engine/SkeletalMeshSceneProxy.h"
#include "Render/SkinningSettings.h"

// 언리얼 USkinnedMeshComponent에 해당한다.
// 본 행렬(컴포넌트 기준)을 받아 스키닝하고 그리는 일만 맡는다. 포즈를 어떻게 만드는지는 모른다.
// 포즈 생성(로컬 포즈 편집, 애니메이션)은 파생 클래스인 USkeletalMeshComponent가 맡는다.
class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	REFLECT_START(USkinnedMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()
public:
	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent() override;

	virtual void BeginPlay() override;

	// 메시가 바뀌면 슬롯 구성이 달라지므로 덮어쓰기를 비운다
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	virtual const FStaticMeshData* GetMeshData() const override;
	virtual UStaticMesh* GetRenderMesh() const override { return RenderMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override { return new FSkeletalMeshSceneProxy(this); }

	int32 GetBoneCount() const { return ComponentSpaceTransforms.Num(); }
	int32 FindBoneIndex(const FString& BoneName) const;
	FVector GetBoneLocation(int32 BoneIndex) const;

	const TArray<FMatrix>& GetComponentSpaceTransforms() const { return ComponentSpaceTransforms; }
	const TArray<FMatrix>& GetSkinningMatrices() const { return SkinningMatrices; }
	const TArray<FVertex>& GetSkinnedVertices() const { return SkinnedVertices; }

	// 현재 포즈로 본 행렬을 다시 계산하고 스키닝한다.
	// 이 클래스는 포즈 입력이 없으므로 기준 자세를 쓴다. 파생 클래스가 자기 포즈로 덮어쓴다.
	virtual void RefreshBoneTransforms();

	void SubmitToRenderQueue(FRenderQueue& Queue) override;

	// Log
	void LogBoneTransforms() const;
	void LogSkinningMatrices() const;

protected:
	// 메시가 바뀌었을 때 배열 크기와 렌더 메시를 다시 만든다. 파생 클래스는 자기 포즈를 준비한 뒤 Super를 부른다
	virtual void InitPoseFromMesh();
	void EnsurePoseInitialized();

	// 부모 기준 로컬 포즈를 부모부터 누적해 ComponentSpaceTransforms를 채운다
	void FillComponentSpaceTransforms(const TArray<FTransform>& LocalPose);
	// ComponentSpaceTransforms로 스키닝 행렬을 만들고 정점을 갱신한다
	void FinalizeBoneTransform();

	USkeletalMesh* SkeletalMesh = nullptr;

	TArray<FMatrix> ComponentSpaceTransforms;
	TArray<FMatrix> SkinningMatrices;

private:
	void CreateRenderMesh();
	void DestroyRenderMesh();
	void UpdateSkinnedVertices();

	USkeletalMesh* PoseSourceMesh = nullptr;
	UStaticMesh* RenderMesh = nullptr;
	TArray<FVertex> SkinnedVertices;
	bool bVertexBufferIsBindPose = false;
};
