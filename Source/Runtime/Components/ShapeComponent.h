#pragma once

#include "PrimitiveComponent.h"
#include "Core/TDelegate.h"

struct FCollisionShape;
struct FCollisionContact;
class FLineBatcher;
class AActor;

class UShapeComponent : public UPrimitiveComponent
{
	DECLARE_CLASS(UShapeComponent, UPrimitiveComponent)
	REFLECT_START(UShapeComponent)
		PROPERTY(ShapeColor)
		PROPERTY(bDrawOnlyIfSelected)
	REFLECT_END()

public:
	UShapeComponent();

	DECLARE_DELEGATE(OnComponentBeginOverlap, UPrimitiveComponent*, AActor*, UPrimitiveComponent*)
	DECLARE_DELEGATE(OnComponentEndOverlap, UPrimitiveComponent*, AActor*, UPrimitiveComponent*)
	DECLARE_DELEGATE(OnComponentHit, UPrimitiveComponent*, AActor*, UPrimitiveComponent*, const FHitResult&)

	void OnBeginOverlap(UShapeComponent* Other);
	void OnEndOverlap(UShapeComponent* Other);
	void OnHit(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit);

	virtual FCollisionShape GetCollisionShape() const = 0;
	void DrawDebug(FLineBatcher& LineBatcher, bool bHighlight) const;

	bool IsOverlapping(const UShapeComponent& Other) const;
	bool ComputeContact(const UShapeComponent& Other, FCollisionContact& OutContact) const;
	bool ShouldDrawOnlyIfSelected() const;
	void SetDrawOnlyIfSelected(bool bInDrawOnlyIfSelected) { bDrawOnlyIfSelected = bInDrawOnlyIfSelected; }
	float GetShapeScale() const;

protected:
	virtual bool ShouldCreateRenderState() const override;

	FColor ShapeColor{ 223, 149, 157, 255 };
	bool bDrawOnlyIfSelected = false;
};
