#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

enum class ECollisionShape : uint8
{
	None,
	Box,
	Sphere,
	Capsule
};

struct FCollisionShape
{
	ECollisionShape ShapeType = ECollisionShape::None;
	FVector BoxHalfExtent;
	float Radius = 0.0f;
	float HalfHeight = 0.0f;

	static FCollisionShape MakeBox(const FVector& HalfExtent);
	static FCollisionShape MakeSphere(float Radius);
	static FCollisionShape MakeCapsule(float Radius, float HalfHeight);

	bool IsBox() const;
	bool IsSphere() const;
	bool IsCapsule() const;
	float GetCapsuleAxisHalfLength() const;
};

struct FCollisionContact
{
	FVector Normal;
	float PenetrationDepth = 0.0f;
	FVector Point;
};

namespace Collision
{
struct FCapsuleSegment
{
	FVector A, B;
	float Radius = 0.0f;
};

struct FOrientedBox
{
	FVector Center;
	FVector Axis[3];
	FVector Extent;
};

FCapsuleSegment ToCapsuleSegment(const FCollisionShape& Shape, const FVector& Pos, const FQuat& Rot);
FOrientedBox ToOrientedBox(const FCollisionShape& Shape, const FVector& Pos, const FQuat& Rot);

bool Overlap(const FCollisionShape& A, const FVector& PosA, const FQuat& RotA,
	const FCollisionShape& B, const FVector& PosB, const FQuat& RotB,
	FCollisionContact* OutContact = nullptr);

bool OverlapCapsuleCapsule(const FCapsuleSegment& A, const FCapsuleSegment& B, FCollisionContact* OutContact);
bool OverlapCapsuleBox(const FCapsuleSegment& Capsule, const FOrientedBox& Box, FCollisionContact* OutContact);
bool OverlapBoxBox(const FOrientedBox& A, const FOrientedBox& B, FCollisionContact* OutContact);

FVector ClosestPointOnSegment(const FVector& P, const FVector& A, const FVector& B);
float ClosestPointsSegmentSegment(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2, FVector& OutC1, FVector& OutC2);
FVector ClosestPointOnBox(const FVector& P, const FOrientedBox& Box);

struct FSweepTarget
{
	FCollisionShape Shape;
	FVector Pos;
	FQuat Rot;
};

struct FSweepResult
{
	bool bBlockingHit = false;
	bool bStartPenetrating = false;
	float Time = 1.0f;
	int32 TargetIndex = -1;
	FCollisionContact Contact;
};

FSweepResult Sweep(const FCollisionShape& Shape, const FVector& Start, const FVector& Delta, const FQuat& Rot,
	const TArray<FSweepTarget>& Targets);
}
