#pragma once

#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"

class ABoxActor : public AActor
{
	DECLARE_CLASS(ABoxActor, AActor)

public:
	ABoxActor();

	UBoxComponent* GetBoxComponent() const { return BoxComponent; }

private:
	UBoxComponent* BoxComponent = nullptr;
};
