#include "EnginePCH.h"
#include "DebugSkelMeshComponent.h"

#include "DebugSkelMeshSceneProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"

namespace
{
template <int32 N>
FSkinWeightColorRamp MakeUniformRamp(const float (&Table)[N][3])
{
	static_assert(N >= 2 && N <= MaxSkinWeightRampStops);

	FSkinWeightColorRamp Ramp;
	for (int32 i = 0; i < N; ++i)
	{
		Ramp.Stops.Add(FVector4(Table[i][0], Table[i][1], Table[i][2], static_cast<float>(i) / static_cast<float>(N - 1)));
	}
	return Ramp;
}

constexpr float TurboTable[16][3] = {
	{ 0.18995f, 0.07176f, 0.23217f },
	{ 0.25369f, 0.26327f, 0.65406f },
	{ 0.27691f, 0.44145f, 0.91328f },
	{ 0.24427f, 0.60937f, 0.99697f },
	{ 0.13278f, 0.77165f, 0.88580f },
	{ 0.10342f, 0.89600f, 0.71500f },
	{ 0.27597f, 0.97092f, 0.51653f },
	{ 0.53255f, 0.99919f, 0.30581f },
	{ 0.72596f, 0.96470f, 0.20640f },
	{ 0.88331f, 0.86553f, 0.21719f },
	{ 0.98000f, 0.73000f, 0.22161f },
	{ 0.99297f, 0.55214f, 0.15417f },
	{ 0.94084f, 0.35566f, 0.07031f },
	{ 0.83926f, 0.20654f, 0.02305f },
	{ 0.68602f, 0.09536f, 0.00481f },
	{ 0.47960f, 0.01583f, 0.01055f },
};

constexpr float ViridisTable[16][3] = {
	{ 0.267004f, 0.004874f, 0.329415f },
	{ 0.282656f, 0.100196f, 0.422160f },
	{ 0.277134f, 0.185228f, 0.489898f },
	{ 0.253935f, 0.265254f, 0.529983f },
	{ 0.221989f, 0.339161f, 0.548752f },
	{ 0.190631f, 0.407061f, 0.556089f },
	{ 0.163625f, 0.471133f, 0.558148f },
	{ 0.139147f, 0.533812f, 0.555298f },
	{ 0.120565f, 0.596422f, 0.543611f },
	{ 0.134692f, 0.658636f, 0.517649f },
	{ 0.208030f, 0.718701f, 0.472873f },
	{ 0.327796f, 0.773980f, 0.406640f },
	{ 0.477504f, 0.821444f, 0.318195f },
	{ 0.647257f, 0.858400f, 0.209861f },
	{ 0.824940f, 0.884720f, 0.106217f },
	{ 0.993248f, 0.906157f, 0.143936f },
};

constexpr float InfernoTable[16][3] = {
	{ 0.001462f, 0.000466f, 0.013866f },
	{ 0.046915f, 0.030324f, 0.150164f },
	{ 0.142378f, 0.046242f, 0.308553f },
	{ 0.258234f, 0.038571f, 0.406485f },
	{ 0.366529f, 0.071579f, 0.431994f },
	{ 0.472328f, 0.110547f, 0.428334f },
	{ 0.578304f, 0.148039f, 0.404411f },
	{ 0.682656f, 0.189501f, 0.360757f },
	{ 0.780517f, 0.243327f, 0.299523f },
	{ 0.865006f, 0.316822f, 0.226055f },
	{ 0.929644f, 0.411479f, 0.145367f },
	{ 0.970919f, 0.522853f, 0.058367f },
	{ 0.987622f, 0.645320f, 0.039886f },
	{ 0.978806f, 0.774545f, 0.176037f },
	{ 0.950018f, 0.903409f, 0.380271f },
	{ 0.988362f, 0.998364f, 0.644924f },
};
}

FSkinWeightColorRamp FSkinWeightColorRamp::Greyscale()
{
	FSkinWeightColorRamp Ramp;
	Ramp.Stops.Add(FVector4(0.0f, 0.0f, 0.0f, 0.0f));
	Ramp.Stops.Add(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	return Ramp;
}

FSkinWeightColorRamp FSkinWeightColorRamp::Turbo()
{
	return MakeUniformRamp(TurboTable);
}

FSkinWeightColorRamp FSkinWeightColorRamp::Jet()
{
	FSkinWeightColorRamp Ramp;
	Ramp.Stops.Add(FVector4(0.0f, 0.0f, 0.5f, 0.0f));
	Ramp.Stops.Add(FVector4(0.0f, 0.0f, 1.0f, 0.125f));
	Ramp.Stops.Add(FVector4(0.0f, 1.0f, 1.0f, 0.375f));
	Ramp.Stops.Add(FVector4(1.0f, 1.0f, 0.0f, 0.625f));
	Ramp.Stops.Add(FVector4(1.0f, 0.0f, 0.0f, 0.875f));
	Ramp.Stops.Add(FVector4(0.5f, 0.0f, 0.0f, 1.0f));
	return Ramp;
}

FSkinWeightColorRamp FSkinWeightColorRamp::Viridis()
{
	return MakeUniformRamp(ViridisTable);
}

FSkinWeightColorRamp FSkinWeightColorRamp::Inferno() {
	return MakeUniformRamp(InfernoTable);
}

FPrimitiveSceneProxy* UDebugSkelMeshComponent::CreateSceneProxy()
{
	return new FDebugSkelMeshSceneProxy(this, MeshObject.get());
}

void UDebugSkelMeshComponent::SetShowBoneWeight(bool bShow)
{
	if (bDrawBoneInfluences != bShow)
	{
		bDrawBoneInfluences = bShow;
		MarkRenderStateDirty();
	}
}

void UDebugSkelMeshComponent::SetBoneOfInterest(int32 BoneIndex)
{
	if (BoneOfInterest != BoneIndex)
	{
		BoneOfInterest = BoneIndex;
		if (bDrawBoneInfluences)
		{
			MarkRenderStateDirty();
		}
	}
}

void UDebugSkelMeshComponent::SetBoneWeightColorRamp(const FSkinWeightColorRamp& InRamp)
{
	BoneWeightColorRamp = InRamp;
	if (bDrawBoneInfluences)
	{
		MarkRenderStateDirty();
	}
}

namespace
{
// 바닥 범위를 벗어나면 반대편으로 넘긴다 (UE WrapInRange)
float WrapInRange(float Value, float Min, float Max)
{
	const float Size = Max - Min;
	if (Size <= 0.0f)
	{
		return Value;
	}
	while (Value > Max)
	{
		Value -= Size;
	}
	while (Value < Min)
	{
		Value += Size;
	}
	return Value;
}

// 그 시각 루트 본을 루트 기준 자세에 대한 상대값으로 (UE UE::Anim::ExtractRootTransformFromAnimationAsset)
FTransform ExtractRootTransformFromAnimationAsset(const UAnimSequence& Sequence, float Time)
{
	const USkeleton* Skeleton = Sequence.GetSkeleton();
	if (!Skeleton || Skeleton->RefSkeleton.GetNum() == 0)
	{
		return FTransform::Identity;
	}
	FTransform RootTransform = Sequence.ExtractRootTrackTransform(Time);
	RootTransform.Scale = FVector(1.0f, 1.0f, 1.0f);
	return Skeleton->RefSkeleton.RefBonePose[0].Inverse() * RootTransform;
}
}

bool UDebugSkelMeshComponent::DoesCurrentAssetHaveRootMotion() const
{
	const UAnimSingleNodeInstance* PreviewInstance = GetSingleNodeInstance();
	const UAnimSequenceBase* Sequence = PreviewInstance ? Cast<UAnimSequenceBase>(PreviewInstance->GetCurrentAsset()) : nullptr;
	return Sequence && Sequence->HasRootMotion();
}

bool UDebugSkelMeshComponent::CanUseProcessRootMotionMode(EProcessRootMotionMode Mode) const
{
	if (!GetSingleNodeInstance())
	{
		return false;
	}
	return Mode == EProcessRootMotionMode::Ignore || DoesCurrentAssetHaveRootMotion();
}

void UDebugSkelMeshComponent::SetProcessRootMotionMode(EProcessRootMotionMode Mode)
{
	RequestedProcessRootMotionMode = Mode;
	if (CanUseProcessRootMotionMode(Mode))
	{
		SetProcessRootMotionModeInternal(Mode);
	}
}

void UDebugSkelMeshComponent::SetProcessRootMotionModeInternal(EProcessRootMotionMode Mode)
{
	ProcessRootMotionMode = Mode;
	ResetRootTransform();
}

void UDebugSkelMeshComponent::ResetRootTransform()
{
	// 크기는 두고 위치, 회전만 되돌린다
	FTransform RootMotionTransform = FTransform::Identity;
	const UAnimSingleNodeInstance* PreviewInstance = GetSingleNodeInstance();
	if (PreviewInstance)
	{
		ConsumeRootMotionPreviousPlaybackTime = PreviewInstance->GetCurrentTime();
		// Loop and Reset은 지금 시각까지 이동한 자리에 둔다. 다음 반복 전까지 메시가 튀지 않는다
		const UAnimSequence* Sequence = Cast<UAnimSequence>(PreviewInstance->GetCurrentAsset());
		if (ProcessRootMotionMode == EProcessRootMotionMode::LoopAndReset && Sequence)
		{
			RootMotionTransform = ExtractRootTransformFromAnimationAsset(*Sequence, PreviewInstance->GetCurrentTime());
		}
	}
	RootMotionTransform.Scale = GetTransform().Scale;
	SetTransform(RootMotionTransform);
}

void UDebugSkelMeshComponent::ConsumeRootMotion(const FVector& FloorMin, const FVector& FloorMax)
{
	UAnimSingleNodeInstance* PreviewInstance = GetSingleNodeInstance();
	if (!PreviewInstance)
	{
		return;
	}

	// 에셋이 Root Motion을 안 쓰면 Ignore로, 쓰게 되면 요청 값으로
	if (ProcessRootMotionMode != EProcessRootMotionMode::Ignore && !DoesCurrentAssetHaveRootMotion())
	{
		SetProcessRootMotionModeInternal(EProcessRootMotionMode::Ignore);
	}
	if (ProcessRootMotionMode != RequestedProcessRootMotionMode && CanUseProcessRootMotionMode(RequestedProcessRootMotionMode))
	{
		SetProcessRootMotionModeInternal(RequestedProcessRootMotionMode);
	}

	const FRootMotionMovementParams ExtractedRootMotion = USkeletalMeshComponent::ConsumeRootMotion();
	const float CurrentTime = PreviewInstance->GetCurrentTime();
	const float PreviousTime = ConsumeRootMotionPreviousPlaybackTime;
	ConsumeRootMotionPreviousPlaybackTime = CurrentTime;

	const UAnimSequence* Sequence = Cast<UAnimSequence>(PreviewInstance->GetCurrentAsset());
	if (ProcessRootMotionMode == EProcessRootMotionMode::Ignore || !Sequence)
	{
		return;
	}

	const FVector Scale = GetTransform().Scale;
	const auto AddLocalTransform = [this, &Scale](const FTransform& Delta)
	{
		// UE AddLocalTransform: 변화량을 먼저 적용하고 지금 상대 변환을 적용한다
		FTransform Relative = GetTransform();
		Relative.Scale = FVector(1.0f, 1.0f, 1.0f);
		FTransform NewRelative = Delta * Relative;
		NewRelative.Scale = Scale;
		SetTransform(NewRelative);
	};

	if (!PreviewInstance->IsPlaying())
	{
		// 재생하지 않아도 타임라인을 끌면 시간이 바뀐다. 그 구간의 변화량을 직접 꺼낸다
		if (CurrentTime != PreviousTime)
		{
			AddLocalTransform(Sequence->ExtractRootMotionFromRange(PreviousTime, CurrentTime));
		}
		return;
	}

	const bool bLooped = PreviewInstance->IsReverse() ? CurrentTime > PreviousTime : CurrentTime < PreviousTime;
	const FTransform RootMotionDelta = ExtractedRootMotion.bHasRootMotion ? ExtractedRootMotion.GetRootMotionTransform() : FTransform::Identity;

	if (ProcessRootMotionMode == EProcessRootMotionMode::Loop)
	{
		FTransform Relative = GetTransform();
		Relative.Scale = FVector(1.0f, 1.0f, 1.0f);
		FTransform RootMotionTransform = RootMotionDelta * Relative;
		// 바닥 밖으로 나가면 반대편으로 넘긴다
		RootMotionTransform.Location.X = WrapInRange(RootMotionTransform.Location.X, FloorMin.X, FloorMax.X);
		RootMotionTransform.Location.Y = WrapInRange(RootMotionTransform.Location.Y, FloorMin.Y, FloorMax.Y);
		RootMotionTransform.Scale = Scale;
		SetTransform(RootMotionTransform);
	}
	else if (ProcessRootMotionMode == EProcessRootMotionMode::LoopAndReset)
	{
		if (bLooped)
		{
			// 반복되면 시작(역재생이면 끝)부터 지금 시각까지 이동한 자리로 되돌린다
			const float SectionStartPosition = PreviewInstance->IsReverse() ? Sequence->GetPlayLength() : 0.0f;
			const FTransform InitialTransform = ExtractRootTransformFromAnimationAsset(*Sequence, SectionStartPosition);
			FTransform RootMotionTransform = Sequence->ExtractRootMotionFromRange(SectionStartPosition, CurrentTime) * InitialTransform;
			RootMotionTransform.Scale = Scale;
			SetTransform(RootMotionTransform);
		}
		else
		{
			AddLocalTransform(RootMotionDelta);
		}
	}
}
