#pragma once

#include "Render/SkinningSettings.h"
#include "Render/VertexFactory.h"

struct FVertex;
class FIndexBuffer;
class USkinnedMeshComponent;
class USkeletalMesh;

class FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObject(const USkeletalMesh& InMesh)
		: Mesh(&InMesh)
	{}
	virtual ~FSkeletalMeshObject() = default;

	FSkeletalMeshObject(const FSkeletalMeshObject&) = delete;
	const FSkeletalMeshObject& operator=(const FSkeletalMeshObject&) = delete;
	FSkeletalMeshObject(FSkeletalMeshObject&&) = delete;
	const FSkeletalMeshObject& operator=(FSkeletalMeshObject&&) = delete;

	// 현재 포즈로 스키닝 행렬을 만들어 GPU 올린다. 실패하면 false
	bool Update(const USkinnedMeshComponent& Component);

	virtual ESkinningMode GetSkinningMode() const = 0;
	const FVertexFactory& GetVertexFactory() const { return VertexFactory; }
	FIndexBuffer* GetIndexBuffer() const;
	const TArray<FMatrix>& GetReferenceToLocalMatrices() const { return ReferenceToLocal; }

	static TUniquePtr<FSkeletalMeshObject> Create(const USkeletalMesh& InMesh, ESkinningMode Mode);

protected:
	virtual bool UpdateSkin() = 0;

	const USkeletalMesh* Mesh;
	FVertexFactory VertexFactory;
	TArray<FMatrix> ReferenceToLocal;
};

class FSkeletalMeshObjectGPUSkin final : public FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObjectGPUSkin(const USkeletalMesh& InMesh);

	ESkinningMode GetSkinningMode() const override { return ESkinningMode::GPU; }

private:
	bool UpdateSkin() override;
	TUniquePtr<FStructuredBuffer> BoneBuffer;
};

class FSkeletalMeshObjectCPUSkin final : public FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObjectCPUSkin(const USkeletalMesh& InMesh);

	ESkinningMode GetSkinningMode() const override { return ESkinningMode::CPU; }

private:
	bool UpdateSkin() override;
	TArray<FVertex> SkinnedVertices;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
};
