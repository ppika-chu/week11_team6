#include "EnginePCH.h"
#include "SequenceData.h"
#include "Serialization/StructuredArchive.h"
#include <cmath>
#include <limits>
#include <type_traits>

namespace
{
template<typename SectionType>
void SerializeSections(FStructuredArchive::FRecord Record, std::vector<SectionType>& Sections)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	int32 Count = static_cast<int32>(Sections.size());
	auto Array = Record.EnterArray("Sections", Count);
	if (Count < 0)
	{
		Ar.SetError();
		return;
	}
	if (Ar.IsLoading())
	{
		Sections.resize(Count);
	}
	for (auto& Section : Sections)
	{
		auto Item = Array.EnterElement().EnterRecord();
		int32 Curve = static_cast<int32>(Section.Curve);
		Item << SA_VALUE("Curve", Curve) << SA_VALUE("EaseExp", Section.EaseExp);
		if (Curve < CurveType::Linear || Curve > CurveType::Bezier ||
			!std::isfinite(Section.EaseExp) || Section.EaseExp <= 0)
		{
			Ar.SetError();
			return;
		}
		Section.Curve = static_cast<CurveType>(Curve);
		if constexpr (std::is_same_v<SectionType, FCameraSection>)
		{
			// 기존 씬에는 이 필드가 없으므로 기본값인 최단 회전을 사용한다.
			if (Ar.IsLoading())
			{
				Section.bUseShortestRotation = true;
			}
			if (auto Slot = Item.TryEnterField("UseShortestRotation", true))
			{
				Slot.GetValue() << Section.bUseShortestRotation;
			}
			if (Ar.IsLoading())
			{
				Section.mCutAtStart = false;
			}
			if (auto Slot{ Item.TryEnterField("CutAtStart", true) })
			{
				Slot.GetValue() << Section.mCutAtStart;
			}
		}
		int32 KeyCount = Section.KeyCount;
		auto Keys = Item.EnterArray("Keys", KeyCount);
		if (KeyCount < 0 || KeyCount > 2)
		{
			Ar.SetError();
			return;
		}
		Section.KeyCount = KeyCount;
		for (int32 Index = 0; Index < KeyCount; ++Index)
		{
			auto Key = Keys.EnterElement().EnterRecord();
			Key << SA_VALUE("Frame", Section.KeyFrames[Index]);
			if constexpr (std::is_same_v<SectionType, FCameraSection>)
			{
				auto& Camera = Section.KeyCam[Index];
				auto Location = Key.EnterRecord("Location");
				Location << SA_VALUE("X", Camera.Location.X) << SA_VALUE("Y", Camera.Location.Y) << SA_VALUE("Z", Camera.Location.Z);
				auto Rotation = Key.EnterRecord("Rotation");
				Rotation << SA_VALUE("Pitch", Camera.Rotation.Pitch) << SA_VALUE("Yaw", Camera.Rotation.Yaw) << SA_VALUE("Roll", Camera.Rotation.Roll);
				Key << SA_VALUE("FOV", Camera.FieldOfView);
				if (!std::isfinite(Camera.Location.X) || !std::isfinite(Camera.Location.Y) || !std::isfinite(Camera.Location.Z) ||
					!std::isfinite(Camera.Rotation.Pitch) || !std::isfinite(Camera.Rotation.Yaw) || !std::isfinite(Camera.Rotation.Roll) ||
					!std::isfinite(Camera.FieldOfView) || Camera.FieldOfView <= 0 || Camera.FieldOfView >= 180)
				{
					Ar.SetError();
				}
			}
			else
			{
				float& Alpha = Index == 0 ? Section.KeyFade.StartAlpha : Section.KeyFade.EndAlpha;
				Key << SA_VALUE("Alpha", Alpha);
				if (!std::isfinite(Alpha) || Alpha < 0 || Alpha > 1)
				{
					Ar.SetError();
				}
			}
		}
		if (KeyCount == 2 && Section.KeyFrames[0] == Section.KeyFrames[1])
		{
			Ar.SetError();
		}
	}
}
}

void FSequenceData::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	Record << SA_VALUE("StartFrame", StartFrame) << SA_VALUE("DurationSeconds", EndFrameSecond) << SA_VALUE("FPS", PlaybackFPS);
	const int64 End = int64(StartFrame) + int64(EndFrameSecond) * PlaybackFPS;
	if (StartFrame < 0 || EndFrameSecond <= 0 || PlaybackFPS <= 0 || End > std::numeric_limits<int32>::max())
	{
		Ar.SetError();
		return;
	}
	int32 Count = static_cast<int32>(Rows.size());
	auto Tracks = Record.EnterArray("Tracks", Count);
	if (Count < 0 || Count > 2)
	{
		Ar.SetError();
		return;
	}
	if (Ar.IsLoading())
	{
		Rows.clear();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Rows.push_back(FTimeline{ FString{}, FLinearColor(0, 0, 0, 0), FCameraTimelineTrack{} });
		}
	}
	bool HasCamera = false, HasFade = false;
	for (auto& Row : Rows)
	{
		auto Track = Tracks.EnterElement().EnterRecord();
		FString Type = std::holds_alternative<FCameraTimelineTrack>(Row.Rows) ? "Camera" : "Fade";
		Track << SA_VALUE("Name", Row.Name) << SA_VALUE("Color", Row.Color) << SA_VALUE("Type", Type);
		if (Type == "Camera" && !HasCamera)
		{
			HasCamera = true;
			if (Ar.IsLoading())
			{
				Row.Rows = FCameraTimelineTrack{};
			}
			auto& CameraTrack{ std::get<FCameraTimelineTrack>(Row.Rows) };
			if (Ar.IsLoading())
			{
				CameraTrack.mUseCatmullRom = false;
			}
			if (auto Slot{ Track.TryEnterField("UseCatmullRom", true) })
			{
				Slot.GetValue() << CameraTrack.mUseCatmullRom;
			}
			SerializeSections(Track, CameraTrack.CamSections);
		}
		else if (Type == "Fade" && !HasFade)
		{
			HasFade = true;
			if (Ar.IsLoading())
			{
				Row.Rows = FFadeTimelineTrack{};
			}
			SerializeSections(Track, std::get<FFadeTimelineTrack>(Row.Rows).FadeSections);
		}
		else
		{
			Ar.SetError();
			return;
		}
		if (Ar.IsError())
		{
			return;
		}
		std::visit([&](auto& TypedTrack)
			{
			const auto& Sections = [&]() -> const auto&
			{
				if constexpr (std::is_same_v<std::decay_t<decltype(TypedTrack)>, FCameraTimelineTrack>){ return TypedTrack.CamSections;
}
				else{ return TypedTrack.FadeSections;
}
			}();
			for (const auto& Section : Sections){
				for (int Index = 0; Index < Section.KeyCount; ++Index){
					if (Section.KeyFrames[Index] < StartFrame || Section.KeyFrames[Index] > End){ Ar.SetError();
}}} }, Row.Rows);
	}
}
