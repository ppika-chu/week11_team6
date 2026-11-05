#pragma once

#include "ShapeComponent.h"

class UCapsuleComponent : public UShapeComponent
{
	DECLARE_CLASS(UCapsuleComponent, UShapeComponent)
	REFLECT_START(UCapsuleComponent)
		PROPERTY(CapsuleHalfHeight)
		PROPERTY(CapsuleRadius)
	REFLECT_END()

public:
	virtual FCollisionShape GetCollisionShape() const override;
	virtual FBox CalcLocalBounds() const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetCapsuleSize(float InRadius, float InHalfHeight);
	float GetScaledCapsuleRadius() const;
	float GetScaledCapsuleHalfHeight() const;
	float GetScaledCapsuleHalfHeight_WithoutHemisphere() const;

protected:
	// 반구를 포함한 높이
	float CapsuleHalfHeight = 1.0f;
	float CapsuleRadius = 0.5f;
};
