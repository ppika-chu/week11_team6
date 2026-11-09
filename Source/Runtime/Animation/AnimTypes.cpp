#include "EnginePCH.h"
#include "AnimTypes.h"
#include "Math/MathUtility.h"

float GetTriggerTimeOffsetForType(EAnimEventTriggerOffsets::Type OffsetType)
{
	switch (OffsetType)
	{
	case EAnimEventTriggerOffsets::OffsetBefore: return -KINDA_SMALL_NUMBER;
	case EAnimEventTriggerOffsets::OffsetAfter: return KINDA_SMALL_NUMBER;
	default: return 0.0f;
	}
}

float FAnimNotifyEvent::GetTriggerTime() const
{
	return GetTime() + TriggerTimeOffset;
}

float FAnimNotifyEvent::GetEndTriggerTime() const
{
	return NotifyStateClass ? GetTriggerTime() + GetDuration() + EndTriggerTimeOffset : GetTriggerTime();
}

float FAnimNotifyEvent::GetDuration() const
{
	return NotifyStateClass ? Duration : 0.0f;
}

void FAnimNotifyEvent::SetDuration(float NewDuration)
{
	Duration = NewDuration;
}

void FAnimNotifyEvent::RefreshTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType)
{
	if (PredictedOffsetType == EAnimEventTriggerOffsets::NoOffset || TriggerTimeOffset == 0.0f)
	{
		TriggerTimeOffset = GetTriggerTimeOffsetForType(PredictedOffsetType);
	}
}

void FAnimNotifyEvent::RefreshEndTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType)
{
	if (PredictedOffsetType == EAnimEventTriggerOffsets::NoOffset || EndTriggerTimeOffset == 0.0f)
	{
		EndTriggerTimeOffset = GetTriggerTimeOffsetForType(PredictedOffsetType);
	}
}

FName FAnimNotifyEvent::GetNotifyEventName() const
{
	return NotifyName.IsNone() ? NAME_None : FName("AnimNotify_" + NotifyName.ToString());
}

bool FAnimNotifyEvent::operator==(const FAnimNotifyEvent& Other) const
{
	return (Notify && Notify == Other.Notify) || (NotifyStateClass && NotifyStateClass == Other.NotifyStateClass) || (!IsBlueprintNotify() && NotifyName == Other.NotifyName);
}
