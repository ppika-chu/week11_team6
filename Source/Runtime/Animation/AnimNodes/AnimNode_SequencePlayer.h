#pragma once

#include "Animation/AnimPose.h"
#include "AnimNode_Base.h"

class UAnimSequencePlayer; // Todo : 애니메이션 시퀀스 플레이어 추가시 해당 이름에 맞게 수정

// Animation Sequence의 재생 시간, 재생 속도, 반복 여부를 관리하는 클립 재생 노드.
// Update에서 시간을 진행하고, Evaluate에서 해당 시점의 본 트랙을 샘플링해 로컬 포즈를 만든다.
// 상태 내부에서는 SequencePlayer의 출력 포즈를 StateResult에 연결한다.
struct FAnimNode_SequencePlayer : public FAnimNode_Base
{
public:
	void Initialize() override;
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

	UAnimSequencePlayer* GetSequencePlayer() const { return SequencePlayer; }
	void SetSequencePlayer(UAnimSequencePlayer* InSequencePlayer) { SequencePlayer = InSequencePlayer; }
	// Todo : 애니메이션 시퀀스 플레이어 관련 멤버 함수 추가

private:
	UAnimSequencePlayer* SequencePlayer = nullptr;
	// Todo : 애니메이션 시퀀스 플레이어 관련 멤버 변수 추가
};
