#include "EnginePCH.h"
#include "GameFramework/PlayerController.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

APlayerController::~APlayerController()
{
	// 커서를 잠근 채 지워지면 창이 마우스를 놓지 못한다.
	if (CursorMode == ECursorMode::Locked)
	{
		FMouseCapture::Release();
	}
}

void APlayerController::EndPlay()
{
	SetCursorMode(ECursorMode::Free);
	Super::EndPlay();
}

APlayerCameraManager* APlayerController::GetPlayerCameraManager()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (PlayerCameraManager && PlayerCameraManager->IsActorBeingDestroyed())
	{
		PlayerCameraManager = nullptr;
	}

	// 직접 배치한 PlayerCameraManager가 있는 예전 씬은 그것을 쓴다.
	if (!PlayerCameraManager)
	{
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		{
			APlayerCameraManager* CameraManager = Cast<APlayerCameraManager>(Actor);
			if (CameraManager && !CameraManager->IsActorBeingDestroyed())
			{
				PlayerCameraManager = CameraManager;
				break;
			}
		}
	}

	if (!PlayerCameraManager)
	{
		PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
		if (!PlayerCameraManager)
		{
			return nullptr;
		}
		PlayerCameraManager->SetFlags(EObjectFlags::RF_Transient);
	}

	if (!PlayerCameraManager->GetViewTargetCamera())
	{
		UCameraComponent* PawnCamera = GetPawn() ? GetPawn()->FindCameraComponent() : nullptr;
		PlayerCameraManager->SetViewTargetCamera(PawnCamera ? PawnCamera : World->GetPlayCameraComponent());
	}
	return PlayerCameraManager;
}

void APlayerController::SetPlayerCameraManager(APlayerCameraManager* InCameraManager)
{
	PlayerCameraManager = InCameraManager;
}

void APlayerController::ClearPlayerCameraManager(const APlayerCameraManager* InCameraManager)
{
	if (PlayerCameraManager == InCameraManager)
	{
		PlayerCameraManager = nullptr;
	}
}

void APlayerController::SetCursorMode(ECursorMode Mode)
{
	CursorMode = Mode;
	if (GEngine)
	{
		GEngine->SetGameMouseCapture(GetWorld(), Mode == ECursorMode::Locked);
	}
}

void APlayerController::OnPossess(APawn* InPawn)
{
	// 카메라를 가진 Pawn을 조종하면 그 카메라로 본다.
	UCameraComponent* PawnCamera = InPawn ? InPawn->FindCameraComponent() : nullptr;
	if (PawnCamera)
	{
		if (APlayerCameraManager* CameraManager = GetPlayerCameraManager())
		{
			CameraManager->SetViewTargetCamera(PawnCamera);
		}
	}
}
