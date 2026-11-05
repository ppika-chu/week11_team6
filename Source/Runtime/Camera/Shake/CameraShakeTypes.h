#pragma once

#include "../CameraTypes.h"

// 쉐이크 오프셋을 어느 공간 기준으로 적용할지
enum class ECameraShakePlaySpace : uint8
{
	CameraLocal, // 카메라 회전 기준 (기본)
	World,       // 월드 축 기준
	UserDefined, // UserPlaySpaceRot 기준
};

// 패턴 한 프레임의 계산 결과. POV에 더해질 오프셋이다.
struct FCameraShakeUpdateResult
{
	FVector Location{ FVector::ZeroVector };
	FRotator Rotation{}; // Degree
	float FOV{};

	void Reset()
	{
		*this = FCameraShakeUpdateResult{};
	}

	void ApplyScale(float Scale)
	{
		Location *= Scale;
		Rotation.Pitch *= Scale;
		Rotation.Yaw *= Scale;
		Rotation.Roll *= Scale;
		FOV *= Scale;
	}

	void Add(const FCameraShakeUpdateResult& Other)
	{
		Location += Other.Location;
		Rotation.Pitch += Other.Rotation.Pitch;
		Rotation.Yaw += Other.Rotation.Yaw;
		Rotation.Roll += Other.Rotation.Roll;
		FOV += Other.FOV;
	}
};

// 패턴 갱신 입력
struct FCameraShakeUpdateParams
{
	float DeltaTime{};
	// 쉐이크가 적용되기 전의 POV. 패턴이 카메라 상태를 참고해야 할 때 쓴다.
	FMinimalViewInfo POV{};
};
