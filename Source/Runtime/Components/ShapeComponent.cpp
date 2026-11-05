#include "EnginePCH.h"
#include "ShapeComponent.h"
#include "Collision/CollisionShape.h"
#include "Render/LineBatcher.h"

namespace
{
constexpr int32 CircleSegment = 32;

// 각도는 모두 라디안
void AddArc(FLineBatcher& LineBatcher, const FVector& C, const FVector& U, const FVector& V,
	float R, float StartAngleRad, float EndAngleRad, const FVector4& Color)
{
	const int32 Segments = std::max(1, static_cast<int32>(CircleSegment * (EndAngleRad - StartAngleRad) / (2.0f * PI)));
	const float StepRad = (EndAngleRad - StartAngleRad) / Segments;

	FVector Prev = C + U * (std::cos(StartAngleRad) * R) + V * (std::sin(StartAngleRad) * R);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float AngleRad = StartAngleRad + StepRad * i;
		const FVector Point = C + U * (std::cos(AngleRad) * R) + V * (std::sin(AngleRad) * R);
		LineBatcher.AddLine(Prev, Point, Color);
		Prev = Point;
	}
}
}

UShapeComponent::UShapeComponent()
{
}

void UShapeComponent::OnBeginOverlap(UShapeComponent* Other)
{
	OnComponentBeginOverlap.Broadcast(this, Other ? Other->GetOwner() : nullptr, Other);
}

void UShapeComponent::OnEndOverlap(UShapeComponent* Other)
{
	OnComponentEndOverlap.Broadcast(this, Other ? Other->GetOwner() : nullptr, Other);
}

void UShapeComponent::OnHit(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit)
{
	OnComponentHit.Broadcast(this, OtherActor, OtherComp, Hit);
}

bool UShapeComponent::ShouldCreateRenderState() const
{
	return false;
}

void UShapeComponent::DrawDebug(FLineBatcher& LineBatcher, bool bHighlight) const
{
	FCollisionShape Shape = GetCollisionShape();
	FVector Pos = GetWorldLocation();
	FQuat Rot = GetWorldRotation().Quaternion();
	FVector4 Color = bHighlight ? FVector4(1.0f, 0.0f, 0.0f, 1.0f) : ShapeColor.ToVector4();

	if (Shape.IsBox())
	{
		Collision::FOrientedBox Box = Collision::ToOrientedBox(Shape, Pos, Rot);

		FVector V0 = Box.Center + Box.Axis[0] * Box.Extent.X + Box.Axis[1] * Box.Extent.Y + Box.Axis[2] * Box.Extent.Z;
		FVector V1 = Box.Center + Box.Axis[0] * Box.Extent.X - Box.Axis[1] * Box.Extent.Y + Box.Axis[2] * Box.Extent.Z;
		FVector V2 = Box.Center + Box.Axis[0] * Box.Extent.X - Box.Axis[1] * Box.Extent.Y - Box.Axis[2] * Box.Extent.Z;
		FVector V3 = Box.Center + Box.Axis[0] * Box.Extent.X + Box.Axis[1] * Box.Extent.Y - Box.Axis[2] * Box.Extent.Z;
		FVector V4 = Box.Center - Box.Axis[0] * Box.Extent.X + Box.Axis[1] * Box.Extent.Y + Box.Axis[2] * Box.Extent.Z;
		FVector V5 = Box.Center - Box.Axis[0] * Box.Extent.X - Box.Axis[1] * Box.Extent.Y + Box.Axis[2] * Box.Extent.Z;
		FVector V6 = Box.Center - Box.Axis[0] * Box.Extent.X - Box.Axis[1] * Box.Extent.Y - Box.Axis[2] * Box.Extent.Z;
		FVector V7 = Box.Center - Box.Axis[0] * Box.Extent.X + Box.Axis[1] * Box.Extent.Y - Box.Axis[2] * Box.Extent.Z;

		LineBatcher.AddLine(V0, V1, Color);
		LineBatcher.AddLine(V1, V2, Color);
		LineBatcher.AddLine(V2, V3, Color);
		LineBatcher.AddLine(V3, V0, Color);
		LineBatcher.AddLine(V4, V5, Color);
		LineBatcher.AddLine(V5, V6, Color);
		LineBatcher.AddLine(V6, V7, Color);
		LineBatcher.AddLine(V7, V4, Color);
		LineBatcher.AddLine(V0, V4, Color);
		LineBatcher.AddLine(V1, V5, Color);
		LineBatcher.AddLine(V2, V6, Color);
		LineBatcher.AddLine(V3, V7, Color);
	}
	else if (Shape.IsSphere())
	{
		Collision::FCapsuleSegment Sphere = Collision::ToCapsuleSegment(Shape, Pos, Rot);

		const FVector F = Rot.GetForwardVector();
		const FVector Rt = Rot.GetRightVector();
		const FVector Up = Rot.GetUpVector();
		const float R = Sphere.Radius;

		AddArc(LineBatcher, Sphere.A, F, Rt, R, 0.0f, 2.0f * PI, Color);
		AddArc(LineBatcher, Sphere.A, F, Up, R, 0.0f, 2.0f * PI, Color);
		AddArc(LineBatcher, Sphere.A, Rt, Up, R, 0.0f, 2.0f * PI, Color);
	}
	else if (Shape.IsCapsule())
	{
		Collision::FCapsuleSegment Capsule = Collision::ToCapsuleSegment(Shape, Pos, Rot);

		const FVector F = Rot.GetForwardVector();
		const FVector Rt = Rot.GetRightVector();
		const FVector Up = Rot.GetUpVector();
		const float R = Capsule.Radius;

		AddArc(LineBatcher, Capsule.A, F, Rt, R, 0.0f, 2.0f * PI, Color);
		AddArc(LineBatcher, Capsule.B, F, Rt, R, 0.0f, 2.0f * PI, Color);

		LineBatcher.AddLine(Capsule.A + F * R, Capsule.B + F * R, Color);
		LineBatcher.AddLine(Capsule.A - F * R, Capsule.B - F * R, Color);
		LineBatcher.AddLine(Capsule.A + Rt * R, Capsule.B + Rt * R, Color);
		LineBatcher.AddLine(Capsule.A - Rt * R, Capsule.B - Rt * R, Color);

		AddArc(LineBatcher, Capsule.B, F, Up, R, 0.0f, PI, Color);
		AddArc(LineBatcher, Capsule.B, Rt, Up, R, 0.0f, PI, Color);
		AddArc(LineBatcher, Capsule.A, F, -Up, R, 0.0f, PI, Color);
		AddArc(LineBatcher, Capsule.A, Rt, -Up, R, 0.0f, PI, Color);
	}
}

bool UShapeComponent::IsOverlapping(const UShapeComponent& Other) const
{
	if (&Other == this || !CalcBounds().Intersects(Other.CalcBounds()))
	{
		return false;
	}

	return Collision::Overlap(GetCollisionShape(), GetWorldLocation(), GetWorldRotation().Quaternion(),
		Other.GetCollisionShape(), Other.GetWorldLocation(), Other.GetWorldRotation().Quaternion());
}

bool UShapeComponent::ComputeContact(const UShapeComponent& Other, FCollisionContact& OutContact) const
{
	if (&Other == this || !CalcBounds().Intersects(Other.CalcBounds()))
	{
		return false;
	}

	return Collision::Overlap(GetCollisionShape(), GetWorldLocation(), GetWorldRotation().Quaternion(),
		Other.GetCollisionShape(), Other.GetWorldLocation(), Other.GetWorldRotation().Quaternion(),
		&OutContact);
}

bool UShapeComponent::ShouldDrawOnlyIfSelected() const
{
	return bDrawOnlyIfSelected;
}

float UShapeComponent::GetShapeScale() const
{
	FVector Scale = GetWorldScale3D();
	return std::min({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
}
