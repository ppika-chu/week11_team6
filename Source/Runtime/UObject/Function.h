#pragma once

#include "Core/NameTypes.h"

class UObject;

// UE의 함수 Reflection 방식을 적용한 함수 등록 정보.
// TODO: 현재는 void() 멤버 함수만 등록할 수 있다.
struct FFunction
{
	FName Name;
	void (*Invoke)(UObject* Object);
};
