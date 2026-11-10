#pragma once

#include "SequenceData.h"

struct FAnimationSample
{
	UAnimSequence* Animation = nullptr;
	float Time = 0.0f;
};

class FSequencePlayer
{
public:
	void SetSequenceData(const FSequenceData& InData); // Preview의 SequenceData 세팅용
	const FSequenceData& GetSequenceData() const { return SequenceData; }
	FSequenceData& GetSequenceData() { return SequenceData; }

	void Play();
	void Pause() { bIsPlaying = false; }
	void Stop();
	void Tick(float DeltaTime);
	bool IsPlaying() const { return bIsPlaying; }
	// Pause와 구분하기 위한 상태. Stop/데이터 교체 시 초기화하고 끝 프레임 도달 시 완료된다.
	bool IsStopped() const { return !bPlaybackStarted; }
	bool HasFinished() const { return bFinished; }
	float GetPlaybackFrame() const { return PlaybackFrame; }
	void SetPlaybackFrame(float InFrame);
	void SetPlaybackSettings(int InFPS, int32 InDurationSeconds);
	bool EvaluateAtTime(double Time);

	bool GetCurrentCameraKey(CameraKey& OutCamera) const; // 현재 PlaybackFrame의 Camera 정보를 가져옴
	bool GetCurrentFadeAlpha(float& OutAlpha) const;      // 현재 PlaybackFrame의 Alpha를 가져옴

	bool EvaluateAnimationTrack(const FAnimationTimelineTrack& Track, FAnimationSample& OutSample) const;

private:
	double mSampleFrame{};
	FSequenceData SequenceData;
	float PlaybackFrame = 0.0f;
	bool bIsPlaying = false;
	bool bPlaybackStarted = false;
	bool bFinished = false;
};
