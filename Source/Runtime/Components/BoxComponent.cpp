#include "EnginePCH.h"
#include "BoxComponent.h"
#include "Collision/CollisionShape.h"

FCollisionShape UBoxComponent::GetCollisionShape() const
{
	return FCollisionShape::MakeBox(GetScaledBoxExtent());
}

FBox UBoxComponent::CalcLocalBounds() const
{
	return FBox{ -BoxExtent, BoxExtent };
}

void UBoxComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	BoxExtent = FVector{
		std::max(BoxExtent.X, 0.0f),
		std::max(BoxExtent.Y, 0.0f),
		std::max(BoxExtent.Z, 0.0f)
	};
}

void UBoxComponent::SetBoxExtent(const FVector& InExtent)
{
	BoxExtent = FVector{
		std::max(InExtent.X, 0.0f),
		std::max(InExtent.Y, 0.0f),
		std::max(InExtent.Z, 0.0f)
	};
}

FVector UBoxComponent::GetScaledBoxExtent() const
{
	FVector Scale = GetWorldScale3D();
	return FVector(BoxExtent.X * std::abs(Scale.X),
		BoxExtent.Y * std::abs(Scale.Y),
		BoxExtent.Z * std::abs(Scale.Z));
}
