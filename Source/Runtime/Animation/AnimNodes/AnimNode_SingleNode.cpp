#include "EnginePCH.h"
#include "AnimNode_SingleNode.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationRuntime.h"

void FAnimNode_SingleNode::Update(float DeltaTime)
{
	auto* Sequence = Cast<UAnimSequenceBase>(Instance->CurrentAsset);
	if (!Sequence)
	{
		return;
	}
	const float CombinedPlayRate = (Instance->bPlaying ? Instance->PlayRate : 0.0f) * Sequence->RateScale;
	FAnimationRuntime::AdvanceTime(Instance->bLooping, DeltaTime * CombinedPlayRate, Instance->CurrentTime, Sequence->GetPlayLength());
	if (!Instance->bLooping && ((CombinedPlayRate < 0.0f && Instance->CurrentTime <= 0.0f) || (CombinedPlayRate > 0.0f && Instance->CurrentTime >= Instance->GetLength())))
	{
		Instance->SetPlaying(false);
	}
}

void FAnimNode_SingleNode::Evaluate(FAnimPose& OutPose)
{
	if (auto* Sequence = Cast<UAnimSequenceBase>(Instance->CurrentAsset))
	{
		Sequence->GetAnimationPose(OutPose, FAnimExtractContext(Instance->CurrentTime));
	}
}
