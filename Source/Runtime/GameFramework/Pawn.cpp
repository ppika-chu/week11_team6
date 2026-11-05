#include "EnginePCH.h"
#include "GameFramework/Pawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"

APawn::~APawn()
{
	if (Controller && Controller->Pawn == this)
	{
		Controller->Pawn = nullptr;
	}
}

void APawn::EndPlay()
{
	if (Controller)
	{
		Controller->UnPossess();
	}
	Super::EndPlay();
}

bool APawn::IsPlayerControlled() const
{
	return Cast<APlayerController>(Controller) != nullptr;
}

UCameraComponent* APawn::FindCameraComponent() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			return Camera;
		}
	}
	return nullptr;
}
