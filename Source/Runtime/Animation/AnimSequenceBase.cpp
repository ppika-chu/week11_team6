#include "EnginePCH.h"
#include "AnimSequenceBase.h"
#include "AnimNotifyQueue.h"
#include <cmath>
#include <stdexcept>
#include "AnimNotifies/AnimNotify.h"
#include "AnimNotifies/AnimNotifyState.h"
#include "UObject/UObjectHash.h"

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


UAnimSequenceBase::~UAnimSequenceBase()
{
	// 엔진 전역 종료에서 자식 UObject가 먼저 해제된 경우를 처리한다(DataModel과 같은 경계).
	for (UObject* Object : OwnedNotifyObjects)
	{
		if (FindLiveObjectByAddress(Object)) delete Object;
	}
	if (DataModel && FindLiveObjectByAddress(DataModel)) delete DataModel;
}

UObject* UAnimSequenceBase::CreateNotifyObject(UClass* NotifyClass)
{
	if (!NotifyClass || !NotifyClass->Constructor || (!NotifyClass->IsChildOf(UAnimNotify::StaticClass()) && !NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))) return nullptr;
	UObject* Object = NewObject<UObject>(this, NotifyClass);
	OwnedNotifyObjects.Add(Object);
	return Object;
}

void UAnimSequenceBase::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	const auto ValidateEvent = [](const FAnimNotifyEvent& Event)
	{
		if (!std::isfinite(Event.GetTime()) || Event.GetTime() < 0 || !std::isfinite(Event.Duration) || Event.Duration < 0 ||
			!std::isfinite(Event.TriggerTimeOffset) || !std::isfinite(Event.EndTriggerTimeOffset) || !std::isfinite(Event.TriggerWeightThreshold) || Event.TriggerWeightThreshold < 0 || Event.TriggerWeightThreshold > 1 || (Event.Notify && Event.NotifyStateClass))
		{
			throw std::runtime_error("Invalid animation Notify data");
		}
	};
	if (!bIsLoading)
	{
		Handle["Notifies"] = json::array();
		for (const FAnimNotifyEvent& Event : Notifies)
		{
			ValidateEvent(Event);
			json Item = { {"Time", Event.GetTime()}, {"Duration", Event.Duration}, {"Name", Event.NotifyName.ToString()},
				{"TriggerTimeOffset", Event.TriggerTimeOffset}, {"EndTriggerTimeOffset", Event.EndTriggerTimeOffset}, {"TriggerWeightThreshold", Event.TriggerWeightThreshold} };
			UObject* Object = Event.NotifyStateClass ? static_cast<UObject*>(Event.NotifyStateClass) : static_cast<UObject*>(Event.Notify);
			Item["Kind"] = Event.NotifyStateClass ? "State" : Event.Notify ? "Notify" : "Name";
			if (Object)
			{
				Item["Class"] = Object->GetClass()->Name;
				json Properties = json::object(); Object->Serialize(Properties, false); Item["Properties"] = std::move(Properties);
			}
			Handle["Notifies"].push_back(std::move(Item));
		}
		return;
	}

	// 파일 오류로 기존 편집 배열을 절반만 바꾸지 않도록 새 목록을 완성한 뒤 교체한다.
	TArray<FAnimNotifyEvent> NewNotifies;
	TArray<UObject*> NewObjects;
	try
	{
		const json& Items = Handle.at("Notifies");
		if (!Items.is_array()) throw std::runtime_error("Notifies must be an array");
		for (const json& Item : Items)
		{
			FAnimNotifyEvent Event;
			Event.SetTime(Item.at("Time").get<float>()); Event.SetDuration(Item.at("Duration").get<float>());
			Event.NotifyName = FName(Item.at("Name").get<FString>());
			Event.TriggerTimeOffset = Item.at("TriggerTimeOffset").get<float>(); Event.EndTriggerTimeOffset = Item.at("EndTriggerTimeOffset").get<float>();
			Event.TriggerWeightThreshold = Item.at("TriggerWeightThreshold").get<float>();
			ValidateEvent(Event);
			const FString Kind = Item.at("Kind").get<FString>();
			if (Kind != "Name")
			{
				UClass* Base = Kind == "Notify" ? UAnimNotify::StaticClass() : Kind == "State" ? UAnimNotifyState::StaticClass() : nullptr;
				UClass* Class = FindClass(Item.at("Class").get<FString>());
				if (!Base || !Class || !Class->Constructor || !Class->IsChildOf(Base)) throw std::runtime_error("Unknown or invalid Notify class");
				UObject* Object = NewObject<UObject>(this, Class); NewObjects.Add(Object);
				json Properties = Item.at("Properties");
				if (!Properties.is_object()) throw std::runtime_error("Notify properties must be an object");
				Object->Serialize(Properties, true);
				if (Kind == "State") Event.NotifyStateClass = Cast<UAnimNotifyState>(Object);
				else Event.Notify = Cast<UAnimNotify>(Object);
			}
			// 시각·Duration은 보존하고 새 클립 길이에 맞춰 경계 보정만 갱신한다.
			Event.RefreshTriggerOffset(CalculateOffsetForNotify(Event.GetTime()));
			Event.RefreshEndTriggerOffset(CalculateOffsetForNotify(Event.GetTime() + Event.GetDuration()));
			NewNotifies.Add(Event);
		}
	}
	catch (...)
	{
		for (UObject* Object : NewObjects) delete Object;
		throw;
	}
	for (UObject* Object : OwnedNotifyObjects) delete Object;
	OwnedNotifyObjects = std::move(NewObjects);
	Notifies = std::move(NewNotifies);
}
