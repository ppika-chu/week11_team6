#pragma once

#include "SceneComponent.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Collision/CollisionShape.h"
// PROPERTY가 UMaterial이 UObject 파생인지 검사하므로 완전한 타입이 필요하다.
#include "Render/Material.h"

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		// 디테일 패널의 텍스처 슬롯으로 편집한다. 저장은 머티리얼 인스턴스까지 다루는 Serialize가 따로 한다.
		PROPERTY_FLAGS(DecalMaterial, CPF_EditAnywhere | CPF_Transient)
		PROPERTY(DecalSize)
		PROPERTY_TYPE(DecalColor, LinearColor)
		PROPERTY(SortOrder)
		PROPERTY(FadeStartDelay)
		PROPERTY(FadeDuration)
		PROPERTY(FadeInStartDelay)
		PROPERTY(FadeInDuration)
		PROPERTY(bDestroyOwnerAfterFade)
	REFLECT_END()

public:
	UDecalComponent();

	virtual void BeginPlay() override;
	// 게임 월드에서만 불린다. 에디터 월드에서는 FadeAlpha가 1로 남아 항상 보인다.
	virtual void TickComponent(float DeltaTime) override;
	virtual void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override { OutMaterials.Add(DecalMaterial); }

	void SetDecalMaterial(UMaterial* NewDecalMaterial);
	UMaterial* GetDecalMaterial() const;

	void SetDecalSize(const FVector& Size);
	FVector GetDecalSize() const;

	void SetDecalColor(const FLinearColor& Color);
	FLinearColor GetDecalColor() const;

	void SetSortOrder(int32 Order);
	int32 GetSortOrder() const;

	void SetFadeStartDelay(float Delay);
	float GetFadeStartDelay() const;

	void SetFadeDuration(float Duration);
	float GetFadeDuration() const;

	void SetFadeInStartDelay(float Delay);
	float GetFadeInStartDelay() const;

	void SetFadeInDuration(float Duration);
	float GetFadeInDuration() const;

	void SetDestroyOwnerAfterFade(bool bDestroy);
	bool GetDestroyOwnerAfterFade() const;

	float GetFadeAlpha() const;

	FMatrix GetDecalWorldMatrix() const;

	// Decal NDC는 -1 <= z <= 1
	FMatrix GetDecalViewProjection() const;

	Collision::FOrientedBox GetOrientedBox() const;
	virtual FBox CalcLocalBounds() const override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	virtual bool ShouldCreateRenderState() const override;
	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

	UMaterial* DecalMaterial = nullptr;
	FVector DecalSize = FVector(1.0f, 1.0f, 1.0f);
	FLinearColor DecalColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	int32 SortOrder = 0;
	float FadeStartDelay = 0.0f;
	float FadeDuration = 0.0f;
	float FadeInStartDelay = 0.0f;
	float FadeInDuration = 0.0f;
	bool bDestroyOwnerAfterFade = true;

private:
	// FadeElapsedTime으로 페이드 인·아웃 값을 계산해 FadeAlpha에 넣는다. 페이드 아웃이 끝났으면 true.
	bool UpdateFadeAlpha();

	float FadeElapsedTime = 0.0f;
	float FadeAlpha = 1.0f;
};
