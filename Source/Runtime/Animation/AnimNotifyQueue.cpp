#include "EnginePCH.h"
#include "AnimNotifyQueue.h"

bool FAnimNotifyEventReference::operator==(const FAnimNotifyEventReference& Other) const
{
	return Notify == Other.Notify || (Notify && Other.Notify && *Notify == *Other.Notify);
}

void FAnimNotifyQueue::AddAnimNotifies(const TArray<FAnimNotifyEventReference>& NewNotifies, float InstanceWeight)
{
	for (const FAnimNotifyEventReference& NotifyRef : NewNotifies)
	{
		const FAnimNotifyEvent* Notify = NotifyRef.GetNotify();
		if (Notify && Notify->TriggerWeightThreshold <= InstanceWeight)
		{
			// Loop로 같은 State를 여러 번 지나도 활성 항목은 하나다.
			if (!Notify->NotifyStateClass || AnimNotifies.Find(NotifyRef) == -1)
			{
				AnimNotifies.Add(NotifyRef);
			}
		}
	}
}
