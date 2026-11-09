#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"

// 시연용 단발 Notify. 에셋에는 메시지만 저장하고 재생별 상태는 보관하지 않는다.
class UAnimNotify_Log : public UAnimNotify
{
	DECLARE_CLASS(UAnimNotify_Log, UAnimNotify)
	REFLECT_START(UAnimNotify_Log)
		PROPERTY(Message)
	REFLECT_END()

public:
	FString Message = "Footstep";
	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
