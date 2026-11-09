#include "EnginePCH.h"
#include "DebugSkelMeshComponent.h"

#include "DebugSkelMeshSceneProxy.h"

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
