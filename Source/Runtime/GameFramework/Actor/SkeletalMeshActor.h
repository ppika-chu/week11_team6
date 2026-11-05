#pragma once
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"

class ASkeletalMeshActor : public AActor
{
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
public:
	ASkeletalMeshActor();
	virtual ~ASkeletalMeshActor() = default;

	virtual void BeginPlay() override;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
	void SetPrimitiveType(EPrimitiveType Type);

private:
	USkeletalMeshComponent* SkeletalMeshComponent;
};
