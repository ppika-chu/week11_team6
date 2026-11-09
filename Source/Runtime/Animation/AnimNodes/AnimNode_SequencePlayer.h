#pragma once

#include "Animation/AnimPose.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationRuntime.h"
#include "AnimNode_Base.h"
#include "Math/EngineMath.h"

// Animation Sequence의 재생 시간, 재생 속도, 반복 여부를 관리하는 클립 재생 노드.
// Update에서 시간을 진행하고, Evaluate에서 해당 시점의 본 트랙을 샘플링해 로컬 포즈를 만든다.
// 상태 내부에서는 SequencePlayer의 출력 포즈를 StateResult에 연결한다.
struct FAnimNode_SequencePlayer : public FAnimNode_Base
{
public:
	void Initialize()
	{
		if (SequencePlayer)
		{
			CurrentTime = FMath::Clamp(StartPosition, 0.0f, SequencePlayer->GetPlayLength());
		}
		else
		{
			CurrentTime = 0.0f;
		}
		PreviousTime = CurrentTime;
		bPlaying = SequencePlayer != nullptr;
	}
	void Update(float DeltaTime)
	{
		PreviousTime = CurrentTime;

		if (!SequencePlayer || !bPlaying)
		{
			return;
		}

		const float Length = SequencePlayer->GetPlayLength();
		if (Length <= 0.0f)
		{
			CurrentTime = 0.0f;
			PreviousTime = 0.0f;
			bPlaying = false;
			return;
		}

		const float CombinedPlayRate = PlayRate * SequencePlayer->RateScale;
		const float MoveDelta = FMath::Max(DeltaTime, 0.0f) * CombinedPlayRate;

		FAnimationRuntime::AdvanceTime(bLooping, MoveDelta, CurrentTime, Length);

		if (!bLooping &&
			((MoveDelta > 0.0f && CurrentTime >= Length) ||
				(MoveDelta < 0.0f && CurrentTime <= 0.0f)))
		{
			bPlaying = false;
		}
	}
	void Evaluate(FAnimPose& OutPose)
	{
		OutPose.LocalTransforms.Reset();
		if (SequencePlayer)
		{
			SequencePlayer->GetAnimationPose(OutPose,FAnimExtractContext(CurrentTime));
		}
	}

	UAnimSequence* GetSequencePlayer() const { return SequencePlayer; }
	void SetSequencePlayer(UAnimSequence* InSequencePlayer) { SequencePlayer = InSequencePlayer; }
	void SetStartPosition(float InStartPosition) { StartPosition = FMath::Max(InStartPosition, 0.0f); }
	float GetStartPosition() const { return StartPosition; }
	void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }
	float GetPlayRate() const { return PlayRate; }
	void SetLooping(bool bInLooping) { bLooping = bInLooping; }
	bool IsLooping() const { return bLooping; }
	void SetPlaying(bool bInPlaying) { bPlaying = bInPlaying; }
	bool IsPlaying() const { return bPlaying; }
	float GetCurrentTime() const { return CurrentTime; }
	float GetPreviousTime() const { return PreviousTime; }

private:
	UAnimSequence* SequencePlayer = nullptr; // 재생할 공유 Asset의 비소유 참조
	float StartPosition = 0.0f; // 초기화 시 시작 시각(초). Initialize에서 클립 길이에 맞춰 보정한다.
	float PlayRate = 1.0f;      // 노드의 재생 배율. 음수는 역재생이며 Sequence의 RateScale과 함께 적용한다.
	bool bLooping = true;      // 클립 끝에서 반복 재생할지 여부
	bool bPlaying = true;      // Update에서 시간을 진행할지 여부. 정지 중에도 Evaluate로 현재 포즈를 얻을 수 있다.
	float CurrentTime = 0.0f;  // 이 노드의 현재 재생 시각(초)
	float PreviousTime = 0.0f; // 직전 Update 시작 시각(초). Notify 구간 판정 등에 사용한다.
};
