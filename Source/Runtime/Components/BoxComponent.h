#pragma once

#include "ShapeComponent.h"

class UBoxComponent : public UShapeComponent
{
	DECLARE_CLASS(UBoxComponent, UShapeComponent)
	REFLECT_START(UBoxComponent)
		PROPERTY(BoxExtent)
	REFLECT_END()

public:
	virtual FCollisionShape GetCollisionShape() const override;
	virtual FBox CalcLocalBounds() const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetBoxExtent(const FVector& InExtent);
	FVector GetScaledBoxExtent() const;

protected:
	FVector BoxExtent{ 0.5f, 0.5f, 0.5f };
};
