#pragma once

#include "AnimNode_Base.h"

// StateMachine 등에서 받은 기본 포즈에 지정한 Slot의 Montage 포즈를 혼합하는 노드.
// AnimInstance가 관리하는 Montage 재생 정보와 블렌드 가중치를 사용한다.
// 해당 Slot에 재생 중인 Montage가 없으면 기본 포즈를 그대로 출력한다.
struct FAnimNode_Slot : public FAnimNode_Base
{
};
