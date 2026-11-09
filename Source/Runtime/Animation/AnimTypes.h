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
	// 경계 보정 — 시작·끝 시각 자체가 아니라 판정 시각에 더하는 작은 보정량(초).
	// 클립의 정확히 0초·끝에 배치된 이벤트를 경계 안쪽으로 옮길 때 사용한다.
	float TriggerTimeOffset = 0.0f;
	float EndTriggerTimeOffset = 0.0f;

	// 발동 조건 — 재생 Node의 최종 Weight가 이 값 이상일 때 Queue에 넣는다.
	float TriggerWeightThreshold = ZERO_ANIMWEIGHT_THRESH;

	// 실행 대상 — 일회성은 이름 수신 또는 Notify(), 구간은 State의 Begin/Tick/End로 처리한다.
	// 구간 이벤트인지는 Duration이 아니라 NotifyStateClass 유무로 구분한다.
	FName NotifyName;
	UAnimNotify* Notify = nullptr;
	UAnimNotifyState* NotifyStateClass = nullptr;

	// 배치 시각·구간 길이 — Time은 시작 시각, Duration은 State의 길이(초).
	// 예: Time 0.3, Duration 0.4, Offset 0이면 0.3~0.7초 구간이다.
	float Duration = 0.0f;
	float GetTime() const { return Time; }
	void SetTime(float NewTime) { Time = NewTime; }
	float GetDuration() const;
	void SetDuration(float NewDuration);

	// 판정 시각 — 시작은 Time + 시작 Offset, State 끝은 시작 판정 시각 + Duration + 끝 Offset.
	// 일회성은 시작·끝 판정 시각이 같으며 GetDuration()도 0을 반환한다.
	float GetTriggerTime() const;
	float GetEndTriggerTime() const;

	// TODO: 이벤트 편집·로드 시 경계 Offset 갱신 연결 예정.
	void RefreshTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType);
	void RefreshEndTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType);

	// 실행 대상 조회·비교 — 객체 유무, 이름 수신 함수명, 같은 이벤트인지 확인한다.
	// UE 명칭을 유지한다. 여기서는 Notify 객체 유무를 뜻하며 Blueprint 실행은 없다.
	bool IsBlueprintNotify() const { return Notify != nullptr || NotifyStateClass != nullptr; }
	FName GetNotifyEventName() const;
	bool operator==(const FAnimNotifyEvent& Other) const;

private:
	// Asset에 배치한 시작 시각. 재생 중인 캐릭터의 현재 시간이 아니다.
	float Time = 0.0f;
};
