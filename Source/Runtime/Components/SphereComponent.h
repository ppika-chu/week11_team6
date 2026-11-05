#pragma once

#include "ShapeComponent.h"

class USphereComponent : public UShapeComponent
{
	DECLARE_CLASS(USphereComponent, UShapeComponent)
	REFLECT_START(USphereComponent)
		PROPERTY(SphereRadius)
	REFLECT_END()

public:
	virtual FCollisionShape GetCollisionShape() const override;
	virtual FBox CalcLocalBounds() const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetSphereRadius(float InRadius);
	float GetScaledSphereRadius() const;

protected:
	float SphereRadius = 0.5f;
};
