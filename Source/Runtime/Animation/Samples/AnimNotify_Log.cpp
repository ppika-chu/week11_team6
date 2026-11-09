#include "EnginePCH.h"
#include "AnimNotify_Log.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_Log::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	HTR_LOG(Info, "[Notify Demo][Notify] {} | Mesh={} Clip={} Time={:.3f}", Message, MeshComp->GetName(), Animation->GetPath(), EventReference.GetCurrentAnimationTime());
}
