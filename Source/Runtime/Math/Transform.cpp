#include "EnginePCH.h"
#include "Transform.h"

#include <algorithm>

FTransform::FTransform(FVector InL, FVector InS, FRotator InR)
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

	const FQuat From = A.Rotation.Quaternion();
	const FQuat To = B.Rotation.Quaternion();
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
	Result.Rotation = BlendedRotation.Normalized().ToFRotator();
	return Result;
}

FMatrix FTransform::GetLocalMatrix() const
{
	FMatrix RotationMatrix = Rotation.Quaternion().ToFMatrix();

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
	return FQuat::MakeFromEuler(FMath::DegreesToRadians(Rotation.Roll), FMath::DegreesToRadians(Rotation.Pitch), FMath::DegreesToRadians(Rotation.Yaw));
}

FTransform FTransform::Identity = FTransform(FVector(0, 0, 0), FVector(1, 1, 1), FRotator(0, 0, 0));
