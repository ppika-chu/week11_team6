#include "EnginePCH.h"
#include "AnimNotifyState_Log.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyState_Log::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	HTR_LOG(Info, "[Notify Demo][Begin] {} | Mesh={} Clip={} Time={:.3f} Duration={:.3f}", Message, MeshComp->GetName(), Animation->GetPath(), EventReference.GetCurrentAnimationTime(), TotalDuration);
}

void UAnimNotifyState_Log::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	if (bLogTick)
	{
		HTR_LOG(Info, "[Notify Demo][Tick] {} | Mesh={} Clip={} Time={:.3f} Delta={:.3f}", Message, MeshComp->GetName(), Animation->GetPath(), EventReference.GetCurrentAnimationTime(), FrameDeltaTime);
	}
}

void UAnimNotifyState_Log::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	HTR_LOG(Info, "[Notify Demo][End] {} | Mesh={} Clip={}", Message, MeshComp->GetName(), Animation->GetPath());
}
