#include "EnginePCH.h"
#include "Transform.h"
#include <cmath>

FTransform::FTransform(FVector InL, FVector InS, FQuat InR)
{
	Location = InL;
	Scale = InS;
	Rotation = InR;
}

FTransform FTransform::Blend(const FTransform& A, const FTransform& B, float Alpha)
{
	const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
	if (Weight <= 0.0f)
	{
		return A;
	}
	if (Weight >= 1.0f)
	{
		return B;
	}

	const FQuat From = A.Rotation;
	const FQuat To = B.Rotation;
	const float Dot = From.X * To.X + From.Y * To.Y + From.Z * To.Z + From.W * To.W;
	const float ToWeight = Dot < 0.0f ? -Weight : Weight;
	const float FromWeight = 1.0f - Weight;
	const FQuat BlendedRotation(
		From.X * FromWeight + To.X * ToWeight,
		From.Y * FromWeight + To.Y * ToWeight,
		From.Z * FromWeight + To.Z * ToWeight,
		From.W * FromWeight + To.W * ToWeight);

	FTransform Result;
	Result.Location = A.Location + (B.Location - A.Location) * Weight;
	Result.Scale = A.Scale + (B.Scale - A.Scale) * Weight;
	Result.Rotation = BlendedRotation.Normalized();
	return Result;
}

bool FTransform::ContainsNaN() const
{
	return !std::isfinite(Location.X) || !std::isfinite(Location.Y) || !std::isfinite(Location.Z)
		|| !std::isfinite(Scale.X) || !std::isfinite(Scale.Y) || !std::isfinite(Scale.Z)
		|| !std::isfinite(Rotation.X) || !std::isfinite(Rotation.Y) || !std::isfinite(Rotation.Z) || !std::isfinite(Rotation.W);
}

FTransform FTransform::operator*(const FTransform& Other) const
{
	FTransform Result;
	Result.Rotation = (Other.Rotation * Rotation).Normalized();
	Result.Scale = FVector(Scale.X * Other.Scale.X, Scale.Y * Other.Scale.Y, Scale.Z * Other.Scale.Z);
	Result.Location = Other.TransformPosition(Location);
	return Result;
}

FTransform FTransform::Inverse() const
{
	const auto SafeReciprocal = [](float Value) { return std::abs(Value) > 1.e-8f ? 1.0f / Value : 0.0f; };
	FTransform Result;
	Result.Rotation = Rotation.Inverse();
	Result.Scale = FVector(SafeReciprocal(Scale.X), SafeReciprocal(Scale.Y), SafeReciprocal(Scale.Z));
	// UE: InvRotation * (InvScale3D * -Translation)
	Result.Location = Result.Rotation.RotateVector(FVector(-Location.X * Result.Scale.X, -Location.Y * Result.Scale.Y, -Location.Z * Result.Scale.Z));
	return Result;
}

FVector FTransform::TransformPosition(const FVector& V) const
{
	return Rotation.RotateVector(FVector(V.X * Scale.X, V.Y * Scale.Y, V.Z * Scale.Z)) + Location;
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