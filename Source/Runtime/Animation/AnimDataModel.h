#pragma once

#include "Animation/AnimTypes.h"
#include "Core/EngineString.h"
#include "UObject/Object.h"
#include "UObject/Class.h"

struct FBoneAnimationTrack
{
	FRawAnimSequenceTrack InternalTrackData;
	int32 BoneTreeIndex = INDEX_NONE;
	FString Name;
};

class UAnimDataModel : public UObject
{
	DECLARE_CLASS(UAnimDataModel, UObject)
public:
	float GetPlayLength() const { return FrameRate > 0.0f ? NumberOfFrames / FrameRate : 0.0f; }

	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	float FrameRate = 30.0f;
	int32 NumberOfFrames = 0;
	int32 NumberOfKeys = 0;
};
