#pragma once

#include "GameFramework/Actor.h"
#include "Components/SphereComponent.h"

class ASphereActor : public AActor
{
	DECLARE_CLASS(ASphereActor, AActor)

public:
	ASphereActor();

	USphereComponent* GetSphereComponent() const { return SphereComponent; }

private:
	USphereComponent* SphereComponent = nullptr;
};
