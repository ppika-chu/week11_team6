#pragma once

#include "GameFramework/Pawn.h"

class UCameraComponent;

// 씬에 조종할 Pawn도 카메라도 없을 때 PIE가 만드는 자유 비행 Pawn. UE의 ADefaultPawn에 해당한다.
// WASD로 이동, Q/E로 하강·상승, Shift로 가속, 마우스로 시점을 돌린다.
class ADefaultPawn : public APawn
{
	DECLARE_CLASS(ADefaultPawn, APawn)

public:
	ADefaultPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UCameraComponent* GetCameraComponent() const { return CameraComponent; }

private:
	UCameraComponent* CameraComponent = nullptr;
	float Pitch = 0.0f;
	float Yaw = 0.0f;
};
