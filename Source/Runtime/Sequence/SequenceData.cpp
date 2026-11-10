#include "EnginePCH.h"
#include "SequenceData.h"
#include "Serialization/StructuredArchive.h"
#include "Animation/AnimSequence.h"
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

void SerializeAnimationSections(FStructuredArchive::FRecord Record, std::vector<FAnimationSection>& Sections)
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
		Item << SA_VALUE("StartFrame", Section.StartFrame);

		UObject* Asset = Section.Animation;
		Item << SA_VALUE("Animation", Asset);
		if (Ar.IsLoading())
		{
			Section.Animation = Cast<UAnimSequence>(Asset);
		}
	}
}
}

void FSequenceBinding::Serialize(FStructuredArchive::FRecord Record)
{
	Record << SA_VALUE("ActorName", ActorName) << SA_VALUE("ComponentName", ComponentName);
}

int32 FAnimationSection::GetLengthInFrames(int FPS) const
{
	const float Length = Animation ? Animation->GetPlayLength() : 0.0f;
	if (FPS <= 0 || !std::isfinite(Length) || Length <= 0.0f)
	{
		return 0;
	}

	const double Frames = std::ceil(static_cast<double>(Length) * static_cast<double>(FPS));
	return static_cast<int32>(FMath::Clamp<double>(Frames, 1.0, std::numeric_limits<int32>::max()));
}

int32 FAnimationSection::GetEndFrame(int FPS) const
{
	const int64 End = static_cast<int64>(StartFrame) + static_cast<int64>(GetLengthInFrames(FPS));
	return static_cast<int32>(FMath::Min<int64>(End, std::numeric_limits<int32>::max()));
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
	if (Count < 0)
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
		FString Type = std::holds_alternative<FCameraTimelineTrack>(Row.Rows) ? "Camera"
			: std::holds_alternative<FFadeTimelineTrack>(Row.Rows)            ? "Fade"
																			  : "Animation";

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
		else if (Type == "Animation")
		{
			if (Ar.IsLoading())
			{
				Row.Rows = FAnimationTimelineTrack{};
			}
			auto& AnimationTrack = std::get<FAnimationTimelineTrack>(Row.Rows);
			AnimationTrack.Binding.Serialize(Track.EnterRecord("Binding"));
			SerializeAnimationSections(Track, AnimationTrack.AnimSections);
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
			using TrackType = std::decay_t<decltype(TypedTrack)>;
			if constexpr (std::is_same_v<TrackType, FAnimationTimelineTrack>)
			{
				for (const auto& Section : TypedTrack.AnimSections)
				{
					if (Section.StartFrame < StartFrame || Section.StartFrame > End)
					{
						Ar.SetError();
					}
				}
			}
			else
			{
				const auto& Sections = [&]() -> const auto&
				{
					if constexpr (std::is_same_v<TrackType, FCameraTimelineTrack>)
					{
						return TypedTrack.CamSections;
					}
					else
					{
						return TypedTrack.FadeSections;
					}
				}();

				for (const auto& Section : Sections)
				{
					for (int Index = 0; Index < Section.KeyCount; ++Index)
					{
						if (Section.KeyFrames[Index] < StartFrame || Section.KeyFrames[Index] > End)
						{
							Ar.SetError();
						}
					}
				}
			}
		}, Row.Rows);
	}

	if (Ar.IsLoading() && !Ar.IsError())
	{
		RemoveInvalidAnimationData();
	}
}

FAnimationTimelineTrack* FSequenceData::FindAnimationTrack(const FSequenceBinding& Binding)
{
	for (auto& Row : Rows)
	{
		auto* Track = std::get_if<FAnimationTimelineTrack>(&Row.Rows);
		if (Track && Track->Binding.ActorName == Binding.ActorName && Track->Binding.ComponentName == Binding.ComponentName)
		{
			return Track;
		}
	}
	return nullptr;
}

FAnimationTimelineTrack& FSequenceData::FindOrAddAnimationTrack(const FSequenceBinding& Binding)
{
	if (FAnimationTimelineTrack* Existing = FindAnimationTrack(Binding))
	{
		return *Existing;
	}

	FAnimationTimelineTrack Track;
	Track.Binding = Binding;
	Rows.push_back({
		.Name = Binding.ActorName,
		.Color = FLinearColor(0.35f, 0.75f, 0.45f, 0.30f),
		.Rows = std::move(Track),
	});
	return std::get<FAnimationTimelineTrack>(Rows.back().Rows);
}

void FSequenceData::RemoveInvalidAnimationData()
{
	std::vector<const FSequenceBinding*> SeenBindings;
	std::erase_if(Rows, [&](FTimeline& Row)
	{
		auto* Track = std::get_if<FAnimationTimelineTrack>(&Row.Rows);
		if (!Track)
		{
			return false;
		}

		const bool bDuplicate = std::ranges::any_of(SeenBindings, [&](const FSequenceBinding* Seen)
		{
			return Seen->ActorName == Track->Binding.ActorName && Seen->ComponentName == Track->Binding.ComponentName;
		});

		if (!Track->Binding.IsSet() || bDuplicate)
		{
			HTR_LOG(Warning, "[Sequence] Dropped animation track '{}'", Row.Name);
			return true;
		}
		
		std::erase_if(Track->AnimSections, [&](const FAnimationSection& Section)
		{
			if (!Section.Animation)
			{
				HTR_LOG(Warning, "[Sequence] Dropped animation section on '{}' (animation not found)", Track->Binding.ActorName);
				return true;
			}
			return false;
		});

		Row.Name = Track->Binding.ActorName;
		SeenBindings.push_back(&Track->Binding);
		return false;
	});
}
