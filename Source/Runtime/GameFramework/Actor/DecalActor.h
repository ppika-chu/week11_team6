#pragma once

#include "GameFramework/Actor.h"

class UDecalComponent;
class UBillboardComponent;

// DecalComponent를 루트로 갖는 액터. 에디터에서는 빌보드 아이콘으로 위치를 표시한다.
class ADecalActor : public AActor
{
	DECLARE_CLASS(ADecalActor, AActor)

public:
	ADecalActor();
	virtual ~ADecalActor() override = default;

	ADecalActor(const ADecalActor&) = delete;
	ADecalActor& operator=(const ADecalActor&) = delete;

	// 데칼은 페이드가 끝나면 지워질 수 있으므로 포인터를 들고 있지 않는다. 지워졌으면 nullptr.
	UDecalComponent* GetDecal() const;
};
