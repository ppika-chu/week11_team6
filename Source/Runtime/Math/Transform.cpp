#include "EnginePCH.h"
#include "Transform.h"
#include <cmath>

namespace
{
	constexpr float ZERO_ANIMWEIGHT_THRESH = 0.00001f;
}

FTransform::FTransform(FVector InL, FVector InS, FQuat InR)
{
	Location = InL;
	Scale = InS;
	Rotation = InR;
}

void FTransform::Blend(const FTransform& A, const FTransform& B, float Alpha)
{
	if (fabsf(Alpha) <= ZERO_ANIMWEIGHT_THRESH)
	{
		*this = A;
	}
	else if (fabsf(Alpha - 1.0f) <= ZERO_ANIMWEIGHT_THRESH)
	{
		*this = B;
	}
	else
	{
		Location = FMath::Lerp(A.Location, B.Location, Alpha);
		Scale = FMath::Lerp(A.Scale, B.Scale, Alpha);
		Rotation = FQuat::FastLerp(A.Rotation, B.Rotation, Alpha).Normalized();
	}
}

bool FTransform::ContainsNaN() const
{
	return !std::isfinite(Location.X) || !std::isfinite(Location.Y) || !std::isfinite(Location.Z)
		|| !std::isfinite(Scale.X) || !std::isfinite(Scale.Y) || !std::isfinite(Scale.Z)
		|| !std::isfinite(Rotation.X) || !std::isfinite(Rotation.Y) || !std::isfinite(Rotation.Z) || !std::isfinite(Rotation.W);
}

FMatrix FTransform::GetLocalMatrix() const
{
	FMatrix RotationMatrix = Rotation.ToFMatrix();

	FMatrix WorldMatrix = FMatrix(
		RotationMatrix[0][0] * Scale.X, RotationMatrix[0][1] * Scale.X, RotationMatrix[0][2] * Scale.X, 0.0f,
		RotationMatrix[1][0] * Scale.Y, RotationMatrix[1][1] * Scale.Y, RotationMatrix[1][2] * Scale.Y, 0.0f,
		RotationMatrix[2][0] * Scale.Z, RotationMatrix[2][1] * Scale.Z, RotationMatrix[2][2] * Scale.Z, 0.0f,
		Location.X, Location.Y, Location.Z, 1.0f);

	return WorldMatrix;
}

FVector FTransform::GetForward() const
{
	FQuat Quat = GetOrientation();
	return Quat.RotateVector(FVector(1.0f, 0.0f, 0.0f));
}

FVector FTransform::GetUp() const
{
	FQuat Quat = GetOrientation();
	return Quat.RotateVector(FVector(0.0f, 0.0f, 1.0f));
}

FVector FTransform::GetRight() const
{
	return GetOrientation().RotateVector(FVector(0.0f, 1.0f, 0.0f));
}

FQuat FTransform::GetOrientation() const
{
	return Rotation;
}

FTransform FTransform::Identity = FTransform(FVector(0, 0, 0), FVector(1, 1, 1), FQuat::Identity());