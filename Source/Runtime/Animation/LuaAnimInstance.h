#pragma once

#include "Animation/AnimInstance.h"
#include <vector>

struct FAnimNode_Slot;
class UAnimSequence;

// Lua로 구성한 Graph를 실행하는 엔진 제공 Instance. Named Notify 함수는 파생 클래스에 등록한다.
// 노드는 Lua 바인딩이 보관하며, C++에서 연결할 때는 호출 측이 노드 수명을 보장한다.
class ULuaAnimInstance : public UAnimInstance
{
	DECLARE_CLASS(ULuaAnimInstance, UAnimInstance)
public:
	// Graph 연결 — 이전 Graph의 노드는 재연결에 따른 정리가 끝날 때까지 유지한다.
	void SetGraphRoot(FAnimNode_Base* InRoot);
	FAnimNode_Base* GetGraphRoot() const;

	// Slot 재생 — Sequence 하나를 공급하는 경량 재생기이며 UE의 전체 Montage 기능은 아니다.
	void PlayMontage(FAnimNode_Slot* Slot, UAnimSequence* Sequence, float PlayRate, float BlendIn, float BlendOut, bool bLooping);
	void StopMontage(FAnimNode_Slot* Slot, float BlendOut);
	bool IsMontagePlaying(const FAnimNode_Slot* Slot) const;
	float GetMontageTime(const FAnimNode_Slot* Slot) const;

protected:
	// 파생 클래스에서 Native hook을 재정의하면 Super를 호출해 Slot 갱신·정리를 유지한다.
	FAnimNode_Base* GetCustomRootNode() override;
	void NativeUninitializeAnimation() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	struct FActiveMontage
	{
		FAnimNode_Slot* Slot = nullptr;
		UAnimSequence* Sequence = nullptr;
		float CurrentTime = 0.0f;
		float ElapsedTime = 0.0f;
		float PlayRate = 1.0f;
		float BlendIn = 0.0f;
		float BlendOut = 0.0f;
		bool bLooping = false;
		bool bStopping = false;
		bool bFinished = false;
		float StopDuration = 0.0f;
		float StopElapsed = 0.0f;
		float StopStartWeight = 0.0f;
	};
	static void ClearMontagePose(FAnimNode_Slot* Slot);
	void ClearMontages();
	FAnimNode_Base* GraphRoot = nullptr;
	std::vector<FActiveMontage> Montages;
};
