#include "EnginePCH.h"
#include "SphereActor.h"

ASphereActor::ASphereActor()
{
	SphereComponent = CreateDefaultSubobject<USphereComponent>("CollisionComponent");
	SetRootComponent(SphereComponent);
	SphereComponent->SetBlockComponent(true);
}
