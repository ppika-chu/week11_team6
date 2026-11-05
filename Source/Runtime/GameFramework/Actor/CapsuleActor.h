#pragma once

#include "GameFramework/Actor.h"
#include "Components/CapsuleComponent.h"

class ACapsuleActor : public AActor
{
	DECLARE_CLASS(ACapsuleActor, AActor)

public:
	ACapsuleActor();

	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }

private:
	UCapsuleComponent* CapsuleComponent = nullptr;
};
