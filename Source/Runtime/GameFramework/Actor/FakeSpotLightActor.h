#pragma once

#include "GameFramework/Actor.h"

class UDecalComponent;
class UBillboardComponent;

// 조명 계산 없이 스포트라이트처럼 보이게 하는 액터.
// 빌보드가 빛의 원천을 보여 주고, 아래로 쏘는 Additive 데칼이 바닥에 빛 웅덩이를 더한다.
class AFakeSpotLightActor : public AActor
{
	DECLARE_CLASS(AFakeSpotLightActor, AActor)

public:
	AFakeSpotLightActor();
	virtual ~AFakeSpotLightActor() override = default;

	AFakeSpotLightActor(const AFakeSpotLightActor&) = delete;
	AFakeSpotLightActor& operator=(const AFakeSpotLightActor&) = delete;

	// 데칼은 페이드가 끝나면 지워질 수 있으므로 포인터를 들고 있지 않는다. 지워졌으면 nullptr.
	UDecalComponent* GetDecal() const;
	UBillboardComponent* GetBillboard() const;
};
