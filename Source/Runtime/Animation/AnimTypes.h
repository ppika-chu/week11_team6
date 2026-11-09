#pragma once

#include "Core/NameTypes.h"
#include "Containers/Array.h"
#include "Math/EngineMath.h"

// 배열 길이는 1 또는 전체 키 수다. 1이면 클립 내내 같은 값이다.
struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;
};

class UAnimNotify;
class UAnimNotifyState;

inline constexpr float ZERO_ANIMWEIGHT_THRESH = 0.00001f;

namespace EAnimEventTriggerOffsets
{
enum Type
{
	OffsetBefore,
	OffsetAfter,
	NoOffset
};
}

float GetTriggerTimeOffsetForType(EAnimEventTriggerOffsets::Type OffsetType);

// 공유 클립의 이벤트 정의. Notify 객체에 재생 Instance별 상태를 저장하지 않는다.
struct FAnimNotifyEvent
{
	float TriggerTimeOffset = 0.0f;
	float EndTriggerTimeOffset = 0.0f;
	float TriggerWeightThreshold = ZERO_ANIMWEIGHT_THRESH;
	FName NotifyName;
	UAnimNotify* Notify = nullptr;
	UAnimNotifyState* NotifyStateClass = nullptr;
	float Duration = 0.0f;

	// 표시 시각·구간 길이와 실제 발동 시각.
	float GetTime() const { return Time; }
	void SetTime(float NewTime) { Time = NewTime; }
	float GetTriggerTime() const;
	float GetEndTriggerTime() const;
	float GetDuration() const;
	void SetDuration(float NewDuration);

	// TODO: 이벤트 편집·로드 시 경계 Offset 갱신 연결 예정.
	void RefreshTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType);
	void RefreshEndTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType);

	// UE 명칭을 유지한다. 여기서는 Notify 객체 유무를 뜻하며 Blueprint 실행은 없다.
	bool IsBlueprintNotify() const { return Notify != nullptr || NotifyStateClass != nullptr; }
	FName GetNotifyEventName() const;
	bool operator==(const FAnimNotifyEvent& Other) const;

private:
	float Time = 0.0f;
};
