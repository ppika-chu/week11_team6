#include "EnginePCH.h"
#include "PitchingAnimInstance.h"

#include "Components/ScriptComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UPitchingAnimInstance::AnimNotify_ReleaseBall()
{
	// 이 시연은 투수 Actor당 그래프와 이벤트를 담당하는 ScriptComponent 하나를 사용한다.
	if (UScriptComponent* Script = GetSkelMeshComponent()->GetOwner()->FindComponentByClass<UScriptComponent>())
	{
		Script->OnAnimNotify("ReleaseBall");
	}
}
