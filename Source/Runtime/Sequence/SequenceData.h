#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Color.h"
#include "Serialization/StructuredArchive.h"
#include <variant>
#include <vector>
#include <limits>

class UAnimSequence;

enum CurveType
{
	Linear,
	EaseIn,
	EaseOut,
	Bezier
};

struct CameraKey // KeyFrame에서의 Camera 정보
{
	FVector Location;
	FRotator Rotation;
	float FieldOfView = 60.0f;
};

struct FadeKey // 첫 번째 키와 두 번째 키의 Alpha
{
	float StartAlpha = 0.0f;
	float EndAlpha = 0.0f;
};

struct FSequenceBinding
{
	FString ActorName;
	FString ComponentName;

	bool IsSet() const { return !ActorName.empty(); }
	void Serialize(FStructuredArchive::FRecord Record);
};

struct FCameraSection // 한 Track 내에서 생긴 Camera 구간의 정보
{
	int32 KeyFrames[2] = {};
	int KeyCount = 0;
	CameraKey KeyCam[2] = {};
	bool bUseShortestRotation = true; // 끄면 입력한 각도 차이(360도 이상 포함)를 그대로 보간한다.
	CurveType Curve = CurveType::Linear;
	float EaseExp = 2.0f;
	bool mCutAtStart{};
};

struct FFadeSection // 한 Track 내에서 생긴 Fade 구간의 정보
{
	int32 KeyFrames[2] = {};
	int KeyCount = 0;
	FadeKey KeyFade;
	CurveType Curve = CurveType::Linear;
	float EaseExp = 2.0f;
};

struct FAnimationSection
{
	int32 StartFrame = 0;
	UAnimSequence* Animation = nullptr;

	int32 GetLengthInFrames(int FPS) const;
	int32 GetEndFrame(int FPS) const;
};

struct FCameraTimelineTrack // Camera Section 모음, 한 Row의 정보
{
	std::vector<FCameraSection> CamSections;
	bool mUseCatmullRom{ true };
};

struct FFadeTimelineTrack // Fade Section 모음, 한 Row의 정보
{
	std::vector<FFadeSection> FadeSections;
};

struct FAnimationTimelineTrack
{
	FSequenceBinding Binding;
	std::vector<FAnimationSection> AnimSections;
};

struct FTimeline // 두 Track 중 하나를 선택한 Sequencer 표시용 Row
{
	FString Name;
	FLinearColor Color;
	std::variant<FCameraTimelineTrack, FFadeTimelineTrack, FAnimationTimelineTrack> Rows;
};

// Sequence UI에서 값을 가져오기 위한 Data
struct FSequenceData
{
	void Serialize(FStructuredArchive::FRecord Record);
	std::vector<FTimeline> Rows = {
		{ "Viewport Camera", FLinearColor(0.31f, 0.59f, 0.90f, 0.30f), FCameraTimelineTrack{} },
		{ "Fade", FLinearColor(0.59f, 0.43f, 0.82f, 0.30f), FFadeTimelineTrack{} }
	};
	int32 StartFrame = 0;
	int32 EndFrameSecond = 100;
	int PlaybackFPS = 30;

	int32 GetEndFrame() const
	{
		const int64 End = static_cast<int64>(StartFrame) + static_cast<int64>(EndFrameSecond) * PlaybackFPS;
		return static_cast<int32>(FMath::Clamp<int64>(End, StartFrame, std::numeric_limits<int32>::max()));
	}

	template <typename Func>
	void ForEachBinding(Func&& Action)
	{
		for (auto& Row : Rows)
		{
			// 트랙에 Binding이 있는 경우에만 Action을 호출한다.
			std::visit([&](auto& Track)
			{
				if constexpr (requires { Track.Binding; })
				{
					Action(Track.Binding);
				}
			}, Row.Rows);
		}
	}

	FAnimationTimelineTrack* FindAnimationTrack(const FSequenceBinding& Binding);
	FAnimationTimelineTrack& FindOrAddAnimationTrack(const FSequenceBinding& Binding);

	void RemoveInvalidAnimationData();
};
