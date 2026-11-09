#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;
struct FAnimNotifyEventReference;

class UAnimNotify : public UObject
{
	DECLARE_CLASS(UAnimNotify, UObject)
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) {}
};
