#pragma once

#include "AnimNode_Base.h"

// FAnimState와 FAnimTransition의 관계를 바탕으로 현재 상태와 진행 중인 전이를 관리하는 노드.
// TransitionResult의 조건 결과로 전이를 선택하고, 선택한 상태의 StateResult에서 포즈를 얻는다.
// 전이 중에는 이전 상태와 다음 상태의 포즈를 전이 가중치에 따라 혼합해 출력한다.
struct FAnimNode_StateMachine : public FAnimNode_Base
{
};
