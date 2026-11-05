#pragma once

#include "PrimitiveComponent.h"
#include "LightComponentBase.h"

struct FSubUVConstants
{
	// 한 파티클을 그릴 때 Shader에 전달할 Atlas 프레임과 투명도 값을 담는다.
	float CurrentFrame;
	float AtlasRowSize;
	float AtlasColSize;
	float Alpha;
	FVector4 Tint = FVector4(1, 1, 1, 1); // default 흰색
	// 1이면 텍스처·Tint를 sRGB -> 선형으로 바꾼다 (HDR 씬에 그리는 파티클·빌보드).
	// 에디터 아이콘은 톤 매핑 이후 LDR 타깃에 그리므로 0 (sRGB 값 그대로).
	float DecodeSRGB = 1.0f;
	float Padding[3]{};
};
static_assert(sizeof(FSubUVConstants) == 48, "ParticleSubUVShader.hlsl cbuffer subuv와 크기가 같아야 한다");

class UWorld;

// View 정보 없이 카메라를 향하는 쿼드 행렬이 필요할 때(게임 Trace, 기본 카메라 경로) World의 게임 카메라 쪽으로 세운다.
// 게임 카메라가 없는 에디터 World에서는 FallbackRotation 방향을 쓴다. 쿼드의 X는 정면, Y·Z는 폭·높이 축이다.
FMatrix MakePlayCameraFacingMatrix(UWorld* World, const FRotator& FallbackRotation, const FVector& Position, const FVector& Scale);

class UBillboardComponent : public UPrimitiveComponent
{
	DECLARE_CLASS(UBillboardComponent, UPrimitiveComponent)

	REFLECT_START(ClassName)
	REFLECT_END()

public:
	UBillboardComponent();
	virtual ~UBillboardComponent() override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	// View별 렌더 상태는 Billboard 전용 Proxy가 준비한다.
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit) override;
	// 클릭한 View의 실제 렌더 행렬로 Quad Mesh 교차를 판정한다.
	bool LineTraceComponentForView(const FRay& WorldRay, FHitResult& OutHit, const FMatrix& BillboardWorldMatrix);

	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const override { return SlotIndex == 0 ? Material : nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) override
	{
		if (SlotIndex == 0)
		{
			Material = InMaterial;
		}
	}

	virtual void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override
	{
		Super::AddReferencedMaterials(OutMaterials);
		OutMaterials.Add(Material);
	}

	void SetTintSource(const ULightComponentBase* InSource) { TintSource = InSource; }
	virtual void OnSiblingComponentDestroyed(UActorComponent* DestroyedComponent) override;

	virtual const FStaticMeshData* GetMeshData() const override { return QuadMesh ? &QuadMesh->GetMeshData() : nullptr; }
	// View마다 카메라 쪽으로 돌아가므로 어느 방향으로 돌아도 쿼드를 감싸는 상자를 BVH에 넘긴다.
	virtual FBox CalcLocalBounds() const override;

	void GetWorldTransformedMatrix(FMatrix* OutWorldMatrix) const;
	// 기본 카메라 기준 월드 행렬로 Billboard 렌더 패킷을 제출한다.
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue) override;
	// 지정 행렬로 기본 카메라 경로의 렌더 패킷을 구성한다.
	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FMatrix& BillboardWorldMatrix);

	virtual void Serialize(json& Handle, bool bIsLoading) override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	bool IsEditorIcon() const { return bEditorIcon; }
	void SetEditorIcon(bool IsEditorIcon) { bEditorIcon = IsEditorIcon; }
	void SetIconSize(float S)
	{
		IconSize = S;
		MarkRenderStateDirty();
	}
	// 화면에 그릴 쿼드의 폭·높이. 에디터 아이콘은 IconSize, 그 외에는 월드 스케일 Y·Z다.
	// 렌더(Proxy)·피킹·바운드가 모두 이 값을 써야 보이는 크기와 클릭 영역이 맞는다.
	FVector2 GetRenderSize() const
	{
		if (IsEditorIcon())
		{
			return FVector2(IconSize, IconSize);
		}
		const FVector Scale = GetWorldScale3D();
		return FVector2(Scale.Y, Scale.Z);
	}

protected:
	UMaterial* Material = nullptr;
	UStaticMesh* QuadMesh = nullptr;
	FSubUVConstants IconParams{ 0.0f, 1.0f, 1.0f, 1.0f }; // CurrentFrame, RowSize, ColSize, Alpha
private:
	friend class FBillboardSceneProxy;
	const ULightComponentBase* TintSource = nullptr;
	bool bEditorIcon = false;
	float IconSize = 1.0f;
};
