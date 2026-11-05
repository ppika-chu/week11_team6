#include "EnginePCH.h"
#include "SpringArmComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
FVector VInterpTo(const FVector& Current, const FVector& Target, float DeltaTime, float Speed)
{
	if (Speed <= 0.0f)
	{
		return Target;
	}

	const FVector Dist = Target - Current;
	if (Dist.SizeSquared() < SMALL_NUMBER)
	{
		return Target;
	}

	const float Alpha = FMath::Clamp(DeltaTime * Speed, 0.0f, 1.0f);
	return Current + Dist * Alpha;
}

// -180 ~ 180 범위로 정규화 (359° → 1°로 갈 때 한 바퀴 돌지 않도록)
float NormalizeAxis(float Angle)
{
	Angle = std::fmod(Angle + 180.0f, 360.0f);
	if (Angle < 0.0f)
	{
		Angle += 360.0f;
	}
	return Angle - 180.0f;
}

FRotator RInterpTo(const FRotator& Current, const FRotator& Target, float DeltaTime, float Speed)
{
	if (Speed <= 0.0f)
	{
		return Target;
	}

	const float Alpha = FMath::Clamp(DeltaTime * Speed, 0.0f, 1.0f);
	return FRotator(
		Current.Pitch + NormalizeAxis(Target.Pitch - Current.Pitch) * Alpha,
		Current.Yaw + NormalizeAxis(Target.Yaw - Current.Yaw) * Alpha,
		Current.Roll + NormalizeAxis(Target.Roll - Current.Roll) * Alpha);
}
}

USpringArmComponent::USpringArmComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USpringArmComponent::BeginPlay()
{
	Super::BeginPlay();

	// 첫 프레임에 Lag가 원점(0,0,0)에서 날아오지 않도록 현재 값으로 초기화
	PreviousDesiredLoc = GetWorldLocation() + TargetOffset;
	PreviousDesiredRot = GetTargetRotation();
}

void USpringArmComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	UpdateDesiredArmLocation(DeltaTime);
}

FRotator USpringArmComponent::GetTargetRotation() const
{
	FRotator Rotation = GetWorldRotation();

	// 상속하지 않는 축은 부모 회전을 무시하고 자기 Relative 값을 월드 값으로 사용
	const FRotator& Relative = GetRelativeRotation();
	if (!bInheritPitch)
	{
		Rotation.Pitch = Relative.Pitch;
	}
	if (!bInheritYaw)
	{
		Rotation.Yaw = Relative.Yaw;
	}
	if (!bInheritRoll)
	{
		Rotation.Roll = Relative.Roll;
	}

	return Rotation;
}

void USpringArmComponent::UpdateDesiredArmLocation(float DeltaTime)
{
	// 1. 회전 (+ 회전 Lag)
	FRotator DesiredRot = GetTargetRotation();
	if (bEnableCameraRotationLag)
	{
		DesiredRot = RInterpTo(PreviousDesiredRot, DesiredRot, DeltaTime, CameraRotationLagSpeed);
	}
	PreviousDesiredRot = DesiredRot;

	// 2. 팔 원점 (+ 위치 Lag)
	const FVector ArmOrigin = GetWorldLocation() + TargetOffset;
	FVector DesiredLoc = ArmOrigin;
	if (bEnableCameraLag)
	{
		DesiredLoc = VInterpTo(PreviousDesiredLoc, DesiredLoc, DeltaTime, CameraLagSpeed);

		// 너무 빨리 움직여 카메라가 뒤처지는 거리 제한
		if (CameraLagMaxDistance > 0.0f)
		{
			const FVector FromOrigin = DesiredLoc - ArmOrigin;
			DesiredLoc = ArmOrigin + FromOrigin.GetClampedToMaxSize(CameraLagMaxDistance);
		}
	}
	PreviousDesiredLoc = DesiredLoc;

	// 3. 팔 끝 위치 = 원점 - Forward * 길이 + 회전된 SocketOffset
	const FQuat DesiredQuat = DesiredRot.Quaternion();
	DesiredLoc -= DesiredQuat.GetForwardVector() * TargetArmLength;
	DesiredLoc += DesiredQuat.RotateVector(SocketOffset);

	// 4. 충돌 테스트: 원점 → 팔 끝으로 레이를 쏴서 막히면 그 앞까지 당긴다
	FVector ResultLoc = DesiredLoc;
	bIsCameraFixed = false;

	UWorld* World = GetWorld();
	if (bDoCollisionTest && TargetArmLength > 0.0f && World)
	{
		const FVector Delta = DesiredLoc - ArmOrigin;
		const float TraceLength = Delta.Size();
		if (TraceLength > KINDA_SMALL_NUMBER)
		{
			const FRay Ray{ .Origin = ArmOrigin, .Direction = Delta / TraceLength };

			FHitResult Hit;
			// 자기 자신(플레이어 메시/충돌체)은 무시
			if (World->LineTraceSingle(Ray, Hit, nullptr, nullptr, GetOwner()) && Hit.Distance < TraceLength)
			{
				const float SafeDistance = FMath::Max(0.0f, Hit.Distance - ProbeSize);
				ResultLoc = ArmOrigin + Ray.Direction * SafeDistance;
				bIsCameraFixed = true;
			}
		}
	}

	// 5. 결과를 자식(카메라)에 반영. 소켓이 없으므로 자식의 월드 트랜스폼을 직접 설정한다.
	SocketLocation = ResultLoc;
	SocketRotation = DesiredRot;
	for (USceneComponent* Child : GetAttachChildren())
	{
		Child->SetWorldLocationAndRotation(SocketLocation, SocketRotation, false);
	}
}
