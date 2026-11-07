#pragma once

#include "Animation/AnimPose.h"

// AnimInstance가 실행하는 애니메이션 노드의 공통 기반 구조체.
// 노드 초기화, 상태 갱신(Update), 결과 평가(Evaluate)의 공통 실행 계약을 위한 기반이다.
// 직접 생성하지 않는 추상 기반이며, 실제 동작은 파생 노드가 구현한다.
struct FAnimNode_Base
{
	virtual void Initialize() = 0;
	virtual void Update(float DeltaTime) = 0;
	virtual void Evaluate(FAnimPose& OutPose) = 0;
};
