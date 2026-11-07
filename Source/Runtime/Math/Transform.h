#pragma once

#include "Math/EngineMath.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"

struct FTransform
{
	FTransform() = default;
	FTransform(FVector InL, FVector InS, FQuat InR);
	FTransform(const FQuat& InR, const FVector& InL, const FVector& InS = FVector::OneVector)
		: Location(InL)
		, Rotation(InR)
		, Scale(InS)
	{
	}

	static FTransform Identity;

	static FTransform Blend(const FTransform& A, const FTransform& B, float Alpha);

	FMatrix GetLocalMatrix() const;

	FVector GetForward() const;
	FVector GetUp() const;
	FVector GetRight() const;
	FQuat GetOrientation() const;

	FVector Location = FVector(0, 0, 0);
	FVector Scale = FVector(1, 1, 1);
	FQuat Rotation = FQuat::Identity();
};
