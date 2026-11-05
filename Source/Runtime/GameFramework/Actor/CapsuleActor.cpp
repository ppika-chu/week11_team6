#include "EnginePCH.h"
#include "CapsuleActor.h"

ACapsuleActor::ACapsuleActor()
{
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CollisionComponent");
	SetRootComponent(CapsuleComponent);
	CapsuleComponent->SetBlockComponent(true);
}
