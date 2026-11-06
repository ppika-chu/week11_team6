#pragma once

class UAnimInstance; // Todo : AnimInstance 클래스 정의 필요

struct FAnimUpdateContext
{
	const UAnimInstance* AnimInstance = nullptr;
	float DeltaTime = 0.0f;
};