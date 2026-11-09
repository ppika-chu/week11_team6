#include "EnginePCH.h"
#include "AnimNode_SingleNode.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"

void FAnimNode_SingleNode::Update(const FAnimationUpdateContext& Context)
{
	auto* Sequence = Cast<UAnimSequenceBase>(Instance->CurrentAsset);
	if (!Sequence)
	{
		return;
	}
	const float NewPlayRate = Instance->bPlaying ? Instance->PlayRate : 0.0f;
	const float CombinedPlayRate = NewPlayRate * Sequence->RateScale;
	// 이번 Update의 속도를 정한 뒤 전진 전 시각으로 재생 종료를 판정한다.
	if (!Instance->bLooping && ((CombinedPlayRate < 0.0f && Instance->CurrentTime <= 0.0f) || (CombinedPlayRate > 0.0f && Instance->CurrentTime >= Instance->GetLength())))
	{
		Instance->SetPlaying(false);
	}
	Instance->SequenceAdvanceImmediate(Sequence, Instance->bLooping, NewPlayRate, Context.GetDeltaTime(), Instance->CurrentTime, Context.GetFinalBlendWeight());
}

void FAnimNode_SingleNode::Evaluate(FAnimPose& OutPose)
{
	if (auto* Sequence = Cast<UAnimSequenceBase>(Instance->CurrentAsset))
	{
		// Root Motion을 꺼내는 인스턴스면 루트 본을 고정한다
		Sequence->GetAnimationPose(OutPose, FAnimExtractContext(Instance->CurrentTime, Instance->ShouldExtractRootMotion()));
	}
}
