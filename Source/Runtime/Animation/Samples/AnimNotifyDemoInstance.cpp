#include "EnginePCH.h"
#include "AnimNotifyDemoInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyDemoInstance::AnimNotify_FootstepNamed()
{
	HTR_LOG(Info, "[Notify Demo][Named] FootstepNamed | Mesh={} Clip={}", GetSkelMeshComponent()->GetName(), GetCurrentAsset()->GetPath());
}
