#include "EnginePCH.h"
#include "DefaultPawn.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/InputSystem.h"

namespace
{
constexpr float MoveSpeed = 6.0f;
constexpr float FastMoveScale = 3.0f;
constexpr float MouseSensitivity = 0.12f; // 픽셀당 각도
constexpr float MaxPitch = 89.0f;

float GetAxis(EKeyCode Positive, EKeyCode Negative)
{
	return (FInputSystem::IsKeyDown(Positive) ? 1.0f : 0.0f) - (FInputSystem::IsKeyDown(Negative) ? 1.0f : 0.0f);
}
}

ADefaultPawn::ADefaultPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	SetRootComponent(CameraComponent);
}

void ADefaultPawn::BeginPlay()
{
	Super::BeginPlay();

	const FRotator Rotation = GetActorRotation();
	Pitch = Rotation.Pitch;
	Yaw = Rotation.Yaw;

	if (IsPlayerControlled())
	{
		Cast<APlayerController>(GetController())->SetCursorMode(ECursorMode::Locked);
	}
}

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 커서를 꺼낸 동안(Shift+F1)에는 툴바로 가는 마우스에 시점이 돌지 않게 입력을 무시한다.
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || PlayerController->GetCursorMode() != ECursorMode::Locked)
	{
		return;
	}

	Yaw += static_cast<float>(FInputSystem::GetMouseDeltaX()) * MouseSensitivity;
	Pitch = FMath::Clamp(Pitch + static_cast<float>(FInputSystem::GetMouseDeltaY()) * MouseSensitivity, -MaxPitch, MaxPitch);

	const float Forward = GetAxis(EKeyCode::W, EKeyCode::S);
	const float Right = GetAxis(EKeyCode::D, EKeyCode::A);
	const float Up = GetAxis(EKeyCode::E, EKeyCode::Q);

	// 엔진 규약: X 전방, Y 오른쪽, Z 위이고 Pitch가 양수면 아래를 본다.
	const float YawRad = Yaw * (PI / 180.0f);
	const float PitchRad = Pitch * (PI / 180.0f);
	const FVector ForwardAxis(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), -std::sin(PitchRad));
	const FVector RightAxis(-std::sin(YawRad), std::cos(YawRad), 0.0f);

	const float Speed = MoveSpeed * (FInputSystem::IsKeyDown(EKeyCode::Shift) ? FastMoveScale : 1.0f) * DeltaTime;
	FVector Location = GetActorLocation();
	Location.X += (ForwardAxis.X * Forward + RightAxis.X * Right) * Speed;
	Location.Y += (ForwardAxis.Y * Forward + RightAxis.Y * Right) * Speed;
	Location.Z += (ForwardAxis.Z * Forward + Up) * Speed;

	GetRootComponent()->SetWorldLocationAndRotation(Location, FRotator(Pitch, Yaw, 0.0f), false);
}
