#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"

class FPrimitiveSceneProxy;

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane,

	Particle,
	ParticleQuad
};

class UPrimitiveComponent : public USceneComponent
{
	DECLARE_CLASS(UPrimitiveComponent, USceneComponent)

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
	REFLECT_START(ClassName)
		PROPERTY(bVisible)
		PROPERTY(bCastShadow)
		PROPERTY(bReceiveShadow)
		PROPERTY(bReceivesDecals)
		PROPERTY(bGenerateOverlapEvents)
		PROPERTY(bBlockComponent)
	REFLECT_END()
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;

	bool IsBlockComponent() const { return bBlockComponent; }
	void SetBlockComponent(bool bInBlock) { bBlockComponent = bInBlock; }
	bool GetGenerateOverlapEvents() const { return bGenerateOverlapEvents; }
	void SetGenerateOverlapEvents(bool bInGenerate) { bGenerateOverlapEvents = bInGenerate; }

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}

	bool IsVisible() const { return bVisible; }
	void SetVisible(bool bInVisible)
	{
		bVisible = bInVisible;
		MarkRenderStateDirty();
	}

	bool IsShadowCaster() const { return bCastShadow; }
	bool IsShadowReceiver() const { return bReceiveShadow; }

	void SetCastShadow(bool bInCastShadow)
	{
		bCastShadow = bInCastShadow;
		MarkRenderStateDirty();
	}

	void SetReceiveShadow(bool bInReceiveShadow)
	{
		bReceiveShadow = bInReceiveShadow;
		MarkRenderStateDirty();
	}

	// 꺼져 있으면 데칼 박스와 겹쳐도 Decal Pass 대상에서 빠진다.
	bool ReceivesDecals() const { return bReceivesDecals; }
	void SetReceivesDecals(bool bInReceivesDecals) { bReceivesDecals = bInReceivesDecals; }

	// Details에서 직접 바꾼 가시성도 Proxy의 렌더 상태에 반영한다.
	void PostEditChangeProperty(const FProperty& Property) override;

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }
	virtual void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override;

	// Proxy의 구체 타입만 선택하며 Scene이 등록과 삭제를 담당한다.
	virtual FPrimitiveSceneProxy* CreateSceneProxy();

	void MarkRenderStateDirty();

	virtual void OnTransformDirty() override;

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

protected:
	virtual bool ShouldCreateRenderState() const override
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;


	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);
	bool bVisible = true;
	bool bGenerateOverlapEvents = false;
	bool bBlockComponent = false;

private:
	friend class FScene;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
	bool bCastShadow = true;
	bool bReceiveShadow = true;
	bool bReceivesDecals = true;
};
