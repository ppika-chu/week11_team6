#include "EnginePCH.h"
#include "GameFramework/Controller.h"

#include "GameFramework/Pawn.h"

AController::~AController()
{
	if (Pawn && Pawn->Controller == this)
	{
		Pawn->Controller = nullptr;
	}
}

void AController::Possess(APawn* InPawn)
{
	if (Pawn == InPawn)
	{
		return;
	}

	UnPossess();
	if (!InPawn)
	{
		return;
	}

	if (InPawn->Controller)
	{
		InPawn->Controller->UnPossess();
	}

	Pawn = InPawn;
	Pawn->Controller = this;
	OnPossess(Pawn);
}

void AController::UnPossess()
{
	if (!Pawn)
	{
		return;
	}

	if (Pawn->Controller == this)
	{
		Pawn->Controller = nullptr;
	}
	Pawn = nullptr;
}

void AController::EndPlay()
{
	UnPossess();
	Super::EndPlay();
}
