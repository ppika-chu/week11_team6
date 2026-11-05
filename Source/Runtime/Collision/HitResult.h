#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"

class UPrimitiveComponent;
class AActor;

struct FHitResult
{
	int32 FaceIndex = -1;

	float Time = 1.0f;
	float Distance = FLT_MAX;

	FVector Location;
	FVector ImpactPoint;
	FVector Normal;
	FVector ImpactNormal;

	FVector TraceStart;
	FVector TraceEnd;

	float PenetrationDepth = 0.0f;

	bool bBlockingHit = false;
	bool bStartPenetrating = false;

	UPrimitiveComponent* HitComponent = nullptr;
	AActor* HitActor = nullptr;

	FHitResult() = default;

	explicit FHitResult(float InTime)
	{
		Time = InTime;
	}

	void Init()
	{
		*this = FHitResult();
	}

	void Init(const FVector& Start, const FVector& End)
	{
		Init();
		TraceStart = Start;
		TraceEnd = End;
	}

	AActor* GetActor() const { return HitActor; }
	UPrimitiveComponent* GetComponent() const { return HitComponent; }

	static FHitResult GetReversedHit(const FHitResult& Hit, UPrimitiveComponent* MovedComponent, AActor* MovedActor)
	{
		FHitResult Result = Hit;
		Result.Normal = -Hit.Normal;
		Result.ImpactNormal = -Hit.ImpactNormal;
		Result.HitComponent = MovedComponent;
		Result.HitActor = MovedActor;
		return Result;
	}
};
