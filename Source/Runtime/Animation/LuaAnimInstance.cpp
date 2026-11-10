#include "EnginePCH.h"
#include "LuaAnimInstance.h"
#include "Animation/AnimNodes/AnimNode_Slot.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <stdexcept>

void ULuaAnimInstance::SetGraphRoot(FAnimNode_Base* InRoot)
{
	ClearMontages();
	GraphRoot = InRoot;
	if (IsInitialized())
	{
		InitializeAnimation();
	}
}

FAnimNode_Base* ULuaAnimInstance::GetGraphRoot() const
{ return GraphRoot; }

void ULuaAnimInstance::PlayMontage(FAnimNode_Slot* Slot, UAnimSequence* Sequence, float PlayRate, float BlendIn, float BlendOut, bool bLooping)
{
	FAnimPose Pose;
	Sequence->GetAnimationPose(Pose, FAnimExtractContext(0.0f));
	const USkeletalMeshComponent* Component = GetSkelMeshComponent();
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || Pose.LocalTransforms.Num() != Mesh->RefSkeleton.GetNum())
	{
		throw std::runtime_error("Montage pose bone count does not match the skeletal mesh");
	}
	StopMontage(Slot, 0.0f);
	FActiveMontage& Playback = Montages.emplace_back();
	Playback.Slot = Slot;
	Playback.Sequence = Sequence;
	Playback.PlayRate = PlayRate;
	Playback.BlendIn = BlendIn;
	Playback.BlendOut = BlendOut;
	Playback.bLooping = bLooping;
	Playback.CurrentTime = 0.0f;
	Playback.ElapsedTime = 0.0f;
	// 재생 직후 평가가 일어나도 이전 Montage 포즈가 남지 않게 한다.
	Slot->SetSlotPose(Pose);
	Slot->SetMontageWeight(BlendIn == 0.0f ? 1.0f : 0.0f);
}

void ULuaAnimInstance::StopMontage(FAnimNode_Slot* Slot, float BlendOut)
{
	for (FActiveMontage& Playback : Montages)
	{
		if (Playback.Slot != Slot) { continue; }
		if (BlendOut > 0.0f)
		{
			Playback.bStopping = true;
			Playback.StopDuration = BlendOut;
			Playback.StopElapsed = 0.0f;
			Playback.StopStartWeight = Slot->GetMontageWeight();
		}
		else { ClearMontagePose(Slot); }
	}
	if (BlendOut <= 0.0f)
	{
		std::erase_if(Montages, [Slot](const FActiveMontage& Playback) { return Playback.Slot == Slot; });
	}
}

bool ULuaAnimInstance::IsMontagePlaying(const FAnimNode_Slot* Slot) const
{
	return std::any_of(Montages.begin(), Montages.end(), [Slot](const FActiveMontage& Playback) { return Playback.Slot == Slot; });
}

float ULuaAnimInstance::GetMontageTime(const FAnimNode_Slot* Slot) const
{
	for (const FActiveMontage& Playback : Montages)
	{
		if (Playback.Slot == Slot) { return Playback.CurrentTime; }
	}
	return 0.0f;
}

FAnimNode_Base* ULuaAnimInstance::GetCustomRootNode()
{ return GraphRoot; }

void ULuaAnimInstance::NativeUninitializeAnimation()
{ ClearMontages(); }

void ULuaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	for (FActiveMontage& Playback : Montages)
	{
		const float Length = Playback.Sequence->GetPlayLength();
		if (Length <= 0.0f) { ClearMontagePose(Playback.Slot); Playback.bFinished = true; continue; }
		const float Step = FMath::Max(0.0f, DeltaSeconds);
		float NextTime = Playback.CurrentTime;
		FAnimationRuntime::AdvanceTime(Playback.bLooping,
			Step * Playback.PlayRate * Playback.Sequence->RateScale, NextTime, Length);
		Playback.ElapsedTime += Step;
		const float InWeight = Playback.BlendIn > 0.0f
			? FMath::Clamp(Playback.ElapsedTime / Playback.BlendIn, 0.0f, 1.0f) : 1.0f;
		const float RemainingSeconds = (Length - NextTime) /
			(Playback.PlayRate * Playback.Sequence->RateScale);
		const float OutWeight = Playback.bLooping ? 1.0f :
			(Playback.BlendOut > 0.0f
				? FMath::Clamp(RemainingSeconds / Playback.BlendOut, 0.0f, 1.0f)
				: (NextTime < Length ? 1.0f : 0.0f));
		float Weight = FMath::Min(InWeight, OutWeight);
		if (Playback.bStopping)
		{
			Playback.StopElapsed += Step;
			Weight = FMath::Min(Weight, Playback.StopStartWeight *
				FMath::Clamp(1.0f - Playback.StopElapsed / Playback.StopDuration, 0.0f, 1.0f));
		}
		SequenceAdvanceImmediate(Playback.Sequence, Playback.bLooping, Playback.PlayRate,
			Step, Playback.CurrentTime, Weight);
		if (Weight <= 0.0f && (Playback.bStopping || (!Playback.bLooping && NextTime >= Length)))
		{
			ClearMontagePose(Playback.Slot);
			Playback.bFinished = true;
			continue;
		}
		FAnimPose Pose;
		Playback.Sequence->GetAnimationPose(Pose, FAnimExtractContext(Playback.CurrentTime));
		Playback.Slot->SetSlotPose(Pose);
		Playback.Slot->SetMontageWeight(Weight);
	}
	std::erase_if(Montages, [](const FActiveMontage& Playback) { return Playback.bFinished; });
}

void ULuaAnimInstance::ClearMontagePose(FAnimNode_Slot* Slot)
{
	Slot->SetMontageWeight(0.0f);
	Slot->SetSlotPose(FAnimPose{});
}

void ULuaAnimInstance::ClearMontages()
{
	for (FActiveMontage& Playback : Montages) { ClearMontagePose(Playback.Slot); }
	Montages.clear();
}
