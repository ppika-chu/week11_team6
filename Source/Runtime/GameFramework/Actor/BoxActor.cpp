#include "EnginePCH.h"
#include "BoxActor.h"

ABoxActor::ABoxActor()
{
	BoxComponent = CreateDefaultSubobject<UBoxComponent>("CollisionComponent");
	SetRootComponent(BoxComponent);
	BoxComponent->SetBlockComponent(true);
}
