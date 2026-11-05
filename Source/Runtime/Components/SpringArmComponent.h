#pragma once

#include "SceneComponent.h"

struct FVector;
struct FRotator;

class USpringArmComponent : public USceneComponent
{
	DECLARE_CLASS(USpringArmComponent, USceneComponent)
	REFLECT_START(USpringArmComponent)
		PROPERTY(TargetArmLength)
		PROPERTY(SocketOffset)
		PROPERTY(TargetOffset)
		PROPERTY(bDoCollisionTest)
		PROPERTY(ProbeSize)
		PROPERTY(bEnableCameraLag)
		PROPERTY(CameraLagSpeed)
		PROPERTY(CameraLagMaxDistance)
		PROPERTY(bEnableCameraRotationLag)
		PROPERTY(CameraRotationLagSpeed)
		PROPERTY(bInheritPitch)
		PROPERTY(bInheritYaw)
		PROPERTY(bInheritRoll)
	REFLECT_END()

public:
	USpringArmComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	bool IsCollisionFixApplied() const { return bIsCameraFixed; }

	float TargetArmLength = 5.0f;
	FVector SocketOffset = FVector::ZeroVector;
	FVector TargetOffset = FVector::ZeroVector;

	bool bDoCollisionTest = true;
	float ProbeSize = 0.2f;

	bool bEnableCameraLag = false;
	float CameraLagSpeed = 10.0f;
	float CameraLagMaxDistance = 0.0f;

	bool bEnableCameraRotationLag = false;
	float CameraRotationLagSpeed = 10.0f;

	bool bInheritPitch = true;
	bool bInheritYaw = true;
	bool bInheritRoll = true;

private:
	FRotator GetTargetRotation() const;
	void UpdateDesiredArmLocation(float DeltaTime);

	FVector PreviousDesiredLoc = FVector::ZeroVector;
	FRotator PreviousDesiredRot;

	FVector SocketLocation = FVector::ZeroVector;
	FRotator SocketRotation;
	bool bIsCameraFixed = false;
};
