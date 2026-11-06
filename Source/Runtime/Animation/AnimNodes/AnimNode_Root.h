#pragma once

#include "AnimNode_Base.h"

// 전체 AnimGraph의 최종 포즈를 AnimInstance에 전달하는 출력 노드.
// AnimInstance는 이 노드에서 시작해 연결된 StateMachine·Slot 등의 갱신과 평가를 요청한다.
// 클립 재생은 SequencePlayer가 담당하고, Root는 입력 그래프의 결과를 최종 출력으로 전달한다.
struct FAnimNode_Root : public FAnimNode_Base
{
};
