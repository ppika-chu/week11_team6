#pragma once

class UAnimInstance;

struct FAnimUpdateContext
{
	const UAnimInstance* AnimInstance = nullptr;
	float DeltaTime = 0.0f;
};
