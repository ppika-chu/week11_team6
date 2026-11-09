#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "AnimNotifyQueue.h"
#include "AnimationAsset.h"

class USkeletalMeshComponent;
struct FAnimNode_Base;
struct FAnimPose;
class UBlendSpace;
class UAnimSequenceBase;
struct FVector;
struct FBlendSampleData;

// 이 인스턴스가 Root Motion을 어떻게 다룰지 (UE ERootMotionMode). 몽타주가 없어 RootMotionFromMontagesOnly는 두지 않는다
namespace ERootMotionMode
{
	enum Type : int32
	{
		NoRootMotionExtraction,   // 꺼내지 않는다. 루트 본이 애니메이션대로 움직인다
		IgnoreRootMotion,         // 꺼내서 루트는 고정하고 변화량은 버린다
		RootMotionFromEverything, // 꺼내서 루트를 고정하고 변화량을 모은다
	};
}

// 컴포넌트별 애니메이션 실행 상태와 Graph 실행을 관리한다.
class UAnimInstance : public UObject
{
	DECLARE_CLASS(UAnimInstance, UObject)
public:
	// UE 기본값은 RootMotionFromMontagesOnly지만 몽타주가 없어 시퀀스에서도 받는다
	ERootMotionMode::Type RootMotionMode = ERootMotionMode::RootMotionFromEverything;
	// 포즈 평가에서 루트를 고정할지 (UE FAnimInstanceProxy::ShouldExtractRootMotion)
	bool ShouldExtractRootMotion() const { return RootMotionMode != ERootMotionMode::NoRootMotionExtraction; }
	// 이번 Update에서 모은 변화량을 꺼내고 비운다 (UE UAnimInstance::ConsumeExtractedRootMotion)
	FRootMotionMovementParams ConsumeExtractedRootMotion();

	void InitializeAnimation();
	void UninitializeAnimation();
	void UpdateAnimation(float DeltaSeconds);
	void EvaluateAnimation(FAnimPose& OutPose);
	bool IsInitialized() const { return !bUninitialized; }

	USkeletalMeshComponent* GetSkelMeshComponent() const;

	// BlendSpace 시간 갱신 — CurrentTime은 0~1 진행률이며 Cache와 구간 index는 BlendSpace Player Node가 보관한다.
	// 시간을 전진시킬 때 Notify도 같이 수집한다.
	// TODO: BlendSpace Player Node의 Update에서 호출하고, Evaluate에서 갱신된 Cache로 UBlendSpace::GetAnimationPose를 호출하도록 연결 예정.
	void BlendSpaceAdvanceImmediate(UBlendSpace* BlendSpace, const FVector& BlendInput, TArray<FBlendSampleData>& BlendSampleDataCache, bool bLooping, float PlayRate, float DeltaSeconds, float& CurrentTime, int32& CachedTriangulationIndex, float EffectiveBlendWeight = 1.0f);

	// Sequence Player Node와 SingleNode가 재생 시간을 전진시키고 Notify를 수집하는 입구.
	// TODO: Graph의 최종 Weight 전달 연결 예정. 단독 재생의 Weight는 1이다.
	void SequenceAdvanceImmediate(UAnimSequenceBase* Sequence, bool bLooping, float PlayRate, float DeltaSeconds, float& CurrentTime, float EffectiveBlendWeight = 1.0f);

	// 프레임별 수집 결과 전달 — 일반 재생은 Pose 확정 뒤 한 번 호출한다.
	void DispatchQueuedAnimEvents();
	void TriggerAnimNotifies(float DeltaSeconds);
	void TriggerSingleAnimNotify(const FAnimNotifyEventReference& EventReference);
	void ClearQueuedAnimEvents();
	virtual bool HandleNotify(const FAnimNotifyEvent& AnimNotifyEvent) { return false; }

	// 활성 State 종료 — 클립·모드 교체와 Instance 정리에서 호출한다.
	void EndNotifyStates();

	// 파생 Instance가 초기화·정리할 상태가 있을 때 재정의한다.
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUninitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}

protected:
	FAnimNotifyQueue NotifyQueue;
	// SequenceAdvanceImmediate가 모으고 ConsumeExtractedRootMotion이 비운다 (UE ExtractedRootMotion)
	FRootMotionMovementParams ExtractedRootMotion;
	// 다음 프레임의 End 비교에 쓸 이벤트 값과 출처 정보. NotifyState 객체는 Asset이 보관한다.
	TArray<FAnimNotifyEvent> ActiveAnimNotifyState;
	TArray<FAnimNotifyEventReference> ActiveAnimNotifyEventReference;

	// 파생 AnimInstance가 Graph의 Node들을 보관하고 실행 시작점의 주소를 반환한다.
	// Graph가 없는 기본 Instance는 Reference Pose를 사용한다.
	virtual FAnimNode_Base* GetCustomRootNode() { return nullptr; }

private:
	// 파생 AnimInstance가 보관한 Graph의 실행 시작점. GetCustomRootNode에서 받은 주소다.
	FAnimNode_Base* RootNode = nullptr;
	bool bUninitialized = true;
	float DeltaTime = 0.0f;
	bool bNeedsQueuedAnimEventsDispatched = false;
	uint64 NotifyDispatchSerial = 0;
};
