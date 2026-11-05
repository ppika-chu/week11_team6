#include "EnginePCH.h"
#include "CapsuleComponent.h"
#include "Collision/CollisionShape.h"

FCollisionShape UCapsuleComponent::GetCollisionShape() const
{
	return FCollisionShape::MakeCapsule(GetScaledCapsuleRadius(), GetScaledCapsuleHalfHeight());
}

FBox UCapsuleComponent::CalcLocalBounds() const
{
	return FBox{ FVector{ -CapsuleRadius, -CapsuleRadius, -CapsuleHalfHeight },
		FVector{ CapsuleRadius, CapsuleRadius, CapsuleHalfHeight } };
}

void UCapsuleComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "CapsuleRadius")
	{
		CapsuleRadius = std::clamp(CapsuleRadius, 0.0f, CapsuleHalfHeight);
	}
	else if (Property.Name == "CapsuleHalfHeight")
	{
		CapsuleHalfHeight = std::max(CapsuleHalfHeight, CapsuleRadius);
	}
}

void UCapsuleComponent::SetCapsuleSize(float InRadius, float InHalfHeight)
{
	CapsuleRadius = std::max(0.0f, InRadius);
	CapsuleHalfHeight = std::max({ 0.0f, InHalfHeight, InRadius });
}

float UCapsuleComponent::GetScaledCapsuleRadius() const
{
	return CapsuleRadius * GetShapeScale();
}

float UCapsuleComponent::GetScaledCapsuleHalfHeight() const
{
	return CapsuleHalfHeight * GetShapeScale();
}

float UCapsuleComponent::GetScaledCapsuleHalfHeight_WithoutHemisphere() const
{
	return (CapsuleHalfHeight - CapsuleRadius) * GetShapeScale();
}
