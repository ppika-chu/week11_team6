#pragma once

#include "AnimTypes.h"
#include "Containers/Array.h"

class UObject;

// 이벤트 배열은 수집부터 전달 완료까지 변경되지 않아야 한다.
// 출처 Asset과 Notify 객체는 활성 State의 End까지 유효해야 한다.
struct FAnimNotifyEventReference
{
	FAnimNotifyEventReference() = default;
	FAnimNotifyEventReference(const FAnimNotifyEvent* InNotify, const UObject* InNotifySource) : Notify(InNotify), NotifySource(InNotifySource) {}

	const FAnimNotifyEvent* GetNotify() const { return NotifySource ? Notify : nullptr; }
	const UObject* GetSourceObject() const { return NotifySource; }
	void SetNotify(const FAnimNotifyEvent* InNotify) { Notify = InNotify; }
	float GetCurrentAnimationTime() const { return CurrentAnimTime; }
	void SetCurrentAnimationTime(float InTime) { CurrentAnimTime = InTime; }
	bool HasReachedEnd() const { return bReachedEnd; }
	void SetReachedEnd(bool bInReachedEnd) { bReachedEnd = bInReachedEnd; }
	bool operator==(const FAnimNotifyEventReference& Other) const;

private:
	const FAnimNotifyEvent* Notify = nullptr;
	const UObject* NotifySource = nullptr;
	float CurrentAnimTime = 0.0f;
	bool bReachedEnd = false;
};

struct FAnimNotifyContext
{
	explicit FAnimNotifyContext(bool bInLooping = false) : bLooping(bInLooping) {}

	TArray<FAnimNotifyEventReference> ActiveNotifies;
	bool bLooping;
};

struct FAnimNotifyQueue
{
	TArray<FAnimNotifyEventReference> AnimNotifies;

	void Reset() { AnimNotifies.Reset(); }
	void AddAnimNotifies(const TArray<FAnimNotifyEventReference>& NewNotifies, float InstanceWeight);
};
