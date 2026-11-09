#pragma once

#include "AnimInstance.h"
#include "AnimNodes/AnimNode_SingleNode.h"

class UAnimationAsset;

class UAnimSingleNodeInstance : public UAnimInstance
{
	DECLARE_CLASS(UAnimSingleNodeInstance, UAnimInstance)
	friend struct FAnimNode_SingleNode;
public:
	void SetAnimationAsset(UAnimationAsset* NewAsset, bool bInIsLooping = true, float InPlayRate = 1.0f);
	UAnimationAsset* GetCurrentAsset() const { return CurrentAsset; }
	void PlayAnim(bool bIsLooping = false, float InPlayRate = 1.0f, float InStartPosition = 0.0f);
	void StopAnim() { SetPlaying(false); }
	void SetPlaying(bool bIsPlaying) { bPlaying = bIsPlaying; }
	bool IsPlaying() const { return bPlaying; }
	void SetLooping(bool bIsLooping) { bLooping = bIsLooping; }
	bool IsLooping() const { return bLooping; }
	void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }
	float GetPlayRate() const { return PlayRate; }
	void SetReverse(bool bInReverse);
	bool IsReverse() const { return bReverse; }
	void SetPosition(float InPosition, bool bFireNotifies = true);
	// 한 프레임 앞 / 뒤 키 위치로 이동한다. 반복 중이면 끝과 처음이 이어진다.
	void StepForward();
	void StepBackward();
	float GetCurrentTime() const { return CurrentTime; }
	float GetLength() const;

	virtual void NativeInitializeAnimation() override;

protected:
	virtual FAnimNode_Base* GetCustomRootNode() override { return &SingleNode; }

private:
	FAnimNode_SingleNode SingleNode; // 이 Instance가 멤버로 보관하며 Root로 제공하는 Node.
	UAnimationAsset* CurrentAsset = nullptr; // 외부에서 지정한 재생 Asset. Instance가 삭제하지 않으며 재생 중 유효해야 한다.
	float CurrentTime = 0.0f;
	float PlayRate = 1.0f;
	bool bPlaying = true;
	bool bLooping = true;
	bool bReverse = false;
};
