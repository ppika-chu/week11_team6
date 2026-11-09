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

	// 위치, 회전, 크기 중 NaN이나 무한대가 있으면 true다.
	bool ContainsNaN() const;

	// UE FTransform과 같은 순서: A * B는 A를 먼저 적용하고 B를 적용한다. 크기는 균일하다고 본다.
	FTransform operator*(const FTransform& Other) const;
	FTransform Inverse() const;
	// this = Result * Other 인 Result (UE FTransform::GetRelativeTransform)
	FTransform GetRelativeTransform(const FTransform& Other) const { return *this * Other.Inverse(); }
	FVector TransformPosition(const FVector& V) const;

	FVector Location = FVector(0, 0, 0);
	FVector Scale = FVector(1, 1, 1);
	FQuat Rotation = FQuat::Identity();
};
