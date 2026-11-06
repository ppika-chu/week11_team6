#pragma once

#include "Animation/AnimPose.h"

// AnimInstance가 실행하는 애니메이션 노드의 공통 기반 구조체.
// 노드 초기화, 상태 갱신(Update), 결과 평가(Evaluate)의 공통 실행 계약을 위한 기반이다.
struct FAnimNode_Base
{
	virtual void Initialize();
	virtual void Update(float DeltaTime);
	virtual void Evaluate(FAnimPose& OutPose);
};
