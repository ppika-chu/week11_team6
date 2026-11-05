#include "EnginePCH.h"
#include "SkeletalMeshActor.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("USkeletalMeshComponent");
	SetRootComponent(SkeletalMeshComponent);
}

void ASkeletalMeshActor::BeginPlay()
{
	Super::BeginPlay();
}

void ASkeletalMeshActor::SetPrimitiveType(EPrimitiveType Type)
{
}
