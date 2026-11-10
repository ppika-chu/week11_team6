#include "EnginePCH.h"
#include "SequencePlayer.h"
#include "Math/FInterpEaseInOut.h"
#include <cmath>
#include <limits>

#include "Animation/AnimSequence.h"

namespace
{
struct FCameraControlPoint
{
	int32 mFrame{};
	const CameraKey* mValue{};
	bool mCut{};
	bool mShortest{ true };
};

float CatmullRom(double First, double Last, double Before, double After, double Span, double BeforeSpan, double AfterSpan, double Alpha)
{
	const double FirstTangent{ (Last - Before) / BeforeSpan };
	const double LastTangent{ (After - First) / AfterSpan };
	const double Square{ Alpha * Alpha };
	const double Cube{ Square * Alpha };
	return static_cast<float>((2.0 * Cube - 3.0 * Square + 1.0) * First + (Cube - 2.0 * Square + Alpha) * Span * FirstTangent + (-2.0 * Cube + 3.0 * Square) * Last + (Cube - Square) * Span * LastTangent);
}

bool GetSplineCamera(const FCameraTimelineTrack& Track, double Frame, CameraKey& OutCamera)
{
	FCameraControlPoint Before{};
	FCameraControlPoint First{};
	FCameraControlPoint Last{};
	FCameraControlPoint After{};
	for (const auto& Section : Track.CamSections)
	{
		if (Section.KeyCount < 0 || Section.KeyCount > 2)
		{
			continue;
		}
		for (int32 Index{ 0 }; Index < Section.KeyCount; ++Index)
		{
			FCameraControlPoint Point{ Section.KeyFrames[Index], &Section.KeyCam[Index], Index == 0 && Section.mCutAtStart, Section.bUseShortestRotation };
			if (Point.mFrame <= Frame)
			{
				if (!First.mValue || Point.mFrame > First.mFrame)
				{
					Before = First;
					First = Point;
				}
				else if (Point.mFrame == First.mFrame)
				{
					Point.mCut = Point.mCut || First.mCut;
					First = Point;
				}
				else if (!Before.mValue || Point.mFrame >= Before.mFrame)
				{
					Before = Point;
				}
			}
			else
			{
				if (!Last.mValue || Point.mFrame < Last.mFrame)
				{
					After = Last;
					Last = Point;
				}
				else if (Point.mFrame == Last.mFrame)
				{
					Point.mCut = Point.mCut || Last.mCut;
					Last = Point;
				}
				else if (!After.mValue || Point.mFrame <= After.mFrame)
				{
					Point.mCut = Point.mCut || (After.mValue && Point.mFrame == After.mFrame && After.mCut);
					After = Point;
				}
			}
		}
	}
	if (!First.mValue)
	{
		if (!Last.mValue)
		{
			return false;
		}
		OutCamera = *Last.mValue;
		return true;
	}
	if (!Last.mValue || Last.mCut)
	{
		OutCamera = *First.mValue;
		return true;
	}
	const double Span{ static_cast<double>(Last.mFrame - First.mFrame) };
	const double Alpha{ (Frame - First.mFrame) / Span };
	const bool HasBefore{ Before.mValue && !First.mCut };
	const bool HasAfter{ After.mValue && !After.mCut };
	const double BeforeSpan{ HasBefore ? static_cast<double>(Last.mFrame - Before.mFrame) : Span };
	const double AfterSpan{ HasAfter ? static_cast<double>(After.mFrame - First.mFrame) : Span };
	const bool HoldLocation{ First.mValue->Location.X == Last.mValue->Location.X && First.mValue->Location.Y == Last.mValue->Location.Y && First.mValue->Location.Z == Last.mValue->Location.Z };
	for (int32 Axis{ 0 }; Axis < 3; ++Axis)
	{
		OutCamera.Location[Axis] = HoldLocation ? First.mValue->Location[Axis] : CatmullRom(First.mValue->Location[Axis], Last.mValue->Location[Axis], HasBefore ? Before.mValue->Location[Axis] : First.mValue->Location[Axis], HasAfter ? After.mValue->Location[Axis] : Last.mValue->Location[Axis], Span, BeforeSpan, AfterSpan, Alpha);
		const double Start{ First.mValue->Rotation[Axis] };
		double Delta{ Last.mValue->Rotation[Axis] - Start };
		if (First.mShortest && Last.mShortest)
		{
			Delta = std::remainder(Delta, 360.0);
		}
		OutCamera.Rotation[Axis] = static_cast<float>(Start + Delta * Alpha);
	}
	OutCamera.FieldOfView = static_cast<float>(std::lerp(static_cast<double>(First.mValue->FieldOfView), static_cast<double>(Last.mValue->FieldOfView), Alpha));
	return true;
}

float LerpAngle(float Start, float End, float Alpha, bool bShortest)
{
	if (!bShortest)
	{
		return std::lerp(Start, End, Alpha);
	}
	// 각도는 360도마다 같은 방향이다. 경계를 지날 때 한 바퀴 돌아가지 않도록 차이를 줄인다.
	float Delta = std::fmod(End - Start, 360.0f);
	if (Delta > 180.0f)
	{
		Delta -= 360.0f;
	}
	else if (Delta < -180.0f)
	{
		Delta += 360.0f;
	}
	return Start + Delta * Alpha;
}

bool IsSectionActive(float Frame, const int32 (&KeyFrames)[2], int KeyCount)
{
	if (KeyCount != 2 || KeyFrames[0] == KeyFrames[1] || !std::isfinite(Frame))
	{
		return false;
	}
	return Frame >= FMath::Min(KeyFrames[0], KeyFrames[1]) &&
		Frame <= FMath::Max(KeyFrames[0], KeyFrames[1]);
}
}

void FSequencePlayer::SetSequenceData(const FSequenceData& InData)
{
	SequenceData = InData;
	SequenceData.StartFrame = FMath::Clamp(SequenceData.StartFrame, 0, std::numeric_limits<int32>::max() - 1);
	SetPlaybackSettings(SequenceData.PlaybackFPS, SequenceData.EndFrameSecond);
	Stop();
}

void FSequencePlayer::SetPlaybackSettings(int InFPS, int32 InDurationSeconds)
{
	SequenceData.StartFrame = FMath::Clamp(SequenceData.StartFrame, 0, std::numeric_limits<int32>::max() - 1);
	const int32 AvailableFrames = std::numeric_limits<int32>::max() - SequenceData.StartFrame;
	SequenceData.PlaybackFPS = FMath::Clamp(InFPS, 1, AvailableFrames);
	const int32 MaxSeconds = AvailableFrames / SequenceData.PlaybackFPS;
	SequenceData.EndFrameSecond = FMath::Clamp(InDurationSeconds, 1, MaxSeconds);
	SetPlaybackFrame(PlaybackFrame);
}

void FSequencePlayer::SetPlaybackFrame(float InFrame)
{
	if (!std::isfinite(InFrame))
	{
		return;
	}
	PlaybackFrame = FMath::Clamp(InFrame, static_cast<float>(SequenceData.StartFrame),
		static_cast<float>(SequenceData.GetEndFrame()));
	bFinished = false;
	mSampleFrame = PlaybackFrame;
	Pause();
}

bool FSequencePlayer::EvaluateAtTime(double Time)
{
	if (!std::isfinite(Time))
	{
		return false;
	}
	SetPlaybackFrame(static_cast<float>(FMath::Clamp(static_cast<double>(SequenceData.StartFrame) + Time * SequenceData.PlaybackFPS, static_cast<double>(SequenceData.StartFrame), static_cast<double>(SequenceData.GetEndFrame()))));
	mSampleFrame = FMath::Clamp(static_cast<double>(SequenceData.StartFrame) + Time * SequenceData.PlaybackFPS, static_cast<double>(SequenceData.StartFrame), static_cast<double>(SequenceData.GetEndFrame()));
	return true;
}

void FSequencePlayer::Play()
{
	if (PlaybackFrame >= SequenceData.GetEndFrame())
	{
		PlaybackFrame = static_cast<float>(SequenceData.StartFrame);
	}
	bIsPlaying = true;
	mSampleFrame = PlaybackFrame;
	bPlaybackStarted = true;
	bFinished = false;
}

void FSequencePlayer::Stop()
{
	Pause();
	bPlaybackStarted = false;
	bFinished = false;
	PlaybackFrame = static_cast<float>(SequenceData.StartFrame);
	mSampleFrame = PlaybackFrame;
}

void FSequencePlayer::Tick(float DeltaTime)
{
	if (!bIsPlaying || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}
	const double NextFrame{ mSampleFrame + static_cast<double>(DeltaTime) * SequenceData.PlaybackFPS };
	const int32 EndFrame = SequenceData.GetEndFrame();
	PlaybackFrame = static_cast<float>(FMath::Min(NextFrame, static_cast<double>(EndFrame)));
	mSampleFrame = FMath::Min(NextFrame, static_cast<double>(EndFrame));
	if (NextFrame >= EndFrame)
	{
		Pause();
		bFinished = true;
	}
}

bool FSequencePlayer::GetCurrentCameraKey(CameraKey& OutCamera) const
{
	(void)OutCamera; // TODO 구현 전에는 출력값을 변경하지 않는다.
	const FCameraTimelineTrack* Track = nullptr;
	for (const auto& Row : SequenceData.Rows)
	{
		Track = std::get_if<FCameraTimelineTrack>(&Row.Rows);
		if (Track)
		{
			break;
		}
	}
	if (!Track)
	{
		return false;
	}
	if (Track->mUseCatmullRom)
	{
		return GetSplineCamera(*Track, mSampleFrame, OutCamera);
	}

	// 시작한 구간 중 가장 최근 구간을 평가한다. 구간 사이와 마지막 키 이후에는 끝 값을 유지한다.
	const FCameraSection* Section = nullptr;
	for (const auto& Candidate : Track->CamSections)
	{
		if (Candidate.KeyCount != 2 || Candidate.KeyFrames[0] == Candidate.KeyFrames[1] ||
			PlaybackFrame < FMath::Min(Candidate.KeyFrames[0], Candidate.KeyFrames[1]))
		{
			continue;
		}
		if (!Section || FMath::Min(Candidate.KeyFrames[0], Candidate.KeyFrames[1]) >= FMath::Min(Section->KeyFrames[0], Section->KeyFrames[1]))
		{
			Section = &Candidate;
		}
	}
	if (!Section || !std::isfinite(PlaybackFrame))
	{
		return false;
	}
	{
		float Playratio = FMath::Clamp((PlaybackFrame - Section->KeyFrames[0]) /
				(static_cast<float>(Section->KeyFrames[1]) - Section->KeyFrames[0]),
			0.0f, 1.0f);
		if (Section->Curve == CurveType::Linear)
		{
			OutCamera.Location.X = std::lerp(Section->KeyCam[0].Location.X, Section->KeyCam[1].Location.X, Playratio);
			OutCamera.Location.Y = std::lerp(Section->KeyCam[0].Location.Y, Section->KeyCam[1].Location.Y, Playratio);
			OutCamera.Location.Z = std::lerp(Section->KeyCam[0].Location.Z, Section->KeyCam[1].Location.Z, Playratio);
			OutCamera.Rotation.Pitch = LerpAngle(Section->KeyCam[0].Rotation.Pitch, Section->KeyCam[1].Rotation.Pitch, Playratio, Section->bUseShortestRotation);
			OutCamera.Rotation.Yaw = LerpAngle(Section->KeyCam[0].Rotation.Yaw, Section->KeyCam[1].Rotation.Yaw, Playratio, Section->bUseShortestRotation);
			OutCamera.Rotation.Roll = LerpAngle(Section->KeyCam[0].Rotation.Roll, Section->KeyCam[1].Rotation.Roll, Playratio, Section->bUseShortestRotation);
			OutCamera.FieldOfView = std::lerp(Section->KeyCam[0].FieldOfView, Section->KeyCam[1].FieldOfView, Playratio);
			return true;
		}

		if (Section->Curve == CurveType::EaseIn || Section->Curve == CurveType::EaseOut)
		{
			Playratio = Section->Curve == CurveType::EaseIn ? InterpEaseIn(0.0f, 1.0f, Playratio, Section->EaseExp) : InterpEaseOut(0.0f, 1.0f, Playratio, Section->EaseExp);
			OutCamera.Location.X = std::lerp(Section->KeyCam[0].Location.X, Section->KeyCam[1].Location.X, Playratio);
			OutCamera.Location.Y = std::lerp(Section->KeyCam[0].Location.Y, Section->KeyCam[1].Location.Y, Playratio);
			OutCamera.Location.Z = std::lerp(Section->KeyCam[0].Location.Z, Section->KeyCam[1].Location.Z, Playratio);
			OutCamera.Rotation.Pitch = LerpAngle(Section->KeyCam[0].Rotation.Pitch, Section->KeyCam[1].Rotation.Pitch, Playratio, Section->bUseShortestRotation);
			OutCamera.Rotation.Yaw = LerpAngle(Section->KeyCam[0].Rotation.Yaw, Section->KeyCam[1].Rotation.Yaw, Playratio, Section->bUseShortestRotation);
			OutCamera.Rotation.Roll = LerpAngle(Section->KeyCam[0].Rotation.Roll, Section->KeyCam[1].Rotation.Roll, Playratio, Section->bUseShortestRotation);
			OutCamera.FieldOfView = std::lerp(Section->KeyCam[0].FieldOfView, Section->KeyCam[1].FieldOfView, Playratio);
			return true;
		}

		if (Section->Curve == CurveType::Bezier)
		{
			OutCamera.Location.X = std::lerp(Section->KeyCam[0].Location.X, Section->KeyCam[1].Location.X, Playratio);
			OutCamera.Location.Y = std::lerp(Section->KeyCam[0].Location.Y, Section->KeyCam[1].Location.Y, Playratio);
			OutCamera.Location.Z = std::lerp(Section->KeyCam[0].Location.Z, Section->KeyCam[1].Location.Z, Playratio);
			OutCamera.Rotation.Pitch = LerpAngle(Section->KeyCam[0].Rotation.Pitch, Section->KeyCam[1].Rotation.Pitch, Playratio, Section->bUseShortestRotation);
			OutCamera.Rotation.Yaw = LerpAngle(Section->KeyCam[0].Rotation.Yaw, Section->KeyCam[1].Rotation.Yaw, Playratio, Section->bUseShortestRotation);
			OutCamera.Rotation.Roll = LerpAngle(Section->KeyCam[0].Rotation.Roll, Section->KeyCam[1].Rotation.Roll, Playratio, Section->bUseShortestRotation);
			OutCamera.FieldOfView = std::lerp(Section->KeyCam[0].FieldOfView, Section->KeyCam[1].FieldOfView, Playratio);
			return true;
		}
		return false;
	}
	return false;
}

bool FSequencePlayer::GetCurrentFadeAlpha(float& OutAlpha) const
{
	const FFadeTimelineTrack* Track = nullptr;
	for (const auto& Row : SequenceData.Rows)
	{
		Track = std::get_if<FFadeTimelineTrack>(&Row.Rows);
		if (Track)
		{
			break;
		}
	}
	if (!Track)
	{
		return false;
	}

	const FFadeSection* ActiveSection = nullptr;
	for (auto Section = Track->FadeSections.rbegin(); Section != Track->FadeSections.rend(); ++Section)
	{
		if (!IsSectionActive(PlaybackFrame, Section->KeyFrames, Section->KeyCount))
		{
			continue;
		}

		if (!ActiveSection || FMath::Min(Section->KeyFrames[0], Section->KeyFrames[1]) > FMath::Min(ActiveSection->KeyFrames[0], ActiveSection->KeyFrames[1]))
		{
			ActiveSection = &*Section;
		}
	}
	if (!ActiveSection)
	{
		return false;
	}

	float Playratio = (PlaybackFrame - ActiveSection->KeyFrames[0]) /
		(static_cast<float>(ActiveSection->KeyFrames[1]) - ActiveSection->KeyFrames[0]);
	switch (ActiveSection->Curve)
	{
	case CurveType::Linear:
	case CurveType::Bezier:
		break;
	case CurveType::EaseIn:
		Playratio = InterpEaseIn(0.0f, 1.0f, Playratio, ActiveSection->EaseExp);
		break;
	case CurveType::EaseOut:
		Playratio = InterpEaseOut(0.0f, 1.0f, Playratio, ActiveSection->EaseExp);
		break;
	default:
		return false;
	}
	OutAlpha = std::lerp(ActiveSection->KeyFade.StartAlpha, ActiveSection->KeyFade.EndAlpha, Playratio);
	return true;
}

bool FSequencePlayer::EvaluateAnimationTrack(const FAnimationTimelineTrack& Track, FAnimationSample& OutSample) const
{
	const int FPS = SequenceData.PlaybackFPS;
	if (FPS <= 0)
	{
		return false;
	}

	const FAnimationSection* Active = nullptr;
	for (const FAnimationSection& Section : Track.AnimSections)
	{
		if (!Section.Animation || Section.StartFrame > mSampleFrame)
		{
			continue;
		}
		if (!Active || Section.StartFrame >= Active->StartFrame)
		{
			Active = &Section;
		}
	}
	if (!Active)
	{
		return false;
	}

	const double Seconds = (mSampleFrame - Active->StartFrame) / static_cast<double>(FPS);
	const double Length = Active->Animation->GetPlayLength();
	OutSample.Animation = Active->Animation;
	OutSample.Time = static_cast<float>(FMath::Clamp(Seconds, 0.0, Length));

	return true;
}
