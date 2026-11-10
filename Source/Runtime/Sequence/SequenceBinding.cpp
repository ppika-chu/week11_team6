#include "EnginePCH.h"
#include "SequenceBinding.h"

#include "SequenceData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"

bool SequenceBinding::Make(const USkeletalMeshComponent* Component, const ULevel* SequenceLevel, FSequenceBinding& OutBinding)
{
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (!Owner || Owner->GetLevel() != SequenceLevel || Owner->HasAnyFlags(EObjectFlags::RF_Transient) || Owner->IsActorBeingDestroyed())
	{
		return false;
	}

	OutBinding.ActorName = Owner->GetName();
	OutBinding.ComponentName = Component->GetName();
	return true;
}

AActor* SequenceBinding::ResolveActor(const FSequenceBinding& Binding, const ULevel* SequenceLevel)
{
	if (!Binding.IsSet() || !SequenceLevel)
	{
		return nullptr;
	}

	for (AActor* Actor : SequenceLevel->GetActors())
	{
		if (Actor && !Actor->IsActorBeingDestroyed() && Actor->GetName() == Binding.ActorName)
		{
			return Actor;
		}
	}

	return nullptr;
}

USkeletalMeshComponent* SequenceBinding::ResolveComponent(const FSequenceBinding& Binding, const ULevel* SequenceLevel)
{
	AActor* Actor = ResolveActor(Binding, SequenceLevel);
	auto* Component = Actor ? Cast<USkeletalMeshComponent>(Actor->FindComponentByName(Binding.ComponentName)) : nullptr;
	return Component && !Component->IsBeingDestroyed() ? Component : nullptr;
}
