#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"

// 시연용 구간 Notify. Begin/End는 항상, Tick은 옵션을 켰을 때 로그로 확인한다.
class UAnimNotifyState_Log : public UAnimNotifyState
{
	DECLARE_CLASS(UAnimNotifyState_Log, UAnimNotifyState)
	REFLECT_START(UAnimNotifyState_Log)
		PROPERTY(Message)
		PROPERTY(bLogTick)
	REFLECT_END()

public:
	FString Message = "AttackWindow";
	bool bLogTick = false;
	void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
