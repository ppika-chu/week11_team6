#include "EnginePCH.h"
#include "LuaAnimNotifyDemoInstance.h"
#include "Components/SkeletalMeshComponent.h"

void ULuaAnimNotifyDemoInstance::AnimNotify_FootstepNamed()
{
	++NotifyCount;
	HTR_LOG(Info, "[Lua Notify Demo][Named] FootstepNamed | Class={} Mesh={} Count={}",
		GetClass()->Name, GetSkelMeshComponent()->GetName(), NotifyCount);
}
