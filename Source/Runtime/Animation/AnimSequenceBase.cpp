#include "EnginePCH.h"
#include "AnimSequenceBase.h"
#include "AnimNotifyQueue.h"
#include <cmath>

void UAnimSequenceBase::GetAnimNotifies(float StartTime, float DeltaTime, FAnimNotifyContext& NotifyContext) const
{
	const float PlayLength = GetPlayLength();
	if (Notifies.IsEmpty() || !std::isfinite(StartTime) || !std::isfinite(DeltaTime) || !std::isfinite(PlayLength) || PlayLength <= 0.0f)
	{
		return;
	}

	double DesiredDeltaMove = DeltaTime;
	float PreviousPosition = FMath::Clamp(StartTime, 0.0f, PlayLength);
	const bool bPlayingBackwards = DeltaTime < 0.0f;
	// 남은 이동량에서 한 바퀴를 빼는 연산 자체가 불가능한 입력은 처리하지 않는다.
	if (NotifyContext.bLooping && std::abs(DesiredDeltaMove) > PlayLength && std::abs(DesiredDeltaMove) - PlayLength == std::abs(DesiredDeltaMove))
	{
		HTR_LOG(Error, "[Anim] Notify traversal delta exceeds time precision");
		return;
	}

	for (;;)
	{
		const double DesiredPosition = static_cast<double>(PreviousPosition) + DesiredDeltaMove;
		const float CurrentPosition = static_cast<float>(FMath::Clamp(DesiredPosition, 0.0, static_cast<double>(PlayLength)));
		GetAnimNotifiesFromDeltaPositions(PreviousPosition, CurrentPosition, NotifyContext);

		const bool bPassedBoundary = DesiredPosition < 0.0 || DesiredPosition > PlayLength;
		if (!NotifyContext.bLooping || !bPassedBoundary)
		{
			break;
		}

		// CurrentPosition은 경계에서 잘린 시각이므로, 이번 구간에서 실제 이동한 양만 빼야 다음 Loop의 남은 이동량이 된다.
		// 예: 길이 1초, 0.8에서 +0.5초 이동 → 경계까지 0.2초를 조회했으므로 0.3초가 남는다. 역재생은 음수 이동량을 뺀다.
		DesiredDeltaMove -= static_cast<double>(CurrentPosition) - PreviousPosition;
		PreviousPosition = bPlayingBackwards ? PlayLength : 0.0f;
	}
}

void UAnimSequenceBase::GetAnimNotifiesFromDeltaPositions(float PreviousPosition, float CurrentPosition, FAnimNotifyContext& NotifyContext) const
{
	if (!std::isfinite(PreviousPosition) || !std::isfinite(CurrentPosition))
	{
		return;
	}

	const bool bPlayingBackwards = CurrentPosition < PreviousPosition;
	for (const FAnimNotifyEvent& Event : Notifies)
	{
		const float NotifyStartTime = Event.GetTriggerTime();
		const float NotifyEndTime = Event.GetEndTriggerTime();
		const bool bCrossed = bPlayingBackwards ? NotifyStartTime < PreviousPosition && NotifyEndTime >= CurrentPosition : NotifyStartTime <= CurrentPosition && NotifyEndTime > PreviousPosition;
		if (bCrossed)
		{
			FAnimNotifyEventReference EventReference(&Event, this);
			EventReference.SetCurrentAnimationTime(CurrentPosition);
			EventReference.SetReachedEnd(bPlayingBackwards ? CurrentPosition <= FMath::Max(NotifyStartTime, 0.0f) : CurrentPosition >= NotifyEndTime);
			NotifyContext.ActiveNotifies.Add(EventReference);
		}
	}
}

EAnimEventTriggerOffsets::Type UAnimSequenceBase::CalculateOffsetForNotify(float NotifyDisplayTime) const
{
	if (NotifyDisplayTime == 0.0f)
	{
		return EAnimEventTriggerOffsets::OffsetAfter;
	}
	if (NotifyDisplayTime == GetPlayLength())
	{
		return EAnimEventTriggerOffsets::OffsetBefore;
	}
	return EAnimEventTriggerOffsets::NoOffset;
}
